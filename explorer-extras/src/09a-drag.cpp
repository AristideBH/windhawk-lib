
// ---- Drag and drop between the columns ----
//
// Rows are dragged with the pointer inside the panel. The mod follows the
// pointer itself, shows a label and lights the folder it would drop on, then
// hands the dragged items to that folder's own drop target, so the shell does the
// move, copy or link, with its own conflict and progress dialogs, as it does
// for Explorer. Ctrl copies, Shift moves and Alt links; without a key, the
// shell chooses (a move on the same drive, a copy to another). Escape cancels.

// The column and folder row under a point of the panel, or false.
bool DragTargetAt(WindowState* state, winrt::Windows::Foundation::Point where, Column** column,
                  int* entry) {
    auto elements = mux::Media::VisualTreeHelper::FindElementsInHostCoordinates(where, state->rootGrid);
    for (auto element : elements) {
        muxc::ListViewItem row{nullptr};
        mux::DependencyObject node = element;
        for (int i = 0; node && i < 40; i++) {
            if (!row) {
                if (auto found = node.try_as<muxc::ListViewItem>()) {
                    row = found;
                }
            }
            if (auto list = node.try_as<muxc::ListView>()) {
                int at = ColumnIndexOfList(state, list);
                if (at < 0) {
                    break;
                }
                *column = state->columns[at].get();
                *entry = -1;
                if (row) {
                    int rowIndex = list.IndexFromContainer(row);
                    if (rowIndex >= 0 && rowIndex < (int)(*column)->rowToEntry.size()) {
                        *entry = (*column)->rowToEntry[rowIndex];
                    }
                }
                return true;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
    }
    return false;
}

// The absolute folder a drop on this column or row would go to.
Pidl DragTargetFolder(Column* column, int entry) {
    if (entry >= 0 && (size_t)entry < column->entries.size()) {
        auto& hit = column->entries[entry];
        return Pidl(hit.linkTarget ? ILClone(hit.linkTarget.get())
                                   : ILCombine(column->folder.get(), hit.child.get()));
    }
    return Pidl(ILClone(column->folder.get()));
}

// Whether the dragged items can be dropped there.
bool DragTargetAllowed(WindowState* state, Column* column, int entry) {
    auto& d = state->drag;
    if (!d.column) {
        return false;
    }
    if (entry >= 0) {
        if ((size_t)entry >= column->entries.size()) {
            return false;
        }
        auto& hit = column->entries[entry];
        if (!hit.folder || hit.archive || hit.launcher) {
            return false;
        }
        if (column == d.column &&
            std::find(d.entries.begin(), d.entries.end(), (size_t)entry) != d.entries.end()) {
            return false;  // A folder onto itself.
        }
    } else {
        if (column == d.column && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            return false;  // Already there. With Ctrl it is a copy in the same folder.
        }
        if (ArchiveParentColumn(state, column) >= 0) {
            return false;
        }
    }
    Pidl target = DragTargetFolder(column, entry);
    wchar_t path[MAX_PATH] = L"";
    if (!target || !SHGetPathFromIDListW(target.get(), path) || !path[0]) {
        return false;  // Not a folder on disk.
    }
    // A folder cannot go into itself or what is inside it.
    for (size_t index : d.entries) {
        if (index >= d.column->entries.size() || !d.column->entries[index].folder) {
            continue;
        }
        std::wstring source = FilePathOf(d.column->folder.get(), d.column->entries[index].child.get());
        if (!source.empty() && _wcsnicmp(path, source.c_str(), source.size()) == 0 &&
            (path[source.size()] == L'\0' || path[source.size()] == L'\\')) {
            return false;
        }
    }
    return true;
}

void DragClearTarget(WindowState* state) {
    auto& d = state->drag;
    try {
        if (d.frame) {
            d.frame.Visibility(mux::Visibility::Collapsed);
        }
    } catch (...) {
        LogHresult(L"clearing the drop target");
    }
    d.targetColumn = nullptr;
    d.targetEntry = -1;
    d.targetOk = false;
}

// The label comes in and goes out the way a flyout does: a short fade with a
// slight scale from the middle. On the way out it is removed when it has faded.
void AnimateGhost(mux::FrameworkElement const& ghost, bool entering) {
    try {
        namespace anim = mux::Media::Animation;
        mux::Media::ScaleTransform scale;
        ghost.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
        ghost.RenderTransform(scale);
        anim::CubicEase ease;
        ease.EasingMode(entering ? anim::EasingMode::EaseOut : anim::EasingMode::EaseIn);
        auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(entering ? 150 : 100));
        anim::Storyboard board;
        auto add = [&](const wchar_t* property, double from, double to) {
            anim::DoubleAnimation step;
            step.From(from);
            step.To(to);
            step.Duration(length);
            step.EasingFunction(ease);
            step.EnableDependentAnimation(true);
            step.FillBehavior(entering ? anim::FillBehavior::Stop : anim::FillBehavior::HoldEnd);
            anim::Storyboard::SetTarget(step, ghost);
            anim::Storyboard::SetTargetProperty(step, property);
            board.Children().Append(step);
        };
        add(L"Opacity", entering ? 0.0 : 1.0, entering ? 1.0 : 0.0);
        add(L"(UIElement.RenderTransform).(ScaleTransform.ScaleX)", entering ? 0.92 : 1.0,
            entering ? 1.0 : 0.96);
        add(L"(UIElement.RenderTransform).(ScaleTransform.ScaleY)", entering ? 0.92 : 1.0,
            entering ? 1.0 : 0.96);
        board.Begin();
    } catch (...) {
        LogHresult(L"animating the drag label");
    }
}

void FadeOutGhost(muxc::Canvas const& overlay, mux::FrameworkElement const& ghost) {
    auto remove = [overlay, ghost]() {
        try {
            uint32_t index = 0;
            if (overlay.Children().IndexOf(ghost, index)) {
                overlay.Children().RemoveAt(index);
            }
        } catch (...) {
        }
    };
    if (g_unloading) {
        remove();
        return;
    }
    try {
        AnimateGhost(ghost, false);
        auto timer = overlay.DispatcherQueue().CreateTimer();
        timer.Interval(std::chrono::milliseconds(130));
        timer.IsRepeating(false);
        Keep(timer, timer.Tick(winrt::auto_revoke, [remove](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
            sender.Stop();
            remove();
        }));
        timer.Start();
    } catch (...) {
        remove();
    }
}

void RemoveTouchHook();
thread_local bool g_keepCapture = false;  // A touch drag goes on outside: its finger stays on the list.

void DragEnd(WindowState* state) {
    auto& d = state->drag;
    for (auto& lock : d.locked) {
        try {
            lock.viewer.VerticalScrollMode(lock.v);
            lock.viewer.HorizontalScrollMode(lock.h);
        } catch (...) {
        }
    }
    DragClearTarget(state);
    state->autoOn = false;
    try {
        if (d.ghost && state->overlay) {
            FadeOutGhost(state->overlay, d.ghost);
        }
        if (d.frame && state->overlay) {
            uint32_t index = 0;
            if (state->overlay.Children().IndexOf(d.frame, index)) {
                state->overlay.Children().RemoveAt(index);
            }
        }
        if (!g_keepCapture) {
            RemoveTouchHook();
        }
        if (d.active && d.column && d.column->list && !g_keepCapture) {
            d.column->list.ReleasePointerCaptures();
        }
    } catch (...) {
        LogHresult(L"ending a drag");
    }
    d = DragState();
}

void CancelDrag(WindowState* state) {
    if (state->drag.pending || state->drag.active) {
        DragEnd(state);
    }
}

void StartOutDrag(WindowState* state);

// For the touch log (0.28.1): the kind of pointer that started the drag that left the panel.
thread_local int g_outDevice = 2;
thread_local int g_dropSourceLogs = 0;
thread_local uint32_t g_outPointerId = 0;  // Of a touch drag that left the panel, else 0.

// The finger of a touch drag: whether it still touches, and where, in screen pixels. The shell's
// drag loop reads the mouse, so for touch the button and the cursor are taken from the finger.
std::atomic<bool> g_touchDown{false};
std::atomic<LONG> g_touchX{0};
std::atomic<LONG> g_touchY{0};
std::atomic<int> g_touchEvents{0};

// Windows gives the finger no pointer id that GetPointerInfo knows (error 87), so XAML's own events
// are the source: whether it touches and where, in screen pixels.
bool TouchContact(uint32_t, POINT* where) {
    if (!g_touchDown.load()) {
        return false;
    }
    if (where) {
        where->x = g_touchX.load();
        where->y = g_touchY.load();
    }
    return true;
}

void TouchTrack(WindowState* state, mux::Input::PointerRoutedEventArgs const& args, bool released) {
    try {
        auto point = args.GetCurrentPoint(nullptr);
        bool down = !released && point.IsInContact();
        auto at = point.Position();
        double scale = 1.0;
        if (state->rootGrid) {
            if (auto xamlRoot = state->rootGrid.XamlRoot()) {
                scale = xamlRoot.RasterizationScale();
            }
        }
        if (scale <= 0) {
            scale = 1.0;
        }
        POINT screen = {(LONG)(at.X * scale), (LONG)(at.Y * scale)};
        ClientToScreen((HWND)state->bridge.WindowId().Value, &screen);
        g_touchX = screen.x;
        g_touchY = screen.y;
        g_touchDown = down;
        if (g_outPointerId && g_touchEvents++ < 12) {
            LogVerbose(L"touch: finger event during the drag out, down=%d at %ld,%ld", down ? 1 : 0, screen.x, screen.y);
        }
    } catch (...) {
    }
}

// The finger as Windows itself sees it. XAML takes the finger away from a row ~40 ms into a drag
// (a pan, the pointer is replaced), so the drag runs in the shell's loop and this follows the
// finger in the window messages: WM_POINTER on the panel's windows, where GetPointerInfo works.
[[clang::no_destroy]] thread_local std::vector<HWND> g_touchHooked;
std::atomic<int> g_touchLogs{0};

LRESULT CALLBACK TouchSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_POINTERUPDATE || msg == WM_POINTERDOWN || msg == WM_POINTERUP) {
        UINT32 id = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE type = PT_POINTER;
        if (GetPointerType(id, &type) && type == PT_TOUCH) {
            POINTER_INFO info{};
            bool ok = GetPointerInfo(id, &info) != 0;
            if (ok) {
                bool down = msg != WM_POINTERUP && (info.pointerFlags & POINTER_FLAG_INCONTACT);
                g_touchX = info.ptPixelLocation.x;
                g_touchY = info.ptPixelLocation.y;
                g_touchDown = down;
            } else if (msg == WM_POINTERUP) {
                g_touchDown = false;
            }
            if (g_touchLogs++ < 30) {
                wchar_t cls[64] = L"";
                GetClassNameW(hwnd, cls, 64);
                LogVerbose(L"touch: win32 message 0x%04X id=%u ok=%d at %ld,%ld hwnd=%p %s", msg, id, ok ? 1 : 0,
                    ok ? info.ptPixelLocation.x : 0L, ok ? info.ptPixelLocation.y : 0L, hwnd, cls);
            }
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void InstallTouchHook(WindowState* state) {
    try {
        HWND bridge = (HWND)state->bridge.WindowId().Value;
        // The touch may be routed to any window of the frame, not the one under the finger, so
        // all of them listen; the log says which one hears it.
        std::vector<HWND> all{state->topLevel, bridge};
        EnumChildWindows(
            state->topLevel,
            [](HWND child, LPARAM param) -> BOOL {
                ((std::vector<HWND>*)param)->push_back(child);
                return TRUE;
            },
            (LPARAM)&all);
        g_touchLogs = 0;
        for (HWND hwnd : all) {
            if (std::find(g_touchHooked.begin(), g_touchHooked.end(), hwnd) == g_touchHooked.end() &&
                SetWindowSubclass(hwnd, TouchSubclassProc, 0x7A11, 0)) {
                g_touchHooked.push_back(hwnd);
            }
        }
        LogVerbose(L"touch: listening to the finger on %d window(s)", (int)g_touchHooked.size());
    } catch (...) {
        LogHresult(L"InstallTouchHook");
    }
}

void RemoveTouchHook() {
    for (HWND hwnd : g_touchHooked) {
        RemoveWindowSubclass(hwnd, TouchSubclassProc, 0x7A11);
    }
    g_touchHooked.clear();
}


// The finger's drag, by the system. XAML takes the finger away from a row at its first real move
// (the contact is replaced, no event of it reaches the panel again), so no drag of ours can follow
// it. The system's own drag (UIElement.StartDragAsync) is started at the hold, while the row still
// has the finger, and the platform follows it from there. It carries the items as files and
// folders, so it works for what is on disk.
// The data a shell target reads to take items from a drag: "Shell IDList Array", the folder's
// pidl and then the pidls of the items. Without it the folders of the shell (the panel's own,
// the navigation pane) refuse the files of the system's drag.
winrt::Windows::Foundation::IInspectable StreamOfBytes(std::vector<uint8_t> const& bytes) {
    namespace sts = winrt::Windows::Storage::Streams;
    sts::Buffer buffer((uint32_t)bytes.size());
    buffer.Length((uint32_t)bytes.size());
    uint8_t* target = nullptr;
    winrt::com_ptr<IBufferByteAccessLocal> access;
    winrt::check_hresult(((::IUnknown*)winrt::get_abi(buffer))->QueryInterface(kIidBufferByteAccess, access.put_void()));
    winrt::check_hresult(access->Buffer(&target));
    memcpy(target, bytes.data(), bytes.size());
    sts::InMemoryRandomAccessStream stream;
    stream.WriteAsync(buffer);  // An in-memory stream takes it at once.
    stream.Seek(0);
    return stream;
}

// The files of a touch drag, fetched while the finger is held, since the system's drag must be
// started in the very event of the first move, before the finger is taken from the row.
struct TouchPrep {
    bool ready = false;
    std::vector<winrt::Windows::Storage::IStorageItem> items;
    std::shared_ptr<std::vector<uint8_t>> cida;
    winrt::Windows::Graphics::Imaging::SoftwareBitmap picture{nullptr};  // Our label, as the picture of the drag.
    uint64_t serial = 0;
};
[[clang::no_destroy]] thread_local TouchPrep g_touchPrep;

// The operations a touch-drag coroutine is waiting on, so the unload can cancel them.
[[clang::no_destroy]] thread_local winrt::Windows::Foundation::IAsyncInfo g_prepOp{nullptr};
[[clang::no_destroy]] thread_local winrt::Windows::Foundation::IAsyncInfo g_dragOp{nullptr};

void CancelTouchDragOps() {
    for (auto* held : {&g_prepOp, &g_dragOp}) {
        try {
            if (*held) {
                held->Cancel();
            }
        } catch (...) {
        }
    }
}

winrt::fire_and_forget TouchDragPrepare(std::vector<std::wstring> paths, std::shared_ptr<std::vector<uint8_t>> cida) {
    CoroutineGuard counted;
    namespace st = winrt::Windows::Storage;
    uint64_t serial = ++g_touchPrep.serial;
    g_touchPrep.ready = false;
    g_touchPrep.items.clear();
    std::vector<st::IStorageItem> items;
    try {
        for (auto const& path : paths) {
            DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                auto op = st::StorageFolder::GetFolderFromPathAsync(path);
                g_prepOp = op.as<winrt::Windows::Foundation::IAsyncInfo>();
                items.push_back(co_await op);
            } else {
                auto op = st::StorageFile::GetFileFromPathAsync(path);
                g_prepOp = op.as<winrt::Windows::Foundation::IAsyncInfo>();
                items.push_back(co_await op);
            }
        }
    } catch (...) {
        items.clear();
        LogVerbose(L"touch: the files of the drag could not be opened, only the shell items go");
    }
    // The label of the panel's own drag, drawn once into a bitmap: the system's drag shows that
    // instead of a snapshot of the row. It is built off screen, then taken away.
    winrt::Windows::Graphics::Imaging::SoftwareBitmap picture{nullptr};
    try {
        WindowState* state = StateForThisThread(false);
        if (state && state->on && !g_unloading && state->overlay && state->drag.column && !state->drag.ghost &&
            serial == g_touchPrep.serial) {
            ShowGhost(state, SpecOf(state->drag.column, state->drag.entries));
            DragUpdateLabel(state);
            auto ghost = state->drag.ghost;
            if (ghost) {
                muxc::Canvas::SetLeft(ghost, -4000);
                muxc::Canvas::SetTop(ghost, 0);
                ghost.UpdateLayout();
                mux::Media::Imaging::RenderTargetBitmap target;
                co_await target.RenderAsync(ghost);
                auto pixels = co_await target.GetPixelsAsync();
                int width = target.PixelWidth(), height = target.PixelHeight();
                if (width > 0 && height > 0) {
                    picture = winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(
                        pixels, winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, width, height,
                        winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);
                }
                LogVerbose(L"touch: the label was drawn for the drag, %d x %d", width, height);
            }
            if (WindowState* after = StateForThisThread(false)) {
                RemoveGhost(after);
            }
        }
    } catch (...) {
        LogVerbose(L"touch: the label could not be drawn, the row's picture will do");
        if (WindowState* after = StateForThisThread(false)) {
            RemoveGhost(after);
        }
    }
    if (serial == g_touchPrep.serial) {
        g_touchPrep.items = std::move(items);
        g_touchPrep.cida = cida;
        g_touchPrep.picture = picture;
        g_touchPrep.ready = true;
        LogVerbose(L"touch: the drag's items are ready (%d file(s))", (int)g_touchPrep.items.size());
    }
}

winrt::fire_and_forget TouchDragRun(mux::UIElement list, decltype(DragState::touchPoint) point,
                                    std::vector<winrt::Windows::Storage::IStorageItem> items,
                                    std::shared_ptr<std::vector<uint8_t>> cida,
                                    winrt::Windows::Graphics::Imaging::SoftwareBitmap picture) {
    CoroutineGuard counted;
    namespace dt = winrt::Windows::ApplicationModel::DataTransfer;
    winrt::event_token token{};
    bool hooked = false;
    // The panel takes the system's drag as a drop target, for the folders of its own columns.
    mux::UIElement panel{nullptr};
    bool oldAllowDrop = false;
    winrt::event_token tokenEnter{}, tokenOver{}, tokenLeave{}, tokenDrop{};
    bool dropHooked = false;
    bool dragWasOurs = false;
    try {
        token = list.DragStarting([items, cida, picture](mux::UIElement const&, mux::DragStartingEventArgs const& args) {
            if (picture) {
                try {
                    args.DragUI().SetContentFromSoftwareBitmap(picture);
                } catch (...) {
                }
            }
            LogVerbose(L"touch: the system's drag is starting with %d file(s) and %d byte(s) of shell items", (int)items.size(),
                cida ? (int)cida->size() : 0);
            if (!items.empty()) {
                args.Data().SetStorageItems(items);
            }
            if (cida && !cida->empty()) {
                args.Data().SetDataProvider(L"Shell IDList Array", [cida](dt::DataProviderRequest const& request) {
                    try {
                        request.SetData(StreamOfBytes(*cida));
                    } catch (...) {
                    }
                });
            }
            // What the shell's targets take as the effect when no key says otherwise: a move.
            // (0.31.3, to see whether the Copy/Move/Cancel menu of the navigation pane goes.)
            args.Data().SetDataProvider(L"Preferred DropEffect", [](dt::DataProviderRequest const& request) {
                try {
                    request.SetData(StreamOfBytes(std::vector<uint8_t>{2, 0, 0, 0}));
                } catch (...) {
                }
            });
            args.AllowedOperations(dt::DataPackageOperation::Copy | dt::DataPackageOperation::Move |
                                   dt::DataPackageOperation::Link);
        });
        hooked = true;
        WindowState* state = StateForThisThread(false);
        if (!state || !state->on || g_unloading || !state->drag.pending || !state->drag.touchPoint) {
            list.DragStarting(token);
            co_return;
        }
        state->drag.platformDrag = true;
        state->drag.pending = false;
        state->drag.active = true;
        dragWasOurs = true;
        if (state->rootGrid) {
            panel = state->rootGrid;
            oldAllowDrop = panel.AllowDrop();
            panel.AllowDrop(true);
            auto over = [](winrt::Windows::Foundation::IInspectable const&, mux::DragEventArgs const& args) {
                WindowState* now = StateForThisThread(false);
                if (!now || !now->on || !now->drag.platformDrag || !now->rootGrid) {
                    return;
                }
                auto where = args.GetPosition(now->rootGrid);
                now->autoPoint = where;
                now->autoOn = true;
                {
                    // Does the cursor follow the finger in the system's drag? For the log.
                    static int overLogs = 0;
                    POINT cursor{};
                    GetCursorPos(&cursor);
                    if (overLogs++ < 16) {
                        LogVerbose(L"touch: over the panel at %.0f,%.0f (host), the cursor is at %ld,%ld", where.X, where.Y,
                            cursor.x, cursor.y);
                    }
                }
                DragRefreshTarget(now, where);
                using Mods = decltype(args.Modifiers());
                bool copy = ((uint32_t)args.Modifiers() & (uint32_t)Mods::Control) != 0;
                args.AcceptedOperation(!now->drag.targetOk ? dt::DataPackageOperation::None
                                       : copy              ? dt::DataPackageOperation::Copy
                                                           : dt::DataPackageOperation::Move);
                args.Handled(true);
            };
            tokenEnter = panel.DragEnter(over);
            tokenOver = panel.DragOver(over);
            tokenLeave = panel.DragLeave([](winrt::Windows::Foundation::IInspectable const&, mux::DragEventArgs const&) {
                WindowState* now = StateForThisThread(false);
                if (now && now->drag.platformDrag) {
                    DragClearTarget(now);
                    now->autoOn = false;
                }
            });
            tokenDrop = panel.Drop([](winrt::Windows::Foundation::IInspectable const&, mux::DragEventArgs const& args) {
                WindowState* now = StateForThisThread(false);
                if (!now || !now->on || !now->drag.platformDrag) {
                    return;
                }
                auto where = args.GetPosition(now->rootGrid);
                DragRefreshTarget(now, where);
                LogVerbose(L"touch: dropped in the panel, target column %d entry %d ok=%d",
                    now->drag.targetColumn ? ColumnIndexOfList(now, now->drag.targetColumn->list) + 1 : 0,
                    now->drag.targetEntry, now->drag.targetOk ? 1 : 0);
                now->drag.platformDrag = false;
                OnDragReleased(now);  // The drop itself, as for a drag of the mouse.
                args.AcceptedOperation(dt::DataPackageOperation::Move);
                args.Handled(true);
            });
            dropHooked = true;
        }
        LogVerbose(L"touch: asking the system to drag %d item(s)", (int)items.size());
        auto dragOp = list.StartDragAsync(*point);
        g_dragOp = dragOp.as<winrt::Windows::Foundation::IAsyncInfo>();
        auto result = co_await dragOp;
        g_dragOp = nullptr;
        LogVerbose(L"touch: the system's drag ended, operation %d", (int)result);
    } catch (winrt::hresult_error const& error) {
        LogVerbose(L"touch: the system's drag failed, hr=0x%08lX %s", (unsigned long)error.code().value, error.message().c_str());
        if (WindowState* state = StateForThisThread(false)) {
            state->drag.platformDrag = false;  // The shell's drag takes over at the first move.
        }
    } catch (...) {
        LogVerbose(L"touch: the system's drag failed");
        if (WindowState* state = StateForThisThread(false)) {
            state->drag.platformDrag = false;
        }
    }
    if (hooked) {
        try {
            list.DragStarting(token);
        } catch (...) {
        }
    }
    if (dropHooked) {
        try {
            panel.DragEnter(tokenEnter);
            panel.DragOver(tokenOver);
            panel.DragLeave(tokenLeave);
            panel.Drop(tokenDrop);
            panel.AllowDrop(oldAllowDrop);
        } catch (...) {
        }
    }
    if (WindowState* state = StateForThisThread(false)) {
        if (dragWasOurs && (state->drag.platformDrag || state->drag.active)) {
            state->drag.platformDrag = false;
            DragEnd(state);
        }
    }
}

// A hold on a row with a finger: a drag may now start. Scrolling of the list is switched off for as
// long as the drag may happen, and the finger is kept on the list.
void OnDragHold(WindowState* state, muxc::ListView const& list) {
    auto& d = state->drag;
    if (!d.pending || d.deviceType != 0 || d.armed || d.column == nullptr) {
        return;
    }
    int at = ColumnIndexOfList(state, list);
    if (at < 0 || state->columns[at].get() != d.column) {
        return;
    }
    d.armed = true;
    InstallTouchHook(state);
    try {
        auto lockOne = [&d](muxc::ScrollViewer const& viewer) {
            DragState::LockedScroller lock;
            lock.viewer = viewer;
            lock.v = viewer.VerticalScrollMode();
            lock.h = viewer.HorizontalScrollMode();
            viewer.VerticalScrollMode(muxc::ScrollMode::Disabled);
            viewer.HorizontalScrollMode(muxc::ScrollMode::Disabled);
            d.locked.push_back(lock);
        };
        if (auto inner = FindListScroller(list, 0)) {
            lockOne(inner);
        }
        mux::DependencyObject up = list;
        for (int i = 0; up && i < 40; i++) {
            up = mux::Media::VisualTreeHelper::GetParent(up);
            if (auto viewer = up.try_as<muxc::ScrollViewer>()) {
                lockOne(viewer);
            }
        }
        if (d.touchPointer) {
            list.CapturePointer(d.touchPointer);
        }
    } catch (...) {
        LogHresult(L"arming a touch drag");
    }
    LogVerbose(L"touch: held, the drag is armed, scrollers locked=%d", (int)d.locked.size());
    // The system's drag, for what is on disk.
    try {
        std::vector<size_t> picked = SelectedEntries(d.column, list);
        if (std::find(picked.begin(), picked.end(), (size_t)d.pressedEntry) == picked.end()) {
            picked.assign(1, (size_t)d.pressedEntry);
        }
        d.entries = picked;
        std::vector<std::wstring> paths;
        std::vector<Pidl> kids;
        for (size_t index : picked) {
            if (index >= d.column->entries.size()) {
                continue;
            }
            kids.emplace_back(ILClone(d.column->entries[index].child.get()));
            Pidl absolute(ILCombine(d.column->folder.get(), d.column->entries[index].child.get()));
            wchar_t path[MAX_PATH] = L"";
            if (absolute && SHGetPathFromIDListW(absolute.get(), path) && path[0]) {
                paths.push_back(path);
            }
        }
        auto cida = std::make_shared<std::vector<uint8_t>>();
        if (!kids.empty()) {
            UINT count = (UINT)kids.size();
            cida->assign(4 * (2 + count), 0);
            memcpy(cida->data(), &count, 4);
            auto put = [&cida](PCIDLIST_ABSOLUTE pidl, UINT slot) {
                UINT offset = (UINT)cida->size();
                UINT size = ILGetSize(pidl);
                cida->insert(cida->end(), (const uint8_t*)pidl, (const uint8_t*)pidl + size);
                memcpy(cida->data() + 4 + 4 * slot, &offset, 4);
            };
            put(d.column->folder.get(), 0);
            for (UINT i = 0; i < count; i++) {
                put(kids[i].get(), i + 1);
            }
        }
        if (!kids.empty() && d.touchPoint) {
            TouchDragPrepare(std::move(paths), cida);  // The drag itself starts at the first move.
        } else {
            LogVerbose(L"touch: nothing on disk to hand to the system's drag, the shell's drag will do");
        }
    } catch (...) {
        LogHresult(L"starting the system's drag");
    }
}

// A press on a row: a drag may start from it once the pointer moves.
void OnDragPressed(WindowState* state, muxc::ListView const& list, muxc::ListViewItem const& row,
                   mux::Input::PointerRoutedEventArgs const& args) {
    if (!args.GetCurrentPoint(list).Properties().IsLeftButtonPressed()) {
        return;
    }
    int at = ColumnIndexOfList(state, list);
    if (at < 0) {
        return;
    }
    Column* column = state->columns[at].get();
    state->activeAtPress = state->active;  // Which column was the active one before this click.
    int rowIndex = list.IndexFromContainer(row);
    int entry = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size() ? column->rowToEntry[rowIndex] : -1;
    if (entry < 0) {
        return;
    }
    DragEnd(state);
    auto& d = state->drag;
    d.pending = true;
    d.column = column;
    d.fromArchive = ArchiveParentColumn(state, column) >= 0;
    d.pressedEntry = entry;
    d.start = args.GetCurrentPoint(list).Position();
    d.deviceType = (int)args.Pointer().PointerDeviceType();
    g_touchPrep.ready = false;
    g_touchPrep.serial++;
    d.pointerId = args.Pointer().PointerId();
    d.touchPointer = d.deviceType == 0 ? args.Pointer() : nullptr;
    if (d.deviceType == 0) {
        d.touchPoint = args.GetCurrentPoint(row);
        d.pressedRow = row;
    }
    if (d.deviceType != 2) {
        auto point = args.GetCurrentPoint(list);
        LogVerbose(L"touch: a press on a row, device=%d id=%u left=%d inContact=%d pressure=%.2f", d.deviceType,
            args.Pointer().PointerId(), point.Properties().IsLeftButtonPressed() ? 1 : 0, point.IsInContact() ? 1 : 0,
            point.Properties().Pressure());
    }
    static int logged = 0;
    if (logged++ < 6) {
        LogVerbose(L"drag may start: pressed on row %d (entry %d) of column %d", rowIndex, entry, at + 1);
    }
}

// Puts the accent frame around the folder row, or the column, a drop would go
// to. Run again whenever the pointer moves or the columns scroll, since the row
// moves under a frame that stays still.
void DragPlaceFrame(WindowState* state) {
    auto& d = state->drag;
    if (!d.targetOk || !d.targetColumn || !state->overlay) {
        if (d.frame) {
            d.frame.Visibility(mux::Visibility::Collapsed);
        }
        return;
    }
    try {
        Column* column = d.targetColumn;
        mux::FrameworkElement target{nullptr};
        if (d.targetEntry >= 0) {
            int rowIndex = column->entryToRow[d.targetEntry];
            target = column->list.Items().GetAt((uint32_t)rowIndex).try_as<muxc::ListViewItem>();
        } else {
            target = column->cell;
        }
        if (!target) {
            if (d.frame) {
                d.frame.Visibility(mux::Visibility::Collapsed);
            }
            return;
        }
        auto at = target.TransformToVisual(state->overlay).TransformPoint(winrt::Windows::Foundation::Point{0, 0});
        double left = at.X, top = at.Y, width = target.ActualWidth(), height = target.ActualHeight();
        if (d.targetEntry >= 0) {
            // A row that is half scrolled out of its column is framed only where it shows.
            auto listAt = column->list.TransformToVisual(state->overlay)
                              .TransformPoint(winrt::Windows::Foundation::Point{0, 0});
            double listTop = listAt.Y, listBottom = listAt.Y + column->list.ActualHeight();
            if (top < listTop) {
                height -= listTop - top;
                top = listTop;
            }
            if (top + height > listBottom) {
                height = listBottom - top;
            }
        }
        if (width < 4 || height < 4) {
            if (d.frame) {
                d.frame.Visibility(mux::Visibility::Collapsed);
            }
            return;
        }
        if (!d.frame) {
            winrt::Windows::UI::Color tint = state->colorsSet ? state->pillColor
                                                              : winrt::Windows::UI::Color{255, 76, 194, 255};
            muxc::Border frame;
            frame.BorderBrush(Brush(255, tint.R, tint.G, tint.B));
            frame.BorderThickness(mux::Thickness{1.5, 1.5, 1.5, 1.5});
            frame.Background(Brush(12, tint.R, tint.G, tint.B));
            frame.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
            frame.IsHitTestVisible(false);
            state->overlay.Children().InsertAt(0, frame);  // Under the label.
            d.frame = frame;
        }
        muxc::Canvas::SetLeft(d.frame, left);
        muxc::Canvas::SetTop(d.frame, top);
        d.frame.Width(width);
        d.frame.Height(height);
        d.frame.Visibility(mux::Visibility::Visible);
    } catch (...) {
        LogHresult(L"framing the drop target");
    }
}

// Lights the folder row, or the column, a drop would go to.
void DragLight(WindowState* state, Column*, int) {
    DragPlaceFrame(state);
}

// Finds the target under a point of the panel (host coordinates) for the drag
// of rows, and lights it when the drop is allowed. Also run by the tick while
// a column scrolls under a pointer that does not move.
void DragRefreshTarget(WindowState* state, winrt::Windows::Foundation::Point where) {
    auto& d = state->drag;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    Column* targetColumn = nullptr;
    int targetEntry = -1;
    bool found = false;
    try {
        found = DragTargetAt(state, where, &targetColumn, &targetEntry);
    } catch (...) {
        LogHresult(L"finding the drop target");
    }
    if (!found || targetColumn != d.targetColumn || targetEntry != d.targetEntry ||
        ctrl != d.lastCtrl) {
        d.lastCtrl = ctrl;
        DragClearTarget(state);
        if (found) {
            d.targetColumn = targetColumn;
            d.targetEntry = targetEntry;
            d.targetOk = DragTargetAllowed(state, targetColumn, targetEntry);
            if (d.targetOk) {
                DragLight(state, targetColumn, targetEntry);
            }
        }
    }
    if (d.targetOk) {
        DragPlaceFrame(state);
    }
    if (d.ghost) {
        d.ghost.Opacity(d.targetOk || !found ? 1.0 : 0.8);
    }
}

// The label's content, as it is now.
GhostSpec SpecOf(Column* column, std::vector<size_t> const& entries) {
    GhostSpec spec;
    spec.count = entries.size();
    spec.name = entries.size() == 1 && entries[0] < column->entries.size()
                    ? column->entries[entries[0]].name
                    : std::to_wstring(entries.size()) + L" items";
    for (size_t at = 0; at < std::min<size_t>(3, entries.size()); ++at) {
        mux::Media::ImageSource source{nullptr};
        size_t entry = entries[at];
        try {
            if (entry < column->iconHosts.size() && column->iconHosts[entry] &&
                column->iconHosts[entry].Children().Size() > 0) {
                if (auto image = column->iconHosts[entry].Children().GetAt(0).try_as<muxc::Image>()) {
                    source = image.Source();
                }
            }
        } catch (...) {
        }
        spec.icons.push_back(source);
    }
    return spec;
}

// Builds the label that follows the pointer, on the surface of a WinUI tooltip
// (no stroke of its own): up to three icons of what is dragged, stacked, the
// name or the count, and a line saying what a drop would do there. It goes into
// the drag state and the overlay; the caller keeps the rest.
void ShowGhost(WindowState* state, GhostSpec const& spec) {
    auto& d = state->drag;
    muxc::ToolTip ghost;
    ghost.BorderThickness(mux::Thickness{0, 0, 0, 0});
    ghost.Padding(mux::Thickness{10, 8, 14, 8});
    ghost.IsHitTestVisible(false);
    muxc::StackPanel content;
    content.Orientation(muxc::Orientation::Horizontal);
    content.Spacing(10);
    size_t shown = std::max<size_t>(1, std::min<size_t>(3, spec.count));
    const double iconSize = 32;  // As big as the icon of Explorer's own drag image.
    muxc::Grid stack;
    stack.Width(iconSize + 6.0 * (shown - 1));
    stack.Height(iconSize + 4.0 * (shown - 1));
    stack.VerticalAlignment(mux::VerticalAlignment::Center);
    for (size_t at = shown; at-- > 0;) {  // Back to front.
        mux::UIElement piece{nullptr};
        if (at < spec.icons.size() && spec.icons[at]) {
            muxc::Image image;
            image.Source(spec.icons[at]);
            image.Width(iconSize);
            image.Height(iconSize);
            image.Stretch(mux::Media::Stretch::Uniform);
            piece = image;
        } else {
            muxc::FontIcon glyph;
            glyph.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            glyph.Glyph(L"");
            glyph.FontSize(26);
            glyph.Width(iconSize);
            glyph.Height(iconSize);
            piece = glyph;
        }
        auto frame = piece.as<mux::FrameworkElement>();
        if (shown == 1) {
            frame.HorizontalAlignment(mux::HorizontalAlignment::Center);
            frame.VerticalAlignment(mux::VerticalAlignment::Center);
        } else {
            frame.HorizontalAlignment(mux::HorizontalAlignment::Left);
            frame.VerticalAlignment(mux::VerticalAlignment::Bottom);
            frame.Margin(mux::Thickness{6.0 * at, 0, 0, 4.0 * at});
            piece.Opacity(at == 0 ? 1.0 : 0.85);
        }
        stack.Children().Append(piece);
    }
    content.Children().Append(stack);
    muxc::StackPanel texts;
    texts.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::TextBlock text;
    text.FontSize(12);
    text.MaxWidth(240);
    text.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    texts.Children().Append(text);
    muxc::StackPanel row;
    row.Orientation(muxc::Orientation::Horizontal);
    row.Spacing(5);
    row.Opacity(0.7);
    row.Visibility(mux::Visibility::Collapsed);
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.FontSize(11);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    row.Children().Append(icon);
    muxc::TextBlock action;
    action.FontSize(11);
    action.MaxWidth(220);
    action.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    row.Children().Append(action);
    texts.Children().Append(row);
    content.Children().Append(texts);
    ghost.Content(content);
    d.ghost = ghost;
    d.ghostText = text;
    d.ghostAction = action;
    d.ghostIcon = icon;
    d.ghostRow = row;
    d.labelName = spec.name;
    state->overlay.Children().Append(ghost);
    AnimateGhost(ghost, true);
}

// The label goes out, with its animation, and the drag state forgets it.
void RemoveGhost(WindowState* state) {
    auto& d = state->drag;
    if (d.ghost && state->overlay) {
        FadeOutGhost(state->overlay, d.ghost);
    }
    d.ghost = nullptr;
    d.ghostText = nullptr;
    d.ghostAction = nullptr;
    d.ghostIcon = nullptr;
    d.ghostRow = nullptr;
}

// The name of a folder, as the shell shows it.
std::wstring FolderDisplayName(PCIDLIST_ABSOLUTE pidl) {
    std::wstring result;
    PWSTR text = nullptr;
    if (pidl && SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &text)) && text) {
        result = text;
        CoTaskMemFree(text);
    }
    return result;
}

