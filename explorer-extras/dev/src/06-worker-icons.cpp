// ---- Worker thread: reads folders off the UI thread ----

struct Loaded {
    unsigned generation = 0;
    FolderResult data;
};

struct Job {
    LONG iconSide = 0;  // The size of the icons to ask for: the scale of the window's monitor. 0: the system's.
    unsigned generation = 0;
    Pidl folder;
    SortSpec spec;
    bool hidden = false;
    int maxItems = 1500;
    mud::DispatcherQueue queue{nullptr};
    std::shared_ptr<std::atomic<bool>> cancel;  // The column no longer wants this.
    // A job for icons only: rows that came on screen after the first batch.
    bool iconsOnly = false;
    std::vector<size_t> iconIndices;
    std::vector<Pidl> iconChildren;
};

// One reader thread per window, so a slow folder in one window never holds up
// the folders and icons of another. A worker is stopped when its window's panel
// goes away and cleaned up later; all of them end when the mod unloads.
struct Worker {
    std::mutex mutex;
    std::deque<std::unique_ptr<Job>> jobs;
    HANDLE event = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
};

// The icon size of the job the worker is on.
thread_local LONG t_iconSide = 0;

std::mutex g_workersMutex;
[[clang::no_destroy]] std::vector<std::unique_ptr<Worker>> g_workers;

void ApplyLoaded(std::shared_ptr<Loaded> const& loaded);
void CompleteSwap(WindowState* state);

// ---- Icons and thumbnails, read on the worker thread ----

struct IconBits {
    size_t index = 0;  // Index into the column's entries.
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;  // Premultiplied, top-down.
};

struct IconBatch {
    unsigned generation = 0;
    std::vector<IconBits> icons;
};

void ApplyIcons(std::shared_ptr<IconBatch> const& batch);

// Icons read with the first load. The rows beyond are asked for when they are
// built, so the ones on screen come first even far down a long folder.
constexpr size_t kMaxIcons = 120;
constexpr size_t kIconBatch = 16;

// Copies a shell bitmap's pixels. False if it can't be read.
bool ReadBitmapBits(HBITMAP bitmap, IconBits* out, bool folder = false) {
    BITMAP info = {};
    if (!GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 ||
        info.bmHeight <= 0 || info.bmWidth > 512 || info.bmHeight > 512) {
        return false;
    }
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = info.bmWidth;
    bmi.bmiHeader.biHeight = -info.bmHeight;  // Top-down.
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    out->width = info.bmWidth;
    out->height = info.bmHeight;
    out->bgra.assign((size_t)info.bmWidth * info.bmHeight * 4, 0);
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, bitmap, 0, info.bmHeight, out->bgra.data(), &bmi,
                          DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != info.bmHeight) {
        return false;
    }
    // A bitmap without an alpha channel comes back with alpha 0 everywhere.
    bool anyAlpha = false;
    for (size_t i = 3; i < out->bgra.size(); i += 4) {
        if (out->bgra[i]) {
            anyAlpha = true;
            break;
        }
    }
    if (!anyAlpha && !folder) {
        for (size_t i = 3; i < out->bgra.size(); i += 4) {
            out->bgra[i] = 255;  // A picture without alpha (a photo, a video frame) is opaque, black included.
        }
    } else if (!anyAlpha) {
        // A folder's picture without an alpha channel sits on black. The black that touches the edge is the background, and goes.
        int w = out->width, h = out->height;

        std::vector<char> seen((size_t)w * h, 0);
        std::vector<int> stack;
        auto black = [&](int x, int y) {
            const unsigned char* px = &out->bgra[((size_t)y * w + x) * 4];
            return !px[0] && !px[1] && !px[2];
        };
        auto push = [&](int x, int y) {
            size_t at = (size_t)y * w + x;
            if (!seen[at] && black(x, y)) {
                seen[at] = 1;
                stack.push_back((int)at);
            }
        };
        for (int x = 0; x < w; x++) {
            push(x, 0);
            push(x, h - 1);
        }
        for (int y = 0; y < h; y++) {
            push(0, y);
            push(w - 1, y);
        }
        while (!stack.empty()) {
            int at = stack.back();
            stack.pop_back();
            int x = at % w, y = at / w;
            if (x > 0) push(x - 1, y);
            if (x < w - 1) push(x + 1, y);
            if (y > 0) push(x, y - 1);
            if (y < h - 1) push(x, y + 1);
        }
        for (size_t i = 0; i < seen.size(); i++) {
            out->bgra[i * 4 + 3] = seen[i] ? 0 : 255;
        }
    }
    return true;
}

