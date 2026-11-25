// ==WindhawkMod==
// @id              explorer-search-box-resize
// @name            Explorer Search Box Resize
// @description     Resize File Explorer's search box by dragging its edge, or give it a fixed width. A double click on the edge goes back to the width in the settings.
// @version         1.0.0
// @author          AristideBH
// @github          https://github.com/AristideBH
// @donateUrl       https://ko-fi.com/aristidebh
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshlwapi
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Explorer Search Box Resize

Make File Explorer's search box as wide as you want.

- **Drag to resize.** Drag the edge between the address bar and the search box, with the mouse, a finger or a pen. The width is kept for every window.
- **Go back.** A double click or double tap on the edge goes back to the width in the settings.
- **Fixed width.** Set a width in pixels in the settings, or leave 0 for Explorer's own proportion.

The address bar always keeps at least 240 pixels, and the search box at least 160.

For tighter buttons and round corners in the same toolbar, use [Windows 11 File Explorer Styler](https://windhawk.net/mods/windows-11-file-explorer-styler).

Tested on Windows 11 24H2 (build 26100) and later, on ARM64. The mod needs Windhawk to download the symbols of `FileExplorerExtensions.dll`.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- dragToResize: true
  $name: Resize by dragging
  $description: Drag the edge between the address bar and the search box. A double click on it goes back to the width below.
- width: 0
  $name: Search box width
  $description: In pixels. 0 keeps Explorer's own proportion. A width you dragged is kept until you change this value.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shlwapi.h>

#undef GetCurrentTime

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <windhawk_utils.h>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
namespace mud = winrt::Microsoft::UI::Dispatching;
using Inspectable = winrt::Windows::Foundation::IInspectable;

std::atomic<bool> g_unloading{false};
std::atomic<bool> g_dragToResize{true};
std::atomic<int> g_width{0};

void LoadSettings() {
    g_dragToResize = Wh_GetIntSetting(L"dragToResize") != 0;
    g_width = std::max(0, Wh_GetIntSetting(L"width"));
}

// ---- Per window -------------------------------------------------------------------------------------------------------
//
// Each Explorer window has its own UI thread and one XAML island. NavigationBarControlGrid holds three columns: the
// navigation buttons, the address bar, and the search box (Explorer gives it 3*). The mod sets the third column's width and
// puts a transparent handle on its left edge. Everything a window holds lives on its thread.

struct WindowParts {
    winrt::weak_ref<muxc::Grid> grid;
    winrt::weak_ref<muxc::Border> handle;
    mux::GridLength original = {3, mux::GridUnitType::Star};
};

struct ThreadState {
    std::vector<std::shared_ptr<WindowParts>> windows;
    std::vector<std::shared_ptr<void>> revokers;  // Every handler the mod gave, taken off at unload.
    mud::DispatcherQueueTimer timer{nullptr};    // Looks for the grid of a window that is being built.
    std::vector<winrt::weak_ref<mux::XamlRoot>> pending;
    int ticks = 0;
    bool registered = false;
};
[[clang::no_destroy]] thread_local ThreadState t_state;

// The threads that hold a window, so settings changes and the unload reach each one.
struct KnownThread {
    DWORD id = 0;
    mud::DispatcherQueue queue{nullptr};
};
std::mutex g_threadsMutex;
[[clang::no_destroy]] std::vector<KnownThread> g_threads;

void RegisterThread() {
    if (t_state.registered) {
        return;
    }
    try {
        if (auto queue = mud::DispatcherQueue::GetForCurrentThread()) {
            std::lock_guard<std::mutex> lock(g_threadsMutex);
            g_threads.push_back({GetCurrentThreadId(), queue});
            t_state.registered = true;
        }
    } catch (...) {
    }
}

template <typename Revoker>
void Keep(Revoker&& revoker) {
    t_state.revokers.push_back(std::make_shared<std::decay_t<Revoker>>(std::move(revoker)));
}

mux::FrameworkElement FindByName(mux::DependencyObject const& node, const wchar_t* name, int depth) {
    if (!node || depth > 30) {
        return nullptr;
    }
    if (auto element = node.try_as<mux::FrameworkElement>(); element && element.Name() == name) {
        return element;
    }
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        if (auto found = FindByName(mux::Media::VisualTreeHelper::GetChild(node, i), name, depth + 1)) {
            return found;
        }
    }
    return nullptr;
}

// The width the search box gets for a wanted one: at least 160, at most what leaves 240 for the address bar.
double ClampWidth(double wanted, double gridWidth, double leftWidth) {
    double most = std::max(160.0, gridWidth - leftWidth - 240);
    return std::min(std::max(wanted, 160.0), most);
}