std::wstring Shortened(std::wstring text, size_t most) {
    if (text.size() > most) {
        text = text.substr(0, most - 1) + L"…";
    }
    return text;
}

// The two lines of the label: what is dragged, and what a drop would do.
void DragUpdateLabel(WindowState* state) {
    auto& d = state->drag;
    if (!d.ghostText) {
        return;
    }
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    d.ghostText.Text(Shortened(d.labelName, 40).c_str());
    if (!d.ghostAction) {
        return;
    }
    std::wstring line;
    const wchar_t* glyph = L"";
    if (d.targetColumn) {
        if (!d.targetOk) {
            line = L"Can’t drop here";
            glyph = L"";  // Forbidden.
        } else {
            std::wstring target =
                d.targetEntry >= 0 && (size_t)d.targetEntry < d.targetColumn->entries.size()
                    ? d.targetColumn->entries[d.targetEntry].name
                    : FolderDisplayName(d.targetColumn->folder.get());
            const wchar_t* verb = L"Drop in ";
            bool extract = d.fromArchive || state->outFromArchive;
            glyph = L"";  // Move to a folder.
            if (ctrl && !shift) {
                verb = L"Copy to ";
                glyph = L"";
            } else if (alt) {
                verb = L"Link in ";
                glyph = L"";
            } else if (shift) {
                verb = L"Move to ";
            }
            if (extract) {
                verb = L"Extract to ";
                glyph = L"\uE8C8";
            }
            line = std::wstring(verb) + Shortened(target, 28);
        }
    }
    if (line.empty()) {
        d.ghostRow.Visibility(mux::Visibility::Collapsed);
    } else {
        d.ghostIcon.Glyph(glyph);
        d.ghostAction.Text(line.c_str());
        d.ghostRow.Visibility(mux::Visibility::Visible);
    }
}

