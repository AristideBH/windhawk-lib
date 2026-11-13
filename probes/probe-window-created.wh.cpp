// ==WindhawkMod==
// @id              probe-window-created
// @name            Probe: File Explorer window created
// @description     Diagnostic only. Finds the earliest hook in the command bar that runs when an Explorer window opens, and whether the View flyout can be reached from it.
// @version         0.1
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

Diagnostic for Explorer Extras 0.3. It lists the methods of the command bar
classes in `FileExplorerExtensions.dll`, hooks the ones that look like
creation or load steps, and logs each call with the thread, the window state
and whether the View flyout can be found from the object.

**Run it:** enable, then open one new Explorer window and one new tab. Send
back `probes\logs\probe-window-created.log`.

Turn off "Look inside objects" if Explorer crashes with the probe on.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- tryObjects: true
  $name: Look inside objects
  $description: After each hooked call, treat the arguments as XAML objects and look for the View flyout. Turn off if Explorer crashes.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shlwapi.h>

#undef GetCurrentTime

#include <array>
#include <atomic>
#include <cstdarg>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;

namespace {

constexpr int kMaxSlots = 40;
constexpr int kMaxEnterLogsPerSlot = 40;

std::atomic<bool> g_tryObjects{true};
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_hooked{false};
std::mutex g_outMutex;
wchar_t g_outPath[MAX_PATH] = L"";
ULONGLONG g_startTick = 0;

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
    // UTF-8 so the log reads in any editor.
    char utf8[3000];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, n, utf8, sizeof(utf8),
                                    nullptr, nullptr);
    DWORD written = 0;
    if (bytes > 0) {
        WriteFile(file, utf8, bytes, &written, nullptr);
    }
    CloseHandle(file);
}

// ---- Pointer checks, so a wrong guess about an argument can't crash ----

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
    return (const char*)p + size <= (const char*)info.BaseAddress + info.RegionSize;
}

// Looks like a COM object: readable, its first word points to a table whose
// first entry is code inside a loaded module.
bool LooksLikeObject(const void* p) {
    if (!Readable(p, sizeof(void*))) {
        return false;
    }
    const void* vtable = *(const void* const*)p;
    if (!Readable(vtable, sizeof(void*) * 3)) {
        return false;
    }
    const void* first = *(const void* const*)vtable;
    HMODULE module = nullptr;
    return first && GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)first, &module);
}

// ---- What a thread's window looks like right now ----

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

std::wstring WindowState() {
    FindTop ctx;
    EnumThreadWindows(GetCurrentThreadId(), FindTopProc, (LPARAM)&ctx);
    if (!ctx.top) {
        return L"no CabinetWClass on this thread";
    }
    HWND bridge = FindWindowExW(
        ctx.top, nullptr, L"Microsoft.UI.Content.DesktopChildSiteBridge",
        nullptr);
    HWND tab = FindWindowExW(ctx.top, nullptr, L"ShellTabWindowClass", nullptr);
    wchar_t text[200];
    _snwprintf_s(text, ARRAYSIZE(text), _TRUNCATE,
                 L"window=%p visible=%d bridge=%d tab=%d", ctx.top,
                 IsWindowVisible(ctx.top) ? 1 : 0, bridge ? 1 : 0, tab ? 1 : 0);
    return text;
}

// ---- Looking for the View flyout from an object ----

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

// `iface` must already look like a COM object.
void TryObject(const wchar_t* label, const wchar_t* name, void* iface) {
    try {
        winrt::Windows::Foundation::IUnknown unknown{nullptr};
        winrt::copy_from_abi(unknown, iface);
        auto dep = unknown.try_as<mux::DependencyObject>();
        if (!dep) {
            Out(L"  %s %s: object, not a DependencyObject", name, label);
            return;
        }
        std::wstring cls = winrt::get_class_name(dep).c_str();
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
        Out(L"  %s %s: class=%s depthToRoot=%d viewFlyoutFound=%d visited=%d buttons=%d",
            name, label, cls.c_str(), up, scan.found ? 1 : 0, scan.visited,
            scan.buttons);
    } catch (...) {
        Out(L"  %s %s: exception while looking", name, label);
    }
}

// ---- Generic hooks ----

std::wstring g_slotNames[kMaxSlots];
std::atomic<int> g_slotEnters[kMaxSlots];
std::atomic<int> g_slotCount{0};
std::atomic<int> g_callSeq{0};
thread_local ULONGLONG t_firstCall = 0;

using Generic_t = void*(WINAPI*)(void*, void*, void*, void*, void*, void*,
                                 void*, void*);

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

void Leave(int slot, void* a0, void* a1) {
    if (g_unloading || !g_tryObjects) {
        return;
    }
    if (g_slotEnters[slot] > kMaxEnterLogsPerSlot) {
        return;
    }
    const wchar_t* name = g_slotNames[slot].c_str();
    if (LooksLikeObject(a0)) {
        TryObject(L"this", name, a0);
    }
    // A handler's `sender` argument is a reference to an IInspectable.
    if (Readable(a1, sizeof(void*)) && LooksLikeObject(*(void**)a1)) {
        TryObject(L"arg1", name, *(void**)a1);
    }
}