// The shell hands some folder thumbnails over as a picture on opaque black. True for such a bitmap: all four corners opaque black.
bool HasBlackCorners(IconBits const& bits) {
    if (bits.width < 2 || bits.height < 2) {
        return false;
    }
    auto black = [&](int x, int y) {
        const unsigned char* px = &bits.bgra[((size_t)y * bits.width + x) * 4];
        return !px[0] && !px[1] && !px[2] && px[3] == 255;
    };
    return black(0, 0) && black(bits.width - 1, 0) && black(0, bits.height - 1) && black(bits.width - 1, bits.height - 1);
}

// The picture of an item, its thumbnail where the shell has one. A thumbnail on opaque black is given up for the icon, which has its
// transparency. `flags` are the SIIGBF flags of the first try.
bool GetItemBits(IShellItemImageFactory* factory, LONG side, int flags, IconBits* bits, bool folder, HRESULT* result = nullptr) {
    HBITMAP bitmap = nullptr;
    HRESULT got = factory->GetImage(SIZE{side, side}, flags, &bitmap);
    if (result) {
        *result = got;
    }
    bool ok = false;
    if (SUCCEEDED(got) && bitmap) {
        ok = ReadBitmapBits(bitmap, bits, folder);
        DeleteObject(bitmap);
        if (ok && folder && HasBlackCorners(*bits)) {
            HBITMAP plain = nullptr;
            if (SUCCEEDED(factory->GetImage(SIZE{side, side}, (flags & ~SIIGBF_INCACHEONLY) | SIIGBF_ICONONLY, &plain)) && plain) {
                IconBits again;
                if (ReadBitmapBits(plain, &again, folder)) {
                    again.index = bits->index;
                    *bits = std::move(again);
                }
                DeleteObject(plain);
            }
        }
    }
    return ok;
}

// Reads icons, or thumbnails where the shell has one, for the first items of
// a folder and sends them to the window's UI thread in small batches.
void LoadIcons(PCIDLIST_ABSOLUTE folderPidl, std::vector<Pidl>& children,
               std::vector<size_t> const& indices, size_t limit,
               unsigned generation, mud::DispatcherQueue const& queue,
               std::atomic<bool> const& stop, std::atomic<bool> const* cancel) {
    IShellFolder* folder = nullptr;
    HRESULT hr = ILIsEmpty(folderPidl)
                     ? SHGetDesktopFolder(&folder)
                     : SHBindToObject(nullptr, folderPidl, nullptr,
                                      IID_PPV_ARGS(&folder));
    if (FAILED(hr) || !folder) {
        return;
    }
    auto batch = std::make_shared<IconBatch>();
    batch->generation = generation;
    auto flush = [&]() {
        if (batch->icons.empty()) {
            return;
        }
        auto sent = batch;
        try {
            queue.TryEnqueue([sent]() { ApplyIcons(sent); });
        } catch (...) {
        }
        batch = std::make_shared<IconBatch>();
        batch->generation = generation;
    };
    size_t count = std::min(std::min(children.size(), indices.size()), limit);
    std::vector<size_t> missed;
    // The first pass asks only for what the shell already has in its cache, which
    // is quick, so the rows on screen fill at once. What is not cached is made in
    // the second pass, in the same order.
    auto readOne = [&](size_t i, bool cachedOnly) -> bool {
        IShellItem* item = nullptr;
        if (FAILED(SHCreateItemWithParent(nullptr, folder, children[i].get(), IID_PPV_ARGS(&item))) || !item) {
            return true;  // Nothing more to try.
        }
        bool done = false;
        IShellItemImageFactory* factory = nullptr;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) && factory) {
            // A row's icon is 20 DIP: the picture is asked for at that size on this screen, 32 px at least.
            LONG side = t_iconSide ? t_iconSide : MulDiv(20, (int)GetDpiForSystem(), 96);
            side = side < 32 ? 32 : side;
            SFGAOF attributes = SFGAO_FOLDER;
            PCUITEMID_CHILD one = children[i].get();
            bool isFolder = SUCCEEDED(folder->GetAttributesOf(1, &one, &attributes)) && (attributes & SFGAO_FOLDER);
            IconBits bits;
            bits.index = indices[i];
            HRESULT got = S_OK;
            if (GetItemBits(factory, side, cachedOnly ? SIIGBF_INCACHEONLY : 0, &bits, isFolder, &got)) {
                batch->icons.push_back(std::move(bits));
                done = true;
            } else if (!cachedOnly) {
                // No thumbnail to be had: the icon of the type, which every item has.
                IconBits plain;
                plain.index = indices[i];
                if (GetItemBits(factory, side, SIIGBF_ICONONLY, &plain, isFolder)) {
                    batch->icons.push_back(std::move(plain));
                }
                done = true;  // Asked in full: not again.
            }
            factory->Release();
        } else {
            done = true;
        }
        item->Release();
        return done;
    };
    for (size_t i = 0; i < count && !stop && !(cancel && *cancel); i++) {
        if (!readOne(i, true)) {
            missed.push_back(i);
        }
        if (batch->icons.size() >= kIconBatch) {
            flush();
        }
    }
    flush();
    for (size_t k = 0; k < missed.size() && !stop && !(cancel && *cancel); k++) {
        readOne(missed[k], false);
        if (batch->icons.size() >= kIconBatch) {
            flush();
        }
    }
    flush();
    folder->Release();
}

