// ==WindhawkMod==
// @id              probe-window-created
// @name            Probe: File Explorer window created
// @description     Diagnostic only. Finds the earliest hook in the command bar that runs when an Explorer window opens, and when the View flyout appears after it.
// @version         0.6
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

Diagnostic for Explorer Extras 0.3. It hooks a few command bar methods behind
a signature check and logs the full symbol of each.

Version 0.2 crashed Explorer inside Microsoft.UI.Xaml.dll when it treated
`this` of a command bar method as a XAML object. Version 0.3 never does that.
It takes the control from the `sender` argument of
`OnCommandBarItemVMsChanged`, which is a real XAML reference, and checks that
the object's code lives in FileExplorerExtensions.dll or Microsoft.UI.Xaml.dll
before it touches it.

Run it in three steps, with no recompile between them. Open one new window
for each:

1. "Inspect objects" at 0. Logs timing and which argument holds the control.
2. Set it to 2. Timers fire after the window opens and log only that they
   fired. This tells timers apart from object access if something crashes.
3. Set it to 1. The probe looks for the View flyout at 0, 100, 300, 700, 1500
   and 3000 ms and logs what it finds.

A line "about to inspect" is written before each look. Send back
`probes\logs\probe-window-created.log` after each step if Explorer crashes.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- inspect: 0
  $name: Inspect objects
  $description: 0 logs timing only. 2 adds timers that touch no objects. 1 looks for the View flyout after the command bar loads.
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
    if (g_inspect.load() == 2) {
        Out(L"  timer +%d ms fired (real +%llu ms), no object touched",
            look->delay, GetTickCount64() - look->since);
        return;
    }
    Out(L"  about to inspect +%d ms", look->delay);
    try {
        winrt::Windows::Foundation::IInspectable control{nullptr};
        winrt::copy_from_abi(control, look->control);
        mux::DependencyObject dep{nullptr};
        if (auto xamlRoot = control.try_as<mux::XamlRoot>()) {
            // The whole island's content, which holds the command bar.
            dep = xamlRoot.Content().try_as<mux::DependencyObject>();
            Out(L"  object is a XamlRoot, size=%.0fx%.0f scale=%.2f",
                xamlRoot.Size().Width, xamlRoot.Size().Height,
                xamlRoot.RasterizationScale());
        } else {
            dep = control.try_as<mux::DependencyObject>();
        }
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

void ScheduleLooks(void* controlPtr) {
    try {
        winrt::Windows::Foundation::IInspectable control{nullptr};
        winrt::copy_from_abi(control, controlPtr);
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
bool g_slotStatic[kMaxSlots];
std::atomic<int> g_slotEnters[kMaxSlots];
std::atomic<int> g_slotCount{0};
std::atomic<int> g_callSeq{0};
thread_local ULONGLONG t_firstCall = 0;

using Generic_t = void*(WINAPI*)(void*, void*, void*, void*);

// ---- Telling a real XAML reference from anything else ----

bool Readable(const void* p, size_t size) {
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

// The first code address of the object's table must lie in one of the two
// modules that make the command bar.
bool OwnedByCommandBarModules(const void* object) {
    if (!Readable(object, sizeof(void*))) {
        return false;
    }
    const void* vtable = *(const void* const*)object;
    if (!Readable(vtable, sizeof(void*) * 3)) {
        return false;
    }
    const void* first = *(const void* const*)vtable;
    HMODULE module = nullptr;
    if (!first || !GetModuleHandleExW(
                      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      (LPCWSTR)first, &module)) {
        return false;
    }
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        return false;
    }
    return StrStrIW(path, L"FileExplorerExtensions.dll") ||
           StrStrIW(path, L"Microsoft.UI.Xaml.dll") ||
           StrStrIW(path, L"\\WindowsApps\\");
}

// What an argument looks like, for the log. Reads only memory that is
// readable.
void DescribeArgument(int index, void* arg) {
    if (!Readable(arg, sizeof(void*))) {
        Out(L"  arg%d %p: not readable", index, arg);
        return;
    }
    void* object = *(void**)arg;
    if (!Readable(object, sizeof(void*))) {
        Out(L"  arg%d %p: slot holds %p, not readable", index, arg, object);
        return;
    }
    void* vtable = *(void**)object;
    void* first = Readable(vtable, sizeof(void*)) ? *(void**)vtable : nullptr;
    wchar_t path[MAX_PATH] = L"?";
    HMODULE module = nullptr;
    if (first && GetModuleHandleExW(
                     GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     (LPCWSTR)first, &module)) {
        GetModuleFileNameW(module, path, ARRAYSIZE(path));
    }
    Out(L"  arg%d %p: slot=%p vtable=%p first=%p module=%s", index, arg, object,
        vtable, first, path);
}

// `arg` is a reference to a projected object, so it points at a slot that
// holds the object's pointer. Returns that pointer or null.
void* ControlFromReference(void* arg) {
    if (!Readable(arg, sizeof(void*))) {
        return nullptr;
    }
    void* object = *(void**)arg;
    return OwnedByCommandBarModules(object) ? object : nullptr;
}

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

thread_local void* t_lastControl = nullptr;

// Looked for BEFORE the original runs. OnDpiChanged is a coroutine that takes
// its XamlRoot by value and moves it into its own frame, so after the call
// the caller's slot is empty (confirmed in the 0.5 log). `this` of a
// non-static method is skipped: treating it as an object crashed version 0.2.
void* FindControlBefore(int slot, void* a0, void* a1, void* a2, void* a3,
                        bool isStatic) {
    if (g_unloading || !g_slotLooks[slot]) {
        return nullptr;
    }
    void* args[4] = {a0, a1, a2, a3};
    for (int i = isStatic ? 0 : 1; i < 4; i++) {
        if (void* control = ControlFromReference(args[i])) {
            Out(L"  control found in argument %d before the call", i);
            return control;
        }
    }
    if (g_slotEnters[slot] <= 3) {
        Out(L"  no control in the arguments before the call");
        for (int i = isStatic ? 0 : 1; i < 4; i++) {
            DescribeArgument(i, args[i]);
        }
    }
    return nullptr;
}

void LeaveWith(int slot, void* control) {
    if (g_unloading || !control || g_inspect.load() < 1 ||
        control == t_lastControl) {
        return;
    }
    t_lastControl = control;
    Out(L"LEAVE %s, scheduling looks", g_slotNames[slot].c_str());
    ScheduleLooks(control);
}

// Four register arguments are enough on both x64 and ARM64. The signature
// check below only lets through methods that fit.
template <int N>
struct Slot {
    static inline Generic_t original = nullptr;
    static void* WINAPI Hook(void* a0, void* a1, void* a2, void* a3) {
        Enter(N);
        void* control = FindControlBefore(N, a0, a1, a2, a3, g_slotStatic[N]);
        // A reference held across the call, so the object can't go away
        // under the original. Released here, while the window still exists.
        winrt::Windows::Foundation::IInspectable hold{nullptr};
        if (control) {
            winrt::copy_from_abi(hold, control);
        }
        void* result = original(a0, a1, a2, a3);
        LeaveWith(N, control);
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
            // A C++/WinRT object passed by value is one pointer wide and
            // arrives as a pointer to a temporary. Plain value types such as
            // Size or Rect are not, so they stay excluded.
            bool winrtObject =
                one.find(L"struct winrt::") == 0 &&
                !Contains(one, L"::Size") && !Contains(one, L"::Rect") &&
                !Contains(one, L"::Point") && !Contains(one, L"::Thickness") &&
                !Contains(one, L"::CornerRadius") &&
                !Contains(one, L"::TimeSpan") && !Contains(one, L"::DateTime") &&
                !Contains(one, L"::Color") && !Contains(one, L"::GridLength") &&
                !Contains(one, L"::Duration") && !Contains(one, L"::Vector");
            if (!byRef && !integer && !winrtObject) {
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
    {L"CommandBarControl_Wave1", L"OnDpiChanged", true},
    {L"CommandBarControl_Wave1", L"OnApplyTemplate", false},
};

// Methods of the command bar classes that take a XAML object, listed in full
// so the next version can pick a better hook. Nothing is hooked from this.
void ListXamlMethods(const std::wstring& text) {
    static int listed = 0;
    if (listed >= 60 || !Contains(text, L"implementation::CommandBar") ||
        Contains(text, L"winrt::impl::") || Contains(text, L"`vcall'") ||
        Contains(text, L"$_") || Contains(text, L"~")) {
        return;
    }
    size_t open = text.find(L'(', text.find(L"implementation::"));
    if (open == std::wstring::npos) {
        return;
    }
    std::wstring tail = text.substr(open);
    if (Contains(tail, L"Xaml::XamlRoot") ||
        Contains(tail, L"Xaml::UIElement") ||
        Contains(tail, L"Xaml::FrameworkElement") ||
        Contains(tail, L"Xaml::DependencyObject") ||
        Contains(tail, L"Xaml::RoutedEventArgs")) {
        Out(L"xaml-method: %.500s", text.c_str());
        listed++;
    }
}

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
        ListXamlMethods(text);
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
            g_slotStatic[slot] = Contains(text, L"static ");
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
    Out(L"PROBE START probe-window-created v0.6, inspect=%d",
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
