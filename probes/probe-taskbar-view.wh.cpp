// ==WindhawkMod==
// @id              probe-taskbar-view
// @name            Probe: taskbar tree from Taskbar.View.dll
// @description     Diagnostic only. Tests whether the taskbar's XAML tree can be reached from Taskbar.View.dll alone, without taskbar.dll symbols.
// @version         0.3
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshlwapi
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: taskbar tree from Taskbar.View.dll

The taskbar widget mods in this repo reach the taskbar through symbols in
`taskbar.dll`. Microsoft publishes no symbol file for that module on this
Windows build, so those mods can't load. `Taskbar.View.dll` ships in an app
package and its symbols work. This probe tests whether that module alone is
enough.

It hooks five functions of the taskbar's own classes. Four of them receive a
XAML element or a pointer event from Explorer, so the probe needs no knowledge
of how those classes are laid out in memory. From that element it climbs to the
top of the taskbar's tree and logs what it finds: the root grid, the icon
repeater and the tray grid.

**Marker (on by default).** Adds a small orange square labelled "WH" at the
left edge of the taskbar, to prove that an element inserted this way shows. It
is removed when the setting is turned off or the mod is disabled.

## How to run it

1. Check that the marker setting is on, then enable the mod and wait for
   `HOOKS READY`.
2. Move the pointer off the taskbar and back onto it.
3. Click an app button on the taskbar.
4. Click the Start button, then close the Start menu.
5. Open an app that isn't running yet, so a new button appears.
6. Send the log, and say whether the orange square showed.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- addMarker: true
  $name: Add a visible marker to the taskbar
  $description: A small orange square at the taskbar's left edge. Off means log only, and removes the square.
- scanSymbols: false
  $name: List candidate hook functions
  $description: Logs the taskbar classes' own methods from Taskbar.View.dll, to find a hook that fires without touching Settings.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <windows.h>
#include <shlwapi.h>

#include <atomic>
#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <string>

#undef GetCurrentTime

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>

namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;

namespace {

std::atomic<bool> g_addMarker{false};
std::atomic<bool> g_unloading{false};
std::atomic<int> g_hookCalls{0};
thread_local ULONGLONG g_lastWalk = 0;

// Every log line also goes to a file in the repo, so the log can be read from
// disk: %USERPROFILE%\Code\windhawk-lib\probes\logs\probe-taskbar-view.log
std::mutex g_outMutex;
wchar_t g_outPath[MAX_PATH] = L"";

void OutInit() {
    wchar_t profile[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return;
    }
    std::wstring dir = std::wstring(profile) + L"\\Code\\windhawk-lib\\probes\\logs";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\probe-taskbar-view.log";
    if (path.size() < MAX_PATH) {
        wcscpy_s(g_outPath, path.c_str());
        // Start each run with an empty file.
        HANDLE file = CreateFileW(g_outPath, GENERIC_WRITE, FILE_SHARE_READ,
                                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }
}

void Out(const wchar_t* format, ...) {
    wchar_t text[1200];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, ARRAYSIZE(text), _TRUNCATE, format, args);
    va_end(args);
    Wh_Log(L"%s", text);

    if (!g_outPath[0]) {
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t line[1300];
    int chars = _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE,
                             L"%02d:%02d:%02d.%03d %s\r\n", now.wHour,
                             now.wMinute, now.wSecond, now.wMilliseconds, text);
    if (chars < 0) {
        chars = (int)wcslen(line);
    }
    char utf8[4000];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, chars, utf8,
                                    sizeof(utf8), nullptr, nullptr);
    if (bytes <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_outMutex);
    HANDLE file = CreateFileW(g_outPath, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, (DWORD)bytes, &written, nullptr);
        CloseHandle(file);
    }
}

// The marker, so unload can take it out on the thread that owns it.
std::mutex g_markerMutex;
[[clang::no_destroy]] wuxc::Border g_marker{nullptr};
[[clang::no_destroy]] wuxc::Panel g_markerParent{nullptr};

void LogHresult(const wchar_t* where) {
    HRESULT hr = winrt::to_hresult();
    Out(L"  %s threw hr=0x%08lX", where, hr);
}

std::wstring ClassOf(winrt::Windows::Foundation::IInspectable const& obj) {
    try {
        return std::wstring{winrt::get_class_name(obj).c_str()};
    } catch (...) {
        return L"?";
    }
}