// Moves of the pointer. True while a drag is under way, so the hover code
// stands aside.
bool OnDragMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args) {
    auto& d = state->drag;
    if (args.Pointer().PointerDeviceType() == winrt::Microsoft::UI::Input::PointerDeviceType::Touch) {
        TouchTrack(state, args, false);
    }
    if (!d.pending && !d.active) {
        return false;
    }
    if (d.platformDrag) {
        return true;  // The system's drag has the finger.
    }
    if (d.column != column) {
        return d.active;
    }
    auto point = args.GetCurrentPoint(list);
    if (!point.Properties().IsLeftButtonPressed()) {
        DragEnd(state);
        return false;
    }
    auto at = point.Position();
    if (d.deviceType != 2 && d.movesLogged < 8) {
        d.movesLogged++;
        LogVerbose(L"touch: move %d at %.0f,%.0f, left=%d inContact=%d, pending=%d active=%d", d.movesLogged, at.X, at.Y,
            point.Properties().IsLeftButtonPressed() ? 1 : 0, point.IsInContact() ? 1 : 0, d.pending ? 1 : 0, d.active ? 1 : 0);
    }
    if (d.pending) {
        if (d.deviceType == 0 && (!d.armed || d.platformDrag)) {
            return false;  // A finger drags only after a hold: until then it pans.
        }
        if (std::abs(at.X - d.start.X) + std::abs(at.Y - d.start.Y) < 8) {
            return false;  // Still a click.
        }
        if (d.deviceType == 0) {
            // The finger moved after a hold: the system's drag starts now, in this event, while the
            // row still has the finger. Held and let go without moving, it is the menu instead.
            if (!g_touchPrep.ready || !d.pressedRow) {
                return false;
            }
            LogVerbose(L"touch: moved after the hold, starting the system's drag");
            decltype(DragState::touchPoint) now = args.GetCurrentPoint(d.pressedRow);
            TouchDragRun(d.pressedRow.as<mux::UIElement>(), now, g_touchPrep.items, g_touchPrep.cida, g_touchPrep.picture);
            return true;
        }
        std::vector<size_t> picked = SelectedEntries(column, list);
        if (std::find(picked.begin(), picked.end(), (size_t)d.pressedEntry) == picked.end()) {
            picked.assign(1, (size_t)d.pressedEntry);
        }
        d.entries = picked;
        d.pending = false;
        d.active = true;
        state->slowRenameAt = 0;
        Wh_Log(L"drag: moved far enough, taking the pointer");
        if (!d.armed) {
            list.CapturePointer(args.Pointer());
        }
        Wh_Log(L"drag: pointer taken, making the label");
        ShowGhost(state, SpecOf(column, d.entries));
        Wh_Log(L"drag started from column %d with %d item(s)", ColumnIndexOfList(state, list) + 1,
            (int)d.entries.size());
        if (d.deviceType == 0) {
            // A finger is taken from the panel's own drag at once, so the shell's drag loop
            // carries it, wherever the finger goes (the panel takes drops like any window).
            StartOutDrag(state);
            return true;
        }
    }

    // Where the label goes, and the target under the pointer.
    try {
        auto overlayAt = args.GetCurrentPoint(state->overlay).Position();
        muxc::Canvas::SetLeft(d.ghost, overlayAt.X + 16);
        muxc::Canvas::SetTop(d.ghost, overlayAt.Y + 16);
    } catch (...) {
    }
    state->autoPoint = args.GetCurrentPoint(nullptr).Position();
    state->autoOn = true;
    DragRefreshTarget(state, state->autoPoint);
    DragUpdateLabel(state);
    // Past the edge of the panel the drag goes on as a real one.
    if (state->rootGrid) {
        auto inRoot = args.GetCurrentPoint(state->rootGrid).Position();
        if (inRoot.X < 0 || inRoot.Y < 0 || inRoot.X > state->rootGrid.ActualWidth() ||
            inRoot.Y > state->rootGrid.ActualHeight()) {
            StartOutDrag(state);
            return true;
        }
    }
    return true;
}

