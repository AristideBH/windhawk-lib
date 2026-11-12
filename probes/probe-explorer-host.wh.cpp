// ==WindhawkMod==
// @id              probe-explorer-host
// @name            Probe: WinUI 3 panel over the file list
// @description     Diagnostic only. Tests whether a mod can place its own WinUI 3 panel over File Explorer's file list, the base a native column view needs.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: WinUI 3 panel over the file list

A column view for File Explorer needs a place to draw. This probe tests the
simplest candidate: a WinUI 3 panel of the mod's own, hosted in the Explorer
window and laid over the file list.

It waits for a click on the command bar, the row with New, Sort and View. That
click runs on the window's UI thread, where WinUI 3 is already running. The
probe then creates a panel with three dummy columns and two buttons, sizes it
to the file list and keeps it in step when the window is resized.

It reads no files and changes nothing in Explorer. The panel goes away when you
click its Close button, turn the setting off or disable the mod.

The log also goes to `probes\logs\probe-explorer-host.log` in the repo.

## How to run it

1. Enable the mod and wait for `HOOKS READY`.
2. Open a File Explorer window and click an empty spot of the command bar.
3. The panel should cover the file list. Click a row, click "Count", resize
   the window, switch folder.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- showPanel: true
  $name: Show the test panel
  $description: Off removes the panel and makes the probe log only.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <windows.h>

#undef GetCurrentTime

#include <atomic>
#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <string>

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.h>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
namespace mud = winrt::Microsoft::UI::Dispatching;

namespace {

using Inspectable = winrt::Windows::Foundation::IInspectable;

std::atomic<bool> g_showPanel{true};
std::atomic<bool> g_unloading{false};
std::atomic<int> g_hookCalls{0};

// Every log line also goes to a file in the repo, so the log can be read from
// disk: %USERPROFILE%\Code\windhawk-lib\probes\logs\probe-explorer-host.log
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
    std::wstring path = dir + L"\\probe-explorer-host.log";
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


void LogHresult(const wchar_t* where) {
    HRESULT hr = winrt::to_hresult();
    Out(L"  %s threw hr=0x%08lX", where, hr);
}

// Everything the panel owns. Created, used and destroyed on one Explorer
// window's UI thread. Other threads only read g_panelThread and g_panelQueue.
struct Panel {
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token tickToken{};
    muxc::Button countButton{nullptr};
    winrt::event_token countToken{};
    muxc::Button closeButton{nullptr};
    winrt::event_token closeToken{};
    muxc::ListView lists[3]{{nullptr}, {nullptr}, {nullptr}};
    winrt::event_token listTokens[3]{};
    muxc::TextBlock status{nullptr};
    HWND topLevel = nullptr;
    HWND fileList = nullptr;
    RECT lastRect{};
    int clicks = 0;
    int moves = 0;
};

[[clang::no_destroy]] Panel g_panel;
std::atomic<DWORD> g_panelThread{0};
std::mutex g_queueMutex;
[[clang::no_destroy]] mud::DispatcherQueue g_panelQueue{nullptr};

std::wstring ClassNameOf(HWND hwnd) {
    wchar_t name[128] = L"";
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    return name;
}

struct FindContext {
    HWND found = nullptr;
};

BOOL CALLBACK FindVisibleDefView(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd) && ClassNameOf(hwnd) == L"SHELLDLL_DefView") {
        // Visible itself and through all its parents.
        bool shown = true;
        for (HWND p = GetParent(hwnd); p; p = GetParent(p)) {
            if (!IsWindowVisible(p)) {
                shown = false;
                break;
            }
        }
        if (shown) {
            ((FindContext*)lParam)->found = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

HWND FindFileList(HWND topLevel) {
    FindContext context;
    EnumChildWindows(topLevel, FindVisibleDefView, (LPARAM)&context);
    return context.found;
}

BOOL CALLBACK FindCabinetOnThread(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd) && ClassNameOf(hwnd) == L"CabinetWClass") {
        ((FindContext*)lParam)->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

// The Explorer window the click happened in.
HWND FindTopLevel() {
    HWND foreground = GetForegroundWindow();
    if (foreground && ClassNameOf(foreground) == L"CabinetWClass") {
        return foreground;
    }
    FindContext context;
    EnumThreadWindows(GetCurrentThreadId(), FindCabinetOnThread,
                      (LPARAM)&context);
    return context.found;
}

// The file list's rectangle in the top-level window's client coordinates.
bool FileListRect(HWND topLevel, HWND fileList, RECT* rect) {
    if (!GetWindowRect(fileList, rect)) {
        return false;
    }
    MapWindowPoints(nullptr, topLevel, (POINT*)rect, 2);
    return rect->right > rect->left && rect->bottom > rect->top;
}

void ClosePanelOnThisThread(const wchar_t* why) {
    if (g_panelThread != GetCurrentThreadId()) {
        return;
    }
    Out(L"PANEL CLOSING: %s. clicks=%d moves=%d", why, g_panel.clicks,
        g_panel.moves);
    try {
        if (g_panel.timer) {
            g_panel.timer.Stop();
            g_panel.timer.Tick(g_panel.tickToken);
        }
        if (g_panel.countButton) {
            g_panel.countButton.Click(g_panel.countToken);
        }
        if (g_panel.closeButton) {
            g_panel.closeButton.Click(g_panel.closeToken);
        }
        for (int i = 0; i < 3; i++) {
            if (g_panel.lists[i]) {
                g_panel.lists[i].SelectionChanged(g_panel.listTokens[i]);
            }
        }
        if (g_panel.source) {
            g_panel.source.Content(nullptr);
            g_panel.source.Close();
        }
    } catch (...) {
        LogHresult(L"closing the panel");
    }
    g_panel = Panel{};
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_panelQueue = nullptr;
    }
    g_panelThread = 0;
    Out(L"PANEL CLOSED");
}

void OnTick() {
    if (g_panelThread != GetCurrentThreadId() || !g_panel.bridge) {
        return;
    }
    if (g_unloading || !g_showPanel) {
        ClosePanelOnThisThread(L"setting off or mod unloading");
        return;
    }
    try {
        if (!IsWindow(g_panel.topLevel)) {
            ClosePanelOnThisThread(L"the Explorer window is gone");
            return;
        }
        HWND fileList = FindFileList(g_panel.topLevel);
        if (fileList != g_panel.fileList) {
            Out(L"  file list window changed: %p -> %p", g_panel.fileList,
                fileList);
            g_panel.fileList = fileList;
        }
        if (!fileList) {
            return;
        }
        RECT rect;
        if (!FileListRect(g_panel.topLevel, fileList, &rect)) {
            return;
        }
        if (!EqualRect(&rect, &g_panel.lastRect)) {
            g_panel.lastRect = rect;
            g_panel.moves++;
            g_panel.bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
                rect.left, rect.top, rect.right - rect.left,
                rect.bottom - rect.top});
            if (g_panel.moves <= 20) {
                Out(L"  panel moved to %ld,%ld size %ldx%ld", rect.left,
                    rect.top, rect.right - rect.left, rect.bottom - rect.top);
            }
        }
    } catch (...) {
        LogHresult(L"OnTick");
    }
}