// The wanted width: the dragged one while the setting it was made against is unchanged, else the setting.
double WantedWidth() {
    int stored = Wh_GetIntValue(L"draggedWidth", 0);
    if (g_dragToResize && stored > 0 && Wh_GetIntValue(L"draggedBase", -1) == g_width) {
        return stored;
    }
    return g_width;
}

void Apply(WindowParts const& parts, double forced = 0) {
    auto grid = parts.grid.get();
    if (!grid) {
        return;
    }
    try {
        auto columns = grid.ColumnDefinitions();
        if (columns.Size() < 3) {
            return;
        }
        auto column = columns.GetAt(2);
        double want = forced > 0 ? forced : WantedWidth();
        if (g_unloading || want <= 0 || grid.ActualWidth() <= 0) {
            column.Width(parts.original);
        } else {
            column.Width(mux::GridLength{ClampWidth(want, grid.ActualWidth(), columns.GetAt(0).ActualWidth()),
                                         mux::GridUnitType::Pixel});
        }
        if (auto handle = parts.handle.get()) {
            handle.Visibility(g_dragToResize && !g_unloading ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        }
    } catch (...) {
    }
}

void ApplyThread() {
    for (auto const& parts : t_state.windows) {
        Apply(*parts);
    }
}

// A handle on the edge between the address bar and the search box.
void MakeHandle(std::shared_ptr<WindowParts> const& parts, muxc::Grid const& grid) {
    muxc::Border handle;
    handle.Width(16);
    handle.Height(36);
    handle.HorizontalAlignment(mux::HorizontalAlignment::Left);
    handle.VerticalAlignment(mux::VerticalAlignment::Center);
    handle.Margin(mux::Thickness{-8, 0, 0, 0});
    handle.Background(mux::Media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
    muxc::Grid::SetColumn(handle, 2);
    try {
        handle.as<mux::IUIElementProtected>().ProtectedCursor(winrt::Microsoft::UI::Input::InputSystemCursor::Create(
            winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));
    } catch (...) {
    }
    auto drag = std::make_shared<std::pair<bool, double>>(false, 0.0);  // dragging, the pointer's offset from the edge
    auto weakGrid = winrt::make_weak(grid);
    std::weak_ptr<WindowParts> self = parts;
    auto edgeOf = [weakGrid]() {
        auto g = weakGrid.get();
        return g ? g.ActualWidth() - g.ColumnDefinitions().GetAt(2).ActualWidth() : 0.0;
    };
    Keep(handle.PointerPressed(winrt::auto_revoke, [drag, weakGrid, edgeOf](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
        if (auto g = weakGrid.get()) {
            drag->first = true;
            drag->second = args.GetCurrentPoint(g).Position().X - edgeOf();
            sender.as<mux::UIElement>().CapturePointer(args.Pointer());
            args.Handled(true);
        }
    }));
    Keep(handle.PointerMoved(winrt::auto_revoke, [drag, weakGrid, self](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
        if (drag->first) {
            auto g = weakGrid.get();
            auto parts = self.lock();
            if (g && parts) {
                double edge = args.GetCurrentPoint(g).Position().X - drag->second;
                Apply(*parts, g.ActualWidth() - edge);
            }
        }
    }));
    auto finish = [drag, weakGrid]() {
        if (!drag->first) {
            return;
        }
        drag->first = false;
        if (auto g = weakGrid.get()) {
            auto width = g.ColumnDefinitions().GetAt(2).Width();
            if (width.GridUnitType == mux::GridUnitType::Pixel && width.Value > 0) {
                Wh_SetIntValue(L"draggedWidth", (int)std::lround(width.Value));
                Wh_SetIntValue(L"draggedBase", g_width);
            }
        }
    };
    Keep(handle.PointerReleased(winrt::auto_revoke, [finish](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
        sender.as<mux::UIElement>().ReleasePointerCapture(args.Pointer());
        finish();
    }));
    Keep(handle.PointerCanceled(winrt::auto_revoke, [finish](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) { finish(); }));
    Keep(handle.PointerCaptureLost(winrt::auto_revoke, [finish](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) { finish(); }));
    Keep(handle.DoubleTapped(winrt::auto_revoke, [self](Inspectable const&, mux::Input::DoubleTappedRoutedEventArgs const& args) {
        Wh_SetIntValue(L"draggedWidth", 0);
        if (auto parts = self.lock()) {
            Apply(*parts);
        }
        args.Handled(true);
    }));
    // A narrower window keeps the address bar's room.
    Keep(grid.SizeChanged(winrt::auto_revoke, [self](Inspectable const&, mux::SizeChangedEventArgs const&) {
        auto parts = self.lock();
        if (parts && !g_unloading) {
            Apply(*parts);
        }
    }));
    grid.Children().Append(handle);
    parts->handle = winrt::make_weak(handle);
}

// Looks for the grid of one window; true when it was found.
bool TryAttach(mux::XamlRoot const& root) {
    auto content = root ? root.Content() : nullptr;
    if (!content) {
        return false;
    }
    auto grid = FindByName(content, L"NavigationBarControlGrid", 0).try_as<muxc::Grid>();
    if (!grid || grid.ColumnDefinitions().Size() < 3) {
        return false;
    }
    // Windows that closed are dropped; a window already done is left alone.
    t_state.windows.erase(std::remove_if(t_state.windows.begin(), t_state.windows.end(),
                                         [](auto const& known) { return !known->grid.get(); }),
                          t_state.windows.end());
    for (auto const& known : t_state.windows) {
        if (known->grid.get() == grid) {
            return true;
        }
    }
    auto parts = std::make_shared<WindowParts>();
    parts->grid = winrt::make_weak(grid);
    parts->original = grid.ColumnDefinitions().GetAt(2).Width();
    MakeHandle(parts, grid);
    t_state.windows.push_back(parts);
    Apply(*parts);
    Wh_Log(L"search box attached, %d window(s) on this thread", (int)t_state.windows.size());
    return true;
}

// A window was built or used: its grid is looked for on every tick for up to 10 s, since the island fills after the call.
void OnWindow(mux::XamlRoot const& root) {
    if (g_unloading || !root) {
        return;
    }
    RegisterThread();
    if (TryAttach(root)) {
        return;
    }
    t_state.pending.push_back(winrt::make_weak(root));
    t_state.ticks = 0;
    if (!t_state.timer) {
        auto queue = mud::DispatcherQueue::GetForCurrentThread();
        if (!queue) {
            return;
        }
        t_state.timer = queue.CreateTimer();
        t_state.timer.Interval(std::chrono::milliseconds(100));
        Keep(t_state.timer.Tick(winrt::auto_revoke, [](mud::DispatcherQueueTimer const& timer, Inspectable const&) {
            auto pending = std::move(t_state.pending);
            t_state.pending.clear();
            for (auto& weak : pending) {
                auto root = weak.get();
                if (root && !g_unloading && !TryAttach(root)) {
                    t_state.pending.push_back(weak);
                }
            }
            if (t_state.pending.empty() || ++t_state.ticks > 100 || g_unloading) {
                t_state.pending.clear();
                timer.Stop();
            }
        }));
    }
    t_state.timer.Start();
}

// At unload, on each window's thread: Explorer's width back, the handle and every handler gone.
void ReleaseThread() {
    if (t_state.timer) {
        try {
            t_state.timer.Stop();
        } catch (...) {
        }
    }
    auto revokers = std::move(t_state.revokers);
    t_state.revokers.clear();
    revokers.clear();
    for (auto const& parts : t_state.windows) {
        try {
            if (auto grid = parts->grid.get()) {
                if (grid.ColumnDefinitions().Size() >= 3) {
                    grid.ColumnDefinitions().GetAt(2).Width(parts->original);
                }
                if (auto handle = parts->handle.get()) {
                    uint32_t at = 0;
                    if (grid.Children().IndexOf(handle, at)) {
                        grid.Children().RemoveAt(at);
                    }
                }
            }
        } catch (...) {
        }
    }
    t_state = ThreadState();
}

// ---- Hooks ------------------------------------------------------------------------------------------------------------

bool ReadableMemory(const void* p, size_t size) {
    MEMORY_BASIC_INFORMATION info;
    if (!p || !VirtualQuery(p, &info, sizeof(info))) {
        return false;
    }
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || info.Protect == 0) {
        return false;
    }
    return (const char*)p + size <= (const char*)info.BaseAddress + info.RegionSize;
}

// OnDpiChanged takes the window's XamlRoot by value: `arg` is the caller's slot holding it. The object's first code address
// must be in a XAML or package module before it is treated as one.
void* XamlRootFromArgument(void* arg) {
    if (!ReadableMemory(arg, sizeof(void*))) {
        return nullptr;
    }
    void* object = *(void**)arg;
    if (!ReadableMemory(object, sizeof(void*))) {
        return nullptr;
    }
    const void* vtable = *(const void* const*)object;
    if (!ReadableMemory(vtable, sizeof(void*))) {
        return nullptr;
    }
    const void* first = *(const void* const*)vtable;
    HMODULE module = nullptr;
    if (!first || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      (LPCWSTR)first, &module)) {
        return nullptr;
    }
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        return nullptr;
    }
    return StrStrIW(path, L"Microsoft.UI.Xaml.dll") || StrStrIW(path, L"\\WindowsApps\\") ? object : nullptr;
}

