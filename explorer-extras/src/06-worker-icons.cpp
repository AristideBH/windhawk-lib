// ---- Worker thread: reads folders off the UI thread ----

struct Loaded {
    unsigned generation = 0;
    FolderResult data;
};

struct Job {
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
bool ReadBitmapBits(HBITMAP bitmap, IconBits* out) {
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
    if (!anyAlpha) {
        for (size_t i = 3; i < out->bgra.size(); i += 4) {
            out->bgra[i] = 255;
        }
    }
    return true;
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
            HBITMAP bitmap = nullptr;
            HRESULT got = factory->GetImage(SIZE{32, 32}, cachedOnly ? SIIGBF_INCACHEONLY : 0, &bitmap);
            if (SUCCEEDED(got) && bitmap) {
                IconBits bits;
                bits.index = indices[i];
                if (ReadBitmapBits(bitmap, &bits)) {
                    batch->icons.push_back(std::move(bits));
                }
                DeleteObject(bitmap);
                done = true;
            } else if (!cachedOnly) {
                done = true;  // Asked in full and failed: not again.
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

// A new worker for a window. Workers that have ended are cleaned up here.
Worker* CreateWorker() {
    std::lock_guard<std::mutex> lock(g_workersMutex);
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
    Out(L"a reader thread was started for a window, %d running", (int)g_workers.size());
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

void StartWorker() {}  // Workers start with the first load of a window.

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
    for (auto& worker : all) {
        WaitForSingleObject(worker->thread, 10000);
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
    if (job->iconsOnly) {
        worker->jobs.insert(at, std::move(job));
    } else {
        worker->jobs.insert(at, std::move(job));
    }
}

void RequestLoad(WindowState* state, Column* column) {
    if (g_unloading) {
        return;
    }
    if (!state->worker) {
        state->worker = CreateWorker();
    }
    // The load this column asked before is not wanted any more.
    if (column->cancel) {
        *column->cancel = true;
    }
    column->cancel = std::make_shared<std::atomic<bool>>(false);
    column->generation = ++state->nextGeneration;
    auto job = std::make_unique<Job>();
    job->generation = column->generation;
    job->folder.reset(ILClone(column->folder.get()));
    job->spec = column->hasSpec ? column->spec : state->spec;
    job->hidden = ShowHiddenFiles();
    job->maxItems = g_maxItems;
    job->queue = state->queue;
    job->cancel = column->cancel;
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
    QueueJob(state->worker, std::move(job));
    SetEvent(state->worker->event);
}

