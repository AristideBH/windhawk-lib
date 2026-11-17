// ==WindhawkMod==
// @id              probe-ctx-symbols
// @name            Probe: context menu symbols
// @description     Diagnostic only, read-only. Lists the symbols of FileExplorerExtensions.dll that look like the right-click menu.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @compilerOptions -lshlwapi
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: context menu symbols

Lists the symbols of `FileExplorerExtensions.dll` whose names contain
`ContextMenu`, `MenuFlyout` with `Show`, `ShellContext`, `Popup` or `Overflow`,
to the log file `%LOCALAPPDATA%\ProbeLabel\ctx-symbols.log`. Changes nothing.
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <shlwapi.h>

#include <string>

namespace {

HANDLE g_thread = nullptr;
volatile bool g_stop = false;
wchar_t g_path[MAX_PATH] = L"";

void Line(const wchar_t* text) {
    Wh_Log(L"%s", text);
    if (!g_path[0]) {
        return;
    }
    char utf8[2400];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8) - 3, nullptr, nullptr);
    if (bytes <= 0) {
        return;
    }
    utf8[bytes - 1] = '\r';
    utf8[bytes] = '\n';
    HANDLE file = CreateFileW(g_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, (DWORD)bytes + 1, &written, nullptr);
        CloseHandle(file);
    }
}

bool Interesting(const wchar_t* name) {
    static const wchar_t* words[] = {L"ContextMenu", L"ShellContext", L"Popup", L"OverflowMenu",
                                     L"ShowAt", L"MenuFlyout"};
    for (auto word : words) {
        if (StrStrIW(name, word)) {
            return true;
        }
    }
    return false;
}

DWORD WINAPI Scan(LPVOID) {
    Sleep(4000);
    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        Line(L"FileExplorerExtensions.dll is not loaded in this process");
        return 0;
    }
    WH_FIND_SYMBOL symbol = {};
    HANDLE search = Wh_FindFirstSymbol(module, nullptr, &symbol);
    if (!search) {
        Line(L"no symbols available");
        return 0;
    }
    int total = 0, logged = 0;
    do {
        total++;
        if (symbol.symbol && Interesting(symbol.symbol) && logged < 400) {
            logged++;
            wchar_t text[800];
            _snwprintf_s(text, ARRAYSIZE(text), _TRUNCATE, L"sym %.700s", symbol.symbol);
            Line(text);
        }
    } while (!g_stop && Wh_FindNextSymbol(search, &symbol));
    Wh_FindCloseSymbol(search);
    wchar_t done[120];
    _snwprintf_s(done, ARRAYSIZE(done), _TRUNCATE, L"done: %d symbols, %d logged", total, logged);
    Line(done);
    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    wchar_t local[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (len && len < MAX_PATH - 40) {
        wchar_t dir[MAX_PATH];
        swprintf_s(dir, L"%s\\ProbeLabel", local);
        CreateDirectoryW(dir, nullptr);
        swprintf_s(g_path, L"%s\\ctx-symbols.log", dir);
        DeleteFileW(g_path);
    }
    g_thread = CreateThread(nullptr, 0, Scan, nullptr, 0, nullptr);
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_stop = true;
    if (g_thread) {
        WaitForSingleObject(g_thread, 20000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

void Wh_ModUninit() {}
