// ==WindhawkMod==
// @id              probe-symbols
// @name            Probe: symbol lookup
// @description     Diagnostic only, read-only. Finds out why Windhawk can't read symbols for System32 modules on this machine.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lwinhttp -lversion -ladvapi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: symbol lookup

On 2026-11-12, Windhawk's symbol lookup returned nothing for `taskbar.dll`,
`ExplorerFrame.dll` and other System32 modules on this machine, while it worked
for `FileExplorerExtensions.dll`. This probe finds out why. It hooks nothing
and changes nothing.

For each module it logs:

1. The file version, and whether the file is a hybrid ARM64X binary.
2. The name and id of the symbol file the module was built with.
3. Whether Microsoft's public symbol server has that symbol file.
4. Whether Windhawk's symbol lookup works, tried two ways.

**Network use.** Step 3 sends one small request per module to
`msdl.microsoft.com`, Microsoft's symbol server. The request contains the
symbol file's name and build id, the same thing Windhawk itself sends when it
downloads symbols. Nothing else leaves the machine. Turn the setting off to
skip this step.

Keep a File Explorer window open, enable the mod, and send the whole log from
`PROBE START` to `PROBE END`. It takes under a minute.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- checkSymbolServer: true
  $name: Ask Microsoft's symbol server
  $description: One small request per module to msdl.microsoft.com. Off skips the network step.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <string>

namespace {

HANDLE g_thread = nullptr;
std::atomic<bool> g_stop{false};

void LogOsBuild() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        Wh_Log(L"OS: can't read the version key");
        return;
    }
    WCHAR build[32] = L"?", display[32] = L"?";
    DWORD ubr = 0;
    DWORD size = sizeof(build);
    RegQueryValueExW(key, L"CurrentBuild", nullptr, nullptr, (BYTE*)build,
                     &size);
    size = sizeof(display);
    RegQueryValueExW(key, L"DisplayVersion", nullptr, nullptr, (BYTE*)display,
                     &size);
    size = sizeof(ubr);
    RegQueryValueExW(key, L"UBR", nullptr, nullptr, (BYTE*)&ubr, &size);
    RegCloseKey(key);
    build[ARRAYSIZE(build) - 1] = 0;
    display[ARRAYSIZE(display) - 1] = 0;
    Wh_Log(L"OS: Windows %s build %s.%lu", display, build, ubr);
}

void LogFileVersion(const wchar_t* path) {
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (!size) {
        Wh_Log(L"  file version: unavailable, gle=%lu", GetLastError());
        return;
    }
    std::string buffer(size, '\0');
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!GetFileVersionInfoW(path, 0, size, buffer.data()) ||
        !VerQueryValueW(buffer.data(), L"\\", (void**)&info, &infoSize) ||
        !info) {
        Wh_Log(L"  file version: unreadable");
        return;
    }
    Wh_Log(L"  file version: %u.%u.%u.%u", HIWORD(info->dwFileVersionMS),
           LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
           LOWORD(info->dwFileVersionLS));
}

// Asks the server whether a file exists, without downloading it. Returns the
// HTTP status, or 0 with `error` set when the request itself failed.
DWORD HttpHead(const wchar_t* host, const wchar_t* path, DWORD* error) {
    *error = 0;
    DWORD status = 0;
    HINTERNET session =
        WinHttpOpen(L"Microsoft-Symbol-Server/10.0.0.0",
                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        *error = GetLastError();
        return 0;
    }
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);
    HINTERNET connect =
        WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request =
        connect ? WinHttpOpenRequest(connect, L"HEAD", path, nullptr,
                                     WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE)
                : nullptr;
    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(
                request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                WINHTTP_NO_HEADER_INDEX)) {
            *error = GetLastError();
            status = 0;
        }
    } else {
        *error = GetLastError();
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return status;
}

// The record a linker leaves in a module to name its symbol file.
#pragma pack(push, 1)
struct CodeViewRsds {
    DWORD signature;  // 'RSDS'
    GUID guid;
    DWORD age;
    char pdbName[1];
};
#pragma pack(pop)

