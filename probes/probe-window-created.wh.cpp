// ==WindhawkMod==
// @id              probe-window-created
// @name            Probe: File Explorer window created
// @description     Diagnostic only. Finds the earliest hook in the command bar that runs when an Explorer window opens, and when the View flyout appears after it.
// @version         0.2
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshlwapi -luuid
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: File Explorer window created

Diagnostic for Explorer Extras 0.3. Version 0.2 hooks at most three methods
of the command bar, and only ones whose signature it can check is safe. It
logs the full symbol of each, so the next version can use the exact type.

Run it in two steps, with no recompile between them:

1. "Inspect objects" at 0. Open one new window and one new tab. This only
   logs timing and window state.
2. Set "Inspect objects" to 1. Open one new window. After the command bar
   loads, the probe looks for the View flyout at 0, 100, 300, 700, 1500 and
   3000 ms and logs what it finds.

Version 0.1 crashed Explorer. A line "about to inspect" is written before
each look, so a crash shows where it happened. Send back
`probes\logs\probe-window-created.log`.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- inspect: 0
  $name: Inspect objects
  $description: 0 logs timing only. 1 also looks for the View flyout after the command bar loads.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shlwapi.h>

#undef GetCurrentTime

#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <mutex>
#include <string>
#include <utility>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
namespace mud = winrt::Microsoft::UI::Dispatching;

namespace {

constexpr int kMaxSlots = 4;
constexpr int kMaxEnterLogsPerSlot = 30;

std::atomic<int> g_inspect{0};
std::atomic<bool> g_unloading{false};
std::mutex g_outMutex;
wchar_t g_outPath[MAX_PATH] = L"";

void OutInit() {
    wchar_t profile[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return;
    }
    std::wstring dir =
        std::wstring(profile) + L"\\Code\\windhawk-lib\\probes\\logs";
    std::wstring path = dir + L"\\probe-window-created.log";
    if (path.size() >= MAX_PATH ||
        GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    wcscpy_s(g_outPath, path.c_str());
}

void Out(const wchar_t* format, ...) {
    wchar_t text[1500];
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
    wchar_t line[1600];
    int n = _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE,
                         L"%02d:%02d:%02d.%03d [%lu/%lu] %s\r\n", now.wHour,
                         now.wMinute, now.wSecond, now.wMilliseconds,
                         GetCurrentProcessId(), GetCurrentThreadId(), text);
    if (n <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_outMutex);
    HANDLE file = CreateFileW(g_outPath, FILE_APPEND_DATA, FILE_SHARE_READ,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    char utf8[4000];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8),
                                    nullptr, nullptr);
    DWORD written = 0;
    if (bytes > 0) {
        WriteFile(file, utf8, bytes, &written, nullptr);
    }
    CloseHandle(file);
}

// ---- The window on this thread ----

struct FindTop {
    HWND top = nullptr;
};

BOOL CALLBACK FindTopProc(HWND hwnd, LPARAM lParam) {
    wchar_t cls[64];
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) &&
        wcscmp(cls, L"CabinetWClass") == 0) {
        ((FindTop*)lParam)->top = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND TopOfThisThread() {
    FindTop ctx;
    EnumThreadWindows(GetCurrentThreadId(), FindTopProc, (LPARAM)&ctx);
    return ctx.top;
}

std::wstring WindowState() {
    HWND top = TopOfThisThread();
    if (!top) {
        return L"no CabinetWClass on this thread";
    }
    HWND bridge = FindWindowExW(
        top, nullptr, L"Microsoft.UI.Content.DesktopChildSiteBridge", nullptr);
    HWND tab = FindWindowExW(top, nullptr, L"ShellTabWindowClass", nullptr);
    wchar_t text[200];
    _snwprintf_s(text, ARRAYSIZE(text), _TRUNCATE,
                 L"window=%p visible=%d bridge=%d tab=%d", top,
                 IsWindowVisible(top) ? 1 : 0, bridge ? 1 : 0, tab ? 1 : 0);
    return text;
}

// ---- Looking for the View flyout ----

struct FlyoutScan {
    bool found = false;
    int visited = 0;
    int buttons = 0;
};

void ScanForViewFlyout(mux::DependencyObject const& node, int depth,
                       FlyoutScan& scan) {
    if (!node || depth > 40 || scan.visited > 3000 || scan.found) {
        return;
    }
    scan.visited++;
    if (auto button = node.try_as<muxc::Button>()) {
        scan.buttons++;
        if (auto flyout = button.Flyout()) {
            if (auto menu = flyout.try_as<muxc::MenuFlyout>()) {
                auto items = menu.Items();
                uint32_t radios = 0;
                for (uint32_t i = 0; i < items.Size() && i < 9; i++) {
                    if (!items.GetAt(i).try_as<muxc::RadioMenuFlyoutItem>()) {
                        break;
                    }
                    radios++;
                }
                if (items.Size() > 8 && radios >= 8) {
                    scan.found = true;
                    return;
                }
            }
        }
    }
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children && !scan.found; i++) {
        ScanForViewFlyout(mux::Media::VisualTreeHelper::GetChild(node, i),
                          depth + 1, scan);
    }
}

