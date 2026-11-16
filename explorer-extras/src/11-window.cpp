// ---- Window creation ----
//
// CommandBarControl_Wave1::OnApplyTemplate runs when a window is built, and
// OnDpiChanged runs right after it, in the same call. OnDpiChanged takes the
// window's XamlRoot by value, which gives the whole island's tree. The View
// flyout is in that tree by the first timer tick after the call. Confirmed
// with probes/probe-window-created.wh.cpp 0.6 on 2026-11-13, build 26100.9974.
// OnDpiChanged is a coroutine: it moves the XamlRoot out of the caller's slot,
// so the slot has to be read before the original runs. `this` of command bar
// methods is never treated as a XAML object. That crashed Explorer in 0.1 and
// 0.2 of the probe.

bool ReadableMemory(const void* p, size_t size) {
    if (!p) {
        return false;
    }
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(p, &info, sizeof(info))) {
        return false;
    }
    if (info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || info.Protect == 0) {
        return false;
    }
    return (const char*)p + size <=
           (const char*)info.BaseAddress + info.RegionSize;
}

// `arg` is the caller's temporary: a slot holding the XamlRoot pointer. The
// object's first code address must lie in a package module.
void* XamlRootFromArgument(void* arg) {
    if (!ReadableMemory(arg, sizeof(void*))) {
        return nullptr;
    }
    void* object = *(void**)arg;
    if (!ReadableMemory(object, sizeof(void*))) {
        return nullptr;
    }
    const void* vtable = *(const void* const*)object;
    if (!ReadableMemory(vtable, sizeof(void*) * 3)) {
        return nullptr;
    }
    const void* first = *(const void* const*)vtable;
    HMODULE module = nullptr;
    if (!first || !GetModuleHandleExW(
                      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      (LPCWSTR)first, &module)) {
        return nullptr;
    }
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        return nullptr;
    }
    bool ours = StrStrIW(path, L"Microsoft.UI.Xaml.dll") ||
                StrStrIW(path, L"\\WindowsApps\\");
    return ours ? object : nullptr;
}

BOOL CALLBACK FindAnyCabinetOnThread(HWND hwnd, LPARAM lParam) {
    wchar_t cls[64];
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) &&
        wcscmp(cls, L"CabinetWClass") == 0) {
        *(HWND*)lParam = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND TopLevelOfThisThread() {
    HWND top = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), FindAnyCabinetOnThread, (LPARAM)&top);
    return top;
}

// Stops the timer and revokes its handler. The timer object itself is kept
// until the window's own cleanup, since it can't be released from inside its
// own tick.
void StopEntry(WindowState* state) {
    state->entryStopped = true;
    state->armTab = nullptr;
    try {
        if (state->entryTimer) {
            state->entryTimer.Stop();
            state->entryTimer.Tick(state->entryToken);
        }
    } catch (...) {
        LogHresult(L"StopEntry");
    }
}

// The window's own cleanup, on its thread, while the window still exists.
void ReleaseEntry(WindowState* state) {
    StopEntry(state);
    state->entryTimer = nullptr;
    state->xamlRoot = nullptr;
}