muxc::ListView MakeColumn(int index) {
    muxc::ListView list;
    list.Width(220);
    list.SelectionMode(muxc::ListViewSelectionMode::Single);
    for (int row = 1; row <= 8; row++) {
        wchar_t text[64];
        _snwprintf_s(text, ARRAYSIZE(text), _TRUNCATE, L"Column %d, row %d",
                      index + 1, row);
        list.Items().Append(winrt::box_value(winrt::hstring{text}));
    }
    g_panel.lists[index] = list;
    g_panel.listTokens[index] = list.SelectionChanged(
        [index](Inspectable const&,
                muxc::SelectionChangedEventArgs const&) {
            if (g_panelThread != GetCurrentThreadId()) {
                return;
            }
            int selected = g_panel.lists[index]
                               ? g_panel.lists[index].SelectedIndex()
                               : -1;
            Out(L"  INPUT: column %d selected row %d", index + 1,
                selected + 1);
        });
    return list;
}

void CreatePanel() {
    if (g_panelThread != 0) {
        return;
    }
    DWORD thread = GetCurrentThreadId();
    HWND topLevel = FindTopLevel();
    if (!topLevel) {
        Out(L"PANEL: no Explorer window found for this click");
        return;
    }
    HWND fileList = FindFileList(topLevel);
    DWORD topThread = GetWindowThreadProcessId(topLevel, nullptr);
    DWORD listThread =
        fileList ? GetWindowThreadProcessId(fileList, nullptr) : 0;
    Out(L"PANEL: click thread=%lu, window=%p on thread %lu, file list=%p on thread %lu",
        thread, topLevel, topThread, fileList, listThread);
    if (!fileList) {
        Out(L"PANEL: no visible file list in this window");
        return;
    }
    RECT rect;
    if (!FileListRect(topLevel, fileList, &rect)) {
        Out(L"PANEL: the file list has no size");
        return;
    }
    Out(L"PANEL: file list at %ld,%ld size %ldx%ld, dpi=%u", rect.left,
        rect.top, rect.right - rect.left, rect.bottom - rect.top,
        GetDpiForWindow(topLevel));

    g_panelThread = thread;
    const wchar_t* step = L"start";
    try {
        step = L"getting the thread's dispatcher queue";
        auto queue = mud::DispatcherQueue::GetForCurrentThread();
        if (!queue) {
            Out(L"PANEL: this thread has no WinUI dispatcher queue");
            g_panelThread = 0;
            return;
        }

        step = L"building the content";
        muxc::StackPanel columns;
        columns.Orientation(muxc::Orientation::Horizontal);
        columns.Spacing(1);
        for (int i = 0; i < 3; i++) {
            columns.Children().Append(MakeColumn(i));
        }

        muxc::TextBlock status;
        status.Text(L"Windhawk test panel. Clicks: 0");
        status.VerticalAlignment(mux::VerticalAlignment::Center);
        g_panel.status = status;

        muxc::Button countButton;
        countButton.Content(winrt::box_value(L"Count"));
        g_panel.countButton = countButton;
        g_panel.countToken = countButton.Click(
            [](Inspectable const&, mux::RoutedEventArgs const&) {
                if (g_panelThread != GetCurrentThreadId()) {
                    return;
                }
                g_panel.clicks++;
                Out(L"  INPUT: Count clicked, total %d", g_panel.clicks);
                if (g_panel.status) {
                    wchar_t text[80];
                    _snwprintf_s(text, ARRAYSIZE(text), _TRUNCATE,
                                  L"Windhawk test panel. Clicks: %d",
                                  g_panel.clicks);
                    g_panel.status.Text(text);
                }
            });

        muxc::Button closeButton;
        closeButton.Content(winrt::box_value(L"Close"));
        g_panel.closeButton = closeButton;
        g_panel.closeToken = closeButton.Click(
            [](Inspectable const&, mux::RoutedEventArgs const&) {
                // Close after this handler returns, not from inside it.
                auto queue = mud::DispatcherQueue::GetForCurrentThread();
                if (queue) {
                    queue.TryEnqueue([]() {
                        g_showPanel = false;
                        ClosePanelOnThisThread(L"Close button");
                    });
                }
            });

        muxc::StackPanel bar;
        bar.Orientation(muxc::Orientation::Horizontal);
        bar.Spacing(12);
        bar.Margin(mux::Thickness{12, 8, 12, 8});
        bar.Children().Append(status);
        bar.Children().Append(countButton);
        bar.Children().Append(closeButton);

        muxc::StackPanel content;
        content.Orientation(muxc::Orientation::Vertical);
        content.Children().Append(bar);
        content.Children().Append(columns);

        muxc::Grid root;
        root.Background(mux::Media::SolidColorBrush(
            winrt::Windows::UI::Color{255, 32, 32, 32}));
        root.BorderBrush(mux::Media::SolidColorBrush(
            winrt::Windows::UI::Color{255, 255, 140, 0}));
        root.BorderThickness(mux::Thickness{2, 2, 2, 2});
        root.RequestedTheme(mux::ElementTheme::Dark);
        root.Children().Append(content);

        step = L"creating DesktopWindowXamlSource";
        mux::Hosting::DesktopWindowXamlSource source;
        g_panel.source = source;

        step = L"Initialize with the Explorer window";
        source.Initialize(winrt::Microsoft::UI::WindowId{(uint64_t)topLevel});

        step = L"setting the content";
        source.Content(root);

        step = L"getting the site bridge";
        auto bridge = source.SiteBridge();
        g_panel.bridge = bridge;

        step = L"MoveAndResize";
        bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
            rect.left, rect.top, rect.right - rect.left,
            rect.bottom - rect.top});

        step = L"Show";
        bridge.Show();

        step = L"raising the panel's window";
        HWND bridgeWindow = (HWND)bridge.WindowId().Value;
        BOOL raised = SetWindowPos(bridgeWindow, HWND_TOP, 0, 0, 0, 0,
                                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Out(L"PANEL: bridge window=%p class=%s parent=%p raised=%d",
            bridgeWindow, ClassNameOf(bridgeWindow).c_str(),
            GetParent(bridgeWindow), raised);

        g_panel.topLevel = topLevel;
        g_panel.fileList = fileList;
        g_panel.lastRect = rect;

        step = L"starting the timer";
        auto timer = queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(250));
        g_panel.tickToken = timer.Tick(
            [](mud::DispatcherQueueTimer const&, Inspectable const&) {
                OnTick();
            });
        timer.Start();
        g_panel.timer = timer;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_panelQueue = queue;
        }
        Out(L"PANEL SHOWN. Click a row, click Count, resize the window, switch folder.");
    } catch (...) {
        Out(L"PANEL FAILED at step: %s", step);
        LogHresult(L"CreatePanel");
        ClosePanelOnThisThread(L"creation failed");
    }
}