// Runs when a window is built. OnDpiChanged is a coroutine that moves the XamlRoot out of the caller's slot, so the
// reference is taken before the original runs. `this` is never treated as a XAML object.
using DpiChanged_t = void*(WINAPI*)(void* pThis, void* ret, void* root, void* args);
DpiChanged_t DpiChanged_Original;
void* WINAPI DpiChanged_Hook(void* pThis, void* ret, void* root, void* args) {
    Inspectable hold{nullptr};
    if (!g_unloading) {
        try {
            if (void* object = XamlRootFromArgument(root)) {
                winrt::copy_from_abi(hold, object);
            }
        } catch (...) {
        }
    }
    void* result = DpiChanged_Original(pThis, ret, root, args);
    if (hold) {
        try {
            OnWindow(hold.as<mux::XamlRoot>());
        } catch (...) {
        }
    }
    return result;
}

// A press on the command bar reaches windows that were open before the mod loaded. The sender is the control itself.
using PointerPressed_t = void(WINAPI*)(void* pThis, Inspectable const& sender, void* args);
PointerPressed_t PointerPressed_Original;
void WINAPI PointerPressed_Hook(void* pThis, Inspectable const& sender, void* args) {
    PointerPressed_Original(pThis, sender, args);
    if (!g_unloading) {
        try {
            if (auto element = sender.try_as<mux::UIElement>()) {
                OnWindow(element.XamlRoot());
            }
        } catch (...) {
        }
    }
}