// Hands the items to the folder's drop target. All the arguments are copies, so
// nothing depends on the columns still being as they were.
void DropItems(WindowState* state, Pidl sourceFolder, std::vector<Pidl> children, Pidl targetFolder,
               DWORD keyState) {
    IShellFolder* source = nullptr;
    HRESULT hr = ILIsEmpty(sourceFolder.get())
                     ? SHGetDesktopFolder(&source)
                     : SHBindToObject(nullptr, sourceFolder.get(), nullptr, IID_PPV_ARGS(&source));
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && source) {
        std::vector<PCUITEMID_CHILD> items;
        for (auto& child : children) {
            items.push_back(child.get());
        }
        hr = source->GetUIObjectOf(state->topLevel, (UINT)items.size(), items.data(), IID_IDataObject,
                                   nullptr, (void**)&data);
    }
    if (source) {
        source->Release();
    }
    IShellFolder* target = nullptr;
    if (SUCCEEDED(hr) && data) {
        hr = SHBindToObject(nullptr, targetFolder.get(), nullptr, IID_PPV_ARGS(&target));
    }
    IDropTarget* drop = nullptr;
    if (SUCCEEDED(hr) && target) {
        hr = target->CreateViewObject(state->topLevel, IID_PPV_ARGS(&drop));
    }
    if (target) {
        target->Release();
    }
    if (FAILED(hr) || !drop || !data) {
        Wh_Log(L"drop: no data object or drop target, hr=0x%08lX", hr);
        if (drop) {
            drop->Release();
        }
        if (data) {
            data->Release();
        }
        return;
    }
    POINT cursor;
    GetCursorPos(&cursor);
    POINTL where = {cursor.x, cursor.y};
    DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    hr = drop->DragEnter(data, keyState, where, &effect);
    if (SUCCEEDED(hr) && effect != DROPEFFECT_NONE) {
        DWORD offered = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
        drop->DragOver(keyState, where, &offered);
        DWORD result = offered;
        ModalScope modal;
        hr = drop->Drop(data, keyState, where, &result);
        Wh_Log(L"dropped %d item(s) on \"%s\": effect %lu, hr=0x%08lX", (int)children.size(),
            PathOf(targetFolder.get()).c_str(), (unsigned long)result, hr);
    } else {
        drop->DragLeave();
        Wh_Log(L"the folder takes no drop, hr=0x%08lX effect=%lu", hr, (unsigned long)effect);
    }
    data->Release();
    drop->Release();
    ReloadAll(state);
}