using TwoArgs_t = void(WINAPI*)(void* pThis, Inspectable const& a,
                                Inspectable const& b);

void OnCommandBarClick(const wchar_t* name) {
    int call = ++g_hookCalls;
    if (call <= 5) {
        Out(L"> %s call %d, thread=%lu", name, call, GetCurrentThreadId());
    }
    if (g_unloading || !g_showPanel) {
        return;
    }
    CreatePanel();
}

TwoArgs_t PointerPressed_Original;
void WINAPI PointerPressed_Hook(void* pThis, Inspectable const& sender,
                                Inspectable const& args) {
    PointerPressed_Original(pThis, sender, args);
    OnCommandBarClick(L"CommandBarControl::PointerPressedHandler");
}

TwoArgs_t PointerPressedWave1_Original;
void WINAPI PointerPressedWave1_Hook(void* pThis, Inspectable const& sender,
                                     Inspectable const& args) {
    PointerPressedWave1_Original(pThis, sender, args);
    OnCommandBarClick(L"CommandBarControl_Wave1::PointerPressedHandler");
}

bool g_hooked = false;

void HookExtensions(HMODULE module) {
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &PointerPressed_Original,
         PointerPressed_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &PointerPressedWave1_Original,
         PointerPressedWave1_Hook,
         true},
    };
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Out(L"HookSymbols returned %d, PointerPressedHandler=%p, Wave1=%p",
        ok ? 1 : 0, (void*)PointerPressed_Original,
        (void*)PointerPressedWave1_Original);
    g_hooked = PointerPressed_Original || PointerPressedWave1_Original;
}