std::atomic<bool> g_hooked{false};

bool HookExtensions(HMODULE module) {
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: struct winrt::fire_and_forget __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::OnDpiChanged(struct winrt::Microsoft::UI::Xaml::XamlRoot,struct winrt::Microsoft::UI::Xaml::XamlRootChangedEventArgs))"},
         &DpiChanged_Original,
         DpiChanged_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &PointerPressed_Original,
         PointerPressed_Hook,
         true},
    };
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Wh_Log(L"HookSymbols returned %d, OnDpiChanged=%p, PointerPressed=%p", ok ? 1 : 0, (void*)DpiChanged_Original,
           (void*)PointerPressed_Original);
    return DpiChanged_Original || PointerPressed_Original;
}

// Explorer can start the mod before it loads FileExplorerExtensions.dll: the module is hooked when it arrives.
using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR name, HANDLE file, DWORD flags) {
    HMODULE module = LoadLibraryExW_Original(name, file, flags);
    const DWORD dataOnly = LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    if (module && !(flags & dataOnly) && !g_hooked && !g_unloading) {
        if (HMODULE extensions = GetModuleHandleW(L"FileExplorerExtensions.dll"); extensions && !g_hooked.exchange(true)) {
            HookExtensions(extensions);
            Wh_ApplyHookOperations();
        }
    }
    return module;
}

// ---- Windhawk entry points --------------------------------------------------------------------------------------------

BOOL Wh_ModInit() {
    LoadSettings();
    Wh_Log(L"explorer-search-box-resize v%s start, pid=%lu", WH_MOD_VERSION, GetCurrentProcessId());
    if (HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll")) {
        g_hooked = true;
        if (!HookExtensions(module)) {
            Wh_Log(L"no hook could be installed, the mod does nothing");
        }
        return TRUE;
    }
    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibrary = kernelBase ? (decltype(&LoadLibraryExW))GetProcAddress(kernelBase, "LoadLibraryExW") : nullptr;
    if (loadLibrary) {
        WindhawkUtils::SetFunctionHook(loadLibrary, LoadLibraryExW_Hook, &LoadLibraryExW_Original);
    }
    return TRUE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    std::vector<KnownThread> threads;
    {
        std::lock_guard<std::mutex> lock(g_threadsMutex);
        threads = g_threads;
    }
    for (auto& thread : threads) {
        try {
            thread.queue.TryEnqueue([]() {
                if (!g_unloading) {
                    ApplyThread();
                }
            });
        } catch (...) {
        }
    }
}

// At unload every window thread puts Explorer's width back and takes its handlers off, on its own thread, while the mod is
// still mapped. A thread whose window closed meanwhile is not waited for.
void Wh_ModBeforeUninit() {
    g_unloading = true;
    std::vector<KnownThread> threads;
    {
        std::lock_guard<std::mutex> lock(g_threadsMutex);
        threads.swap(g_threads);
    }
    for (auto& thread : threads) {
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!done) {
            continue;
        }
        bool queued = false;
        try {
            queued = thread.queue.TryEnqueue([done]() {
                ReleaseThread();
                SetEvent(done);
            });
        } catch (...) {
        }
        if (queued) {
            HANDLE handle = OpenThread(SYNCHRONIZE, FALSE, thread.id);
            HANDLE waits[2] = {done, handle};
            WaitForMultipleObjects(handle ? 2 : 1, waits, FALSE, INFINITE);
            if (handle) {
                CloseHandle(handle);
            }
        }
        CloseHandle(done);
    }
}

void Wh_ModUninit() {
    Wh_Log(L"explorer-search-box-resize end");
}