// ---- Delayed looks, from OnApplyTemplate's `this` ----
//
// One reference to the control is taken and never released, and each timer
// is kept alive the same way. Objects are only touched while the window
// they belong to still exists.

struct Look {
    void* control;  // IInspectable, one reference held for good
    HWND top;
    ULONGLONG since;
    int delay;
};

void RunLook(Look* look) {
    if (g_unloading) {
        return;
    }
    if (!IsWindow(look->top)) {
        Out(L"  look +%d ms: window is gone, not touching the control",
            look->delay);
        return;
    }
    Out(L"  about to inspect +%d ms", look->delay);
    try {
        winrt::Windows::Foundation::IInspectable control{nullptr};
        winrt::copy_from_abi(control, look->control);
        auto dep = control.try_as<mux::DependencyObject>();
        if (!dep) {
            Out(L"  look +%d ms: not a DependencyObject", look->delay);
            return;
        }
        mux::DependencyObject root = dep;
        int up = 0;
        for (; up < 80; up++) {
            auto parent = mux::Media::VisualTreeHelper::GetParent(root);
            if (!parent) {
                break;
            }
            root = parent;
        }
        FlyoutScan scan;
        ScanForViewFlyout(root, 0, scan);
        Out(L"  look +%d ms (real +%llu ms): class=%s depthToRoot=%d viewFlyoutFound=%d visited=%d buttons=%d",
            look->delay, GetTickCount64() - look->since,
            winrt::get_class_name(dep).c_str(), up, scan.found ? 1 : 0,
            scan.visited, scan.buttons);
    } catch (...) {
        Out(L"  look +%d ms: exception", look->delay);
    }
}

void ScheduleLooks(void* thisPtr) {
    try {
        winrt::Windows::Foundation::IInspectable control{nullptr};
        winrt::copy_from_abi(control, thisPtr);
        auto queue = mud::DispatcherQueue::GetForCurrentThread();
        if (!queue) {
            Out(L"  no dispatcher queue on this thread");
            return;
        }
        HWND top = TopOfThisThread();
        void* held = winrt::detach_abi(control);  // never released
        static const int kDelays[] = {0, 100, 300, 700, 1500, 3000};
        for (int delay : kDelays) {
            Look* look = new Look{held, top, GetTickCount64(), delay};
            auto timer = queue.CreateTimer();
            timer.Interval(std::chrono::milliseconds(delay ? delay : 1));
            timer.IsRepeating(false);
            timer.Tick([look](auto&&, auto&&) { RunLook(look); });
            timer.Start();
            winrt::detach_abi(timer);  // kept alive on purpose
        }
        Out(L"  looks scheduled");
    } catch (...) {
        Out(L"  could not schedule looks");
    }
}

// ---- Hooks ----

std::wstring g_slotNames[kMaxSlots];
bool g_slotLooks[kMaxSlots];
std::atomic<int> g_slotEnters[kMaxSlots];
std::atomic<int> g_slotCount{0};
std::atomic<int> g_callSeq{0};
thread_local ULONGLONG t_firstCall = 0;

