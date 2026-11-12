// ==WindhawkMod==
// @id              probe-dui70-exports
// @name            Probe: DirectUI exports
// @description     Diagnostic only, read-only. Lists the functions dui70.dll and its neighbours export, to see whether the File Explorer file list can be reached without symbol files.
// @version         0.2
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -ldbghelp -lshlwapi
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: DirectUI exports

Microsoft publishes no symbol files for the System32 modules of this Windows
build, so Windhawk can't hook their internal functions. Exported functions
need no symbol file. This probe lists what `dui70.dll`, `ExplorerFrame.dll`
and `duser.dll` export, filtered to the functions that paint, lay out or walk
the elements of the file list.

It hooks nothing and changes nothing. It makes no network request. It runs in a
second or two.

Enable it. The log also goes to `probes\logs\probe-dui70-exports.log` in the
repo.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- maxPerKeyword: 40
  $name: Maximum functions logged per keyword
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <dbghelp.h>
#include <shlwapi.h>

#include <atomic>
#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <string>

namespace {

HANDLE g_thread = nullptr;
std::atomic<bool> g_stop{false};

// Every log line also goes to a file in the repo, so the log can be read from
// disk: %USERPROFILE%\Code\windhawk-lib\probes\logs\probe-dui70-exports.log
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
    std::wstring path = dir + L"\\probe-dui70-exports.log";
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


struct ExportTable {
    BYTE* base = nullptr;
    DWORD* names = nullptr;
    DWORD count = 0;
};

bool GetExports(HMODULE module, ExportTable* table) {
    auto base = (BYTE*)module;
    auto dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || !dir.Size) {
        return false;
    }
    auto exports = (IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    table->base = base;
    table->names = (DWORD*)(base + exports->AddressOfNames);
    table->count = exports->NumberOfNames;
    Out(L"  export table: %lu named functions, %lu functions in all",
           exports->NumberOfNames, exports->NumberOfFunctions);
    return true;
}

// Logs one export, readable form first, then the raw name a mod would pass to
// GetProcAddress.
void LogExport(const char* raw) {
    char readable[1024] = "";
    if (raw[0] == '?' &&
        UnDecorateSymbolName(raw, readable, sizeof(readable),
                             UNDNAME_COMPLETE)) {
        Out(L"    %.500S", readable);
        Out(L"      raw: %.300S", raw);
    } else {
        Out(L"    %.300S", raw);
    }
}

void ScanKeyword(const ExportTable& table, const char* keyword, int maxLogged) {
    int matched = 0;
    for (DWORD i = 0; i < table.count && !g_stop; i++) {
        const char* name = (const char*)(table.base + table.names[i]);
        if (StrStrIA(name, keyword)) {
            matched++;
        }
    }
    Out(L"  keyword \"%S\": %d exports", keyword, matched);
    int logged = 0;
    for (DWORD i = 0; i < table.count && logged < maxLogged && !g_stop; i++) {
        const char* name = (const char*)(table.base + table.names[i]);
        if (StrStrIA(name, keyword)) {
            LogExport(name);
            logged++;
        }
    }
}

void ProbeModule(const wchar_t* moduleName, const char* const* keywords,
                 int keywordCount, int maxLogged) {
    if (g_stop) {
        return;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(0, moduleName, &module)) {
        module =
            LoadLibraryExW(moduleName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!module) {
        Out(L"MODULE %s: can't load, gle=%lu", moduleName, GetLastError());
        return;
    }
    Out(L"MODULE %s: base=%p", moduleName, module);

    ExportTable table;
    if (!GetExports(module, &table)) {
        Out(L"  no export table");
        FreeLibrary(module);
        return;
    }
    for (int i = 0; i < keywordCount; i++) {
        ScanKeyword(table, keywords[i], maxLogged);
    }
    FreeLibrary(module);
}

DWORD WINAPI ProbeThread(LPVOID) {
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
    OutInit();
    Out(L"PROBE START probe-dui70-exports v0.2 compiled as %s, pid=%lu",
           arch, GetCurrentProcessId());

    int maxLogged = Wh_GetIntSetting(L"maxPerKeyword");
    if (maxLogged < 5) maxLogged = 5;
    if (maxLogged > 200) maxLogged = 200;

    // DirectUI itself: painting, the element tree, and hosting.
    static const char* const kDui[] = {
        "?Paint@",          // any class's Paint method
        "PaintBackground",
        "PaintBorder",
        "PaintContent",
        "?FindDescendent@",
        "?GetChildren@Element",
        "?GetParent@Element",
        "?GetID@Element",
        "?GetClassInfoW@",
        "?GetRootRelativeBounds@",
        "?GetLocation@Element",
        "?GetExtent@Element",
        "?GetVisible@Element",
        "?Add@Element",
        "?Create@Element@DirectUI",
        "?GetElementFromHWND@",
        "StrToID",
        "HWNDElement@DirectUI@@",  // class count only matters here
        "?OnPropertyChanged@Element",
        "?GetAccName@",
    };
    ProbeModule(L"dui70.dll", kDui, ARRAYSIZE(kDui), maxLogged);

    // The module that owns the file list's item classes.
    static const char* const kFrame[] = {
        "UIItem", "ItemsView", "Paint", "ViewMode", "DUI",
    };
    ProbeModule(L"ExplorerFrame.dll", kFrame, ARRAYSIZE(kFrame), maxLogged);

    // The layer under DirectUI that draws gadgets.
    static const char* const kDuser[] = {
        "Draw", "Paint", "Gadget",
    };
    ProbeModule(L"duser.dll", kDuser, ARRAYSIZE(kDuser), maxLogged);

    Out(L"PROBE END");
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