void LogPeFacts(HMODULE module, bool checkServer) {
    auto base = (BYTE*)module;
    auto dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        Wh_Log(L"  not a PE image");
        return;
    }
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    WORD machine = nt->FileHeader.Machine;
    Wh_Log(L"  machine=0x%04X (0xAA64 arm64, 0x8664 x64)", machine);

    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        Wh_Log(L"  not a 64-bit image, skipping the rest");
        return;
    }
    auto& dirs = nt->OptionalHeader.DataDirectory;

    // A hybrid ARM64X or ARM64EC binary carries a CHPE metadata pointer in
    // its load configuration, at offset 0xC8 of the 64-bit structure.
    auto& loadConfig = dirs[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    if (loadConfig.VirtualAddress) {
        BYTE* config = base + loadConfig.VirtualAddress;
        DWORD configSize = *(DWORD*)config;
        ULONGLONG chpe = 0;
        if (configSize >= 0xD0) {
            chpe = *(ULONGLONG*)(config + 0xC8);
        }
        Wh_Log(L"  load config size=0x%lX hybrid(CHPE metadata)=%d", configSize,
               chpe ? 1 : 0);
    } else {
        Wh_Log(L"  no load config directory");
    }

    auto& debug = dirs[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!debug.VirtualAddress || !debug.Size) {
        Wh_Log(L"  no debug directory, so no symbol file is named");
        return;
    }
    auto* entries = (IMAGE_DEBUG_DIRECTORY*)(base + debug.VirtualAddress);
    int count = (int)(debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY));
    Wh_Log(L"  debug directory: %d entries", count);
    for (int i = 0; i < count && i < 16; i++) {
        if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
            !entries[i].AddressOfRawData ||
            entries[i].SizeOfData < sizeof(CodeViewRsds)) {
            Wh_Log(L"    [%d] type=%lu size=%lu", i, entries[i].Type,
                   entries[i].SizeOfData);
            continue;
        }
        auto* cv = (CodeViewRsds*)(base + entries[i].AddressOfRawData);
        if (cv->signature != 0x53445352) {  // 'RSDS'
            Wh_Log(L"    [%d] CodeView record with an unknown signature 0x%08lX",
                   i, cv->signature);
            continue;
        }
        // Only the file name, in case the record holds a full build path.
        const char* name = cv->pdbName;
        for (const char* p = cv->pdbName; *p; p++) {
            if (*p == '\\' || *p == '/') {
                name = p + 1;
            }
        }
        WCHAR id[64];
        swprintf(id, ARRAYSIZE(id),
                 L"%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX",
                 cv->guid.Data1, cv->guid.Data2, cv->guid.Data3,
                 cv->guid.Data4[0], cv->guid.Data4[1], cv->guid.Data4[2],
                 cv->guid.Data4[3], cv->guid.Data4[4], cv->guid.Data4[5],
                 cv->guid.Data4[6], cv->guid.Data4[7], cv->age);
        Wh_Log(L"    [%d] symbol file=%S id=%s", i, name, id);

        if (checkServer && !g_stop) {
            WCHAR path[512];
            swprintf(path, ARRAYSIZE(path), L"/download/symbols/%S/%s/%S", name,
                     id, name);
            DWORD error = 0;
            ULONGLONG started = GetTickCount64();
            DWORD status = HttpHead(L"msdl.microsoft.com", path, &error);
            Wh_Log(L"    [%d] symbol server: HTTP %lu, error=%lu, %llu ms (200 present, 404 not published)",
                   i, status, error, GetTickCount64() - started);
        }
    }
}

int CountSymbols(HMODULE module, const WH_FIND_SYMBOL_OPTIONS* options,
                 ULONGLONG* elapsed) {
    ULONGLONG started = GetTickCount64();
    WH_FIND_SYMBOL symbol = {};
    HANDLE search = Wh_FindFirstSymbol(module, options, &symbol);
    int count = 0;
    if (search) {
        do {
            count++;
        } while (!g_stop && count < 5000 &&
                 Wh_FindNextSymbol(search, &symbol));
        Wh_FindCloseSymbol(search);
    }
    *elapsed = GetTickCount64() - started;
    return count;
}

void ProbeModule(const wchar_t* name, bool checkServer) {
    if (g_stop) {
        return;
    }
    HMODULE module = nullptr;
    bool loadedHere = false;
    if (!GetModuleHandleExW(0, name, &module)) {
        module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        loadedHere = module != nullptr;
    }
    if (!module) {
        Wh_Log(L"MODULE %s: not loaded and can't be loaded, gle=%lu", name,
               GetLastError());
        return;
    }

    WCHAR path[MAX_PATH] = L"";
    GetModuleFileNameW(module, path, ARRAYSIZE(path));
    Wh_Log(L"MODULE %s: base=%p %s path=%s", name, module,
           loadedHere ? L"(loaded by the probe)" : L"(already loaded)", path);

    LogFileVersion(path);
    LogPeFacts(module, checkServer);

    ULONGLONG elapsed = 0;
    int count = CountSymbols(module, nullptr, &elapsed);
    Wh_Log(L"  Windhawk lookup, default options: %d symbols%s in %llu ms",
           count, count >= 5000 ? L" or more" : L"", elapsed);

    if (count == 0) {
        WH_FIND_SYMBOL_OPTIONS options = {};
        options.optionsSize = sizeof(options);
        options.noUndecoratedSymbols = TRUE;
        count = CountSymbols(module, &options, &elapsed);
        Wh_Log(L"  Windhawk lookup, decorated only: %d symbols in %llu ms",
               count, elapsed);

        options.noUndecoratedSymbols = FALSE;
        options.symbolServer = L"https://msdl.microsoft.com/download/symbols";
        count = CountSymbols(module, &options, &elapsed);
        Wh_Log(L"  Windhawk lookup, server named explicitly: %d symbols in %llu ms",
               count, elapsed);
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
    Wh_Log(L"PROBE START probe-symbols v0.1 compiled as %s, pid=%lu", arch,
           GetCurrentProcessId());
    LogOsBuild();

    bool checkServer = Wh_GetIntSetting(L"checkSymbolServer") != 0;
    Wh_Log(L"checkSymbolServer=%d", checkServer ? 1 : 0);

    // The first is the control: lookup worked on it earlier today.
    static const wchar_t* const kModules[] = {
        L"FileExplorerExtensions.dll",
        L"taskbar.dll",
        L"Taskbar.View.dll",
        L"ExplorerFrame.dll",
        L"Windows.UI.FileExplorer.dll",
        L"shell32.dll",
        L"dui70.dll",
    };
    for (const wchar_t* name : kModules) {
        ProbeModule(name, checkServer);
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