template <int N>
struct Slot {
    static inline Generic_t original = nullptr;
    static void* WINAPI Hook(void* a0, void* a1, void* a2, void* a3, void* a4,
                             void* a5, void* a6, void* a7) {
        Enter(N);
        void* result = original(a0, a1, a2, a3, a4, a5, a6, a7);
        Leave(N, a0, a1);
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

// "...implementation::Class::Method(" gives Class and Method. Templates and
// odd shapes are skipped.
bool SplitName(const std::wstring& symbol, std::wstring* cls,
               std::wstring* method) {
    size_t impl = symbol.find(L"implementation::");
    size_t paren = symbol.find(L'(');
    if (impl == std::wstring::npos || paren == std::wstring::npos ||
        paren < impl) {
        return false;
    }
    std::wstring head = symbol.substr(impl + 16, paren - (impl + 16));
    if (head.find(L'<') != std::wstring::npos ||
        head.find(L' ') != std::wstring::npos) {
        return false;
    }
    size_t sep = head.rfind(L"::");
    if (sep == std::wstring::npos) {
        return false;
    }
    *cls = head.substr(0, sep);
    *method = head.substr(sep + 2);
    return true;
}

bool WantHook(const std::wstring& cls, const std::wstring& method) {
    static const wchar_t* const kSkip[] = {
        L"get_",     L"put_",        L"add_",       L"remove_",
        L"Pointer",  L"Measure",     L"Arrange",    L"Size",
        L"Opacity",  L"Destructor",  L"Dispose",    L"GotFocus",
        L"LostFocus", L"KeyDown",    L"KeyUp",      L"Tapped",
    };
    for (const wchar_t* skip : kSkip) {
        if (method.find(skip) == 0 || Contains(method, skip)) {
            return false;
        }
    }
    if (method == cls) {
        return true;  // constructor
    }
    static const wchar_t* const kWant[] = {
        L"OnApplyTemplate", L"Loaded", L"Initialize", L"Create", L"Build",
        L"Setup",           L"Attach", L"Apply",      L"Load",   L"Populate",
        L"Init",            L"Start",  L"Register",   L"Update",
    };
    for (const wchar_t* want : kWant) {
        if (Contains(method, want)) {
            return true;
        }
    }
    return false;
}

void HookModule(HMODULE module) {
    static const auto installers =
        MakeInstallers(std::make_integer_sequence<int, kMaxSlots>{});
    static const wchar_t* const kClasses[] = {
        L"CommandBarControl_Wave1", L"CommandBarControl", L"CommandBarFlyout",
        L"CommandBarManager",       L"MenuFlyoutControl", L"CommandBarItemVM",
        L"CommandBarExtension",
    };

    WH_FIND_SYMBOL symbol = {};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &symbol);
    if (!search) {
        Out(L"no symbols for FileExplorerExtensions.dll, nothing hooked");
        return;
    }
    int listed = 0, hooked = 0, failed = 0;
    do {
        if (!symbol.symbol) {
            continue;
        }
        std::wstring text = symbol.symbol;
        if (Contains(text, L"winrt::impl::") || Contains(text, L"`vcall'") ||
            Contains(text, L"std::") || Contains(text, L"Bindings") ||
            Contains(text, L"consume_")) {
            continue;
        }
        std::wstring cls, method;
        if (!SplitName(text, &cls, &method)) {
            continue;
        }
        bool wanted = false;
        for (const wchar_t* c : kClasses) {
            if (cls == c) {
                wanted = true;
                break;
            }
        }
        if (!wanted) {
            continue;
        }
        bool hook = WantHook(cls, method) && g_slotCount < kMaxSlots;
        bool ok = false;
        if (hook) {
            int slot = g_slotCount;
            g_slotNames[slot] = cls + L"::" + method;
            if (installers[slot](symbol.address)) {
                g_slotCount++;
                hooked++;
                ok = true;
            } else {
                failed++;
            }
        }
        if (listed < 400) {
            Out(L"  method %s%s::%s", hook ? (ok ? L"[HOOKED] " : L"[FAILED] ") : L"", cls.c_str(),
                method.c_str());
            listed++;
        }
    } while (Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    Out(L"methods listed=%d hooked=%d failed=%d", listed, hooked, failed);
    g_hooked = hooked > 0;
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
    g_tryObjects = Wh_GetIntSetting(L"tryObjects") != 0;
    g_startTick = GetTickCount64();
    OutInit();
    Out(L"PROBE START probe-window-created v0.1, tryObjects=%d",
        g_tryObjects ? 1 : 0);
    Out(L"existing: %s", WindowState().c_str());

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
    g_tryObjects = Wh_GetIntSetting(L"tryObjects") != 0;
    Out(L"settings changed: tryObjects=%d", g_tryObjects ? 1 : 0);
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
}

void Wh_ModUninit() {
    Out(L"PROBE END");
}