// Puts the effect a target of the shell takes when no key says otherwise into the
// dragged data.
void SetPreferredEffect(IDataObject* data, DWORD effect) {
    static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!memory) {
        return;
    }
    if (DWORD* value = (DWORD*)GlobalLock(memory)) {
        *value = effect;
        GlobalUnlock(memory);
    }
    FORMATETC wanted = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = memory;
    if (FAILED(data->SetData(&wanted, &medium, TRUE))) {
        GlobalFree(memory);
    }
}

// The drag source of a real drag. It ends the drag on Escape or when the button
// goes up, and notes what the target under the pointer last offered and which
// window it was, for the log.
struct OutDropSource : IDropSource {
    LONG refs = 1;
    DWORD lastEffect = 0;
    DWORD dropKeys = 0;
    DWORD graceKeys = 0;  // The keys of a moment ago that still count at the release.
    IDataObject* data = nullptr;  // The dragged data, not owned.
    ULONGLONG ctrlAt = 0, shiftAt = 0, altAt = 0;  // When each key was last seen down.
    HWND dropWindow = nullptr;
    POINT dropPoint{};
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDropSource) {
            *out = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG now = InterlockedDecrement(&refs);
        if (now == 0) {
            delete this;
        }
        return (ULONG)now;
    }
    STDMETHODIMP QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (g_outDevice != 2 && g_dropSourceLogs < 25) {
            g_dropSourceLogs++;
            POINT where;
            GetCursorPos(&where);
            LogVerbose(L"touch: the drag loop asks, keys=0x%lX escape=%d, cursor %ld,%ld, left button %s", (unsigned long)keys,
                escape ? 1 : 0, where.x, where.y, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? L"down" : L"up");
        }
        if (g_outPointerId) {
            POINT where;
            if (TouchContact(g_outPointerId, &where)) {
                keys |= MK_LBUTTON;
                SetCursorPos(where.x, where.y);
            } else {
                keys &= ~(DWORD)MK_LBUTTON;
            }
        }
        if (escape) {
            return DRAGDROP_S_CANCEL;
        }
        ULONGLONG now = GetTickCount64();
        if (keys & MK_CONTROL) {
            ctrlAt = now;
        }
        if (keys & MK_SHIFT) {
            shiftAt = now;
        }
        if (keys & MK_ALT) {
            altAt = now;
        }
        if (!(keys & MK_LBUTTON)) {
            // A key let go a moment before the button still counts: the two
            // rarely come up in the same instant, and the wrong one would turn
            // a copy into a move.
            const ULONGLONG grace = 400;
            DWORD wanted = DROPEFFECT_NONE;
            if (now - altAt < grace) {
                wanted = DROPEFFECT_LINK;
            } else if (now - ctrlAt < grace && now - shiftAt >= grace) {
                wanted = DROPEFFECT_COPY;
            } else if (now - shiftAt < grace) {
                wanted = DROPEFFECT_MOVE;
            }
            graceKeys = wanted == DROPEFFECT_COPY   ? MK_CONTROL
                        : wanted == DROPEFFECT_MOVE ? MK_SHIFT
                        : wanted == DROPEFFECT_LINK ? (MK_CONTROL | MK_SHIFT)
                                                    : 0;
            if (wanted != DROPEFFECT_NONE && data) {
                SetPreferredEffect(data, wanted);
                Wh_Log(L"drag out: the keys of a moment ago ask for effect %lu", (unsigned long)wanted);
            }
            POINT cursor;
            GetCursorPos(&cursor);
            dropPoint = cursor;
            dropKeys = keys;
            dropWindow = WindowFromPoint(cursor);
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }
    int feedbackCalls = 0;
    STDMETHODIMP GiveFeedback(DWORD effect) override {
        lastEffect = effect;
        feedbackCalls++;
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// The child of a shell item whose display name is `name`, or null. The caller
// releases it.
IShellItem* FindChildByName(IShellItem* parent, const std::wstring& name) {
    IEnumShellItems* items = nullptr;
    if (FAILED(parent->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))) || !items) {
        return nullptr;
    }
    IShellItem* found = nullptr;
    IShellItem* child = nullptr;
    ULONG fetched = 0;
    while (!found && items->Next(1, &child, &fetched) == S_OK && fetched == 1) {
        PWSTR text = nullptr;
        if (SUCCEEDED(child->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
            if (_wcsicmp(text, name.c_str()) == 0) {
                found = child;
                found->AddRef();
            }
            CoTaskMemFree(text);
        }
        child->Release();
        child = nullptr;
    }
    items->Release();
    return found;
}

