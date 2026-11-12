// ==WindhawkMod==
// @id              probe-explorer-recon
// @name            Probe: File Explorer recon
// @description     Diagnostic only, read-only. Logs the window tree of a File Explorer window, the UI modules loaded, and the symbols that build its View menu and paint its file list.
// @version         0.3
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lshlwapi -lpsapi
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: File Explorer recon

A read-only diagnostic for two ideas: a native entry in File Explorer's View
menu, and a mark drawn on one item in the file list. It hooks nothing and
changes nothing. It only writes to the log.

**Before enabling:** open one File Explorer window on a folder with a few files.

It logs three things:

1. The UI modules loaded in each explorer.exe process.
2. The child window tree of the first File Explorer window in that process.
3. Symbols whose names match a few keywords, from `Windows.UI.FileExplorer.dll`
   and `ExplorerFrame.dll`.

The symbol scan can take a few minutes the first time, while Windhawk downloads
symbols from Microsoft. Disabling the mod during the scan waits for the current
step to end.

Send back the whole log, from `PROBE START` to `PROBE END`. With the option to
open folder windows in a separate process, there are two logs, one per process.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- scanSymbols: true
  $name: Scan symbols
  $description: The slow part. Turn off to get only the modules and the window tree.
- maxSymbolsPerModule: 400
  $name: Maximum matching symbols logged per module
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>

#include <atomic>

namespace {

HANDLE g_thread = nullptr;
std::atomic<bool> g_stop{false};
int g_treeLines = 0;

bool ContainsI(const wchar_t* text, const wchar_t* needle) {
    return text && StrStrIW(text, needle) != nullptr;
}

void LogModules() {
    static const wchar_t* const kKeywords[] = {
        L"xaml",        L"FileExplorer", L"ExplorerFrame", L"dui70",
        L"duser",       L"FrameworkUdk", L"WindowsAppRuntime",
        L"Microsoft.UI", L"shell32",     L"comctl32",      L"dcomp",
    };

    HMODULE mods[2048];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods),
                               &needed)) {
        Wh_Log(L"EnumProcessModules failed, gle=%lu", GetLastError());
        return;
    }
    DWORD count = needed / sizeof(HMODULE);
    if (count > ARRAYSIZE(mods)) {
        count = ARRAYSIZE(mods);
    }
    Wh_Log(L"MODULES: %lu loaded, listing UI-related ones", count);
    for (DWORD i = 0; i < count; i++) {
        WCHAR path[MAX_PATH];
        if (!GetModuleFileNameW(mods[i], path, ARRAYSIZE(path))) {
            continue;
        }
        for (const wchar_t* keyword : kKeywords) {
            if (ContainsI(path, keyword)) {
                Wh_Log(L"  module %p %s", mods[i], path);
                break;
            }
        }
    }
}

void LogWindowTree(HWND parent, int depth) {
    if (depth > 12) {
        return;
    }
    HWND child = nullptr;
    while ((child = FindWindowExW(parent, child, nullptr, nullptr))) {
        if (g_stop || g_treeLines >= 300) {
            return;
        }
        WCHAR cls[128] = L"";
        WCHAR title[64] = L"";
        GetClassNameW(child, cls, ARRAYSIZE(cls));
        GetWindowTextW(child, title, ARRAYSIZE(title));
        RECT rc = {};
        GetWindowRect(child, &rc);
        DWORD tid = GetWindowThreadProcessId(child, nullptr);
        Wh_Log(L"  tree %*s%s \"%s\" hwnd=%p %ldx%ld visible=%d tid=%lu",
               depth * 2, L"", cls, title, child, rc.right - rc.left,
               rc.bottom - rc.top, IsWindowVisible(child) ? 1 : 0, tid);
        g_treeLines++;
        LogWindowTree(child, depth + 1);
    }
}