// Every 50 ms after the window is built: put the Columns entry into the View
// menu, then, if Columns was the last layout chosen, turn it on once the file
// list is visible.
void EntryTick() {
    if (g_unloading) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || !state->entryTimer || state->entryStopped) {
        return;
    }
    try {
        state->entryTries++;
        if (!state->topLevel || !IsWindow(state->topLevel)) {
            StopEntry(state);
            return;
        }
        if (!state->menuItem) {
            int visited = 0;
            auto content = state->xamlRoot.Content();
            if (auto root = content.try_as<mux::DependencyObject>()) {
                WalkForViewFlyout(state, root, 0, visited);
            }
            if (state->menuItem) {
                Out(L"Columns entry added at window creation, after %d ticks",
                    state->entryTries);
            } else if (state->entryTries >= 40) {
                Out(L"no View flyout found within 2 s of window creation, giving up");
                StopEntry(state);
            }
            return;
        }
        if (!g_restore || !LastLayoutWasColumns() || state->on) {
            StopEntry(state);
            return;
        }
        // A new tab: wait until it is the visible one, so the panel isn't
        // built on the tab that was showing before.
        if (state->armTab &&
            FindVisibleChild(state->topLevel, L"ShellTabWindowClass") !=
                state->armTab) {
            if (state->entryTries >= 100) {
                Out(L"the new tab never became visible, not restoring Columns");
                StopEntry(state);
            }
            return;
        }
        HWND fileList = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
        RECT rect;
        if (fileList && FileListRect(state->topLevel, fileList, &rect)) {
            // One tick with a visible file list. Three ticks in 0.3.0 left
            // Explorer's own list showing for about 270 ms.
            if (++state->restoreSettle >= 1) {
                // A folder that was left in another layout stays in it.
                IShellBrowser* openBrowser = BrowserOf(state->topLevel);
                Pidl openFolder = openBrowser ? CurrentFolder(openBrowser) : Pidl();
                if (openFolder && RememberedColumns(PathOf(openFolder.get())) == 0) {
                    Out(L"the folder this window opens on was left in its own layout, not restoring Columns");
                    StopEntry(state);
                    return;
                }
                Out(L"restoring Columns, window=%p, tick %d", state->topLevel,
                    state->entryTries);
                ShowPanel(state);
                if (state->on && state->menuItem) {
                    state->menuItem.IsChecked(true);
                }
                StopEntry(state);
            }
        } else if (state->entryTries >= 100) {
            Out(L"no visible file list within 5 s, not restoring Columns");
            StopEntry(state);
        }
    } catch (...) {
        LogHresult(L"EntryTick");
        StopEntry(state);
    }
}

// A new tab was created in a window this mod already knows. If Columns was the
// last layout chosen and this window isn't showing it, turn it on for the tab
// once the tab is visible.
void OnTabCreated(HWND tab) {
    if (g_unloading || !g_enabled) {
        return;
    }
    // The tab that "open in a new tab" asked for.
    {
        WindowState* waiting = StateForThisThread(false);
        if (waiting && waiting->openTabFolder && !waiting->newTab &&
            GetTickCount64() < waiting->openTabUntil && waiting->topLevel &&
            GetAncestor(tab, GA_PARENT) == waiting->topLevel) {
            waiting->newTab = tab;
        }
    }
    if (!g_restore) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || !state->topLevel || !IsWindow(state->topLevel) ||
        GetAncestor(tab, GA_PARENT) != state->topLevel || state->on ||
        !state->queue || !LastLayoutWasColumns()) {
        return;
    }
    state->armTab = tab;
    state->entryTries = 0;
    state->restoreSettle = 0;
    if (state->entryTimer && !state->entryStopped) {
        return;  // Still running. It waits for this tab now.
    }
    try {
        auto timer = state->entryTimer ? state->entryTimer
                                       : state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(50));
        state->entryToken = timer.Tick(
            [](mud::DispatcherQueueTimer const&, Inspectable const&) {
                EntryTick();
            });
        state->entryStopped = false;
        timer.Start();
        state->entryTimer = timer;
        Out(L"new tab %p, waiting for it to show Columns", tab);
    } catch (...) {
        LogHresult(L"OnTabCreated");
        state->entryStopped = true;
    }
}

using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int,
                                        int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
CreateWindowExW_t CreateWindowExW_Original;
HWND WINAPI CreateWindowExW_Hook(DWORD exStyle, LPCWSTR className,
                                 LPCWSTR title, DWORD style, int x, int y,
                                 int w, int h, HWND parent, HMENU menu,
                                 HINSTANCE instance, LPVOID param) {
    HWND hwnd = CreateWindowExW_Original(exStyle, className, title, style, x, y,
                                         w, h, parent, menu, instance, param);
    // The tab window is returned after its views exist, on the window's own
    // thread. Confirmed with probes/probe-tabs-virtual.wh.cpp on 2026-11-13.
    if (hwnd && parent && className && !IS_INTRESOURCE(className) &&
        !g_unloading && wcscmp(className, L"ShellTabWindowClass") == 0) {
        try {
            OnTabCreated(hwnd);
        } catch (...) {
            LogHresult(L"CreateWindowExW hook");
        }
    }
    return hwnd;
}