// A folder called `name` among the places a pinned folder usually comes from: the
// known folders themselves, then what lies in the Desktop, the user's profile and
// This PC. The caller releases it.
IShellItem* FindInUsualPlaces(const std::wstring& name) {
    const KNOWNFOLDERID* ids[] = {&FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Pictures,
                                  &FOLDERID_Music,     &FOLDERID_Videos,    &FOLDERID_Desktop,
                                  &FOLDERID_Profile};
    std::vector<IShellItem*> places;
    for (const KNOWNFOLDERID* id : ids) {
        IShellItem* place = nullptr;
        if (SUCCEEDED(SHGetKnownFolderItem(*id, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&place))) && place) {
            places.push_back(place);
        }
    }
    PIDLIST_ABSOLUTE computer = nullptr;
    if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &computer)) && computer) {
        IShellItem* place = nullptr;
        if (SUCCEEDED(SHCreateItemFromIDList(computer, IID_PPV_ARGS(&place))) && place) {
            places.push_back(place);
        }
        CoTaskMemFree(computer);
    }
    IShellItem* found = nullptr;
    for (IShellItem* place : places) {
        if (found) {
            break;
        }
        PWSTR text = nullptr;
        if (SUCCEEDED(place->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
            if (_wcsicmp(text, name.c_str()) == 0) {
                found = place;
                found->AddRef();
            }
            CoTaskMemFree(text);
        }
        if (!found) {
            found = FindChildByName(place, name);
        }
    }
    for (IShellItem* place : places) {
        place->Release();
    }
    return found;
}