using Generic_t = void*(WINAPI*)(void*, void*, void*, void*);

void Enter(int slot) {
    if (g_unloading) {
        return;
    }
    int n = ++g_slotEnters[slot];
    if (!t_firstCall) {
        t_firstCall = GetTickCount64();
    }
    if (n <= kMaxEnterLogsPerSlot) {
        Out(L"ENTER #%d %s  +%llu ms since this thread's first call, %s",
            ++g_callSeq, g_slotNames[slot].c_str(),
            GetTickCount64() - t_firstCall, WindowState().c_str());
    }
}

void Leave(int slot, void* a0) {
    if (g_unloading || !g_slotLooks[slot] || g_inspect.load() < 1) {
        return;
    }
    if (g_slotEnters[slot] > kMaxEnterLogsPerSlot) {
        return;
    }
    Out(L"LEAVE %s, scheduling looks at the control", g_slotNames[slot].c_str());
    ScheduleLooks(a0);
}

// Four register arguments are enough on both x64 and ARM64. The signature
// check below only lets through methods that fit.
template <int N>
struct Slot {
    static inline Generic_t original = nullptr;
    static void* WINAPI Hook(void* a0, void* a1, void* a2, void* a3) {
        Enter(N);
        void* result = original(a0, a1, a2, a3);
        Leave(N, a0);
        return result;
    }
    static bool Install(void* address) {
        return Wh_SetFunctionHook(address, (void*)&Hook, (void**)&original);
    }
};

using Installer_t = bool (*)(void*);

template <int... I>
constexpr std::array<Installer_t, sizeof...(I)> MakeInstallers(
    std::integer_sequence<int, I...>) {
    return {{&Slot<I>::Install...}};
}

// ---- Choosing what to hook ----

bool Contains(const std::wstring& text, const wchar_t* needle) {
    return text.find(needle) != std::wstring::npos;
}

// The parameter list of "...Class::Method(params)". Returns false when the
// text has no list.
bool ParamList(const std::wstring& symbol, size_t methodAt, std::wstring* out) {
    size_t open = symbol.find(L'(', methodAt);
    if (open == std::wstring::npos) {
        return false;
    }
    int depth = 0;
    for (size_t i = open; i < symbol.size(); i++) {
        if (symbol[i] == L'(') {
            depth++;
        } else if (symbol[i] == L')') {
            depth--;
            if (depth == 0) {
                *out = symbol.substr(open + 1, i - open - 1);
                return true;
            }
        }
    }
    return false;
}

// Safe to forward through four integer registers: no floating point, no
// structure passed by value, at most three parameters. Everything the
// hooks in explorer-extras use passes this.
bool SignatureIsSafe(const std::wstring& params, std::wstring* why) {
    if (params == L"void" || params.empty()) {
        return true;
    }
    if (Contains(params, L"float") || Contains(params, L"double")) {
        *why = L"floating point parameter";
        return false;
    }
    int count = 1;
    int angle = 0;
    for (wchar_t c : params) {
        if (c == L'<') angle++;
        if (c == L'>') angle--;
        if (c == L',' && angle == 0) count++;
    }
    if (count > 3) {
        *why = L"more than three parameters";
        return false;
    }
    // Each parameter must be a reference, a pointer, or a plain integer.
    size_t start = 0;
    angle = 0;
    for (size_t i = 0; i <= params.size(); i++) {
        wchar_t c = i < params.size() ? params[i] : L',';
        if (c == L'<') angle++;
        if (c == L'>') angle--;
        if (c == L',' && angle == 0) {
            std::wstring one = params.substr(start, i - start);
            start = i + 1;
            bool byRef = one.find(L'&') != std::wstring::npos ||
                         one.find(L'*') != std::wstring::npos;
            bool integer = one == L"int" || one == L"unsigned int" ||
                           one == L"bool" || one == L"unsigned long" ||
                           one == L"long" || one == L"__int64" ||
                           one == L"unsigned __int64" || one == L"char" ||
                           one == L"unsigned char" || one == L"short" ||
                           one == L"unsigned short";
            if (!byRef && !integer) {
                *why = L"parameter by value: " + one.substr(0, 80);
                return false;
            }
        }
    }
    return true;
}

