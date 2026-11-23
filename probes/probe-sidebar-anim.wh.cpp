// ==WindhawkMod==
// @id              probe-sidebar-anim
// @name            Probe: sidebar island over the native pane, animations
// @description     Diagnostic only. Covers the navigation pane with a test XAML island and toggles an accordion section every 3 seconds, to see whether opacity and translation transitions animate in an island and whether the island follows the pane.
// @version         0.3
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshlwapi -luuid
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: sidebar island and animations

Open a NEW Explorer window after loading the mod. About 1.5 s later a test panel
covers the navigation pane. Its "Favorites" header toggles every 3 seconds:
the chevron cross-fades between two glyphs (opacity transition) and the rows
fade and slide (opacity and translation transitions) before the layout collapses.

The log is `probes\logs\probe-sidebar-anim.log`. It records the pane rectangle
every time it changes, the screen position of the first row (a screenshot sampler
reads the pixel there to see the fade), and each toggle.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- toggleMs: 3000
  $name: Toggle every (ms)
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shlobj.h>

#undef GetCurrentTime

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

#include <windhawk_utils.h>

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
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
namespace muh = winrt::Microsoft::UI::Xaml::Hosting;

namespace {

std::atomic<bool> g_unloading{false};
std::atomic<int> g_toggleMs{3000};

std::mutex g_outMutex;
wchar_t g_outPath[MAX_PATH] = L"";

void OutInit() {
    wchar_t profile[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (!len || len >= MAX_PATH) return;
    std::wstring dir = std::wstring(profile) + L"\\Code\\windhawk-lib\\probes\\logs";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\probe-sidebar-anim.log";
    wcscpy_s(g_outPath, path.c_str());
    HANDLE file = CreateFileW(g_outPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}

void Out(const wchar_t* format, ...) {
    wchar_t text[1000];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, ARRAYSIZE(text), _TRUNCATE, format, args);
    va_end(args);
    Wh_Log(L"%s", text);
    if (!g_outPath[0]) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t line[1100];
    int chars = _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE,
                             L"%02d:%02d:%02d.%03d %s\r\n", now.wHour, now.wMinute,
                             now.wSecond, now.wMilliseconds, text);
    if (chars < 0) chars = (int)wcslen(line);
    char utf8[3000];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, chars, utf8, sizeof(utf8),
                                    nullptr, nullptr);
    if (bytes <= 0) return;
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

std::wstring ClassNameOf(HWND hwnd) {
    wchar_t name[128] = L"";
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    return name;
}

struct Find {
    const wchar_t* cls;
    HWND found = nullptr;
};

BOOL CALLBACK FindProc(HWND hwnd, LPARAM lp) {
    auto* f = (Find*)lp;
    if (ClassNameOf(hwnd) == f->cls) {
        f->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindChild(HWND parent, const wchar_t* cls) {
    Find f{cls};
    EnumChildWindows(parent, FindProc, (LPARAM)&f);
    return f.found;
}

BOOL CALLBACK FindCabinet(HWND hwnd, LPARAM lp) {
    if (ClassNameOf(hwnd) == L"CabinetWClass") {
        *(HWND*)lp = hwnd;
        return FALSE;
    }
    return TRUE;
}

// Per-window probe state, one per Explorer UI thread.
struct Panel {
    HWND top = nullptr;
    HWND pane = nullptr;
    muh::DesktopWindowXamlSource source{nullptr};
    mud::DispatcherQueueTimer follow{nullptr};
    mud::DispatcherQueueTimer toggle{nullptr};
    mux::UIElement body{nullptr};
    mux::UIElement chevronUp{nullptr};
    mux::UIElement chevronDown{nullptr};
    bool collapsed = false;
    bool hidden = false;
    std::wstring lastSig;
    RECT lastRect{};
    int tries = 0;
};

thread_local Panel* t_panel = nullptr;
thread_local bool t_armed = false;

std::mutex g_panelsMutex;
struct Kept {
    mud::DispatcherQueue queue{nullptr};
    Panel* panel;
};
std::vector<Kept> g_kept;

// Each tab has its own pane; only the active tab's is visible through all its parents.
BOOL CALLBACK FindVisibleProc(HWND hwnd, LPARAM lp) {
    auto* f = (Find*)lp;
    if (IsWindowVisible(hwnd) && ClassNameOf(hwnd) == f->cls) {
        f->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

bool PaneRect(Panel* p, RECT* rect) {
    Find f{L"NamespaceTreeControl"};
    EnumChildWindows(p->top, FindVisibleProc, (LPARAM)&f);
    if (f.found && f.found != p->pane) {
        Out(L"PANE switched to tab pane %p", f.found);
        p->pane = f.found;
    }
    if (!f.found) {
        if (!p->hidden) {
            p->source.SiteBridge().Hide();
            p->hidden = true;
            Out(L"PANE hidden (no visible pane)");
        }
        return false;
    }
    if (!GetWindowRect(p->pane, rect)) return false;
    MapWindowPoints(nullptr, p->top, (POINT*)rect, 2);
    return rect->right > rect->left && rect->bottom > rect->top;
}

void Place(Panel* p, const RECT& r) {
    auto bridge = p->source.SiteBridge();
    bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
        r.left, r.top, r.right - r.left, r.bottom - r.top});
    bridge.Show();
    HWND bridgeWnd = (HWND)bridge.WindowId().Value;
    SetWindowPos(bridgeWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

mux::FrameworkElement MakeRow(const wchar_t* text) {
    muxc::TextBlock tb;
    tb.Text(text);
    tb.FontSize(14);
    tb.VerticalAlignment(mux::VerticalAlignment::Center);
    tb.Margin(mux::Thickness{16, 0, 0, 0});
    muxc::Border b;
    b.Height(32);
    b.Child(tb);
    return b;
}

void BuildAndShow(Panel* p) {
    p->source = muh::DesktopWindowXamlSource();
    p->source.Initialize(winrt::Microsoft::UI::WindowId{(uint64_t)p->top});

    muxc::StackPanel root;
    root.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
        winrt::Windows::UI::Color{255, 32, 32, 32}));

    // Header: whole row clickable, chevron stack on the right.
    muxc::Grid header;
    header.Height(32);
    header.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
        winrt::Windows::UI::Color{0, 255, 255, 255}));
    muxc::TextBlock title;
    title.Text(L"Favorites");
    title.FontSize(12);
    title.VerticalAlignment(mux::VerticalAlignment::Center);
    title.Margin(mux::Thickness{16, 0, 0, 0});
    header.Children().Append(title);

    muxc::Grid chevron;
    chevron.HorizontalAlignment(mux::HorizontalAlignment::Right);
    chevron.VerticalAlignment(mux::VerticalAlignment::Center);
    chevron.Margin(mux::Thickness{0, 0, 12, 0});
    muxc::FontIcon up;
    up.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe Fluent Icons"));
    up.Glyph(L"");
    up.FontSize(12);
    muxc::FontIcon down;
    down.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe Fluent Icons"));
    down.Glyph(L"");
    down.FontSize(12);
    down.Opacity(0);
    up.OpacityTransition(mux::ScalarTransition());
    down.OpacityTransition(mux::ScalarTransition());
    chevron.Children().Append(up);
    chevron.Children().Append(down);
    header.Children().Append(chevron);
    p->chevronUp = up;
    p->chevronDown = down;
    root.Children().Append(header);

    muxc::StackPanel body;
    for (auto text : {L"Desktop", L"Downloads", L"Documents", L"Images"}) {
        body.Children().Append(MakeRow(text));
    }
    body.OpacityTransition(mux::ScalarTransition());
    body.TranslationTransition(mux::Vector3Transition());
    root.Children().Append(body);
    p->body = body;

    p->source.Content(root);

    RECT r{};
    if (PaneRect(p, &r)) {
        Place(p, r);
        p->lastRect = r;
        Out(L"PANE rect %d,%d %dx%d (client of top)", r.left, r.top, r.right - r.left, r.bottom - r.top);
        // Screen position of the first row, for the screenshot sampler.
        POINT pt{r.left + 60, r.top + 32 + 16};
        ClientToScreen(p->top, &pt);
        Out(L"ROW screen %ld,%ld", pt.x, pt.y);
    }

    // Follow the pane (splitter drags, window resize).
    auto queue = mud::DispatcherQueue::GetForCurrentThread();
    p->follow = queue.CreateTimer();
    p->follow.Interval(std::chrono::milliseconds(100));
    p->follow.Tick([p](auto&&, auto&&) {
        if (g_unloading) return;
        // State dump: the pane and its parents (class, visible, size), logged when it changes.
        {
            std::wstring sig;
            wchar_t part[200];
            for (HWND w = p->pane; w && w != p->top; w = GetParent(w)) {
                RECT wr{};
                GetWindowRect(w, &wr);
                _snwprintf_s(part, ARRAYSIZE(part), _TRUNCATE, L"%s v%d %dx%d at %ld,%ld | ",
                             ClassNameOf(w).c_str(), (int)IsWindowVisible(w), wr.right - wr.left,
                             wr.bottom - wr.top, wr.left, wr.top);
                sig += part;
            }
            if (sig != p->lastSig) {
                p->lastSig = sig;
                Out(L"CHAIN %s", sig.c_str());
            }
        }
        RECT r{};
        if (!PaneRect(p, &r)) return;
        if (memcmp(&r, &p->lastRect, sizeof(r)) != 0 || p->hidden) {
            p->hidden = false;
            p->lastRect = r;
            Place(p, r);
            Out(L"PANE moved %d,%d %dx%d visible=%d", r.left, r.top, r.right - r.left,
                r.bottom - r.top, (int)IsWindowVisible(p->pane));
        }
    });
    p->follow.Start();

    p->toggle = queue.CreateTimer();
    p->toggle.Interval(std::chrono::milliseconds(g_toggleMs.load()));
    p->toggle.Tick([p](auto&&, auto&&) {
        if (g_unloading) return;
        p->collapsed = !p->collapsed;
        Out(L"TOGGLE collapsed=%d", (int)p->collapsed);
        if (p->collapsed) {
            p->chevronUp.Opacity(0);
            p->chevronDown.Opacity(1);
            p->body.Opacity(0);
            p->body.Translation({0, -8, 0});
        } else {
            p->chevronUp.Opacity(1);
            p->chevronDown.Opacity(0);
            p->body.Opacity(1);
            p->body.Translation({0, 0, 0});
        }
    });
    p->toggle.Start();

    {
        std::lock_guard<std::mutex> lock(g_panelsMutex);
        g_kept.push_back({queue, p});
    }
    Out(L"PANEL built on thread %lu", GetCurrentThreadId());
}

VOID CALLBACK ArmProc(HWND, UINT, UINT_PTR id, DWORD) {
    KillTimer(nullptr, id);
    t_armed = false;
    if (g_unloading || t_panel) return;
    HWND top = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), FindCabinet, (LPARAM)&top);
    HWND pane = top ? FindChild(top, L"NamespaceTreeControl") : nullptr;
    if (!top || !pane) {
        static thread_local int tries = 0;
        if (++tries < 6) {
            t_armed = true;
            SetTimer(nullptr, 0, 500, ArmProc);
        } else {
            Out(L"gave up: top=%p pane=%p", top, pane);
        }
        return;
    }
    try {
        auto* p = new Panel();
        p->top = top;
        p->pane = pane;
        t_panel = p;
        BuildAndShow(p);
    } catch (winrt::hresult_error const& e) {
        Out(L"BUILD FAILED hr=0x%08X %s", (unsigned)e.code().value, e.message().c_str());
    } catch (...) {
        Out(L"BUILD FAILED (unknown)");
    }
}

