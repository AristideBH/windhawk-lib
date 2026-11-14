namespace {

std::atomic<bool> g_unloading{false};
std::atomic<bool> g_debugLog{true};
std::atomic<bool> g_follow{true};
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_restore{true};
std::atomic<bool> g_sidebarRoot{false};
std::atomic<bool> g_focusRect{true};
std::atomic<bool> g_compactScroll{false};
std::atomic<bool> g_doubleClickUp{false};
std::atomic<bool> g_headerKeyboard{false};
std::atomic<bool> g_archives{false};
std::atomic<bool> g_dragOut{false};
std::atomic<bool> g_keepStale{true};
std::atomic<bool> g_harmonize{true};
std::atomic<bool> g_springLoad{true};
std::atomic<bool> g_rememberSort{true};
std::atomic<bool> g_rememberWidth{true};
std::atomic<bool> g_rememberLayout{true};
std::atomic<bool> g_remoteMarks{true};
std::atomic<bool> g_desktopRealOnly{false};
std::atomic<int> g_backgroundMode{0};  // 0 Explorer, 1 theme, 2 transparent
std::atomic<int> g_styleVersion{0};
std::atomic<int> g_minWidth{160};
std::atomic<int> g_maxWidth{420};
std::atomic<int> g_maxItems{5000};
thread_local ULONGLONG g_lastWalk = 0;

// Every log line also goes to a file in the repo, so the log can be read from
// disk: %USERPROFILE%\Code\windhawk-lib\probes\logs\explorer-extras.log
std::mutex g_outMutex;
wchar_t g_outPath[MAX_PATH] = L"";

void OutInit() {
    wchar_t profile[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return;
    }
    std::wstring dir = std::wstring(profile) + L"\\Code\\windhawk-lib\\probes\\logs";
    std::wstring path = dir + L"\\explorer-extras.log";
    if (path.size() >= MAX_PATH ||
        GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    wcscpy_s(g_outPath, path.c_str());

    // The log is kept across restarts, so the lines before a crash survive.
    // Past 400 KB, only the last 200 KB are kept.
    HANDLE file = CreateFileW(g_outPath, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 400 * 1024) {
        const DWORD keep = 200 * 1024;
        std::vector<char> tail(keep);
        LARGE_INTEGER from;
        from.QuadPart = size.QuadPart - keep;
        DWORD read = 0;
        if (SetFilePointerEx(file, from, nullptr, FILE_BEGIN) &&
            ReadFile(file, tail.data(), keep, &read, nullptr)) {
            SetFilePointer(file, 0, nullptr, FILE_BEGIN);
            SetEndOfFile(file);
            DWORD written = 0;
            WriteFile(file, tail.data(), read, &written, nullptr);
        }
    }
    CloseHandle(file);
}

void Out(const wchar_t* format, ...) {
    wchar_t text[1200];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, ARRAYSIZE(text), _TRUNCATE, format, args);
    va_end(args);
    Wh_Log(L"%s", text);

    if (!g_debugLog || !g_outPath[0]) {
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t line[1300];
    int chars = _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE,
                             L"%02d:%02d:%02d.%03d [%lu] %s\r\n", now.wHour,
                             now.wMinute, now.wSecond, now.wMilliseconds,
                             GetCurrentThreadId(), text);
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


// ---------------------------------------------------------------------------
// Windows and settings
// ---------------------------------------------------------------------------

std::wstring ClassNameOf(HWND hwnd) {
    wchar_t name[128] = L"";
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    return name;
}

struct FindContext {
    const wchar_t* className = nullptr;
    HWND found = nullptr;
};

// First child of this class that is visible through all its parents.
BOOL CALLBACK FindVisibleChildProc(HWND hwnd, LPARAM lParam) {
    auto* context = (FindContext*)lParam;
    if (IsWindowVisible(hwnd) && ClassNameOf(hwnd) == context->className) {
        bool shown = true;
        for (HWND p = GetParent(hwnd); p; p = GetParent(p)) {
            if (!IsWindowVisible(p)) {
                shown = false;
                break;
            }
        }
        if (shown) {
            context->found = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

HWND FindVisibleChild(HWND parent, const wchar_t* className) {
    FindContext context;
    context.className = className;
    EnumChildWindows(parent, FindVisibleChildProc, (LPARAM)&context);
    return context.found;
}

BOOL CALLBACK FindCabinetOnThread(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd) && ClassNameOf(hwnd) == L"CabinetWClass") {
        ((FindContext*)lParam)->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

// The Explorer window that belongs to this UI thread.
HWND FindTopLevel() {
    DWORD thread = GetCurrentThreadId();
    HWND foreground = GetForegroundWindow();
    if (foreground && ClassNameOf(foreground) == L"CabinetWClass" &&
        GetWindowThreadProcessId(foreground, nullptr) == thread) {
        return foreground;
    }
    FindContext context;
    EnumThreadWindows(thread, FindCabinetOnThread, (LPARAM)&context);
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

DWORD ReadExplorerDword(const wchar_t* subKey, const wchar_t* value,
                        DWORD fallback) {
    DWORD data = 0, size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, value, RRF_RT_REG_DWORD,
                     nullptr, &data, &size) == ERROR_SUCCESS) {
        return data;
    }
    return fallback;
}

// Explorer's own "Compact view" switch.
bool IsCompactMode() {
    return ReadExplorerDword(
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
               L"UseCompactMode", 0) != 0;
}

// The colour of the empty part of Explorer's own file list, read from the
// native view that sits under the panel. Explorer's colours can be changed by
// other mods, so the panel takes whatever the list really shows. CLR_INVALID if
// it can't be read.
COLORREF SampleListBackground(HWND fileList) {
    RECT client;
    if (!fileList || !GetClientRect(fileList, &client)) {
        return CLR_INVALID;
    }
    int width = client.right, height = client.bottom;
    if (width < 80 || height < 80 || width > 8000 || height > 8000) {
        return CLR_INVALID;
    }
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ old = SelectObject(memory, bitmap);
    COLORREF result = CLR_INVALID;
    if (PrintWindow(fileList, memory, 0x2 /* PW_RENDERFULLCONTENT */)) {
        // A row of points along the bottom edge, where a list is usually
        // empty. The colour seen most often wins.
        COLORREF seen[7];
        int count[7] = {};
        int kinds = 0;
        for (int i = 0; i < 7; i++) {
            COLORREF c = GetPixel(memory, width * (10 + i * 13) / 100, height - 4);
            if (c == CLR_INVALID) {
                continue;
            }
            int at = 0;
            while (at < kinds && seen[at] != c) {
                at++;
            }
            if (at == kinds) {
                seen[kinds++] = c;
            }
            count[at]++;
        }
        int best = -1;
        for (int i = 0; i < kinds; i++) {
            if (best < 0 || count[i] > count[best]) {
                best = i;
            }
        }
        if (best >= 0 && seen[best] != RGB(0, 0, 0)) {
            result = seen[best];
        }
    }
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return result;
}

bool IsDarkMode() {
    return ReadExplorerDword(
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", 1) == 0;
}

// ---------------------------------------------------------------------------
// Shell data. Everything here runs on the Explorer window's UI thread.
// ---------------------------------------------------------------------------

struct PidlFree {
    void operator()(ITEMIDLIST* pidl) const {
        if (pidl) {
            CoTaskMemFree(pidl);
        }
    }
};
using Pidl = std::unique_ptr<ITEMIDLIST, PidlFree>;

struct Entry {
    Pidl child;
    std::wstring name;
    bool folder = false;
    // Neither a folder nor a file: a Control Panel applet or the Desktop's
    // Control Panel entry. It can't be opened as a column, only run.
    bool launcher = false;
    // A zip or the like, browsed as a folder when the setting is on. It is a
    // folder to the columns (entry.folder is true) and a file to everything else.
    bool archive = false;
    // Where the item really is: 0 here, 1 a cloud file not on this disk,
    // 2 on a network location, 3 on a device such as a phone, 4 a cloud file
    // kept on this device, 5 a cloud file that is downloaded.
    int remote = 0;
    // A shortcut to a folder: it opens as that folder, in the next column.
    Pidl linkTarget;
};