void LogExplorerWindows() {
    DWORD pid = GetCurrentProcessId();
    HWND top = nullptr;
    int found = 0;
    while ((top = FindWindowExW(nullptr, top, L"CabinetWClass", nullptr))) {
        DWORD windowPid = 0;
        DWORD tid = GetWindowThreadProcessId(top, &windowPid);
        if (windowPid != pid) {
            continue;
        }
        found++;
        WCHAR title[128] = L"";
        GetWindowTextW(top, title, ARRAYSIZE(title));
        Wh_Log(L"WINDOW %d: CabinetWClass \"%s\" hwnd=%p tid=%lu visible=%d minimized=%d foreground=%d",
               found, title, top, tid, IsWindowVisible(top) ? 1 : 0,
               IsIconic(top) ? 1 : 0, GetForegroundWindow() == top ? 1 : 0);
        if (found == 1) {
            g_treeLines = 0;
            LogWindowTree(top, 1);
            if (g_treeLines >= 300) {
                Wh_Log(L"  tree cut at 300 lines");
            }
        }
    }
    if (!found) {
        Wh_Log(L"WINDOWS: no File Explorer window in this process. Open one and re-enable the mod.");
    }

    HWND progman = FindWindowW(L"Progman", nullptr);
    DWORD progmanPid = 0;
    if (progman) {
        GetWindowThreadProcessId(progman, &progmanPid);
    }
    Wh_Log(L"This process %s the desktop and taskbar.",
           progmanPid == pid ? L"owns" : L"does not own");
}

struct SymbolQuery {
    const wchar_t* module;
    // A symbol is logged when it contains one word from `any` and, if
    // `also` isn't empty, one word from `also` as well.
    const wchar_t* any[8];
    const wchar_t* also[6];
    // A symbol that contains a word from `skip` is never logged. Used to drop
    // the generated C++/WinRT plumbing, which is most of the matches.
    const wchar_t* skip[6];
};

bool MatchesList(const wchar_t* text, const wchar_t* const* list, int size) {
    for (int i = 0; i < size && list[i]; i++) {
        if (ContainsI(text, list[i])) {
            return true;
        }
    }
    return false;
}

void ScanSymbols(const SymbolQuery& query, int maxLogged) {
    if (g_stop) {
        return;
    }

    // Already-loaded modules first: some live in package folders that a
    // System32 search can't find.
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(0, query.module, &module)) {
        module = LoadLibraryExW(query.module, nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!module) {
        Wh_Log(L"SYMBOLS %s: can't load, gle=%lu", query.module,
               GetLastError());
        return;
    }

    WCHAR path[MAX_PATH] = L"";
    GetModuleFileNameW(module, path, ARRAYSIZE(path));
    Wh_Log(L"SYMBOLS %s: scan starting, module=%p path=%s", query.module,
           module, path);

    WH_FIND_SYMBOL symbol = {};
    ULONGLONG started = GetTickCount64();
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &symbol);
    if (!search) {
        Wh_Log(L"SYMBOLS %s: default options gave nothing after %llu ms, retrying with explicit options",
               query.module, GetTickCount64() - started);
        WH_FIND_SYMBOL_OPTIONS options = {};
        options.optionsSize = sizeof(options);
        started = GetTickCount64();
        search = Wh_FindFirstSymbol(module, &options, &symbol);
    }
    if (!search) {
        Wh_Log(L"SYMBOLS %s: no symbols available after %llu ms",
               query.module, GetTickCount64() - started);
        FreeLibrary(module);
        return;
    }

    int total = 0, matched = 0, logged = 0;
    do {
        total++;
        if (!symbol.symbol) {
            continue;
        }
        if (!MatchesList(symbol.symbol, query.any, ARRAYSIZE(query.any))) {
            continue;
        }
        if (query.also[0] &&
            !MatchesList(symbol.symbol, query.also, ARRAYSIZE(query.also))) {
            continue;
        }
        if (query.skip[0] &&
            MatchesList(symbol.symbol, query.skip, ARRAYSIZE(query.skip))) {
            continue;
        }
        matched++;
        if (logged < maxLogged) {
            // Long names are cut so one line stays readable. The full name
            // can be looked up later with Windhawk Symbol Helper.
            Wh_Log(L"  sym %.600s", symbol.symbol);
            logged++;
        }
    } while (!g_stop && Wh_FindNextSymbol(search, &symbol));

    Wh_FindCloseSymbol(search);
    FreeLibrary(module);

    Wh_Log(L"SYMBOLS %s: %d symbols seen, %d matched, %d logged%s",
           query.module, total, matched, logged,
           g_stop ? L", stopped early" : L"");
}