struct Target {
    const wchar_t* cls;
    const wchar_t* method;
    bool looks;  // schedule looks after this one
};

const Target kTargets[] = {
    {L"CommandBarControl", L"OnApplyTemplate", true},
    {L"CommandBarControl_Wave1", L"OnApplyTemplate", true},
    {L"CommandBarFlyout", L"CreateSortAndGroupMenu", false},
    {L"CommandBarFlyout", L"CreateNewItemMenu", false},
};

void HookModule(HMODULE module) {
    static const auto installers =
        MakeInstallers(std::make_integer_sequence<int, kMaxSlots>{});

    WH_FIND_SYMBOL symbol = {};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &symbol);
    if (!search) {
        Out(L"no symbols for FileExplorerExtensions.dll, nothing hooked");
        return;
    }
    do {
        if (!symbol.symbol) {
            continue;
        }
        std::wstring text = symbol.symbol;
        if (Contains(text, L"winrt::impl::") || Contains(text, L"`vcall'") ||
            Contains(text, L"std::")) {
            continue;
        }
        for (const Target& target : kTargets) {
            std::wstring needle = std::wstring(L"implementation::") +
                                  target.cls + L"::" + target.method + L"(";
            size_t at = text.find(needle);
            if (at == std::wstring::npos) {
                continue;
            }
            Out(L"candidate: %.700s", text.c_str());
            std::wstring params, why;
            if (!ParamList(text, at, &params)) {
                Out(L"  skipped: no parameter list");
                continue;
            }
            if (!SignatureIsSafe(params, &why)) {
                Out(L"  skipped: %s", why.c_str());
                continue;
            }
            if (g_slotCount >= kMaxSlots) {
                Out(L"  skipped: no free slot");
                continue;
            }
            int slot = g_slotCount;
            g_slotNames[slot] = std::wstring(target.cls) + L"::" + target.method;
            g_slotLooks[slot] = target.looks;
            if (installers[slot](symbol.address)) {
                g_slotCount++;
                Out(L"  HOOKED as slot %d", slot);
            } else {
                Out(L"  hook failed");
            }
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    Out(L"hooked %d of the targets", g_slotCount.load());
}

std::atomic<bool> g_moduleDone{false};

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR name, HANDLE file, DWORD flags) {
    HMODULE module = LoadLibraryExW_Original(name, file, flags);
    const DWORD dataOnly = LOAD_LIBRARY_AS_DATAFILE |
                           LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE |
                           LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    if (module && !(flags & dataOnly) && !g_moduleDone && !g_unloading) {
        HMODULE ext = GetModuleHandleW(L"FileExplorerExtensions.dll");
        if (ext && !g_moduleDone.exchange(true)) {
            Out(L"FileExplorerExtensions.dll has loaded, hooking it now");
            HookModule(ext);
            Wh_ApplyHookOperations();
        }
    }
    return module;
}

}  // namespace

BOOL Wh_ModInit() {
    g_inspect = Wh_GetIntSetting(L"inspect");
    OutInit();
    Out(L"PROBE START probe-window-created v0.2, inspect=%d",
        g_inspect.load());

    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
        void* loadLibrary = kernelBase
            ? (void*)GetProcAddress(kernelBase, "LoadLibraryExW")
            : nullptr;
        BOOL waiting = loadLibrary &&
            Wh_SetFunctionHook(loadLibrary, (void*)LoadLibraryExW_Hook,
                               (void**)&LoadLibraryExW_Original);
        Out(L"FileExplorerExtensions.dll isn't loaded yet, waiting: %d",
            waiting ? 1 : 0);
        return TRUE;
    }
    g_moduleDone = true;
    HookModule(module);
    return TRUE;
}

void Wh_ModSettingsChanged() {
    g_inspect = Wh_GetIntSetting(L"inspect");
    Out(L"settings changed: inspect=%d", g_inspect.load());
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
}

void Wh_ModUninit() {
    Out(L"PROBE END");
}