// The folder a navigation pane tree item stands for, found by walking the names
// of the item and its parents from the root: the tree keeps no pidl we can read.
// A root that is not under the Desktop is taken to be Home (Quick access).
Pidl FolderOfNavItem(HWND tree, HTREEITEM node) {
    std::vector<std::wstring> names;
    for (HTREEITEM walk = node; walk;
         walk = (HTREEITEM)SendMessageW(tree, TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)walk)) {
        wchar_t text[256] = L"";
        TVITEMW info = {};
        info.mask = TVIF_TEXT;
        info.hItem = walk;
        info.pszText = text;
        info.cchTextMax = ARRAYSIZE(text);
        SendMessageW(tree, TVM_GETITEMW, 0, (LPARAM)&info);
        names.insert(names.begin(), text);
    }
    if (names.empty()) {
        return Pidl();
    }
    IShellItem* current = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&current))) ||
        !current) {
        return Pidl();
    }
    size_t index = 0;
    if (IShellItem* root = FindChildByName(current, names[0])) {
        current->Release();
        current = root;
        index = 1;
    } else {
        current->Release();
        current = nullptr;
        // Home: the pinned folders are roots of the tree themselves, or children of
        // a root named Home, so look for the first name in Home, else skip it.
        if (FAILED(SHCreateItemFromParsingName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}", nullptr,
                                               IID_PPV_ARGS(&current))) ||
            !current) {
            Wh_Log(L"drag out: no folder found for the navigation pane item \"%s\"", names.back().c_str());
            return Pidl();
        }
        if (IShellItem* pinned = FindChildByName(current, names[0])) {
            current->Release();
            current = pinned;
        } else if (names.size() < 2) {
            current->Release();
            Wh_Log(L"drag out: no folder found for the navigation pane item \"%s\"", names.back().c_str());
            return Pidl();
        }
        index = 1;
    }
    for (; index < names.size(); ++index) {
        IShellItem* next = FindChildByName(current, names[index]);
        if (!next) {
            next = FindInUsualPlaces(names[index]);
        }
        current->Release();
        current = next;
        if (!current) {
            Wh_Log(L"drag out: no folder found for the navigation pane item \"%s\" (stopped at \"%s\", chain of %d, first \"%s\")",
                names.back().c_str(), names[index].c_str(), (int)names.size(), names[0].c_str());
            return Pidl();
        }
    }
    Pidl result;
    PWSTR path = nullptr;
    if (SUCCEEDED(current->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path, nullptr, &pidl, 0, nullptr)) && pidl) {
            result.reset(pidl);
        }
        CoTaskMemFree(path);
    }
    if (!result) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(current, &pidl)) && pidl) {
            result.reset(pidl);
        }
    }
    current->Release();
    return result;
}

// The shell's drag loop is driven by mouse move messages, and the panel's input
// (a XAML island) takes them as pointer messages, so the loop never sees the
// pointer move: a target hears of the release point only, and the navigation
// pane, the views of other windows and other applications show no hover and
// answer nothing until then. This thread posts the missing moves to the drag
// loop's thread while the drag runs, with the position and the keys as they are.
struct MoveNudger {
    std::atomic<bool> stop{false};
    std::atomic<int> sent{0};
    std::thread worker;
    void Start(DWORD threadId, uint32_t touchId = 0) {
        worker = std::thread([this, threadId, touchId]() {
            POINT last = {LONG_MIN, LONG_MIN};
            bool lastFinger = false;
            while (!stop.load()) {
                POINT now;
                bool finger = touchId && TouchContact(touchId, &now);
                if (finger) {
                    SetCursorPos(now.x, now.y);
                }
                bool fingerChanged = touchId && finger != lastFinger;
                lastFinger = finger;
                if ((finger || GetCursorPos(&now)) && (fingerChanged || now.x != last.x || now.y != last.y)) {
                    last = now;
                    WPARAM keys = 0;
                    if (finger || (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) keys |= MK_LBUTTON;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keys |= MK_CONTROL;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keys |= MK_SHIFT;
                    if (PostThreadMessageW(threadId, WM_MOUSEMOVE, keys,
                                           MAKELPARAM((WORD)now.x, (WORD)now.y))) {
                        sent++;
                    }
                }
                Sleep(15);
            }
        });
    }
    void Stop() {
        stop = true;
        if (worker.joinable()) {
            worker.join();
        }
    }
    ~MoveNudger() { Stop(); }
};

// A real drag, for when the pointer leaves the panel: the shell's own drag loop
// carries the items to whatever is under the pointer, another Explorer window,
// the desktop or another application. Started after the event that noticed it,
// and with the panel's own drag already ended.
void DoOutDrag(WindowState* state, Pidl sourceFolder, std::vector<Pidl> children,
               DWORD preferred) {
    IShellFolder* source = nullptr;
    HRESULT hr = ILIsEmpty(sourceFolder.get())
                     ? SHGetDesktopFolder(&source)
                     : SHBindToObject(nullptr, sourceFolder.get(), nullptr, IID_PPV_ARGS(&source));
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && source) {
        std::vector<PCUITEMID_CHILD> items;
        for (auto& child : children) {
            items.push_back(child.get());
        }
        hr = source->GetUIObjectOf(state->topLevel, (UINT)items.size(), items.data(), IID_IDataObject,
                                   nullptr, (void**)&data);
    }
    if (source) {
        source->Release();
    }
    if (FAILED(hr) || !data) {
        Wh_Log(L"drag out: no data object, hr=0x%08lX", hr);
        return;
    }
    DWORD effect = DROPEFFECT_NONE;
    // The keys held as the pointer left the panel decide the default effect: a
    // target of the shell reads it from the data when no key says otherwise. The
    // drag loop does not see a key pressed after it has started.
    if (state->outFromArchive) {
        preferred = DROPEFFECT_COPY;
    }
    if (preferred != DROPEFFECT_NONE) {
        SetPreferredEffect(data, preferred);
    }
    Wh_Log(L"drag out: %d item(s) leave the panel, starting the shell's drag, default effect %lu",
        (int)children.size(), (unsigned long)preferred);
    // 0.13.9: the panel's pointer capture (XAML or Win32) may still hold the mouse,
    // so the drag loop would see no moves until the release.
    ReleaseCapture();
    OutDropSource* dropSource = new OutDropSource();
    dropSource->data = data;
    MoveNudger nudger;
    nudger.Start(GetCurrentThreadId(), g_outPointerId);
    state->outDragActive = true;
    ULONGLONG outStarted = GetTickCount64();
    ModalScope modal;
    hr = SHDoDragDrop(state->topLevel, data, dropSource,
                      state->outFromArchive ? DROPEFFECT_COPY : (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK),
                      &effect);
    state->outDragActive = false;
    if (g_outDevice != 2) {
        LogVerbose(L"touch: the shell's drag ran for %d ms, hr=0x%08lX, effect %lu", (int)(GetTickCount64() - outStarted), hr,
            (unsigned long)effect);
    }
    g_outDevice = 2;
    if (g_outPointerId) {
        g_touchEvents = 0;
        try {
            for (auto& column : state->columns) {
                if (column && column->list) {
                    column->list.ReleasePointerCaptures();
                }
            }
        } catch (...) {
        }
    }
    g_outPointerId = 0;
    RemoveTouchHook();
    RemoveGhost(state);
    nudger.Stop();
    Wh_Log(L"drag out: the drag loop asked for feedback %d time(s), %d pointer move(s) posted to it",
        dropSource->feedbackCalls, nudger.sent.load());
    {
        wchar_t title[80] = L"";
        DWORD processId = 0;
        if (dropSource->dropWindow) {
            GetWindowTextW(dropSource->dropWindow, title, ARRAYSIZE(title));
            GetWindowThreadProcessId(dropSource->dropWindow, &processId);
        }
        Wh_Log(L"drag out ended: effect %lu, hr=0x%08lX; keys at the drop 0x%lX; the target last offered effect %lu; dropped over a \"%s\" window \"%s\" of process %lu",
            (unsigned long)effect, hr, (unsigned long)dropSource->dropKeys,
            (unsigned long)dropSource->lastEffect,
            dropSource->dropWindow ? ClassNameOf(dropSource->dropWindow).c_str() : L"?", title,
            (unsigned long)processId);
    }
    if (dropSource->dropWindow) {
        HWND root = GetAncestor(dropSource->dropWindow, GA_ROOT);
        RECT rect{};
        wchar_t rootTitle[80] = L"";
        if (root) {
            GetWindowRect(root, &rect);
            GetWindowTextW(root, rootTitle, ARRAYSIZE(rootTitle));
        }
        Wh_Log(L"drag out: released at %ld,%ld over top-level \"%s\" \"%s\" at %ld,%ld to %ld,%ld",
            dropSource->dropPoint.x, dropSource->dropPoint.y,
            root ? ClassNameOf(root).c_str() : L"?", rootTitle, rect.left, rect.top, rect.right,
            rect.bottom);
    }
    // 0.14.0: the drag loop gets no pointer moves from the panel's input, so it only
    // tells a target about the release point, and the navigation pane's tree needs a
    // hovered item to take a drop. Dropped on that tree, the drop is done here, to
    // the folder the item under the pointer stands for, with the folder's own drop
    // target like a drop between columns.
    HWND treeWindow = dropSource->dropWindow;
    POINT treePoint = dropSource->dropPoint;
    DWORD treeKeys = dropSource->dropKeys & (MK_CONTROL | MK_SHIFT | MK_ALT);
    treeKeys |= dropSource->graceKeys;
    DWORD lastOffered = dropSource->lastEffect;
    if (state->outFromArchive) {
        treeKeys = MK_CONTROL;
    }
    // A target that answered the drag with one effect has taken the drop itself;
    // only one that kept the whole offer (all three effects) has heard nothing.
    bool unanswered = (dropSource->lastEffect & (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK)) ==
                      (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK);
    dropSource->Release();
    data->Release();
    DWORD treeProcess = 0;
    if (treeWindow && ClassNameOf(treeWindow) == L"SysTreeView32") {
        GetWindowThreadProcessId(treeWindow, &treeProcess);
    }
    // A folder dropped on the tab strip opens in a new tab. The strip is Explorer's
    // own XAML and took no drop from this drag, so the mod does it: the release is
    // in the top band of the window, left of the caption buttons, and nothing
    // answered the drag.
    if (!children.empty() && lastOffered == 0 && treeWindow && !state->outFromArchive) {
        HWND top = GetAncestor(treeWindow, GA_ROOT);
        DWORD topProcess = 0;
        if (top) {
            GetWindowThreadProcessId(top, &topProcess);
        }
        RECT box{};
        if (top && topProcess == GetCurrentProcessId() && ClassNameOf(top) == L"CabinetWClass" &&
            GetWindowRect(top, &box)) {
            double scale = GetDpiForWindow(top) / 96.0;
            bool inStrip = treePoint.y >= box.top && treePoint.y <= box.top + (LONG)(46 * scale) &&
                           treePoint.x >= box.left && treePoint.x <= box.right - (LONG)(140 * scale);
            if (inStrip) {
                IShellFolder* sourceFolderObject = nullptr;
                HRESULT bound = ILIsEmpty(sourceFolder.get())
                                    ? SHGetDesktopFolder(&sourceFolderObject)
                                    : SHBindToObject(nullptr, sourceFolder.get(), nullptr,
                                                     IID_PPV_ARGS(&sourceFolderObject));
                if (SUCCEEDED(bound) && sourceFolderObject) {
                    for (auto& child : children) {
                        PCUITEMID_CHILD one = child.get();
                        ULONG attributes = SFGAO_FOLDER | SFGAO_STREAM;
                        if (SUCCEEDED(sourceFolderObject->GetAttributesOf(1, &one, &attributes)) &&
                            (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM)) {
                            Pidl folder(ILCombine(sourceFolder.get(), child.get()));
                            Wh_Log(L"drag out: a folder dropped on the tab strip opens in a new tab");
                            OpenFolderInNewTab(state, folder.get());
                            break;  // One tab per drop.
                        }
                    }
                    sourceFolderObject->Release();
                }
            }
        }
    }
    if (treeProcess == GetCurrentProcessId() && !children.empty() && !unanswered) {
        Wh_Log(L"drag out: the navigation pane answered the drag itself, nothing to finish");
    } else if (treeProcess == GetCurrentProcessId() && !children.empty()) {
        TVHITTESTINFO hit = {};
        hit.pt = treePoint;
        ScreenToClient(treeWindow, &hit.pt);
        HTREEITEM node = (HTREEITEM)SendMessageW(treeWindow, TVM_HITTEST, 0, (LPARAM)&hit);
        if (!node) {
            Wh_Log(L"drag out: released on the navigation pane, but not on an item");
        } else {
            Pidl target = FolderOfNavItem(treeWindow, node);
            if (target) {
                // The shell may have done the move itself: look before doing it again.
                IShellFolder* sourceFolderObject = nullptr;
                HRESULT bound = ILIsEmpty(sourceFolder.get())
                                    ? SHGetDesktopFolder(&sourceFolderObject)
                                    : SHBindToObject(nullptr, sourceFolder.get(), nullptr,
                                                     IID_PPV_ARGS(&sourceFolderObject));
                bool stillThere = true;
                if (SUCCEEDED(bound) && sourceFolderObject) {
                    PCUITEMID_CHILD one = children[0].get();
                    ULONG attributes = SFGAO_VALIDATE;
                    stillThere = SUCCEEDED(sourceFolderObject->GetAttributesOf(1, &one, &attributes));
                    sourceFolderObject->Release();
                }
                std::wstring targetPath = PathOf(target.get());
                if (!stillThere) {
                    Wh_Log(L"drag out: the items are gone from their folder, nothing left to drop on the navigation pane item");
                } else if (_wcsicmp(targetPath.c_str(), PathOf(sourceFolder.get()).c_str()) == 0) {
                    Wh_Log(L"drag out: the navigation pane item is the folder the items came from, nothing to do");
                } else {
                    Wh_Log(L"drag out: finishing the drop on the navigation pane item, folder \"%s\", keys 0x%lX",
                        targetPath.c_str(), (unsigned long)treeKeys);
                    DropItems(state, std::move(sourceFolder), std::move(children), std::move(target),
                              treeKeys);
                }
            }
        }
    }
    ReloadAll(state);
}