void OnWindowCreated(Inspectable const& rootObject) {
    if (g_unloading || (!g_enabled && !g_detailsPane)) {
        return;
    }
    auto xamlRoot = rootObject.try_as<mux::XamlRoot>();
    HWND top = TopLevelOfThisThread();
    // At window close the same method runs again, with no tab left.
    if (!xamlRoot || !top ||
        !FindWindowExW(top, nullptr, L"ShellTabWindowClass", nullptr)) {
        return;
    }
    WindowState* state = StateForThisThread(true);
    if (state->topLevel && state->topLevel != top &&
        !IsWindow(state->topLevel)) {
        // The thread's earlier window is gone. Let its objects go.
        AbandonWindow(state);
    }
    if (state->topLevel == top &&
        (state->entryTimer || state->menuItem || state->entryStopped)) {
        return;  // Already handled, this is a later resize or DPI change.
    }
    state->topLevel = top;
    state->queue = mud::DispatcherQueue::GetForCurrentThread();
    state->xamlRoot = xamlRoot;
    state->entryTries = 0;
    state->restoreSettle = 0;
    auto timer = state->queue.CreateTimer();
    timer.Interval(std::chrono::milliseconds(50));
    state->entryToken = timer.Tick(
        [](mud::DispatcherQueueTimer const&, Inspectable const&) {
            EntryTick();
        });
    timer.Start();
    state->entryTimer = timer;
    StartDetails(state);
    Out(L"window created, window=%p, looking for the View flyout", top);
}

using DpiChanged_t = void*(WINAPI*)(void* pThis, void* ret, void* root,
                                    void* args);
DpiChanged_t DpiChanged_Original;
void* WINAPI DpiChanged_Hook(void* pThis, void* ret, void* root, void* args) {
    // The reference is taken before the original, which moves the XamlRoot
    // out of the caller's slot.
    Inspectable hold{nullptr};
    if (!g_unloading && (g_enabled || g_detailsPane)) {
        try {
            if (void* object = XamlRootFromArgument(root)) {
                winrt::copy_from_abi(hold, object);
            }
        } catch (...) {
            LogHresult(L"OnDpiChanged, before");
        }
    }
    void* result = DpiChanged_Original(pThis, ret, root, args);
    if (hold) {
        try {
            OnWindowCreated(hold);
        } catch (...) {
            LogHresult(L"OnDpiChanged, after");
        }
    }
    return result;
}

using BarHandler_t = void(WINAPI*)(void* pThis, Inspectable const& sender,
                                   void* args);


BarHandler_t PointerPressed_Original;
void WINAPI PointerPressed_Hook(void* pThis, Inspectable const& sender,
                                void* args) {
    // Before the original, so the entry is there when the menu opens.
    OnCommandBarPointer(sender);
    PointerPressed_Original(pThis, sender, args);
}

BarHandler_t PointerPressedWave1_Original;
void WINAPI PointerPressedWave1_Hook(void* pThis, Inspectable const& sender,
                                     void* args) {
    OnCommandBarPointer(sender);
    PointerPressedWave1_Original(pThis, sender, args);
}

// Any command chosen in a command bar menu. The Sort menu's choices come
// through here, so this is where a change of sort or group is noticed. The
// settings are read before the command runs and again shortly after.
using InvokeMenuItem_t = void(WINAPI*)(void* pThis, void* menuItem);
InvokeMenuItem_t InvokeMenuItem_Original;
void WINAPI InvokeMenuItem_Hook(void* pThis, void* menuItem) {
    WindowState* state = g_unloading ? nullptr : StateForThisThread(false);
    if (state && state->on) {
        try {
            state->specBeforeMenu = SortSpec{};
            if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
                ReadSortSpec(browser, &state->specBeforeMenu);
            }
        } catch (...) {
        }
    }
    InvokeMenuItem_Original(pThis, menuItem);
    if (state && state->on) {
        ULONGLONG now = GetTickCount64();
        state->specCheckAt = now + 300;
        state->postActionAt = now + 150;
        state->syncSelAt = now + 70;
        state->postActionAt2 = now + 800;
        state->watchEditUntil = now + 1500;
        Out(L"toolbar command invoked");
    }
}