DWORD WINAPI WorkerProc(LPVOID param) {
    Worker* worker = (Worker*)param;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (!worker->stop) {
        MsgWaitForMultipleObjects(1, &worker->event, FALSE, INFINITE, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        for (;;) {
            std::unique_ptr<Job> job;
            {
                std::lock_guard<std::mutex> lock(worker->mutex);
                if (worker->jobs.empty()) {
                    break;
                }
                job = std::move(worker->jobs.front());
                worker->jobs.pop_front();
            }
            if (worker->stop) {
                break;
            }
            // A column that has been removed or asked again doesn't need this.
            if (job->cancel && *job->cancel) {
                continue;
            }
            t_iconSide = job->iconSide;
            if (job->iconsOnly) {
                try {
                    LoadIcons(job->folder.get(), job->iconChildren, job->iconIndices,
                              job->iconChildren.size(), job->generation, job->queue,
                              worker->stop, job->cancel.get());
                } catch (...) {
                }
                continue;
            }
            auto loaded = std::make_shared<Loaded>();
            loaded->generation = job->generation;
            LoadFolder(job->folder.get(), job->spec, job->hidden,
                       job->maxItems, &loaded->data);
            if (job->cancel && *job->cancel) {
                continue;
            }
            // The list moves to the UI thread, so keep copies of the items
            // for the icon pass.
            std::vector<Pidl> children;
            for (auto& entry : loaded->data.entries) {
                children.emplace_back(ILClone(entry.child.get()));
                if (children.size() >= kMaxIcons) {
                    break;
                }
            }
            try {
                job->queue.TryEnqueue([loaded]() { ApplyLoaded(loaded); });
            } catch (...) {
            }
            try {
                std::vector<size_t> indices(children.size());
                for (size_t i = 0; i < indices.size(); i++) {
                    indices[i] = i;
                }
                LoadIcons(job->folder.get(), children, indices, kMaxIcons,
                          job->generation, job->queue, worker->stop,
                          job->cancel.get());
            } catch (...) {
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->jobs.clear();
    }
    CoUninitialize();
    worker->finished = true;
    return 0;
}

// The size of a row's icon on the monitor this window is on (20 DIP at its scale), or 0 when unknown.
LONG IconSideOf(WindowState* state) {
    try {
        if (state->xamlRoot) {
            return (LONG)std::lround(20.0 * state->xamlRoot.RasterizationScale());
        }
    } catch (...) {
    }
    return 0;
}

// A new worker for a window. Workers that have ended are cleaned up here.
Worker* CreateWorker() {
    std::lock_guard<std::mutex> lock(g_workersMutex);
    if (g_unloading) {
        return nullptr;  // StopWorker has swapped the workers out, or is about to: a thread started now would outlive the mod.
    }
    for (size_t i = 0; i < g_workers.size();) {
        Worker* old = g_workers[i].get();
        if (old->finished && WaitForSingleObject(old->thread, 0) == WAIT_OBJECT_0) {
            CloseHandle(old->thread);
            CloseHandle(old->event);
            g_workers.erase(g_workers.begin() + i);
        } else {
            i++;
        }
    }
    auto worker = std::make_unique<Worker>();
    worker->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    worker->thread = CreateThread(nullptr, 0, WorkerProc, worker.get(), 0, nullptr);
    Worker* raw = worker.get();
    g_workers.push_back(std::move(worker));
    Wh_Log(L"a reader thread was started for a window, %d running", (int)g_workers.size());
    return raw;
}

// The window's panel is gone: its worker ends after the job it is on.
void ReleaseWorker(Worker* worker) {
    if (!worker) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_workersMutex);
    for (auto& known : g_workers) {
        if (known.get() == worker) {
            worker->stop = true;
            SetEvent(worker->event);
            break;
        }
    }
}

// At unload: every worker ends, and is waited for.
void StopWorker() {
    std::vector<std::unique_ptr<Worker>> all;
    {
        std::lock_guard<std::mutex> lock(g_workersMutex);
        all.swap(g_workers);
    }
    for (auto& worker : all) {
        worker->stop = true;
        SetEvent(worker->event);
    }
    // Each is waited for as long as it takes: a thread left running would be in an unmapped image.
    // One stuck in a slow network folder is told to give up its call.
    for (auto& worker : all) {
        for (int waited = 0; WaitForSingleObject(worker->thread, 50) == WAIT_TIMEOUT; waited++) {
            if (waited > 5) {
                CancelSynchronousIo(worker->thread);
            }
            if (waited == 200) {
                Wh_Log(L"a reader thread is still running after 10 s, waiting for it");
            }
        }
        CloseHandle(worker->thread);
        CloseHandle(worker->event);
    }
}

// Where a job goes in a worker's queue: a folder is needed to fill a column, so it
// goes before every icon job (and after the folders asked earlier); the newest
// icon request goes first among the icon jobs, since it is for what is on screen
// now, and older ones are for rows that may have scrolled away.
void QueueJob(Worker* worker, std::unique_ptr<Job> job) {
    std::lock_guard<std::mutex> lock(worker->mutex);
    auto at = worker->jobs.begin();
    while (at != worker->jobs.end() && !(*at)->iconsOnly) {
        ++at;
    }
    worker->jobs.insert(at, std::move(job));
}

void RequestLoad(WindowState* state, Column* column) {
    if (g_unloading) {
        return;
    }
    if (!state->worker) {
        state->worker = CreateWorker();
    }
    if (!state->worker) {
        return;  // The mod is unloading.
    }
    // The load this column asked before is not wanted any more.
    if (column->cancel) {
        *column->cancel = true;
    }
    column->cancel = std::make_shared<std::atomic<bool>>(false);
    column->generation = ++state->nextGeneration;
    column->loading = true;
    column->loadStartedAt = GetTickCount64();
    auto job = std::make_unique<Job>();
    job->generation = column->generation;
    job->folder.reset(ILClone(column->folder.get()));
    job->spec = column->hasSpec ? column->spec : state->spec;
    job->hidden = ShowHiddenFiles();
    job->iconSide = IconSideOf(state);
    job->maxItems = g_maxItems;
    job->queue = state->queue;
    job->cancel = column->cancel;
    std::lock_guard<std::mutex> lock(g_workersMutex);  // StopWorker takes the workers out under it.
    if (g_unloading) {
        return;
    }
    QueueJob(state->worker, std::move(job));
    SetEvent(state->worker->event);
}

// Asks the worker for the icons of rows that were built after the first batch.
void RequestIcons(WindowState* state, Column* column) {
    if (g_unloading || !state->worker || column->iconWanted.empty() ||
        !column->cancel) {
        return;
    }
    auto job = std::make_unique<Job>();
    job->iconsOnly = true;
    job->iconSide = IconSideOf(state);
    job->generation = column->generation;
    job->folder.reset(ILClone(column->folder.get()));
    job->queue = state->queue;
    job->cancel = column->cancel;
    // The rows built last are the ones on screen: take those, newest first.
    size_t take = std::min<size_t>(column->iconWanted.size(), 200);
    for (size_t i = 0; i < take; i++) {
        size_t entry = column->iconWanted[column->iconWanted.size() - 1 - i];
        if (entry < column->entries.size()) {
            job->iconIndices.push_back(entry);
            job->iconChildren.emplace_back(ILClone(column->entries[entry].child.get()));
        }
    }
    column->iconWanted.erase(column->iconWanted.end() - take, column->iconWanted.end());
    std::lock_guard<std::mutex> lock(g_workersMutex);  // StopWorker takes the workers out under it.
    if (g_unloading) {
        return;
    }
    QueueJob(state->worker, std::move(job));
    SetEvent(state->worker->event);
}