void StartOutDrag(WindowState* state) {
    auto& d = state->drag;
    if (!d.active || !d.column) {
        return;
    }
    Pidl sourceFolder(ILClone(d.column->folder.get()));
    std::vector<Pidl> children;
    for (size_t index : d.entries) {
        if (index < d.column->entries.size()) {
            children.emplace_back(ILClone(d.column->entries[index].child.get()));
        }
    }
    Wh_Log(L"drag out: the pointer left the panel, ending the panel's own drag");
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    DWORD preferred = alt ? DROPEFFECT_LINK : (ctrl && !shift) ? DROPEFFECT_COPY
                      : shift ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
    state->outSpec = SpecOf(d.column, d.entries);
    state->outFromArchive = d.fromArchive;
    g_outDevice = d.deviceType;
    g_outPointerId = d.deviceType == 0 ? d.pointerId : 0;
    g_dropSourceLogs = 0;
    if (g_outDevice != 2) {
        POINT where;
        GetCursorPos(&where);
        LogVerbose(L"touch: the drag leaves the panel, device=%d, the cursor is at %ld,%ld, the left button is %s", g_outDevice,
            where.x, where.y, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? L"down" : L"up");
    }
    g_keepCapture = g_outPointerId != 0;
    DragEnd(state);
    g_keepCapture = false;
    if (children.empty()) {
        RemoveTouchHook();
        return;
    }
    auto shared = std::make_shared<std::pair<Pidl, std::vector<Pidl>>>(std::move(sourceFolder),
                                                                       std::move(children));
    state->queue.TryEnqueue([shared, preferred]() {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->on || g_unloading) {
            return;
        }
        try {
            DoOutDrag(now, std::move(shared->first), std::move(shared->second), preferred);
        } catch (...) {
            LogHresult(L"dragging out");
        }
    });
}

// The button went up. True if it ended a drag.
bool OnDragReleased(WindowState* state) {
    auto& d = state->drag;
    if (d.platformDrag) {
        return true;  // The system's drag runs on its own; its end is seen by the code that started it.
    }
    if (d.pending) {
        DragEnd(state);
        return false;
    }
    if (!d.active) {
        return false;
    }
    Column* sourceColumn = d.column;
    Column* targetColumn = d.targetColumn;
    int targetEntry = d.targetEntry;
    // Judged again with the keys as they are at the release.
    bool ok = d.targetColumn && DragTargetAllowed(state, d.targetColumn, d.targetEntry);
    std::vector<size_t> entries = d.entries;
    bool fromArchive = d.fromArchive;
    DragEnd(state);
    if (!ok || !targetColumn || !sourceColumn) {
        Wh_Log(L"drag ended outside a folder that takes it");
        return true;
    }
    Pidl sourceFolder(ILClone(sourceColumn->folder.get()));
    std::vector<Pidl> children;
    for (size_t index : entries) {
        if (index < sourceColumn->entries.size()) {
            children.emplace_back(ILClone(sourceColumn->entries[index].child.get()));
        }
    }
    Pidl targetFolder = DragTargetFolder(targetColumn, targetEntry);
    if (children.empty() || !targetFolder) {
        return true;
    }
    DWORD keyState = MK_LBUTTON;
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        keyState |= MK_CONTROL;
    }
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        keyState |= MK_SHIFT;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        keyState |= MK_ALT;
    }
    if (fromArchive) {
        keyState = MK_LBUTTON | MK_CONTROL;  // Taken out of an archive: always a copy.
    }
    // After the event has finished, since the shell may show a dialog.
    auto shared = std::make_shared<std::tuple<Pidl, std::vector<Pidl>, Pidl>>(
        std::move(sourceFolder), std::move(children), std::move(targetFolder));
    state->queue.TryEnqueue([shared, keyState]() {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->on || g_unloading) {
            return;
        }
        try {
            DropItems(now, std::move(std::get<0>(*shared)), std::move(std::get<1>(*shared)),
                      std::move(std::get<2>(*shared)), keyState);
        } catch (...) {
            LogHresult(L"dropping items");
        }
    });
    return true;
}