bool g_hooked = false;

void HookExtensions(HMODULE module) {
    // FileExplorerExtensions.dll
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &PointerPressed_Original,
         PointerPressed_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &PointerPressedWave1_Original,
         PointerPressedWave1_Hook,
         true},
        {{LR"(public: struct winrt::fire_and_forget __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::OnDpiChanged(struct winrt::Microsoft::UI::Xaml::XamlRoot,struct winrt::Microsoft::UI::Xaml::XamlRootChangedEventArgs))"},
         &DpiChanged_Original,
         DpiChanged_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarManager::InvokeMenuItem(struct winrt::WindowsUdk::UI::MenuItem const &))"},
         &InvokeMenuItem_Original,
         InvokeMenuItem_Hook,
         true},
    };
    if (!Wh_SetFunctionHook((void*)TranslateAcceleratorW,
                            (void*)TranslateAcceleratorW_Hook,
                            (void**)&TranslateAcceleratorW_Original)) {
        Out(L"could not hook TranslateAcceleratorW, Backspace in the rename box may not work");
    }
    if (!Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExW_Hook,
                            (void**)&CreateWindowExW_Original)) {
        Out(L"could not hook CreateWindowExW, new tabs won't open in Columns");
    }
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Out(L"HookSymbols returned %d, PointerPressedHandler=%p, Wave1=%p, InvokeMenuItem=%p",
        ok ? 1 : 0, (void*)PointerPressed_Original,
        (void*)PointerPressedWave1_Original, (void*)InvokeMenuItem_Original);
    Out(L"OnDpiChanged=%p", (void*)DpiChanged_Original);
    g_hooked = PointerPressed_Original || PointerPressedWave1_Original ||
               DpiChanged_Original;
}

// Takes the panel and the menu entry out of every window, each on its own UI
// thread, and waits for it.
void CloseEverything() {
    std::vector<std::unique_ptr<WindowState>> states;
    {
        std::lock_guard<std::mutex> lock(g_statesMutex);
        states.swap(g_states);
    }
    for (auto& owned : states) {
        WindowState* state = owned.release();
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool queued = false;
        try {
            if (state->queue) {
                queued = state->queue.TryEnqueue([state, done]() {
                    if (state->topLevel && !IsWindow(state->topLevel)) {
                        AbandonWindow(state);
                    }
                    HidePanel(state, L"mod unloading");
                    RemoveMenuEntry(state);
                    ReleaseDetails(state);
                    ReleaseEntry(state);
                    state->queue = nullptr;
                    delete state;
                    if (done) {
                        SetEvent(done);
                    }
                });
            }
        } catch (...) {
            LogHresult(L"CloseEverything");
        }
        if (queued && done) {
            DWORD wait = WaitForSingleObject(done, 4000);
            Out(L"window cleanup %s",
                wait == WAIT_OBJECT_0 ? L"done" : L"TIMED OUT");
            if (wait == WAIT_OBJECT_0) {
                CloseHandle(done);
            }
        } else {
            // The window's thread is gone. Its objects can't be released from
            // here, so the state is left alone on purpose.
            Out(L"window cleanup skipped, its thread no longer takes work");
            if (done) {
                CloseHandle(done);
            }
        }
    }
}

#else  // MOD_NO_WINRT: stand-ins for a compile check without WinRT headers

bool g_hooked = false;
void HookExtensions(HMODULE) {}
void CloseEverything() {}
void CloseSettingsWindowAtUnload() {}
void StartWorker() {}
void StopWorker() {}