// Closes the panel from a thread that doesn't own it, and waits.
void ClosePanelFromAnyThread(const wchar_t* why) {
    mud::DispatcherQueue queue{nullptr};
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        queue = g_panelQueue;
    }
    if (!queue) {
        return;
    }
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool queued = false;
    try {
        queued = queue.TryEnqueue([done, why]() {
            ClosePanelOnThisThread(why);
            if (done) {
                SetEvent(done);
            }
        });
    } catch (...) {
        LogHresult(L"queueing the panel close");
    }
    queue = nullptr;
    if (queued && done) {
        DWORD wait = WaitForSingleObject(done, 4000);
        Out(L"panel close %s", wait == WAIT_OBJECT_0 ? L"done" : L"TIMED OUT");
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
    Out(L"PROBE START probe-explorer-host v0.1 compiled as %s, pid=%lu", arch,
        GetCurrentProcessId());

    g_showPanel = Wh_GetIntSetting(L"showPanel") != 0;
    Out(L"showPanel=%d", g_showPanel ? 1 : 0);

    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        Out(L"FileExplorerExtensions.dll isn't loaded yet. Open a File Explorer window, then disable and enable this mod.");
        return TRUE;
    }
    HookExtensions(module);
    return TRUE;
}

void Wh_ModAfterInit() {
    if (g_hooked) {
        Out(L"HOOKS READY. Click an empty spot of a File Explorer command bar.");
    }
}

void Wh_ModSettingsChanged() {
    g_showPanel = Wh_GetIntSetting(L"showPanel") != 0;
    Out(L"showPanel=%d", g_showPanel ? 1 : 0);
    if (!g_showPanel) {
        ClosePanelFromAnyThread(L"setting turned off");
    }
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    ClosePanelFromAnyThread(L"mod unloading");
}

void Wh_ModUninit() {
    Out(L"PROBE END");
}