using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int,
                                        int, int, HWND, HMENU, HINSTANCE, LPVOID);
CreateWindowExW_t CreateWindowExW_Original;

HWND WINAPI CreateWindowExW_Hook(DWORD ex, LPCWSTR cls, LPCWSTR name, DWORD style,
                                 int x, int y, int w, int h, HWND parent, HMENU menu,
                                 HINSTANCE inst, LPVOID param) {
    HWND hwnd = CreateWindowExW_Original(ex, cls, name, style, x, y, w, h, parent,
                                         menu, inst, param);
    if (hwnd && !t_panel && !t_armed && !g_unloading && cls &&
        !IS_INTRESOURCE(cls) && wcscmp(cls, L"ShellTabWindowClass") == 0) {
        t_armed = true;
        SetTimer(nullptr, 0, 1500, ArmProc);
        Out(L"tab window created on thread %lu, arming", GetCurrentThreadId());
    }
    return hwnd;
}

}  // namespace

BOOL Wh_ModInit() {
    g_toggleMs = Wh_GetIntSetting(L"toggleMs");
    OutInit();
    Out(L"PROBE START probe-sidebar-anim v0.3");
    Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExW_Hook,
                       (void**)&CreateWindowExW_Original);
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    std::vector<Kept> kept;
    {
        std::lock_guard<std::mutex> lock(g_panelsMutex);
        kept.swap(g_kept);
    }
    for (auto& k : kept) {
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Panel* p = k.panel;
        bool queued = k.queue.TryEnqueue([p, done]() {
            try {
                if (p->follow) p->follow.Stop();
                if (p->toggle) p->toggle.Stop();
                p->follow = nullptr;
                p->toggle = nullptr;
                p->body = nullptr;
                p->chevronUp = nullptr;
                p->chevronDown = nullptr;
                if (p->source) {
                    p->source.Content(nullptr);
                    p->source.Close();
                    p->source = nullptr;
                }
            } catch (...) {
            }
            SetEvent(done);
        });
        if (queued) WaitForSingleObject(done, 3000);
        CloseHandle(done);
    }
}

void Wh_ModUninit() {
    Out(L"PROBE END");
}