std::wstring NameOf(wux::DependencyObject const& obj) {
    try {
        if (auto element = obj.try_as<wux::FrameworkElement>()) {
            return std::wstring{element.Name().c_str()};
        }
    } catch (...) {
    }
    return L"";
}

void LogSize(const wchar_t* label, wux::DependencyObject const& obj) {
    if (!obj) {
        Out(L"  %s: NOT FOUND", label);
        return;
    }
    double width = 0, height = 0;
    if (auto element = obj.try_as<wux::FrameworkElement>()) {
        width = element.ActualWidth();
        height = element.ActualHeight();
    }
    Out(L"  %s: found, class=%s name=\"%s\" size=%dx%d", label,
           ClassOf(obj).c_str(), NameOf(obj).c_str(), (int)width, (int)height);
}

// Depth-first search for the first element with this class or name.
wux::DependencyObject Find(wux::DependencyObject const& node,
                           const wchar_t* className, const wchar_t* name,
                           int depth, int& visited) {
    if (!node || depth > 40 || visited > 4000) {
        return nullptr;
    }
    visited++;
    if ((className && ClassOf(node) == className) ||
        (name && NameOf(node) == name)) {
        return node;
    }
    int count = wux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        auto found = Find(wux::Media::VisualTreeHelper::GetChild(node, i),
                          className, name, depth + 1, visited);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

void RemoveMarkerOnThisThread() {
    wuxc::Border marker{nullptr};
    wuxc::Panel parent{nullptr};
    {
        std::lock_guard<std::mutex> lock(g_markerMutex);
        marker = g_marker;
        parent = g_markerParent;
        g_marker = nullptr;
        g_markerParent = nullptr;
    }
    if (!marker || !parent) {
        return;
    }
    try {
        uint32_t index = 0;
        if (parent.Children().IndexOf(marker, index)) {
            parent.Children().RemoveAt(index);
            Out(L"  marker removed");
        }
    } catch (...) {
        LogHresult(L"removing the marker");
    }
}

void AddMarker(wux::DependencyObject const& rootGrid) {
    try {
        {
            std::lock_guard<std::mutex> lock(g_markerMutex);
            if (g_marker) {
                return;
            }
        }
        auto panel = rootGrid.try_as<wuxc::Panel>();
        if (!panel) {
            Out(L"  marker: the root grid isn't a Panel, class=%s",
                   ClassOf(rootGrid).c_str());
            return;
        }

        wuxc::TextBlock text;
        text.Text(L"WH");
        text.FontSize(11);
        text.HorizontalAlignment(wux::HorizontalAlignment::Center);
        text.VerticalAlignment(wux::VerticalAlignment::Center);

        wuxc::Border marker;
        marker.Name(L"WindhawkProbeMarker");
        marker.Width(28);
        marker.Height(28);
        marker.CornerRadius(wux::CornerRadius{4, 4, 4, 4});
        marker.Background(wux::Media::SolidColorBrush(
            winrt::Windows::UI::Color{255, 255, 140, 0}));
        marker.HorizontalAlignment(wux::HorizontalAlignment::Left);
        marker.VerticalAlignment(wux::VerticalAlignment::Center);
        marker.Margin(wux::Thickness{6, 0, 0, 0});
        marker.Child(text);

        panel.Children().Append(marker);
        {
            std::lock_guard<std::mutex> lock(g_markerMutex);
            g_marker = marker;
            g_markerParent = panel;
        }
        Out(L"  MARKER ADDED to %s, which now has %u children",
               ClassOf(panel).c_str(), panel.Children().Size());
    } catch (...) {
        LogHresult(L"AddMarker");
    }
}

void WalkFromButton(wux::DependencyObject const& button) {
    ULONGLONG now = GetTickCount64();
    if (now - g_lastWalk < 3000) {
        return;
    }
    g_lastWalk = now;

    try {
        Out(L"TREE WALK from a %s, thread=%lu", ClassOf(button).c_str(),
               GetCurrentThreadId());

        // Up to the top, logging each level.
        wux::DependencyObject root = button;
        for (int level = 0; level < 40; level++) {
            Out(L"  up[%d] class=%s name=\"%s\"", level, ClassOf(root).c_str(),
                   NameOf(root).c_str());
            auto parent = wux::Media::VisualTreeHelper::GetParent(root);
            if (!parent) {
                break;
            }
            root = parent;
        }

        // The elements the widget mods need, searched from that top.
        int visited = 0;
        auto frame = Find(root, L"Taskbar.TaskbarFrame", nullptr, 0, visited);
        auto rootGrid = frame ? Find(frame, nullptr, L"RootGrid", 0, visited)
                              : nullptr;
        auto repeater = rootGrid ? Find(rootGrid, nullptr,
                                        L"TaskbarFrameRepeater", 0, visited)
                                 : nullptr;
        auto trayFrame =
            Find(root, L"SystemTray.SystemTrayFrame", nullptr, 0, visited);
        auto trayGrid = trayFrame ? Find(trayFrame, nullptr,
                                         L"SystemTrayFrameGrid", 0, visited)
                                  : nullptr;

        LogSize(L"Taskbar.TaskbarFrame", frame);
        LogSize(L"RootGrid", rootGrid);
        LogSize(L"TaskbarFrameRepeater", repeater);
        LogSize(L"SystemTray.SystemTrayFrame", trayFrame);
        LogSize(L"SystemTrayFrameGrid", trayGrid);
        Out(L"  %d elements visited", visited);

        if (rootGrid && trayGrid) {
            Out(L"TREE FOUND: both the root grid and the tray grid are reachable from Taskbar.View.dll");
        } else {
            Out(L"TREE INCOMPLETE: rootGrid=%d trayGrid=%d",
                   rootGrid ? 1 : 0, trayGrid ? 1 : 0);
        }

        if (rootGrid) {
            if (g_addMarker && !g_unloading) {
                AddMarker(rootGrid);
            } else if (!g_addMarker) {
                RemoveMarkerOnThisThread();
            }
        }
    } catch (...) {
        LogHresult(L"WalkFromButton");
    }
}

using UpdateButtonPadding_t = void(WINAPI*)(void* pThis);
UpdateButtonPadding_t UpdateButtonPadding_Original;
void WINAPI UpdateButtonPadding_Hook(void* pThis) {
    UpdateButtonPadding_Original(pThis);

    int call = ++g_hookCalls;
    if (call <= 3) {
        Out(L"> UpdateButtonPadding call %d, this=%p thread=%lu", call,
               pThis, GetCurrentThreadId());
    }
    if (g_unloading || !pThis) {
        return;
    }

    // The object's second pointer-sized slot holds an interface of the XAML
    // element. The Start button mod in this repo and the published "Start
    // button always on the left" mod both rely on this.
    IUnknown* inner = ((IUnknown**)pThis)[1];
    if (!inner || IsBadReadPtr(inner, sizeof(void*))) {
        Out(L"  the object's second slot isn't a readable pointer: %p",
               inner);
        return;
    }
    wux::FrameworkElement button{nullptr};
    HRESULT hr = inner->QueryInterface(winrt::guid_of<wux::FrameworkElement>(),
                                       winrt::put_abi(button));
    if (FAILED(hr) || !button) {
        Out(L"  QueryInterface for FrameworkElement failed, hr=0x%08lX", hr);
        return;
    }
    WalkFromButton(button);
}

// The hooks below get an element from Explorer instead of converting `this`.
// A reference to a C++/WinRT object arrives as a pointer to one pointer, so
// every such argument is declared as IInspectable const&.
using Inspectable = winrt::Windows::Foundation::IInspectable;

std::atomic<int> g_namedCalls[4];

void WalkFromArguments(int slot, const wchar_t* hookName, void* pThis,
                       Inspectable const* sender, Inspectable const* args) {
    int call = ++g_namedCalls[slot];
    if (call <= 3) {
        Out(L"> %s call %d, this=%p thread=%lu", hookName, call, pThis,
               GetCurrentThreadId());
    }
    if (g_unloading) {
        return;
    }
    try {
        wux::DependencyObject start{nullptr};
        if (args && *args) {
            if (auto routed = args->try_as<wux::RoutedEventArgs>()) {
                start = routed.OriginalSource().try_as<wux::DependencyObject>();
                if (call <= 3) {
                    Out(L"  original source: %s",
                           start ? ClassOf(start).c_str() : L"none");
                }
            }
        }
        if (!start && sender && *sender) {
            start = sender->try_as<wux::DependencyObject>();
            if (call <= 3) {
                Out(L"  sender: class=%s, is an element=%d",
                       ClassOf(*sender).c_str(), start ? 1 : 0);
            }
        }
        if (!start) {
            if (call <= 3) {
                Out(L"  no element in the arguments");
            }
            return;
        }
        WalkFromButton(start);
    } catch (...) {
        LogHresult(hookName);
    }
}

using TwoArgs_t = void(WINAPI*)(void* pThis, Inspectable const& a,
                                Inspectable const& b);
using OneArg_t = void(WINAPI*)(void* pThis, Inspectable const& a);

TwoArgs_t FrameXamlRootPointerEntered_Original;
void WINAPI FrameXamlRootPointerEntered_Hook(void* pThis,
                                             Inspectable const& sender,
                                             Inspectable const& args) {
    FrameXamlRootPointerEntered_Original(pThis, sender, args);
    WalkFromArguments(0, L"TaskbarFrame::OnXamlRootPointerEntered", pThis,
                      &sender, &args);
}

TwoArgs_t FrameElementPrepared_Original;
void WINAPI FrameElementPrepared_Hook(void* pThis, Inspectable const& sender,
                                      Inspectable const& args) {
    FrameElementPrepared_Original(pThis, sender, args);
    // The sender is the icon repeater. These event arguments carry no
    // original source.
    WalkFromArguments(1, L"TaskbarFrame::OnElementPrepared", pThis, &sender,
                      nullptr);
}

OneArg_t TaskListButtonPointerPressed_Original;
void WINAPI TaskListButtonPointerPressed_Hook(void* pThis,
                                              Inspectable const& args) {
    TaskListButtonPointerPressed_Original(pThis, args);
    WalkFromArguments(2, L"TaskListButton::OnPointerPressed", pThis, nullptr,
                      &args);
}

OneArg_t ToggleButtonPointerPressed_Original;
void WINAPI ToggleButtonPointerPressed_Hook(void* pThis,
                                            Inspectable const& args) {
    ToggleButtonPointerPressed_Original(pThis, args);
    WalkFromArguments(3, L"ExperienceToggleButton::OnPointerPressed", pThis,
                      nullptr, &args);
}

HMODULE GetTaskbarViewModule() {
    HMODULE module = GetModuleHandleW(L"Taskbar.View.dll");
    if (!module) {
        module = GetModuleHandleW(L"ExplorerExtensions.dll");
    }
    return module;
}

bool HookTaskbarView(HMODULE module) {
    // All optional, so one missing symbol doesn't stop the others.
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(protected: virtual void __cdecl winrt::Taskbar::implementation::ExperienceToggleButton::UpdateButtonPadding(void))"},
         &UpdateButtonPadding_Original,
         UpdateButtonPadding_Hook,
         true},
        {{LR"(public: void __cdecl winrt::Taskbar::implementation::TaskbarFrame::OnXamlRootPointerEntered(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &FrameXamlRootPointerEntered_Original,
         FrameXamlRootPointerEntered_Hook,
         true},
        {{LR"(private: void __cdecl winrt::Taskbar::implementation::TaskbarFrame::OnElementPrepared(struct winrt::Microsoft::UI::Xaml::Controls::ItemsRepeater const &,struct winrt::Microsoft::UI::Xaml::Controls::ItemsRepeaterElementPreparedEventArgs const &))"},
         &FrameElementPrepared_Original,
         FrameElementPrepared_Hook,
         true},
        {{LR"(public: void __cdecl winrt::Taskbar::implementation::TaskListButton::OnPointerPressed(struct winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &TaskListButtonPointerPressed_Original,
         TaskListButtonPointerPressed_Hook,
         true},
        {{LR"(public: virtual void __cdecl winrt::Taskbar::implementation::ExperienceToggleButton::OnPointerPressed(struct winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &ToggleButtonPointerPressed_Original,
         ToggleButtonPointerPressed_Hook,
         true},
    };
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Out(L"HookSymbols returned %d", ok ? 1 : 0);
    Out(L"  UpdateButtonPadding=%p", (void*)UpdateButtonPadding_Original);
    Out(L"  TaskbarFrame::OnXamlRootPointerEntered=%p",
           (void*)FrameXamlRootPointerEntered_Original);
    Out(L"  TaskbarFrame::OnElementPrepared=%p",
           (void*)FrameElementPrepared_Original);
    Out(L"  TaskListButton::OnPointerPressed=%p",
           (void*)TaskListButtonPointerPressed_Original);
    Out(L"  ExperienceToggleButton::OnPointerPressed=%p",
           (void*)ToggleButtonPointerPressed_Original);
    return ok;
}

// Lists the taskbar classes' own methods, to find a hook that fires on its
// own: on hover, on layout, on load.
void ScanCandidates(HMODULE module) {
    static const wchar_t* const kClasses[] = {
        L"winrt::Taskbar::implementation::TaskbarFrame::",
        L"winrt::Taskbar::implementation::ExperienceToggleButton::",
        L"winrt::Taskbar::implementation::TaskListButton::",
        L"winrt::SystemTray::implementation::SystemTrayFrame::",
    };
    static const wchar_t* const kSkip[] = {
        L"winrt::impl::", L"`vcall'", L"Bindings", L"std::", L"dtor$",
        L"`1'",
    };

    WH_FIND_SYMBOL symbol = {};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &symbol);
    if (!search) {
        Out(L"CANDIDATES: no symbols available");
        return;
    }
    Out(L"CANDIDATES: scanning");
    int total = 0, logged = 0;
    do {
        total++;
        if (!symbol.symbol) {
            continue;
        }
        bool wanted = false;
        for (auto* cls : kClasses) {
            if (StrStrIW(symbol.symbol, cls)) {
                wanted = true;
                break;
            }
        }
        if (!wanted) {
            continue;
        }
        bool skip = false;
        for (auto* word : kSkip) {
            if (StrStrIW(symbol.symbol, word)) {
                skip = true;
                break;
            }
        }
        if (skip) {
            continue;
        }
        if (logged < 250) {
            Out(L"  sym %.500s", symbol.symbol);
        }
        logged++;
    } while (!g_unloading && Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    Out(L"CANDIDATES: %d symbols seen, %d matched, up to 250 logged", total,
           logged);
}

HANDLE g_scanThread = nullptr;

DWORD WINAPI ScanThread(LPVOID) {
    if (HMODULE module = GetTaskbarViewModule()) {
        ScanCandidates(module);
    }
    return 0;
}

// Runs `work` on the thread that owns the marker and waits for it.
void RemoveMarkerFromAnyThread() {
    wuxc::Border marker{nullptr};
    {
        std::lock_guard<std::mutex> lock(g_markerMutex);
        marker = g_marker;
    }
    if (!marker) {
        return;
    }
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool queued = false;
    try {
        auto dispatcher = marker.Dispatcher();
        marker = nullptr;
        dispatcher.RunAsync(
            winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
            [done]() {
                RemoveMarkerOnThisThread();
                if (done) {
                    SetEvent(done);
                }
            });
        queued = true;
    } catch (...) {
        LogHresult(L"queueing the marker removal");
    }
    if (queued && done) {
        DWORD wait = WaitForSingleObject(done, 3000);
        Out(L"marker removal %s",
               wait == WAIT_OBJECT_0 ? L"done" : L"TIMED OUT");
        if (wait == WAIT_OBJECT_0) {
            CloseHandle(done);
        }
    } else if (done) {
        CloseHandle(done);
    }
}

}  // namespace

BOOL Wh_ModInit() {
#if defined(_M_ARM64) || defined(__aarch64__)
    const wchar_t* arch = L"arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif
    OutInit();
    Out(L"PROBE START probe-taskbar-view v0.3 compiled as %s, pid=%lu", arch,
           GetCurrentProcessId());

    g_addMarker = Wh_GetIntSetting(L"addMarker") != 0;
    Out(L"addMarker=%d", g_addMarker ? 1 : 0);

    HMODULE module = GetTaskbarViewModule();
    if (!module) {
        Out(L"Taskbar.View.dll isn't loaded in this process. This is not the process that owns the taskbar.");
        return TRUE;
    }
    HookTaskbarView(module);
    return TRUE;
}

void Wh_ModAfterInit() {
    Out(L"HOOKS READY. Move the pointer onto the taskbar, click an app button, click Start, open a new app.");
    if (Wh_GetIntSetting(L"scanSymbols") && GetTaskbarViewModule()) {
        g_scanThread = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
    }
}

void Wh_ModSettingsChanged() {
    g_addMarker = Wh_GetIntSetting(L"addMarker") != 0;
    Out(L"addMarker=%d", g_addMarker ? 1 : 0);
    if (!g_addMarker) {
        RemoveMarkerFromAnyThread();
    }
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    if (g_scanThread) {
        WaitForSingleObject(g_scanThread, INFINITE);
        CloseHandle(g_scanThread);
        g_scanThread = nullptr;
    }
    RemoveMarkerFromAnyThread();
}

void Wh_ModUninit() {
    Out(L"PROBE END");
}