DWORD WINAPI ProbeThread(LPVOID) {
    // Give Explorer a moment if the mod loaded at process start.
    for (int i = 0; i < 15 && !g_stop; i++) {
        Sleep(100);
    }
    if (g_stop) {
        return 0;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    const wchar_t* arch = L"arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif

    Wh_Log(L"PROBE START probe-explorer-recon v0.3 compiled as %s, pid=%lu",
           arch, GetCurrentProcessId());

    LogModules();
    LogExplorerWindows();

    if (Wh_GetIntSetting(L"scanSymbols")) {
        int maxLogged = Wh_GetIntSetting(L"maxSymbolsPerModule");
        if (maxLogged < 10) maxLogged = 10;
        if (maxLogged > 1000) maxLogged = 1000;

        // Control: this repo's stack mod resolves this symbol on this
        // machine. Run 0.2 found nothing here, so symbol lookup fails for
        // System32 modules from this mod. Kept to see whether Windhawk's
        // detailed debug logs explain why.
        static const SymbolQuery kControl = {
            L"taskbar.dll",
            {L"TrayUI::StartTaskbar"},
            {},
            {},
        };
        // Run 0.2 showed the command bar lives in FileExplorerExtensions.dll.
        // The classes' own methods, without the generated plumbing.
        static const SymbolQuery kBarClasses = {
            L"FileExplorerExtensions.dll",
            {L"implementation::CommandBarFlyout::",
             L"implementation::MenuFlyoutControl::",
             L"implementation::CommandBarManager::",
             L"implementation::CommandBarItemVM::",
             L"implementation::CommandBarControl_Wave1::",
             L"implementation::CommandBarControl::",
             L"implementation::CommandBarExtension::"},
            {},
            {L"winrt::impl::", L"`vcall'", L"Bindings", L"std::"},
        };
        // Anything that names the View menu, its layouts, or the flyout kinds.
        static const SymbolQuery kBarView = {
            L"FileExplorerExtensions.dll",
            {L"ViewMode", L"ViewMenu", L"LayoutMenu", L"FlyoutKind", L"SortBy",
             L"GroupBy", L"ViewLayout"},
            {},
            {L"`vcall'", L"std::"},
        };
        // The controller the command bar talks to.
        static const SymbolQuery kBarController = {
            L"FileExplorerExtensions.dll",
            {L"FileExplorerCommandBarController"},
            {L"consume_"},
            {L"`vcall'", L"std::"},
        };
        // What paints one item in the file list. Run 0.2 got no symbols here.
        static const SymbolQuery kItems = {
            L"ExplorerFrame.dll",
            {L"UIItem", L"ItemsView"},
            {L"Paint", L"Draw", L"Render"},
            {},
        };

        ScanSymbols(kControl, maxLogged);
        ScanSymbols(kBarClasses, maxLogged);
        ScanSymbols(kBarView, maxLogged);
        ScanSymbols(kBarController, maxLogged);
        ScanSymbols(kItems, maxLogged);
    } else {
        Wh_Log(L"Symbol scan is off.");
    }

    Wh_Log(L"PROBE END");
    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    return TRUE;
}

void Wh_ModAfterInit() {
    g_thread = CreateThread(nullptr, 0, ProbeThread, nullptr, 0, nullptr);
}

// A changed setting reloads the mod, which runs the probe again.
BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    *bReload = TRUE;
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_stop = true;
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

void Wh_ModUninit() {}
