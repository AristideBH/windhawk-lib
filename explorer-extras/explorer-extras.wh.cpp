// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Optional additions to File Explorer. First one: a Columns layout in the View menu, Miller columns drawn with Explorer's own WinUI 3 controls.
// @version         0.4.12
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid -ladvapi32 -lgdi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Explorer Extras

Optional additions to File Explorer. Each one has its own settings section
and can be switched off, in which case it leaves no trace.

## Columns

Adds a **Columns** entry to File Explorer's View menu, next to Details and
List. It shows the folder as Miller columns: click a folder and its content
opens in the next column.

The columns are WinUI 3 controls hosted in the Explorer window, so they use
Explorer's own styles. The mod needs no symbol file from Microsoft for
System32 modules, so it keeps working right after a Windows update.

In 0.2:

- Sort and group come from the folder where you turned Columns on, and apply
  to every column. Pick something in the Sort menu and every column follows.
  The mod never changes a folder's saved view.
- Group headers use Explorer's own names.
- Selecting a file selects it in Explorer's own view too, so the preview
  pane, the details pane, the status bar and the command bar act on it.
- Columns fit their longest name, between a minimum and a maximum width. Long
  names are cut in the middle. Drag a column's right edge to resize it,
  double-click the edge to refit.
- Folders load on a background thread.
- Hidden files and compact view follow Explorer's own settings.

In 0.3 the Columns entry is in the View menu as soon as a window opens, with
no click on the command bar needed. When Columns was the last layout you
chose, new windows open in it, also after Explorer restarts. Turn this off
with "Open new windows in Columns".

Also in 0.3: real file icons and thumbnails, read on a background thread (a
glyph shows until each icon arrives), and a right-click menu that shows the
shell's own commands for the item as a flyout.

Also in 0.3: select several items with Ctrl and Shift, and type a few
letters to jump to a name.

Not yet: drag and drop.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- columns:
  - enabled: true
    $name: Enabled
    $description: Adds the Columns entry to the View menu. Off removes it.
  - restoreOnOpen: true
    $name: Open new windows in Columns
    $description: When the last layout you chose was Columns, a new Explorer window opens in Columns too. Choosing any other layout in the View menu turns this off.
  - compactScrollbars: false
    $name: Compact scrollbars
    $description: Keeps the scrollbars of the columns thin, also when the pointer is over them. They can still be dragged and clicked.
  - focusRect: true
    $name: Show the focus outline on the active item
    $description: The outline around the item under the keyboard cursor while you navigate with the arrow keys. Off hides it, the selection highlight stays.
  - background: explorer
    $name: Panel background
    $description: Explorer takes the colour of Explorer's own file list. Theme uses the standard dark or light colour. Transparent shows what is under the panel, for testing only, since Explorer's own list is under it.
    $options:
    - explorer: Explorer's file list colour
    - theme: Theme colour
    - transparent: Transparent (test)
  - sidebarRoot: false
    $name: Sidebar sets the first column
    $description: A click on Home, Gallery, OneDrive or a pinned folder in the navigation pane makes that folder the first column. The expandable tree (This PC, drives, Network) keeps following the columns as before.
  - minWidth: 160
    $name: Minimum column width
  - maxWidth: 420
    $name: Maximum column width
    $description: Columns fit their longest name up to this width. Longer names are cut in the middle.
  - followActiveColumn: true
    $name: Explorer follows the active column
    $description: Explorer's own file list navigates to the folder of the column you are in, so the address bar, Back, the preview pane and the status bar stay in step.
  - maxItems: 1500
    $name: Maximum items per column
  $name: Columns
- debugLog: true
  $name: Write a debug log
  $description: Writes to Code\windhawk-lib\probes\logs\explorer-extras.log in your user folder, if that folder exists.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <oleauto.h>

#undef GetCurrentTime

#include <propkey.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cwchar>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef MOD_NO_WINRT
#include <windhawk_utils.h>

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.h>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
namespace mud = winrt::Microsoft::UI::Dispatching;
#endif

namespace {

std::atomic<bool> g_unloading{false};
std::atomic<bool> g_debugLog{true};
std::atomic<bool> g_follow{true};
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_restore{true};
std::atomic<bool> g_sidebarRoot{false};
std::atomic<bool> g_focusRect{true};
std::atomic<bool> g_compactScroll{false};
std::atomic<int> g_backgroundMode{0};  // 0 Explorer, 1 theme, 2 transparent
std::atomic<int> g_styleVersion{0};
std::atomic<int> g_minWidth{160};
std::atomic<int> g_maxWidth{420};
std::atomic<int> g_maxItems{1500};
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
};

// ---- Who navigates Explorer's view? ----
//
// Explorer's view jumped to another folder after a paste, in a way this mod
// didn't ask for. To find out who asks, IShellBrowser::BrowseObject is hooked
// once, and every call is logged with the modules on its call stack. A Windhawk
// mod shows up there by its file name. Only with the debug log on.

thread_local bool g_ourBrowse = false;  // True while this mod itself navigates.

// True while the rename box of a column has the keyboard. Explorer's accelerator
// table would take Backspace, Delete and Ctrl+C or V for the files, so the keys
// are let through to the box.
thread_local bool g_renaming = false;

using TranslateAcceleratorW_t = int(WINAPI*)(HWND, HACCEL, LPMSG);
TranslateAcceleratorW_t TranslateAcceleratorW_Original;
int WINAPI TranslateAcceleratorW_Hook(HWND hwnd, HACCEL accel, LPMSG msg) {
    if (g_renaming && msg &&
        (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP ||
         msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP)) {
        return 0;  // Not translated: the message goes on to the focused control.
    }
    return TranslateAcceleratorW_Original(hwnd, accel, msg);
}

// A navigation made by the navigation pane (sidebar), seen in the BrowseObject
// hook: its target, kept until the tick sees Explorer arrive there. Per
// thread, like the window.
thread_local ITEMIDLIST* g_panePidl = nullptr;
thread_local ULONGLONG g_paneAt = 0;

// Is the pointer over the navigation pane of this thread's Explorer window,
// and was there input a moment ago? The pane is the area left of the file list.
bool PointerOverNavigationPane() {
    LASTINPUTINFO input = {sizeof(input)};
    if (!GetLastInputInfo(&input) || GetTickCount() - input.dwTime > 800) {
        return false;
    }
    POINT cursor;
    if (!GetCursorPos(&cursor)) {
        return false;
    }
    HWND over = WindowFromPoint(cursor);
    HWND top = over ? GetAncestor(over, GA_ROOT) : nullptr;
    if (!top || GetWindowThreadProcessId(top, nullptr) != GetCurrentThreadId()) {
        return false;
    }
    HWND fileList = FindVisibleChild(top, L"SHELLDLL_DefView");
    RECT list;
    if (!fileList || !FileListRect(top, fileList, &list)) {
        return false;
    }
    ScreenToClient(top, &cursor);
    RECT client;
    GetClientRect(top, &client);
    return cursor.x >= 0 && cursor.x < list.left && cursor.y >= list.top &&
           cursor.y < client.bottom;
}

using BrowseObject_t = HRESULT(STDMETHODCALLTYPE*)(IShellBrowser*,
                                                   PCUIDLIST_RELATIVE, UINT);
BrowseObject_t BrowseObject_Original;

std::wstring ModuleAt(void* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)address, &module) ||
        !module) {
        return L"?";
    }
    wchar_t path[MAX_PATH] = L"";
    GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    wchar_t text[MAX_PATH + 24];
    swprintf(text, ARRAYSIZE(text), L"%s+0x%llX", name ? name + 1 : path,
             (unsigned long long)((char*)address - (char*)module));
    return text;
}

HRESULT STDMETHODCALLTYPE BrowseObject_Hook(IShellBrowser* self,
                                            PCUIDLIST_RELATIVE pidl,
                                            UINT flags) {
    if (g_debugLog) {
        // SBSP_ABSOLUTE is 0: a pidl is absolute unless one of these is set.
        const UINT notAbsolute = SBSP_RELATIVE | SBSP_PARENT |
                                 SBSP_NAVIGATEBACK | SBSP_NAVIGATEFORWARD;
        std::wstring target = L"(relative, parent, back or forward)";
        if (!(flags & notAbsolute) && pidl) {
            PWSTR name = nullptr;
            if (SUCCEEDED(SHGetNameFromIDList((PCIDLIST_ABSOLUTE)pidl,
                                              SIGDN_DESKTOPABSOLUTEPARSING,
                                              &name)) &&
                name) {
                target = name;
                CoTaskMemFree(name);
            }
        }
        void* frames[14] = {};
        USHORT count = CaptureStackBackTrace(1, ARRAYSIZE(frames), frames, nullptr);
        std::wstring stack;
        for (USHORT i = 0; i < count; i++) {
            if (i) {
                stack += L" < ";
            }
            stack += ModuleAt(frames[i]);
        }
        Out(L"BrowseObject %s flags=0x%X to \"%s\" from: %s",
            g_ourBrowse ? L"(this mod)" : L"(not this mod)", flags,
            target.c_str(), stack.c_str());
    }
    // A click in the navigation pane: remember where it goes.
    if (g_sidebarRoot && !g_ourBrowse && pidl &&
        (flags & SBSP_ACTIVATE_NOFOCUS) &&
        !(flags & (SBSP_RELATIVE | SBSP_PARENT | SBSP_NAVIGATEBACK |
                   SBSP_NAVIGATEFORWARD)) &&
        PointerOverNavigationPane()) {
        if (g_panePidl) {
            CoTaskMemFree(g_panePidl);
        }
        g_panePidl = ILClone((PCIDLIST_ABSOLUTE)pidl);
        g_paneAt = GetTickCount64();
    }
    return BrowseObject_Original(self, pidl, flags);
}

// Hooks BrowseObject through the vtable of a live shell browser, once. Every
// tab's browser is the same class and shares that vtable.
void InstallBrowseTrace(IShellBrowser* browser) {
    static std::atomic<bool> tried{false};
    if ((!g_debugLog && !g_sidebarRoot) || !browser || tried.exchange(true)) {
        return;
    }
    void** vtable = *(void***)browser;
    // IUnknown 0-2, IOleWindow 3-4, then InsertMenusSB, SetMenuSB,
    // RemoveMenusSB, SetStatusTextSB, EnableModelessSB, TranslateAcceleratorSB,
    // and BrowseObject at slot 11.
    void* target = vtable[11];
    BOOL ok = Wh_SetFunctionHook(target, (void*)BrowseObject_Hook,
                                 (void**)&BrowseObject_Original);
    if (ok) {
        Wh_ApplyHookOperations();
    }
    Out(L"BrowseObject trace installed: %d, target=%s", ok ? 1 : 0,
        ModuleAt(target).c_str());
}

// Asks the visible tab for its shell browser. The pointer is not AddRef'd and
// belongs to this thread.
IShellBrowser* BrowserOf(HWND topLevel) {
    HWND tab = FindVisibleChild(topLevel, L"ShellTabWindowClass");
    if (!tab || GetWindowThreadProcessId(tab, nullptr) != GetCurrentThreadId()) {
        return nullptr;
    }
    DWORD_PTR result = 0;
    // WM_USER + 7 is the long-standing "get IShellBrowser" message.
    if (!SendMessageTimeoutW(tab, WM_USER + 7, 0, 0,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result)) {
        return nullptr;
    }
    InstallBrowseTrace((IShellBrowser*)result);
    return (IShellBrowser*)result;
}

IFolderView* FolderViewOf(IShellBrowser* browser) {
    IShellView* view = nullptr;
    if (FAILED(browser->QueryActiveShellView(&view)) || !view) {
        return nullptr;
    }
    IFolderView* folderView = nullptr;
    view->QueryInterface(IID_PPV_ARGS(&folderView));
    view->Release();
    return folderView;
}

// The folder Explorer's own file list shows right now.
Pidl CurrentFolder(IShellBrowser* browser) {
    Pidl result;
    IFolderView* folderView = FolderViewOf(browser);
    if (!folderView) {
        return result;
    }
    IPersistFolder2* persist = nullptr;
    if (SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&persist))) && persist) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(persist->GetCurFolder(&pidl))) {
            result.reset(pidl);
        }
        persist->Release();
    }
    folderView->Release();
    return result;
}

bool IsFolderItem(IShellFolder* folder, PCUITEMID_CHILD child) {
    SFGAOF attributes = SFGAO_FOLDER | SFGAO_STREAM;
    if (FAILED(folder->GetAttributesOf(1, &child, &attributes))) {
        return false;
    }
    // Zip files and the like report both. Treat them as files.
    return (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
}

// The parsing name of a child item, such as "::{GUID}" for a virtual folder.
std::wstring ParsingName(IShellFolder* folder, PCUITEMID_CHILD child) {
    STRRET text = {};
    wchar_t buffer[MAX_PATH] = L"";
    if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_FORPARSING, &text)) &&
        SUCCEEDED(StrRetToBufW(&text, child, buffer, MAX_PATH))) {
        return buffer;
    }
    return L"";
}

// Control Panel, or something inside it. Explorer's own view can't follow a
// column into it: it closes the window and opens a Control Panel window in
// its place (seen in the 0.3.14 test).
bool IsInControlPanel(PCIDLIST_ABSOLUTE pidl) {
    PWSTR name = nullptr;
    if (FAILED(SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &name)) ||
        !name) {
        return false;
    }
    bool inside = StrStrIW(name, L"{26EE0668-A00A-44D7-9371-BEB064C98683}") ||
                  StrStrIW(name, L"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}");
    CoTaskMemFree(name);
    return inside;
}

bool IsLauncherItem(IShellFolder* folder, PCUITEMID_CHILD child) {
    SFGAOF attributes = SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM;
    if (FAILED(folder->GetAttributesOf(1, &child, &attributes))) {
        return false;
    }
    return !(attributes & (SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM));
}

std::wstring DisplayName(IShellFolder* folder, PCUITEMID_CHILD child) {
    STRRET text = {};
    wchar_t buffer[MAX_PATH] = L"";
    if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_NORMAL, &text)) &&
        SUCCEEDED(StrRetToBufW(&text, child, buffer, MAX_PATH))) {
        return buffer;
    }
    return L"";
}

// How the columns are sorted and grouped. Read from Explorer's own view, never
// written back to it.
struct SortSpec {
    std::vector<SORTCOLUMN> columns;
    bool grouped = false;
    PROPERTYKEY groupKey{};
    bool groupAscending = true;
};

bool SameSpec(const SortSpec& a, const SortSpec& b) {
    if (a.grouped != b.grouped || a.columns.size() != b.columns.size()) {
        return false;
    }
    if (a.grouped && (!IsEqualPropertyKey(a.groupKey, b.groupKey) ||
                      a.groupAscending != b.groupAscending)) {
        return false;
    }
    for (size_t i = 0; i < a.columns.size(); i++) {
        if (!IsEqualPropertyKey(a.columns[i].propkey, b.columns[i].propkey) ||
            a.columns[i].direction != b.columns[i].direction) {
            return false;
        }
    }
    return true;
}

bool ReadSortSpec(IShellBrowser* browser, SortSpec* spec) {
    IFolderView* folderView = FolderViewOf(browser);
    if (!folderView) {
        return false;
    }
    IFolderView2* view2 = nullptr;
    folderView->QueryInterface(IID_PPV_ARGS(&view2));
    folderView->Release();
    if (!view2) {
        return false;
    }
    spec->columns.clear();
    int count = 0;
    if (SUCCEEDED(view2->GetSortColumnCount(&count)) && count > 0) {
        if (count > 8) {
            count = 8;
        }
        spec->columns.resize(count);
        if (FAILED(view2->GetSortColumns(spec->columns.data(), count))) {
            spec->columns.clear();
        }
    }
    PROPERTYKEY key{};
    BOOL ascending = TRUE;
    spec->grouped = false;
    if (SUCCEEDED(view2->GetGroupBy(&key, &ascending)) &&
        !IsEqualGUID(key.fmtid, GUID_NULL)) {
        spec->grouped = true;
        spec->groupKey = key;
        spec->groupAscending = ascending != FALSE;
    }
    view2->Release();
    return true;
}

bool ShowHiddenFiles() {
    return ReadExplorerDword(
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
               L"Hidden", 2) == 1;
}

// One loaded folder: its items in display order, and the group each belongs
// to when grouping is on.
struct FolderResult {
    std::vector<Entry> entries;
    std::vector<int> groupOf;              // Index into groupNames, per entry.
    std::vector<std::wstring> groupNames;  // In display order.
    bool truncated = false;
    bool ok = false;
};

// The shell's column index for a property, or -1.
int ColumnForKey(IShellFolder2* folder, const PROPERTYKEY& key) {
    // Some folders leave gaps in their column numbers, so a failure doesn't
    // end the search. Thirty failures in a row do.
    int misses = 0;
    for (UINT i = 0; i < 600 && misses < 30; i++) {
        SHCOLUMNID id{};
        if (FAILED(folder->MapColumnToSCID(i, &id))) {
            misses++;
            continue;
        }
        misses = 0;
        if (IsEqualPropertyKey(id, key)) {
            return (int)i;
        }
    }
    return -1;
}

// Rebuilds `result` so its entries are in group order. `ids` gives a group
// id per entry, `order` the ids in display order, `names` a title per id in
// `order`.
void ApplyGroups(FolderResult* result, const std::vector<int>& ids,
                 const std::vector<int>& order,
                 const std::vector<std::wstring>& names) {
    size_t count = result->entries.size();
    std::vector<Entry> regrouped;
    regrouped.reserve(count);
    result->groupOf.clear();
    result->groupNames.clear();
    for (size_t g = 0; g < order.size(); g++) {
        result->groupNames.push_back(names[g]);
        for (size_t i = 0; i < count; i++) {
            if (ids[i] == order[g]) {
                regrouped.push_back(std::move(result->entries[i]));
                result->groupOf.push_back((int)g);
            }
        }
    }
    result->entries = std::move(regrouped);
}

LONGLONG LocalDayNumber(const SYSTEMTIME& local) {
    FILETIME ft{};
    SystemTimeToFileTime(&local, &ft);
    ULARGE_INTEGER value;
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (LONGLONG)(value.QuadPart / 864000000000ULL);
}

// A date bucket like Explorer's, 0 for the most recent. English for now.
int DateBucket(const SYSTEMTIME& itemLocal, const SYSTEMTIME& nowLocal) {
    LONGLONG age = LocalDayNumber(nowLocal) - LocalDayNumber(itemLocal);
    int sinceMonday = (nowLocal.wDayOfWeek + 6) % 7;
    if (age <= 0) return 0;
    if (age == 1) return 1;
    if (age <= sinceMonday) return 2;
    if (age <= sinceMonday + 7) return 3;
    if (itemLocal.wYear == nowLocal.wYear &&
        itemLocal.wMonth == nowLocal.wMonth) return 4;
    int monthsApart = (nowLocal.wYear - itemLocal.wYear) * 12 +
                      (nowLocal.wMonth - itemLocal.wMonth);
    if (monthsApart == 1) return 5;
    if (itemLocal.wYear == nowLocal.wYear) return 6;
    return 7;
}

const wchar_t* const kDateBuckets[] = {
    L"Today",        L"Yesterday",  L"Earlier this week",
    L"Last week",    L"Earlier this month", L"Last month",
    L"Earlier this year", L"A long time ago", L"Unspecified",
};

const wchar_t* const kSizeBuckets[] = {
    L"Unspecified", L"Empty (0 KB)",   L"Tiny (0 - 16 KB)",
    L"Small (16 KB - 1 MB)", L"Medium (1 - 128 MB)",
    L"Large (128 MB - 1 GB)", L"Huge (1 - 4 GB)", L"Gigantic (> 4 GB)",
};

int SizeBucket(ULONGLONG size) {
    if (size == 0) return 1;
    if (size < 16ULL * 1024) return 2;
    if (size < 1024ULL * 1024) return 3;
    if (size < 128ULL * 1024 * 1024) return 4;
    if (size < 1024ULL * 1024 * 1024) return 5;
    if (size < 4ULL * 1024 * 1024 * 1024) return 6;
    return 7;
}

// Groups by the property's own value when the shell offers no categorizer.
// Dates and sizes get ranges, names their first letter, everything else the
// text Explorer shows in that column.
void GroupFallback(IShellFolder* folder, const SortSpec& spec,
                   FolderResult* result) {
    IShellFolder2* folder2 = nullptr;
    folder->QueryInterface(IID_PPV_ARGS(&folder2));
    if (!folder2) {
        Out(L"group fallback: no IShellFolder2");
        return;
    }
    size_t count = result->entries.size();
    const PROPERTYKEY& key = spec.groupKey;
    bool isDate = IsEqualPropertyKey(key, PKEY_DateModified) ||
                  IsEqualPropertyKey(key, PKEY_DateCreated) ||
                  IsEqualPropertyKey(key, PKEY_DateAccessed) ||
                  IsEqualPropertyKey(key, PKEY_ItemDate);
    bool isSize = IsEqualPropertyKey(key, PKEY_Size);
    bool isName = IsEqualPropertyKey(key, PKEY_ItemNameDisplay);
    int column = ColumnForKey(folder2, key);

    std::vector<int> ids(count, 0);
    std::vector<std::wstring> texts;  // Group titles, indexed by id.
    SYSTEMTIME nowLocal{};
    GetLocalTime(&nowLocal);
    int firstType = -1;

    // Read each item's value for the property. Its type decides the grouping:
    // a date gets ranges, a size gets ranges, text groups by itself.
    std::vector<VARIANT> values(count);
    for (size_t i = 0; i < count; i++) {
        VariantInit(&values[i]);
        HRESULT hr = folder2->GetDetailsEx(result->entries[i].child.get(), &key,
                                           &values[i]);
        if (FAILED(hr)) {
            VariantInit(&values[i]);
        }
        if (firstType < 0 && values[i].vt != VT_EMPTY) {
            firstType = values[i].vt;
            if (values[i].vt == VT_DATE) {
                isDate = true;
            }
        }
    }
    if (isDate) {
        texts.assign(std::begin(kDateBuckets), std::end(kDateBuckets));
    } else if (isSize) {
        texts.assign(std::begin(kSizeBuckets), std::end(kSizeBuckets));
    }
    for (size_t i = 0; i < count; i++) {
        VARIANT& value = values[i];
        if (isDate) {
            ids[i] = 8;
            SYSTEMTIME utc{}, local{};
            if (value.vt == VT_DATE &&
                VariantTimeToSystemTime(value.date, &utc) &&
                SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
                ids[i] = DateBucket(local, nowLocal);
            }
            continue;
        }
        if (isSize) {
            ids[i] = 0;
            if (!result->entries[i].folder) {
                if (value.vt == VT_UI8) {
                    ids[i] = SizeBucket(value.ullVal);
                } else if (value.vt == VT_I8) {
                    ids[i] = SizeBucket((ULONGLONG)value.llVal);
                } else if (value.vt == VT_UI4) {
                    ids[i] = SizeBucket(value.ulVal);
                }
            }
            continue;
        }
        std::wstring text;
        if (isName) {
            const std::wstring& name = result->entries[i].name;
            if (!name.empty()) {
                wchar_t first = name[0];
                CharUpperBuffW(&first, 1);
                text = (first >= L'0' && first <= L'9') ? L"0 - 9"
                                                         : std::wstring(1, first);
            }
        } else if (value.vt == VT_BSTR && value.bstrVal) {
            text = value.bstrVal;
        } else if (value.vt != VT_EMPTY && value.vt != VT_NULL) {
            // Numbers and the like: let OLE turn them into text.
            VARIANT asText;
            VariantInit(&asText);
            if (SUCCEEDED(VariantChangeType(&asText, &value, 0, VT_BSTR)) &&
                asText.bstrVal) {
                text = asText.bstrVal;
            }
            VariantClear(&asText);
        }
        if (text.empty() && column >= 0) {
            SHELLDETAILS details{};
            wchar_t buffer[MAX_PATH] = L"";
            PCUITEMID_CHILD child = result->entries[i].child.get();
            if (SUCCEEDED(folder2->GetDetailsOf(child, (UINT)column,
                                                &details)) &&
                SUCCEEDED(StrRetToBufW(&details.str, child, buffer,
                                       MAX_PATH))) {
                text = buffer;
            }
        }
        if (text.empty()) {
            text = L"Unspecified";
        }
        auto found = std::find(texts.begin(), texts.end(), text);
        if (found == texts.end()) {
            texts.push_back(text);
            ids[i] = (int)texts.size() - 1;
        } else {
            ids[i] = (int)(found - texts.begin());
        }
    }
    for (auto& value : values) {
        VariantClear(&value);
    }
    Out(L"group fallback: key pid=%lu, first value type=%d", key.pid, firstType);
    folder2->Release();

    // The groups that occur, in display order.
    std::vector<int> order;
    for (int id : ids) {
        if (std::find(order.begin(), order.end(), id) == order.end()) {
            order.push_back(id);
        }
    }
    bool ascending = spec.groupAscending;
    if (isDate) {
        // Bucket 0 is the most recent, so ascending dates run from 7 to 0.
        std::sort(order.begin(), order.end(), [ascending](int a, int b) {
            return ascending ? a > b : a < b;
        });
    } else if (isSize) {
        std::sort(order.begin(), order.end(), [ascending](int a, int b) {
            return ascending ? a < b : a > b;
        });
    } else {
        std::sort(order.begin(), order.end(),
                  [ascending, &texts](int a, int b) {
                      int cmp = StrCmpLogicalW(texts[a].c_str(),
                                               texts[b].c_str());
                      return ascending ? cmp < 0 : cmp > 0;
                  });
    }
    std::vector<std::wstring> names;
    for (int id : order) {
        names.push_back(id >= 0 && id < (int)texts.size() ? texts[id] : L"");
    }
    if (names.size() == 1 && names[0] == L"Unspecified") {
        Out(L"group fallback: the key gives no values here, not grouping");
        return;
    }
    ApplyGroups(result, ids, order, names);
    Out(L"group fallback: %d groups, date=%d size=%d name=%d column=%d",
        (int)order.size(), isDate ? 1 : 0, isSize ? 1 : 0, isName ? 1 : 0,
        column);
}

// Splits the sorted entries into groups for `spec.groupKey`: the shell's own
// categorizer when the folder offers one, the fallback above otherwise.
void GroupEntries(IShellFolder* folder, const SortSpec& spec,
                  FolderResult* result) {
    size_t count = result->entries.size();
    if (!spec.grouped || count == 0) {
        return;
    }
    ICategorizer* categorizer = nullptr;
    ICategoryProvider* provider = nullptr;
    HRESULT hr = folder->CreateViewObject(nullptr, IID_PPV_ARGS(&provider));
    if (SUCCEEDED(hr) && provider) {
        GUID category{};
        HRESULT can = provider->CanCategorizeOnSCID(&spec.groupKey);
        HRESULT forScid = provider->GetCategoryForSCID(&spec.groupKey, &category);
        HRESULT create = E_FAIL;
        if (SUCCEEDED(forScid)) {
            create = provider->CreateCategory(&category,
                                              IID_PPV_ARGS(&categorizer));
        }
        static std::atomic<int> logged{0};
        if (logged++ < 6) {
            Out(L"group: key pid=%lu, CanCategorizeOnSCID=0x%08lX GetCategoryForSCID=0x%08lX CreateCategory=0x%08lX",
                spec.groupKey.pid, can, forScid, create);
        }
        provider->Release();
    } else {
        Out(L"group: no category provider, hr=0x%08lX", hr);
    }
    if (!categorizer) {
        GroupFallback(folder, spec, result);
        return;
    }

    std::vector<PCUITEMID_CHILD> children(count);
    for (size_t i = 0; i < count; i++) {
        children[i] = result->entries[i].child.get();
    }
    std::vector<DWORD> raw(count, 0);
    hr = categorizer->GetCategory((UINT)count, children.data(), raw.data());
    if (FAILED(hr)) {
        Out(L"group: GetCategory failed, hr=0x%08lX", hr);
        categorizer->Release();
        GroupFallback(folder, spec, result);
        return;
    }
    std::vector<DWORD> distinct;
    for (DWORD id : raw) {
        if (std::find(distinct.begin(), distinct.end(), id) == distinct.end()) {
            distinct.push_back(id);
        }
    }
    bool ascending = spec.groupAscending;
    std::stable_sort(distinct.begin(), distinct.end(),
                     [categorizer, ascending](DWORD a, DWORD b) {
                         HRESULT cmp = categorizer->CompareCategory(
                             CATSORT_DEFAULT, a, b);
                         short value = (short)HRESULT_CODE(cmp);
                         return ascending ? value < 0 : value > 0;
                     });
    std::vector<int> ids(count), order;
    std::vector<std::wstring> names;
    for (size_t g = 0; g < distinct.size(); g++) {
        CATEGORY_INFO info{};
        names.push_back(SUCCEEDED(categorizer->GetCategoryInfo(distinct[g],
                                                               &info))
                            ? info.wszName
                            : L"");
        order.push_back((int)g);
        for (size_t i = 0; i < count; i++) {
            if (raw[i] == distinct[g]) {
                ids[i] = (int)g;
            }
        }
    }
    categorizer->Release();
    ApplyGroups(result, ids, order, names);
}

// Reads a folder, sorted and grouped as `spec` says. Runs on the worker
// thread, with shell objects of its own.
void LoadFolder(PCIDLIST_ABSOLUTE folderPidl, const SortSpec& spec,
                bool includeHidden, int maxItems, FolderResult* result) {
    IShellFolder* folder = nullptr;
    HRESULT hr;
    if (ILIsEmpty(folderPidl)) {
        hr = SHGetDesktopFolder(&folder);
    } else {
        hr = SHBindToObject(nullptr, folderPidl, nullptr,
                            IID_PPV_ARGS(&folder));
    }
    if (FAILED(hr) || !folder) {
        Out(L"load: can't bind, hr=0x%08lX", hr);
        return;
    }
    SHCONTF flags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
    if (includeHidden) {
        flags |= SHCONTF_INCLUDEHIDDEN;
    }
    IEnumIDList* list = nullptr;
    hr = folder->EnumObjects(nullptr, flags, &list);
    if (hr == S_OK && list) {
        PITEMID_CHILD child = nullptr;
        while (list->Next(1, &child, nullptr) == S_OK && child) {
            if ((int)result->entries.size() >= maxItems) {
                result->truncated = true;
                CoTaskMemFree(child);
                break;
            }
            // The Desktop lists Control Panel twice: as a folder and as an
            // item that only opens a window. Keep the folder.
            if (ILIsEmpty(folderPidl) &&
                StrStrIW(ParsingName(folder, child).c_str(),
                         L"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}")) {
                CoTaskMemFree(child);
                child = nullptr;
                continue;
            }
            Entry entry;
            entry.name = DisplayName(folder, child);
            entry.folder = IsFolderItem(folder, child);
            entry.launcher = !entry.folder && IsLauncherItem(folder, child);
            entry.child.reset(child);
            result->entries.push_back(std::move(entry));
            child = nullptr;
        }
        list->Release();
    }
    result->ok = SUCCEEDED(hr);

    // Sort with the shell's own comparison, one sort column after the other.
    struct Key {
        int column;
        int direction;
    };
    std::vector<Key> keys;
    IShellFolder2* folder2 = nullptr;
    folder->QueryInterface(IID_PPV_ARGS(&folder2));
    if (folder2) {
        for (auto& sortColumn : spec.columns) {
            // With grouping on, Explorer lists the group's property as a sort
            // column too. Inside a group the other columns decide the order.
            if (spec.grouped && spec.columns.size() > 1 &&
                IsEqualPropertyKey(sortColumn.propkey, spec.groupKey)) {
                continue;
            }
            int column = ColumnForKey(folder2, sortColumn.propkey);
            static std::atomic<int> logged{0};
            if (logged++ < 8) {
                Out(L"sort: key pid=%lu maps to column %d, direction %d",
                    sortColumn.propkey.pid, column, (int)sortColumn.direction);
            }
            if (column >= 0) {
                keys.push_back({column, sortColumn.direction < 0 ? -1 : 1});
            }
        }
        folder2->Release();
    }
    if (keys.empty()) {
        keys.push_back({0, 1});
    }
    std::stable_sort(result->entries.begin(), result->entries.end(),
                     [folder, &keys](const Entry& a, const Entry& b) {
                         for (auto& key : keys) {
                             HRESULT cmp = folder->CompareIDs(
                                 key.column, a.child.get(), b.child.get());
                             if (FAILED(cmp)) {
                                 continue;
                             }
                             int value = (short)HRESULT_CODE(cmp) * key.direction;
                             if (value != 0) {
                                 return value < 0;
                             }
                         }
                         return false;
                     });

    GroupEntries(folder, spec, result);
    folder->Release();
}

// Selects one item in Explorer's own view. Fails while the view is still
// filling, so callers retry.
bool SelectInExplorer(IShellBrowser* browser, PCUITEMID_CHILD child) {
    IShellView* view = nullptr;
    if (FAILED(browser->QueryActiveShellView(&view)) || !view) {
        return false;
    }
    HRESULT hr = view->SelectItem(
        child, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_ENSUREVISIBLE |
                   SVSI_NOTAKEFOCUS);
    view->Release();
    return SUCCEEDED(hr);
}

// Adds items to the selection Explorer's own view already has.
void AddToSelectionInExplorer(IShellBrowser* browser,
                              std::vector<Pidl> const& children) {
    IShellView* view = nullptr;
    if (FAILED(browser->QueryActiveShellView(&view)) || !view) {
        return;
    }
    for (auto& child : children) {
        view->SelectItem(child.get(), SVSI_SELECT | SVSI_NOTAKEFOCUS);
    }
    view->Release();
}

// Cuts the middle of a name that is longer than `maxChars`, keeping its
// start, its last characters and its extension.
std::wstring MiddleEllipsis(const std::wstring& name, size_t maxChars) {
    if (name.size() <= maxChars || maxChars < 8) {
        return name;
    }
    size_t dot = name.find_last_of(L'.');
    size_t extension =
        (dot != std::wstring::npos && name.size() - dot <= 8) ? name.size() - dot
                                                              : 0;
    size_t tail = extension + 6;
    if (tail + 3 > maxChars) {
        tail = maxChars / 2;
    }
    size_t headLength = maxChars - tail - 1;
    return name.substr(0, headLength) + L"…" +
           name.substr(name.size() - tail);
}

// ---- Change notifications: a hidden window per Explorer window receives
// the shell's "this folder changed" messages for every open column. ----

const UINT kNotifyMessage = WM_APP + 71;
const wchar_t kNotifyClass[] = L"ExplorerExtrasNotify";
std::atomic<bool> g_notifyClassRegistered{false};

LRESULT CALLBACK NotifyProc(HWND hwnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
    if (message == kNotifyMessage) {
        // The window's user data points at the time the columns should be
        // read again. Several changes in a row push it back.
        PIDLIST_ABSOLUTE* pidls = nullptr;
        LONG event = 0;
        bool ownLog = false;
        HANDLE lock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam,
                                                &pidls, &event);
        if (lock) {
            // The mod's own log file changes each time the mod logs. Reacting
            // to it would reload, log, and reload again without end.
            wchar_t path[MAX_PATH] = L"";
            if (pidls && pidls[0] && SHGetPathFromIDListW(pidls[0], path) &&
                g_outPath[0] && lstrcmpiW(path, g_outPath) == 0) {
                ownLog = true;
            }
            SHChangeNotification_Unlock(lock);
        }
        auto* due = (ULONGLONG*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (due && !ownLog) {
            *due = GetTickCount64() + 200;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateNotifyWindow(ULONGLONG* due) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!g_notifyClassRegistered.exchange(true)) {
        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = NotifyProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kNotifyClass;
        RegisterClassW(&windowClass);
    }
    HWND hwnd = CreateWindowExW(0, kNotifyClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, instance, nullptr);
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)due);
    }
    return hwnd;
}

void UnregisterNotifyClass() {
    if (g_notifyClassRegistered.exchange(false)) {
        UnregisterClassW(kNotifyClass, GetModuleHandleW(nullptr));
    }
}

ULONG WatchFolder(HWND notifyWindow, PCIDLIST_ABSOLUTE folder) {
    if (!notifyWindow) {
        return 0;
    }
    SHChangeNotifyEntry entry = {folder, FALSE};
    return SHChangeNotifyRegister(
        notifyWindow,
        SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
        SHCNE_CREATE | SHCNE_DELETE | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER |
            SHCNE_MKDIR | SHCNE_RMDIR | SHCNE_UPDATEDIR,
        kNotifyMessage, 1, &entry);
}

std::wstring PathOf(PCIDLIST_ABSOLUTE pidl) {
    PWSTR name = nullptr;
    std::wstring result;
    if (pidl && SUCCEEDED(SHGetNameFromIDList(
                    pidl, SIGDN_DESKTOPABSOLUTEEDITING, &name)) &&
        name) {
        result = name;
        CoTaskMemFree(name);
    }
    return result;
}

void OpenItem(HWND owner, PCIDLIST_ABSOLUTE pidl) {
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpIDList = (void*)pidl;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        Out(L"OpenItem failed, gle=%lu", GetLastError());
    }
}

#ifndef MOD_NO_WINRT

// ---------------------------------------------------------------------------
// The panel and the menu entry. One WindowState per Explorer window, used only
// on that window's UI thread.
// ---------------------------------------------------------------------------

using Inspectable = winrt::Windows::Foundation::IInspectable;
using ItemsVector =
    winrt::Windows::Foundation::Collections::IVector<muxc::MenuFlyoutItemBase>;

void LogHresult(const wchar_t* where) {
    HRESULT hr = winrt::to_hresult();
    Out(L"  %s threw hr=0x%08lX", where, hr);
}

struct Column {
    Pidl folder;
    std::vector<Entry> entries;
    std::vector<int> rowToEntry;         // Entry index per row, -1 for a header.
    std::vector<muxc::TextBlock> texts;  // Name text per entry.
    std::vector<muxc::Grid> iconHosts;   // Icon cell per entry.
    std::vector<muxc::FontIcon> chevrons;  // Chevron per entry, folders only.
    int hintEntry = -1;  // The entry whose chevron is shown stronger.
    int hoverHeader = -1;  // The group header under the pointer.
    winrt::event_token moveToken{};
    winrt::event_token pressToken{};
    winrt::event_token releaseToken{};
    winrt::event_token captureLostToken{};
    winrt::event_token exitToken{};
    bool filled = false;  // Has shown a listing at least once.
    int scrollStyle = -1;  // The scrollbar style these rows were given.
    std::vector<char> tipBuilt;  // Per entry: the detail tooltip exists.
    double rowHeight = 32;
    int animGroup = -1;        // A group that is opening or closing,
    bool animCollapse = false; // which way,
    ULONGLONG animDoneAt = 0;  // and when the tick finishes it.
    ULONGLONG selectionChangedAt = 0;  // When the selection last changed.
    // Groups: the group of each row (a header has its own), the group titles,
    // the chevron of each header row, and the titles collapsed by the user.
    std::vector<int> rowGroup;
    std::vector<std::wstring> groupTitles;
    std::vector<muxc::FontIcon> headerIcons;
    std::vector<std::wstring> collapsed;
    Pidl selectedChild;                 // Restored after a reload.
    unsigned generation = 0;
    bool userSized = false;
    double charWidth = 7;
    muxc::StackPanel cell{nullptr};
    muxc::ListView list{nullptr};
    muxc::Border grip{nullptr};
    winrt::event_token selectionToken{};
    winrt::event_token doubleTapToken{};
    winrt::event_token keyToken{};
    winrt::event_token loadedToken{};
    winrt::event_token panelWheelToken{};
    mux::UIElement panel{nullptr};  // The list's row panel, for the wheel.
    winrt::event_token gripPressedToken{};
    winrt::event_token gripMovedToken{};
    winrt::event_token gripReleasedToken{};
    winrt::event_token gripLostToken{};
    winrt::event_token gripDoubleToken{};
    winrt::event_token gripEnteredToken{};
    winrt::event_token gripExitedToken{};
    winrt::event_token rightTapToken{};
    winrt::event_token tapToken{};
    muxc::Border pill{nullptr};
    ULONGLONG hoverSince = 0;
    ULONG notifyId = 0;
};

struct SiblingHook {
    muxc::MenuFlyoutItem item{nullptr};
    winrt::event_token token{};
};

// The shell's context menu for one item, shown as a XAML flyout. A native
// popup would run a modal loop inside a XAML handler, which crashed Explorer
// before, so the shell's items are copied into a MenuFlyout instead.
struct ContextMenuState {
    IContextMenu* menu = nullptr;
    HMENU hmenu = nullptr;
    UINT idFirst = 1;
    HWND owner = nullptr;
    Column* column = nullptr;  // The column and items the menu is for.
    std::vector<size_t> entries;
    muxc::MenuFlyout flyout{nullptr};
    winrt::event_token closedToken{};
    std::vector<std::pair<muxc::MenuFlyoutItem, winrt::event_token>> hooks;
};

// What the user did in the columns that Ctrl+Z can take back. Explorer's own
// undo is out of reach from here, so deletes and renames are kept by the mod.
struct UndoEntry {
    bool deletion = false;
    std::vector<std::wstring> paths;  // Deleted paths, to restore.
    std::wstring renamedFrom;         // A rename: the old path
    std::wstring renamedTo;           // and the new one.
};

// An item being renamed in place.
struct RenameState {
    Column* column = nullptr;
    size_t entry = 0;
    muxc::Grid grid{nullptr};
    muxc::TextBlock text{nullptr};
    muxc::TextBox box{nullptr};
    winrt::event_token keyToken{};
    winrt::event_token lostToken{};
    ULONGLONG startedAt = 0;
    bool done = false;
};

struct WindowState {
    DWORD thread = 0;
    std::unique_ptr<ContextMenuState> context;
    std::unique_ptr<RenameState> rename;
    std::vector<UndoEntry> undo;
    int styleVersion = -1;  // The row style the rows were last given.
    bool shownDark = false;  // The theme the panel background was made for.
    // A click on the name of the item that is already selected starts a
    // rename once the double-click time has passed.
    // After a command of Explorer's own toolbar: read the columns again, and
    // watch for Explorer opening its own rename box, which the panel hides.
    ULONGLONG postActionAt = 0;
    ULONGLONG postActionAt2 = 0;
    ULONGLONG watchEditUntil = 0;
    DWORD clipSeen = 0;
    ULONGLONG slowRenameAt = 0;
    ULONGLONG slowRenameBlockUntil = 0;
    Column* slowRenameColumn = nullptr;
    size_t slowRenameEntry = 0;
    HWND topLevel = nullptr;
    mud::DispatcherQueue queue{nullptr};

    // View menu entry.
    ItemsVector menuItems{nullptr};
    muxc::RadioMenuFlyoutItem menuItem{nullptr};
    winrt::event_token menuClickToken{};
    std::vector<SiblingHook> siblings;

    // Entry at window creation, and restoring the last layout.
    mux::XamlRoot xamlRoot{nullptr};
    mud::DispatcherQueueTimer entryTimer{nullptr};
    winrt::event_token entryToken{};
    bool entryStopped = false;
    int entryTries = 0;
    int restoreSettle = 0;
    HWND armTab = nullptr;  // A new tab to wait for before restoring Columns.
    std::wstring renameName;      // A folder just made, to put in edit mode
    Column* renameColumn = nullptr;  // once its column has loaded.

    // Panel.
    bool on = false;
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token tickToken{};
    muxc::ScrollViewer scroller{nullptr};
    muxc::Grid rootGrid{nullptr};
    Inspectable wheelHandler{nullptr};  // The boxed handler, for removal.
    Inspectable stripWheelHandler{nullptr};
    double wheelTarget = 0;             // Where the wheel is scrolling to.
    ULONGLONG wheelAt = 0;              // Tick of the last wheel event.
    muxc::StackPanel strip{nullptr};
    // A rebuild keeps the old columns on screen until the new first column has
    // loaded, then swaps them in one step, so nothing blanks in between.
    size_t swapOld = 0;
    unsigned swapGeneration = 0;
    ULONGLONG swapDeadline = 0;
    // Selection by a rectangle, drawn on the layer over the strip.
    struct Band {
        bool pending = false;  // Pressed on empty space, not yet moved far.
        bool active = false;   // Dragging.
        bool additive = false; // Ctrl was down: keep what was selected.
        Column* column = nullptr;
        winrt::Windows::Foundation::Point start{};  // List coordinates.
        double startOffset = 0;                     // Scroll offset at the press.
        std::vector<double> top, bottom;            // Row extents, content coordinates.
        std::vector<int> base;                      // Rows selected at the start.
        int first = -1, last = -1;                  // The rows the rectangle covers.
    } band;
    muxc::Border bandBox{nullptr};
    // The pill that marks the active column, on a layer over the strip.
    muxc::Canvas overlay{nullptr};
    muxc::Border pill{nullptr};
    double pillLeft = -1;      // Where the pill is, or is going.
    ULONGLONG pillUntil = 0;   // Until when its animation runs.
    std::vector<std::unique_ptr<Column>> columns;
    RECT lastRect{};
    bool building = false;
    unsigned nextGeneration = 0;
    unsigned ticks = 0;

    // Sort and group, shared by every column.
    SortSpec spec;
    SortSpec specBeforeMenu;
    ULONGLONG specCheckAt = 0;

    // Folder tracking.
    HWND lastFileList = nullptr;
    Pidl lastSeen;   // Explorer's own folder when last read.
    Pidl expected;   // A folder this mod asked Explorer to open.
    Pidl pendingNav; // A folder to open once the user stops moving.
    ULONGLONG navDue = 0;
    Pidl selectFolder;  // A file to select in Explorer's view,
    Pidl selectChild;   // once it shows that folder.
    std::vector<Pidl> selectExtra;  // More items to add to that selection.
    // Cut items, dimmed until the clipboard changes.
    Pidl cutFolder;
    std::wstring cutFolderPath;  // The same, as a path. Pidls built from a path
                                 // are rooted elsewhere and never compare equal.
    std::vector<std::wstring> cutPaths;
    DWORD cutSeq = 0;
    // After a paste Explorer's view may jump to another folder. For a moment
    // that is ignored and the view is sent back to this one.
    Pidl pasteReturn;
    ULONGLONG pasteGuardUntil = 0;
    // The column to keep keyboard focus in, for a moment after a key, since
    // Explorer's own view takes focus when it navigates.
    Column* focusColumn = nullptr;
    ULONGLONG focusUntil = 0;
    std::wstring typed;     // Type to jump: what has been typed,
    ULONGLONG typedAt = 0;  // and when the last key came.
    int selectTries = 0;

    // Change notifications and live settings.
    HWND notifyWindow = nullptr;
    ULONGLONG reloadDue = 0;
    ULONGLONG lastReload = 0;
    bool shownHidden = false;
    bool shownCompact = false;

    // The column Explorer's own view is in.
    int active = -1;

    // Column edge being dragged.
    int dragColumn = -1;
    double dragStartX = 0;
    double dragStartWidth = 0;
};

std::mutex g_statesMutex;
[[clang::no_destroy]] std::vector<std::unique_ptr<WindowState>> g_states;

// The state of the Explorer window that owns the current thread.
WindowState* StateForThisThread(bool create) {
    DWORD thread = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(g_statesMutex);
    for (auto& state : g_states) {
        if (state->thread == thread) {
            return state.get();
        }
    }
    if (!create) {
        return nullptr;
    }
    auto state = std::make_unique<WindowState>();
    state->thread = thread;
    g_states.push_back(std::move(state));
    return g_states.back().get();
}

mux::Media::SolidColorBrush Brush(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return mux::Media::SolidColorBrush(winrt::Windows::UI::Color{a, r, g, b});
}

// The panel's background, as the setting says.
mux::Media::SolidColorBrush PanelBackground(HWND fileList, bool dark) {
    int mode = g_backgroundMode;
    if (mode == 2) {
        Out(L"panel background: transparent (test)");
        return Brush(0, 0, 0, 0);
    }
    COLORREF sampled = mode == 0 ? SampleListBackground(fileList) : CLR_INVALID;
    Out(L"panel background: %s",
        mode == 1            ? L"the theme colour, by setting"
        : sampled == CLR_INVALID ? L"not readable, using the theme colour"
                                 : L"taken from Explorer's file list");
    if (sampled != CLR_INVALID) {
        return Brush(255, GetRValue(sampled), GetGValue(sampled), GetBValue(sampled));
    }
    return dark ? Brush(255, 25, 25, 25) : Brush(255, 255, 255, 255);
}

// ---- Worker thread: reads folders off the UI thread ----

struct Loaded {
    unsigned generation = 0;
    FolderResult data;
};

struct Job {
    unsigned generation = 0;
    Pidl folder;
    SortSpec spec;
    bool hidden = false;
    int maxItems = 1500;
    mud::DispatcherQueue queue{nullptr};
};

std::mutex g_jobsMutex;
[[clang::no_destroy]] std::deque<std::unique_ptr<Job>> g_jobs;
HANDLE g_jobEvent = nullptr;
HANDLE g_worker = nullptr;
std::atomic<bool> g_workerStop{false};

void ApplyLoaded(std::shared_ptr<Loaded> const& loaded);
void CompleteSwap(WindowState* state);

// ---- Icons and thumbnails, read on the worker thread ----

struct IconBits {
    size_t index = 0;  // Index into the column's entries.
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;  // Premultiplied, top-down.
};

struct IconBatch {
    unsigned generation = 0;
    std::vector<IconBits> icons;
};

void ApplyIcons(std::shared_ptr<IconBatch> const& batch);

constexpr size_t kMaxIcons = 400;
constexpr size_t kIconBatch = 16;

// Copies a shell bitmap's pixels. False if it can't be read.
bool ReadBitmapBits(HBITMAP bitmap, IconBits* out) {
    BITMAP info = {};
    if (!GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 ||
        info.bmHeight <= 0 || info.bmWidth > 512 || info.bmHeight > 512) {
        return false;
    }
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = info.bmWidth;
    bmi.bmiHeader.biHeight = -info.bmHeight;  // Top-down.
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    out->width = info.bmWidth;
    out->height = info.bmHeight;
    out->bgra.assign((size_t)info.bmWidth * info.bmHeight * 4, 0);
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, bitmap, 0, info.bmHeight, out->bgra.data(), &bmi,
                          DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != info.bmHeight) {
        return false;
    }
    // A bitmap without an alpha channel comes back with alpha 0 everywhere.
    bool anyAlpha = false;
    for (size_t i = 3; i < out->bgra.size(); i += 4) {
        if (out->bgra[i]) {
            anyAlpha = true;
            break;
        }
    }
    if (!anyAlpha) {
        for (size_t i = 3; i < out->bgra.size(); i += 4) {
            out->bgra[i] = 255;
        }
    }
    return true;
}

// Reads icons, or thumbnails where the shell has one, for the first items of
// a folder and sends them to the window's UI thread in small batches.
void LoadIcons(PCIDLIST_ABSOLUTE folderPidl, std::vector<Pidl>& children,
               unsigned generation, mud::DispatcherQueue const& queue) {
    IShellFolder* folder = nullptr;
    HRESULT hr = ILIsEmpty(folderPidl)
                     ? SHGetDesktopFolder(&folder)
                     : SHBindToObject(nullptr, folderPidl, nullptr,
                                      IID_PPV_ARGS(&folder));
    if (FAILED(hr) || !folder) {
        return;
    }
    auto batch = std::make_shared<IconBatch>();
    batch->generation = generation;
    auto flush = [&]() {
        if (batch->icons.empty()) {
            return;
        }
        auto sent = batch;
        try {
            queue.TryEnqueue([sent]() { ApplyIcons(sent); });
        } catch (...) {
        }
        batch = std::make_shared<IconBatch>();
        batch->generation = generation;
    };
    size_t count = std::min(children.size(), kMaxIcons);
    for (size_t i = 0; i < count && !g_workerStop; i++) {
        IShellItem* item = nullptr;
        if (FAILED(SHCreateItemWithParent(nullptr, folder, children[i].get(),
                                          IID_PPV_ARGS(&item))) ||
            !item) {
            continue;
        }
        IShellItemImageFactory* factory = nullptr;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) &&
            factory) {
            HBITMAP bitmap = nullptr;
            if (SUCCEEDED(factory->GetImage(SIZE{32, 32}, 0, &bitmap)) &&
                bitmap) {
                IconBits bits;
                bits.index = i;
                if (ReadBitmapBits(bitmap, &bits)) {
                    batch->icons.push_back(std::move(bits));
                }
                DeleteObject(bitmap);
            }
            factory->Release();
        }
        item->Release();
        if (batch->icons.size() >= kIconBatch) {
            flush();
        }
    }
    flush();
    folder->Release();
}

DWORD WINAPI WorkerProc(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (!g_workerStop) {
        MsgWaitForMultipleObjects(1, &g_jobEvent, FALSE, INFINITE, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        for (;;) {
            std::unique_ptr<Job> job;
            {
                std::lock_guard<std::mutex> lock(g_jobsMutex);
                if (g_jobs.empty()) {
                    break;
                }
                job = std::move(g_jobs.front());
                g_jobs.pop_front();
            }
            if (g_workerStop) {
                break;
            }
            auto loaded = std::make_shared<Loaded>();
            loaded->generation = job->generation;
            LoadFolder(job->folder.get(), job->spec, job->hidden,
                       job->maxItems, &loaded->data);
            // The list moves to the UI thread, so keep copies of the items
            // for the icon pass.
            std::vector<Pidl> children;
            for (auto& entry : loaded->data.entries) {
                children.emplace_back(ILClone(entry.child.get()));
                if (children.size() >= kMaxIcons) {
                    break;
                }
            }
            try {
                job->queue.TryEnqueue([loaded]() { ApplyLoaded(loaded); });
            } catch (...) {
            }
            try {
                LoadIcons(job->folder.get(), children, job->generation,
                          job->queue);
            } catch (...) {
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_jobsMutex);
        g_jobs.clear();
    }
    CoUninitialize();
    return 0;
}

void StartWorker() {
    g_jobEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_worker = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
}

void StopWorker() {
    if (!g_worker) {
        return;
    }
    g_workerStop = true;
    SetEvent(g_jobEvent);
    WaitForSingleObject(g_worker, 10000);
    CloseHandle(g_worker);
    g_worker = nullptr;
}

void RequestLoad(WindowState* state, Column* column) {
    column->generation = ++state->nextGeneration;
    auto job = std::make_unique<Job>();
    job->generation = column->generation;
    job->folder.reset(ILClone(column->folder.get()));
    job->spec = state->spec;
    job->hidden = ShowHiddenFiles();
    job->maxItems = g_maxItems;
    job->queue = state->queue;
    {
        std::lock_guard<std::mutex> lock(g_jobsMutex);
        g_jobs.push_back(std::move(job));
    }
    SetEvent(g_jobEvent);
}

// ---- Columns ----

int ColumnIndexOfList(WindowState* state, muxc::ListView const& list) {
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (state->columns[i]->list == list) {
            return (int)i;
        }
    }
    return -1;
}

int ColumnIndexOfGrip(WindowState* state, Inspectable const& sender) {
    // Compare as the same type. Two different interfaces of one object don't
    // compare equal.
    auto grip = sender.try_as<muxc::Border>();
    if (!grip) {
        return -1;
    }
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (state->columns[i]->grip == grip) {
            return (int)i;
        }
    }
    return -1;
}

void RemoveColumnsFrom(WindowState* state, size_t index) {
    while (state->columns.size() > index) {
        auto& column = state->columns.back();
        try {
            if (column->list) {
                column->list.SelectionChanged(column->selectionToken);
                column->list.DoubleTapped(column->doubleTapToken);
                column->list.RightTapped(column->rightTapToken);
                column->list.Tapped(column->tapToken);
                column->list.PointerMoved(column->moveToken);
                column->list.PointerExited(column->exitToken);
                column->list.PointerPressed(column->pressToken);
                column->list.PointerReleased(column->releaseToken);
                column->list.PointerCaptureLost(column->captureLostToken);
                column->list.PreviewKeyDown(column->keyToken);
                column->list.Loaded(column->loadedToken);
            }
            if (column->panel) {
                column->panel.PointerWheelChanged(column->panelWheelToken);
            }
            if (column->grip) {
                column->grip.PointerPressed(column->gripPressedToken);
                column->grip.PointerMoved(column->gripMovedToken);
                column->grip.PointerReleased(column->gripReleasedToken);
                column->grip.PointerCaptureLost(column->gripLostToken);
                column->grip.DoubleTapped(column->gripDoubleToken);
                column->grip.PointerEntered(column->gripEnteredToken);
                column->grip.PointerExited(column->gripExitedToken);
            }
            if (column->notifyId) {
                SHChangeNotifyDeregister(column->notifyId);
                column->notifyId = 0;
            }
            uint32_t count = state->strip.Children().Size();
            if (count > 0) {
                state->strip.Children().RemoveAt(count - 1);
            }
        } catch (...) {
            LogHresult(L"removing a column");
        }
        state->columns.pop_back();
    }
    if (state->dragColumn >= (int)state->columns.size()) {
        state->dragColumn = -1;
    }
    if (state->active >= (int)state->columns.size()) {
        state->active = (int)state->columns.size() - 1;
    }
}

// Marks the column Explorer's own view is in with a light tint.
// Moves the pill to the active column. A change of column springs: the pill
// stretches while it travels and overshoots a little, like the indicator of a
// WinUI NavigationView. A column resized or scrolled sideways only repositions
// it, with no animation.
void UpdatePill(WindowState* state, bool animate) {
    // During a rebuild the new first column is hidden and not yet fitted:
    // leave the pill where it is until the swap, which moves it once.
    if (!state->pill || !state->strip || state->swapOld) {
        return;
    }
    try {
        int index = state->active;
        if (index < 0 || index >= (int)state->columns.size() ||
            !state->columns[index]->list) {
            state->pill.Opacity(0);
            return;
        }
        auto list = state->columns[index]->list;
        double left = state->columns[index]->cell.TransformToVisual(state->strip)
                          .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                          .X;
        double target = left + list.Width() / 2 - 8;
        ULONGLONG now = GetTickCount64();
        if (state->pill.Opacity() < 1) {
            state->pill.Opacity(1);
            animate = false;
        }
        if (std::abs(target - state->pillLeft) < 0.5) {
            return;
        }
        if (!animate || state->pillLeft < 0) {
            // While a move is animating, wait: the next tick tries again.
            // Remembering the target now would leave the pill where it was.
            if (now < state->pillUntil) {
                return;
            }
            muxc::Canvas::SetLeft(state->pill, target);
            state->pillLeft = target;
            return;
        }
        namespace anim = mux::Media::Animation;
        double from = state->pillLeft;
        muxc::Canvas::SetLeft(state->pill, target);  // Where it stays.
        state->pillLeft = target;
        state->pillUntil = now + 430;

        // A short glide that overshoots only a little.
        anim::BackEase spring;
        spring.EasingMode(anim::EasingMode::EaseOut);
        spring.Amplitude(0.15);
        anim::DoubleAnimation travel;
        travel.From(from);
        travel.To(target);
        travel.Duration(mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(380)));
        travel.EasingFunction(spring);
        travel.EnableDependentAnimation(true);
        travel.FillBehavior(anim::FillBehavior::Stop);
        anim::Storyboard::SetTarget(travel, state->pill);
        anim::Storyboard::SetTargetProperty(travel, L"(Canvas.Left)");

        // The stretch: wider while it moves, back to 16 at the end.
        double distance = std::abs(target - from);
        anim::CubicEase ease;
        ease.EasingMode(anim::EasingMode::EaseOut);
        anim::DoubleAnimation stretch;
        stretch.From(16.0);
        stretch.To(16.0 + std::min(distance / 8, 10.0));
        stretch.Duration(mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(110)));
        stretch.AutoReverse(true);
        stretch.EasingFunction(ease);
        stretch.EnableDependentAnimation(true);
        stretch.FillBehavior(anim::FillBehavior::Stop);
        anim::Storyboard::SetTarget(stretch, state->pill);
        anim::Storyboard::SetTargetProperty(stretch, L"Width");

        anim::Storyboard board;
        board.Children().Append(travel);
        board.Children().Append(stretch);
        board.Begin();
    } catch (...) {
        LogHresult(L"UpdatePill");
    }
}

// Scrolls the strip, smoothly, so that column `index` is fully in view. If the
// column is wider than the view, its left edge wins: the icon and the start of
// the names must be visible.
void EnsureColumnVisible(WindowState* state, int index) {
    if (!state->scroller || !state->strip || index < 0 ||
        index >= (int)state->columns.size() || !state->columns[index]->cell) {
        return;
    }
    try {
        auto cell = state->columns[index]->cell;
        double left = cell.TransformToVisual(state->strip)
                          .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                          .X;
        double width = cell.ActualWidth();
        double offset = state->scroller.HorizontalOffset();
        double view = state->scroller.ViewportWidth();
        double target = offset;
        if (left + width > offset + view) {
            target = left + width - view;
        }
        if (left < target) {
            target = left;  // Also when the column is wider than the view.
        }
        if (target < 0) {
            target = 0;
        }
        if (target != offset) {
            state->scroller.ChangeView(target, nullptr, nullptr, false);
        }
    } catch (...) {
    }
}

void SetActiveColumn(WindowState* state, int index) {
    state->active = index;
    EnsureColumnVisible(state, index);
    // The active column is shown by the pill, not by its background. The
    // transparent brush keeps the empty space clickable.
    for (size_t i = 0; i < state->columns.size(); i++) {
        try {
            state->columns[i]->list.Background(Brush(0, 0, 0, 0));
        } catch (...) {
        }
    }
    UpdatePill(state, true);
}

double MeasureText(const std::wstring& text) {
    muxc::TextBlock block;
    block.Text(text);
    block.Measure(winrt::Windows::Foundation::Size{10000, 10000});
    return block.DesiredSize().Width;
}

// Space a row uses besides the name: icon, chevron, padding and the list's
// own margins.
const double kRowChrome = 24 + 16 + 18 + 22 + 10;  // The last 10 is the gutter.

// Sets the column's width from its longest names, unless the user sized it.
void FitWidth(Column* column) {
    if (column->userSized) {
        return;
    }
    // Measure only the longest names, by character count.
    std::vector<const std::wstring*> names;
    for (auto& entry : column->entries) {
        names.push_back(&entry.name);
    }
    size_t keep = names.size() < 20 ? names.size() : 20;
    std::partial_sort(names.begin(), names.begin() + keep, names.end(),
                      [](const std::wstring* a, const std::wstring* b) {
                          return a->size() > b->size();
                      });
    double widest = 0;
    for (size_t i = 0; i < keep; i++) {
        double width = MeasureText(*names[i]);
        if (width > widest) {
            widest = width;
        }
        if (i == 0 && !names[0]->empty()) {
            column->charWidth = width / (double)names[0]->size();
            if (column->charWidth < 4) {
                column->charWidth = 4;
            }
        }
    }
    double width = widest + kRowChrome;
    double minWidth = g_minWidth, maxWidth = g_maxWidth;
    if (width < minWidth) {
        width = minWidth;
    }
    if (width > maxWidth) {
        width = maxWidth;
    }
    column->list.Width(width);
}

size_t NameCapacity(Column* column) {
    double room = column->list.Width() - kRowChrome;
    if (room < 40) {
        room = 40;
    }
    return (size_t)(room / column->charWidth);
}

// Re-cuts every name for the column's current width.
void RefreshNames(Column* column) {
    size_t capacity = NameCapacity(column);
    for (size_t i = 0; i < column->entries.size() && i < column->texts.size();
         i++) {
        if (column->texts[i]) {
            column->texts[i].Text(
                MiddleEllipsis(column->entries[i].name, capacity));
        }
    }
}

// Compact scrollbars: every scrollbar inside `root` is drawn half as wide (or
// as high), whatever state it is in. The transform moves the hit area with the
// drawing, so a compact bar is still clicked and dragged where it is seen.
int StyleScrollBars(mux::DependencyObject const& root, bool compact, int depth = 0) {
    if (!root || depth > 10) {
        return 0;
    }
    if (auto bar = root.try_as<muxc::Primitives::ScrollBar>()) {
        bool vertical = bar.Orientation() == muxc::Orientation::Vertical;
        if (compact) {
            mux::Media::ScaleTransform scale;
            scale.ScaleX(vertical ? 0.5 : 1.0);
            scale.ScaleY(vertical ? 1.0 : 0.5);
            bar.RenderTransformOrigin(winrt::Windows::Foundation::Point{
                vertical ? 1.0f : 0.5f, vertical ? 0.5f : 1.0f});
            bar.RenderTransform(scale);
        } else {
            bar.RenderTransform(nullptr);
        }
        return 1;
    }
    int found = 0;
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; i++) {
        found += StyleScrollBars(mux::Media::VisualTreeHelper::GetChild(root, i), compact, depth + 1);
    }
    return found;
}

// The focus outline around the item under the keyboard cursor, shown or not
// as the setting says.
void ApplyFocusStyle(muxc::ListViewItem const& row) {
    bool show = g_focusRect;
    row.UseSystemFocusVisuals(show);
    row.FocusVisualPrimaryThickness(mux::Thickness{show ? 2.0 : 0.0, show ? 2.0 : 0.0,
                                                    show ? 2.0 : 0.0, show ? 2.0 : 0.0});
    row.FocusVisualSecondaryThickness(mux::Thickness{show ? 1.0 : 0.0, show ? 1.0 : 0.0,
                                                      show ? 1.0 : 0.0, show ? 1.0 : 0.0});
}

muxc::ListViewItem MakeRow(const Entry& entry, double rowHeight,
                           size_t capacity, muxc::TextBlock* textOut,
                           muxc::Grid* iconHostOut,
                           muxc::FontIcon* chevronOut) {
    muxc::Grid grid;
    muxc::ColumnDefinition iconColumn, nameColumn, chevronColumn;
    iconColumn.Width(mux::GridLength{24, mux::GridUnitType::Pixel});
    nameColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
    chevronColumn.Width(mux::GridLength{16, mux::GridUnitType::Pixel});
    grid.ColumnDefinitions().Append(iconColumn);
    grid.ColumnDefinitions().Append(nameColumn);
    grid.ColumnDefinitions().Append(chevronColumn);

    // Stand-in glyphs until real file icons are drawn.
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(entry.launcher ? L"\uE713" : entry.folder ? L"" : L"");
    icon.FontSize(14);
    icon.HorizontalAlignment(mux::HorizontalAlignment::Left);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid iconHost;
    iconHost.Children().Append(icon);
    muxc::Grid::SetColumn(iconHost, 0);
    grid.Children().Append(iconHost);
    *iconHostOut = iconHost;  // The worker swaps in the real icon later.

    muxc::TextBlock name;
    std::wstring shown = MiddleEllipsis(entry.name, capacity);
    name.Text(shown);
    name.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    name.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid::SetColumn(name, 1);
    grid.Children().Append(name);
    *textOut = name;

    if (entry.folder) {
        muxc::FontIcon chevron;
        chevron.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        chevron.Glyph(L"");
        chevron.FontSize(10);
        chevron.Opacity(0.6);
        chevron.HorizontalAlignment(mux::HorizontalAlignment::Right);
        chevron.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(chevron, 2);
        grid.Children().Append(chevron);
        *chevronOut = chevron;
    }

    muxc::ListViewItem row;
    row.Content(grid);
    row.MinHeight(rowHeight);
    row.Height(rowHeight);
    row.Padding(mux::Thickness{10, 0, 8, 0});
    ApplyFocusStyle(row);
    row.HorizontalContentAlignment(mux::HorizontalAlignment::Stretch);
    // The full name, for names that were cut.
    muxc::ToolTipService::SetToolTip(row, winrt::box_value(winrt::hstring{entry.name}));
    return row;
}

// The chevron of a group header: down when open, right when collapsed.
// One right-pointing chevron, turned to point down when the group is open.
const wchar_t* HeaderGlyph() {
    return L"\uE76C";
}

muxc::ListViewItem MakeHeaderRow(const std::wstring& title, double rowHeight,
                                 bool collapsed, muxc::FontIcon* iconOut) {
    muxc::TextBlock text;
    text.Text(title);
    text.FontSize(12);
    text.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    text.Opacity(0.75);
    text.VerticalAlignment(mux::VerticalAlignment::Bottom);
    muxc::FontIcon chevron;
    chevron.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    chevron.Glyph(HeaderGlyph());
    chevron.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
    mux::Media::RotateTransform turn;
    turn.Angle(collapsed ? 0.0 : 90.0);
    chevron.RenderTransform(turn);
    chevron.FontSize(9);
    chevron.Opacity(0.7);
    chevron.Margin(mux::Thickness{0, 0, 6, 2});
    chevron.VerticalAlignment(mux::VerticalAlignment::Bottom);
    *iconOut = chevron;
    muxc::StackPanel line;
    line.Orientation(muxc::Orientation::Horizontal);
    line.Children().Append(chevron);
    line.Children().Append(text);
    muxc::ListViewItem row;
    row.Content(line);
    row.MinHeight(rowHeight);
    row.Height(rowHeight + 4);
    row.Padding(mux::Thickness{10, 0, 8, 2});
    row.IsHitTestVisible(false);
    row.IsTabStop(false);
    row.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
    return row;
}

// Fills a column's list from freshly loaded data.
void CancelRename(WindowState* state);
void BeginRename(WindowState* state, Column* column, size_t entryIndex);

// Explorer's own in-place rename box: a visible Edit control inside its file
// view.
BOOL CALLBACK FindEditProc(HWND hwnd, LPARAM lParam) {
    wchar_t name[32];
    if (GetClassNameW(hwnd, name, ARRAYSIZE(name)) && wcscmp(name, L"Edit") == 0 &&
        IsWindowVisible(hwnd)) {
        for (HWND up = GetParent(hwnd); up; up = GetParent(up)) {
            if (GetClassNameW(up, name, ARRAYSIZE(name)) &&
                wcscmp(name, L"SHELLDLL_DefView") == 0) {
                *(HWND*)lParam = hwnd;
                return FALSE;
            }
        }
    }
    return TRUE;
}

HWND FindNativeRenameBox(HWND topLevel) {
    HWND found = nullptr;
    EnumChildWindows(topLevel, FindEditProc, (LPARAM)&found);
    return found;
}

// The rows that are cut follow the clipboard: a Cut from anywhere shows the
// items dimmed, as long as they are all in one folder.
void ApplyCutState(WindowState* state);
void SyncCutFromClipboard(WindowState* state) {
    state->cutFolder.reset();
    state->cutFolderPath.clear();
    state->cutPaths.clear();
    IDataObject* data = nullptr;
    if (SUCCEEDED(OleGetClipboard(&data)) && data) {
        static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        bool move = false;
        FORMATETC wanted = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1,
                            TYMED_HGLOBAL};
        STGMEDIUM medium = {};
        if (data->GetData(&wanted, &medium) == S_OK) {
            if (medium.hGlobal) {
                if (DWORD* value = (DWORD*)GlobalLock(medium.hGlobal)) {
                    move = (*value & DROPEFFECT_MOVE) != 0;
                    GlobalUnlock(medium.hGlobal);
                }
            }
            ReleaseStgMedium(&medium);
        }
        FORMATETC files = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        if (move && data->GetData(&files, &medium) == S_OK) {
            HDROP drop = (HDROP)medium.hGlobal;
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::wstring parentPath;
            for (UINT i = 0; i < count && i < 500; i++) {
                wchar_t path[MAX_PATH] = L"";
                if (!DragQueryFileW(drop, i, path, MAX_PATH)) {
                    continue;
                }
                wchar_t folder[MAX_PATH];
                lstrcpynW(folder, path, MAX_PATH);
                PathRemoveFileSpecW(folder);
                if (parentPath.empty()) {
                    parentPath = folder;
                    state->cutFolderPath = folder;
                }
                if (_wcsicmp(parentPath.c_str(), folder) == 0) {
                    state->cutPaths.push_back(path);
                }
            }
            Out(L"cut from the clipboard: %d item(s) in \"%s\"",
                (int)state->cutPaths.size(), parentPath.c_str());
            ReleaseStgMedium(&medium);
        }
        data->Release();
    }
    ApplyCutState(state);
}

// Dims the rows that were cut, in every column that shows their folder.
void ApplyCutState(WindowState* state) {
    for (auto& column : state->columns) {
        bool same = false;
        if (!state->cutFolderPath.empty() && !state->cutPaths.empty()) {
            wchar_t shown[MAX_PATH] = L"";
            same = SHGetPathFromIDListW(column->folder.get(), shown) &&
                   _wcsicmp(shown, state->cutFolderPath.c_str()) == 0;
        }
        IShellFolder* folder = nullptr;
        if (same) {
            HRESULT hr = ILIsEmpty(column->folder.get())
                             ? SHGetDesktopFolder(&folder)
                             : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                              IID_PPV_ARGS(&folder));
            if (FAILED(hr)) {
                folder = nullptr;
            }
        }
        auto items = column->list.Items();
        for (size_t r = 0; r < column->rowToEntry.size() && r < items.Size();
             r++) {
            int entry = column->rowToEntry[r];
            if (entry < 0) {
                continue;
            }
            bool cut = false;
            if (folder) {
                std::wstring path = ParsingName(folder, column->entries[entry].child.get());
                for (auto& cutPath : state->cutPaths) {
                    if (_wcsicmp(cutPath.c_str(), path.c_str()) == 0) {
                        cut = true;
                        break;
                    }
                }
            }
            if (auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
                item.Opacity(cut ? 0.45 : 1.0);
            }
        }
        if (folder) {
            folder->Release();
        }
    }
}

void FillColumn(WindowState* state, Column* column, FolderResult&& data) {
    if (state->rename && state->rename->column == column) {
        CancelRename(state);
    }
    column->entries = std::move(data.entries);
    column->rowToEntry.clear();
    column->texts.clear();
    column->texts.resize(column->entries.size(), muxc::TextBlock{nullptr});
    column->iconHosts.clear();
    column->iconHosts.resize(column->entries.size(), muxc::Grid{nullptr});
    column->chevrons.clear();
    column->chevrons.resize(column->entries.size(), muxc::FontIcon{nullptr});
    column->hintEntry = -1;
    column->tipBuilt.assign(column->entries.size(), 0);
    column->animGroup = -1;
    column->animDoneAt = 0;
    column->rowGroup.clear();
    column->headerIcons.clear();
    column->groupTitles = data.groupNames;

    FitWidth(column);
    size_t capacity = NameCapacity(column);
    double rowHeight = IsCompactMode() ? 24 : 32;
    column->rowHeight = rowHeight;
    bool grouped = data.groupOf.size() == column->entries.size() &&
                   !data.groupNames.empty();

    auto items = column->list.Items();
    items.Clear();
    auto isCollapsed = [&](int group) {
        return grouped && group >= 0 && group < (int)data.groupNames.size() &&
               std::find(column->collapsed.begin(), column->collapsed.end(),
                         data.groupNames[group]) != column->collapsed.end();
    };
    // The selected item's group is opened: a selection can't be hidden.
    if (grouped && column->selectedChild) {
        for (size_t i = 0; i < column->entries.size(); i++) {
            if (ILIsEqual(column->entries[i].child.get(),
                          column->selectedChild.get()) &&
                isCollapsed(data.groupOf[i])) {
                auto at = std::find(column->collapsed.begin(),
                                    column->collapsed.end(),
                                    data.groupNames[data.groupOf[i]]);
                column->collapsed.erase(at);
                break;
            }
        }
    }
    int selectRow = -1;
    for (size_t i = 0; i < column->entries.size(); i++) {
        int group = grouped ? data.groupOf[i] : -1;
        if (grouped && (i == 0 || data.groupOf[i] != data.groupOf[i - 1])) {
            std::wstring title =
                group >= 0 && group < (int)data.groupNames.size()
                    ? data.groupNames[group]
                    : L"";
            muxc::FontIcon icon{nullptr};
            items.Append(MakeHeaderRow(title, rowHeight, isCollapsed(group), &icon));
            column->rowToEntry.push_back(-1);
            column->rowGroup.push_back(group);
            column->headerIcons.push_back(icon);
        }
        if (column->selectedChild &&
            ILIsEqual(column->entries[i].child.get(),
                      column->selectedChild.get())) {
            selectRow = (int)column->rowToEntry.size();
        }
        auto row = MakeRow(column->entries[i], rowHeight, capacity,
                           &column->texts[i], &column->iconHosts[i],
                           &column->chevrons[i]);
        if (isCollapsed(group)) {
            row.Visibility(mux::Visibility::Collapsed);
        }
        items.Append(row);
        column->rowToEntry.push_back((int)i);
        column->rowGroup.push_back(group);
        column->headerIcons.push_back(muxc::FontIcon{nullptr});
    }
    if (data.truncated) {
        muxc::ListViewItem more;
        more.Content(winrt::box_value(L"More items not shown"));
        more.IsEnabled(false);
        items.Append(more);
        column->rowToEntry.push_back(-1);
        column->rowGroup.push_back(-1);
        column->headerIcons.push_back(muxc::FontIcon{nullptr});
    }
    if (selectRow >= 0) {
        column->list.SelectedIndex(selectRow);
    }
    column->filled = true;
    if (!state->cutPaths.empty()) {
        ApplyCutState(state);
    }
    if (!state->renameName.empty() && state->renameColumn == column) {
        std::wstring wanted = state->renameName;
        state->renameName.clear();
        state->renameColumn = nullptr;
        for (size_t i = 0; i < column->entries.size(); i++) {
            if (column->entries[i].name == wanted && state->queue) {
                // After the column has been laid out.
                state->queue.TryEnqueue([column, i]() {
                    WindowState* state = StateForThisThread(false);
                    if (state && state->on && !g_unloading) {
                        try {
                            BeginRename(state, column, i);
                        } catch (...) {
                            LogHresult(L"rename of the new folder");
                        }
                    }
                });
                break;
            }
        }
    }
    Out(L"column \"%s\": %d items, %d groups%s, width %d",
        PathOf(column->folder.get()).c_str(), (int)column->entries.size(),
        grouped ? (int)data.groupNames.size() : 0,
        data.truncated ? L", truncated" : L"", (int)column->list.Width());
}

// Runs on the window's UI thread when the worker has read a folder.
// Is a freshly read folder the same as what the column shows? Then the rows
// stay as they are: no flicker, no lost scroll position.
bool SameListing(const FolderResult& data, const Column& column) {
    if (!column.filled || data.entries.size() != column.entries.size()) {
        return false;
    }
    for (size_t i = 0; i < data.entries.size(); i++) {
        if (data.entries[i].name != column.entries[i].name ||
            data.entries[i].folder != column.entries[i].folder ||
            !ILIsEqual(data.entries[i].child.get(), column.entries[i].child.get())) {
            return false;
        }
    }
    // Groups: the same header count and the same group of each entry.
    size_t headers = 0;
    for (int entry : column.rowToEntry) {
        headers += entry < 0 ? 1 : 0;
    }
    size_t expected = data.groupNames.empty() ? 0 : data.groupNames.size();
    bool more = !column.rowToEntry.empty() && column.rowToEntry.back() < 0 &&
                data.truncated;
    if (more) {
        headers -= 1;
    }
    if (headers != expected) {
        return false;
    }
    return data.truncated == more;
}

void ApplyLoaded(std::shared_ptr<Loaded> const& loaded) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    for (auto& column : state->columns) {
        if (column->generation == loaded->generation) {
            bool wasBuilding = state->building;
            state->building = true;
            try {
                if (!SameListing(loaded->data, *column)) {
                    FillColumn(state, column.get(), std::move(loaded->data));
                }
                state->scroller.UpdateLayout();
                if (state->swapOld && column->generation == state->swapGeneration) {
                    CompleteSwap(state);
                } else if (state->columns.back().get() == column.get()) {
                    EnsureColumnVisible(state, (int)state->columns.size() - 1);
                }
            } catch (...) {
                LogHresult(L"filling a column");
            }
            state->building = wasBuilding;
            return;
        }
    }
    // No column waits for this result any more. Drop it.
}

// The pixel buffer of a WriteableBitmap, as a byte pointer.
struct IBufferByteAccessLocal : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Buffer(uint8_t** value) = 0;
};
const GUID kIidBufferByteAccess = {
    0x905a0fef, 0xbc53, 0x11df,
    {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};

// Runs on the window's UI thread when the worker has read some icons.
void ApplyIcons(std::shared_ptr<IconBatch> const& batch) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    for (auto& column : state->columns) {
        if (column->generation != batch->generation) {
            continue;
        }
        for (auto& icon : batch->icons) {
            if (icon.index >= column->iconHosts.size() ||
                !column->iconHosts[icon.index]) {
                continue;
            }
            try {
                mux::Media::Imaging::WriteableBitmap bitmap(icon.width,
                                                            icon.height);
                auto buffer = bitmap.PixelBuffer();
                winrt::com_ptr<IBufferByteAccessLocal> access;
                winrt::check_hresult(
                    ((::IUnknown*)winrt::get_abi(buffer))
                        ->QueryInterface(kIidBufferByteAccess,
                                         access.put_void()));
                uint8_t* pixels = nullptr;
                winrt::check_hresult(access->Buffer(&pixels));
                if (!pixels || buffer.Capacity() < icon.bgra.size()) {
                    continue;
                }
                memcpy(pixels, icon.bgra.data(), icon.bgra.size());
                buffer.Length((uint32_t)icon.bgra.size());
                bitmap.Invalidate();

                muxc::Image image;
                image.Source(bitmap);
                image.Width(20);
                image.Height(20);
                image.Stretch(mux::Media::Stretch::Uniform);
                image.HorizontalAlignment(mux::HorizontalAlignment::Left);
                image.VerticalAlignment(mux::VerticalAlignment::Center);
                auto host = column->iconHosts[icon.index];
                host.Children().Clear();
                host.Children().Append(image);
            } catch (...) {
                static int logged = 0;
                if (logged++ < 3) {
                    LogHresult(L"applying an icon");
                }
            }
        }
        return;
    }
}

void OnRowSelected(muxc::ListView const& list);
void OnRowDoubleTapped(muxc::ListView const& list);
void OnRowRightTapped(muxc::ListView const& list,
                      mux::Input::RightTappedRoutedEventArgs const& args);
void OnColumnBackgroundTapped(muxc::ListView const& list,
                              mux::Input::TappedRoutedEventArgs const& args);
void OnColumnPointerMoved(muxc::ListView const& list,
                          mux::Input::PointerRoutedEventArgs const& args);
void OnColumnPointerExited(muxc::ListView const& list);
void OnBandPressed(muxc::ListView const& list, mux::Input::PointerRoutedEventArgs const& args);
void OnBandReleased(muxc::ListView const& list);
void OnColumnKey(muxc::ListView const& list,
                 mux::Input::KeyRoutedEventArgs const& args);

// Adds an empty column for `folder` at the right end and asks the worker to
// read it. Takes ownership of `folder`.
void AddColumn(WindowState* state, Pidl folder, bool atFront = false) {
    auto column = std::make_unique<Column>();
    column->folder = std::move(folder);
    column->notifyId = WatchFolder(state->notifyWindow, column->folder.get());

    muxc::ListView list;
    list.Width(g_minWidth);
    list.SelectionMode(muxc::ListViewSelectionMode::Extended);
    list.Padding(mux::Thickness{0, 12, 10, 4});
    column->selectionToken = list.SelectionChanged(
        [](Inspectable const& sender,
           muxc::SelectionChangedEventArgs const&) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnRowSelected(list);
            }
        });
    column->doubleTapToken = list.DoubleTapped(
        [](Inspectable const& sender,
           mux::Input::DoubleTappedRoutedEventArgs const& args) {
            // Only a double-click on a row opens it, not one on a header or
            // on the empty space.
            mux::DependencyObject node =
                args.OriginalSource().try_as<mux::DependencyObject>();
            bool onRow = false;
            for (int i = 0; node && i < 20; i++) {
                if (node.try_as<muxc::ListViewItem>()) {
                    onRow = true;
                    break;
                }
                node = mux::Media::VisualTreeHelper::GetParent(node);
            }
            if (!onRow) {
                return;
            }
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnRowDoubleTapped(list);
            }
        });
    // The empty space is hit-testable, so a click on it can pick the column.
    list.Background(Brush(0, 0, 0, 0));
    column->tapToken = list.Tapped(
        [](Inspectable const& sender, mux::Input::TappedRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnColumnBackgroundTapped(list, args);
            }
        });
    column->moveToken = list.PointerMoved(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnColumnPointerMoved(list, args);
            }
        });
    column->pressToken = list.PointerPressed(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnBandPressed(list, args);
            }
        });
    column->releaseToken = list.PointerReleased(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const&) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnBandReleased(list);
            }
        });
    column->captureLostToken = list.PointerCaptureLost(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const&) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnBandReleased(list);
            }
        });
    column->exitToken = list.PointerExited(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const&) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnColumnPointerExited(list);
            }
        });
    column->rightTapToken = list.RightTapped(
        [](Inspectable const& sender,
           mux::Input::RightTappedRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnRowRightTapped(list, args);
            }
        });

    column->keyToken = list.PreviewKeyDown(
        [](Inspectable const& sender,
           mux::Input::KeyRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnColumnKey(list, args);
            }
        });

    // A horizontal wheel turn must not scroll a column up or down. The list's
    // own scroll viewer takes the event before any handler on the list, so
    // the handler goes on the row panel inside it, which sees the event first.
    // It only marks the event handled. The strip itself is moved by the
    // handler on the panel's root, which also sees handled events.
    column->loadedToken = list.Loaded(
        [](Inspectable const& sender, mux::RoutedEventArgs const&) {
            try {
                auto list = sender.try_as<muxc::ListView>();
                WindowState* state = StateForThisThread(false);
                if (!list || !state) {
                    return;
                }
                int index = ColumnIndexOfList(state, list);
                if (index < 0 || state->columns[index]->panel) {
                    return;
                }
                auto panel = list.ItemsPanelRoot();
                if (!panel) {
                    return;
                }
                Column& column = *state->columns[index];
                column.panel = panel;
                column.panelWheelToken = panel.PointerWheelChanged(
                    [](Inspectable const& source,
                       mux::Input::PointerRoutedEventArgs const& args) {
                        try {
                            auto element = source.try_as<mux::UIElement>();
                            if (element &&
                                args.GetCurrentPoint(element)
                                    .Properties()
                                    .IsHorizontalMouseWheel()) {
                                args.Handled(true);
                            }
                        } catch (...) {
                            LogHresult(L"column wheel");
                        }
                    });
            } catch (...) {
                LogHresult(L"column loaded");
            }
        });

    // The column's right edge: a separator line that can be dragged.
    muxc::Border line;
    line.Width(1);
    line.Background(Brush(48, 128, 128, 128));
    line.HorizontalAlignment(mux::HorizontalAlignment::Center);
    // A short rounded handle, shown only while the pointer is on the edge.
    muxc::Border pill;
    pill.Width(3);
    pill.Height(28);
    pill.CornerRadius(mux::CornerRadius{1.5, 1.5, 1.5, 1.5});
    pill.Background(Brush(200, 150, 150, 150));
    pill.HorizontalAlignment(mux::HorizontalAlignment::Center);
    pill.VerticalAlignment(mux::VerticalAlignment::Center);
    pill.Opacity(0);
    muxc::Grid gripContent;
    gripContent.Children().Append(line);
    gripContent.Children().Append(pill);
    muxc::Border grip;
    grip.Width(9);
    grip.Background(Brush(0, 0, 0, 0));
    grip.Child(gripContent);
    column->pill = pill;
    try {
        // The left-right resize cursor. The property is protected in XAML,
        // and reachable through its interface from C++/WinRT.
        grip.as<mux::IUIElementProtected>().ProtectedCursor(
            winrt::Microsoft::UI::Input::InputSystemCursor::Create(
                winrt::Microsoft::UI::Input::InputSystemCursorShape::
                    SizeWestEast));
    } catch (...) {
        LogHresult(L"setting the resize cursor");
    }
    column->gripEnteredToken = grip.PointerEntered(
        [](Inspectable const& sender,
           mux::Input::PointerRoutedEventArgs const&) {
            WindowState* state = StateForThisThread(false);
            int index = state ? ColumnIndexOfGrip(state, sender) : -1;
            if (index >= 0) {
                state->columns[index]->hoverSince = GetTickCount64();
            }
        });
    column->gripExitedToken = grip.PointerExited(
        [](Inspectable const& sender,
           mux::Input::PointerRoutedEventArgs const&) {
            WindowState* state = StateForThisThread(false);
            int index = state ? ColumnIndexOfGrip(state, sender) : -1;
            if (index >= 0) {
                state->columns[index]->hoverSince = 0;
                if (state->columns[index]->pill && state->dragColumn != index) {
                    state->columns[index]->pill.Opacity(0);
                }
            }
        });
    column->gripPressedToken = grip.PointerPressed(
        [](Inspectable const& sender,
           mux::Input::PointerRoutedEventArgs const& args) {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->on) {
                return;
            }
            try {
                int index = ColumnIndexOfGrip(state, sender);
                auto element = sender.try_as<mux::UIElement>();
                if (index < 0 || !element) {
                    return;
                }
                element.CapturePointer(args.Pointer());
                state->dragColumn = index;
                state->dragStartX =
                    args.GetCurrentPoint(state->strip).Position().X;
                state->dragStartWidth = state->columns[index]->list.Width();
                args.Handled(true);
            } catch (...) {
                LogHresult(L"starting a column resize");
            }
        });
    column->gripMovedToken = grip.PointerMoved(
        [](Inspectable const& sender,
           mux::Input::PointerRoutedEventArgs const& args) {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->on || state->dragColumn < 0) {
                return;
            }
            try {
                int index = ColumnIndexOfGrip(state, sender);
                if (index != state->dragColumn) {
                    return;
                }
                double x = args.GetCurrentPoint(state->strip).Position().X;
                double width = state->dragStartWidth + (x - state->dragStartX);
                if (width < g_minWidth) {
                    width = g_minWidth;
                }
                if (width > g_maxWidth) {
                    width = g_maxWidth;
                }
                Column* column = state->columns[index].get();
                column->userSized = true;
                column->list.Width(width);
                UpdatePill(state, false);  // Follows the drag, frame by frame.
            } catch (...) {
                LogHresult(L"resizing a column");
            }
        });
    auto endDrag = [](Inspectable const& sender,
                      mux::Input::PointerRoutedEventArgs const& args) {
        WindowState* state = StateForThisThread(false);
        if (!state || state->dragColumn < 0) {
            return;
        }
        try {
            int index = state->dragColumn;
            state->dragColumn = -1;
            if (auto element = sender.try_as<mux::UIElement>()) {
                element.ReleasePointerCapture(args.Pointer());
            }
            if (index < (int)state->columns.size()) {
                if (state->columns[index]->pill) {
                    state->columns[index]->pill.Opacity(0);
                }
                RefreshNames(state->columns[index].get());
                Out(L"column %d resized to %d", index + 1,
                    (int)state->columns[index]->list.Width());
            }
        } catch (...) {
            LogHresult(L"ending a column resize");
        }
    };
    column->gripReleasedToken = grip.PointerReleased(endDrag);
    column->gripLostToken = grip.PointerCaptureLost(endDrag);
    column->gripDoubleToken = grip.DoubleTapped(
        [](Inspectable const& sender,
           mux::Input::DoubleTappedRoutedEventArgs const&) {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->on) {
                return;
            }
            try {
                int index = ColumnIndexOfGrip(state, sender);
                if (index < 0) {
                    return;
                }
                Column* column = state->columns[index].get();
                column->userSized = false;
                FitWidth(column);
                RefreshNames(column);
                UpdatePill(state, false);
                Out(L"column %d refitted to %d", index + 1,
                    (int)column->list.Width());
            } catch (...) {
                LogHresult(L"refitting a column");
            }
        });

    muxc::StackPanel cell;
    cell.Orientation(muxc::Orientation::Horizontal);
    cell.Children().Append(list);
    cell.Children().Append(grip);

    column->list = list;
    column->grip = grip;
    column->cell = cell;
    Column* raw = column.get();
    if (atFront) {
        state->strip.Children().InsertAt(0, cell);
        state->columns.insert(state->columns.begin(), std::move(column));
        if (state->active >= 0) {
            state->active++;
        }
    } else {
        state->strip.Children().Append(cell);
        state->columns.push_back(std::move(column));
    }
    RequestLoad(state, raw);
}

// Selects `child` in a column without treating it as a click. Works whether
// the column has loaded or not.
void SelectChildInColumn(WindowState* state, Column* column,
                         PCUITEMID_CHILD child) {
    column->selectedChild.reset(ILClone(child));
    int row = -1;
    for (size_t r = 0; r < column->rowToEntry.size(); r++) {
        int entry = column->rowToEntry[r];
        if (entry >= 0 &&
            ILIsEqual(column->entries[entry].child.get(), child)) {
            row = (int)r;
            break;
        }
    }
    if (row >= 0) {
        bool wasBuilding = state->building;
        state->building = true;
        column->list.SelectedIndex(row);
        state->building = wasBuilding;
    }
}

// Reads every column again, after the sort or group settings changed.
void ReloadAll(WindowState* state) {
    for (auto& column : state->columns) {
        RequestLoad(state, column.get());
    }
}

// Finishes a rebuild: the new first column is shown, the old ones are removed.
void CompleteSwap(WindowState* state) {
    if (!state->swapOld) {
        return;
    }
    state->swapOld = 0;
    try {
        bool wasBuilding = state->building;
        state->building = true;
        RemoveColumnsFrom(state, 1);
        state->building = wasBuilding;
        if (!state->columns.empty() && state->columns[0]->cell) {
            state->columns[0]->cell.Visibility(mux::Visibility::Visible);
        }
        // The pill glides from where it was to the new first column.
        SetActiveColumn(state, 0);
    } catch (...) {
        LogHresult(L"CompleteSwap");
    }
}

// Starts over with Explorer's current folder as the only column.
// Is this folder one of the navigation pane's flat entries: Home, Gallery,
// OneDrive, or a pinned or frequent folder? The expandable tree (This PC,
// drives, Network) is not.
bool IsFlatSidebarTarget(PCIDLIST_ABSOLUTE folder) {
    PWSTR parsing = nullptr;
    if (FAILED(SHGetNameFromIDList(folder, SIGDN_DESKTOPABSOLUTEPARSING,
                                   &parsing)) ||
        !parsing) {
        return false;
    }
    std::wstring target = parsing;
    CoTaskMemFree(parsing);
    if (_wcsicmp(target.c_str(), L"::{F874310E-B6B7-47DC-BC84-B9E6B38F5903}") == 0 ||
        _wcsicmp(target.c_str(), L"::{E88865EA-0E1C-4E20-9AA6-EDCD0212C87C}") == 0) {
        return true;  // Home, Gallery.
    }
    // OneDrive and the user's known folders.
    const KNOWNFOLDERID* known[] = {&FOLDERID_Desktop,   &FOLDERID_Downloads,
                                    &FOLDERID_Documents, &FOLDERID_Pictures,
                                    &FOLDERID_Music,     &FOLDERID_Videos};
    for (auto id : known) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, 0, nullptr, &path)) && path) {
            bool same = _wcsicmp(path, target.c_str()) == 0;
            CoTaskMemFree(path);
            if (same) {
                return true;
            }
        }
    }
    wchar_t oneDrive[MAX_PATH] = L"";
    if (GetEnvironmentVariableW(L"OneDrive", oneDrive, MAX_PATH) &&
        _wcsicmp(oneDrive, target.c_str()) == 0) {
        return true;
    }
    // Pinned and frequent folders, from Quick access.
    bool found = false;
    PIDLIST_ABSOLUTE quick = nullptr;
    if (SUCCEEDED(SHParseDisplayName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}",
                                     nullptr, &quick, 0, nullptr)) &&
        quick) {
        IShellFolder* folder = nullptr;
        if (SUCCEEDED(SHBindToObject(nullptr, quick, nullptr,
                                     IID_PPV_ARGS(&folder))) &&
            folder) {
            IEnumIDList* list = nullptr;
            if (folder->EnumObjects(nullptr, SHCONTF_FOLDERS, &list) == S_OK &&
                list) {
                PITEMID_CHILD child = nullptr;
                int count = 0;
                while (!found && count++ < 100 &&
                       list->Next(1, &child, nullptr) == S_OK && child) {
                    STRRET text = {};
                    wchar_t path[MAX_PATH] = L"";
                    if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_FORPARSING,
                                                           &text)) &&
                        SUCCEEDED(StrRetToBufW(&text, child, path, MAX_PATH))) {
                        found = _wcsicmp(path, target.c_str()) == 0;
                    }
                    CoTaskMemFree(child);
                    child = nullptr;
                }
                list->Release();
            }
            folder->Release();
        }
        CoTaskMemFree(quick);
    }
    return found;
}

void RebuildFromExplorer(WindowState* state) {
    IShellBrowser* browser = BrowserOf(state->topLevel);
    if (!browser) {
        Out(L"rebuild: no shell browser for this window");
        return;
    }
    Pidl current = CurrentFolder(browser);
    if (!current) {
        Out(L"rebuild: can't read the current folder");
        return;
    }
    state->lastSeen.reset(ILClone(current.get()));
    state->expected.reset();
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    // The new first folder brings its own sort and grouping. Without this, a
    // group key of the old folder (Home groups by its own property) put every
    // item of the new one in "Unspecified".
    {
        SortSpec fresh;
        if (ReadSortSpec(browser, &fresh)) {
            state->spec = fresh;
            Out(L"rebuild: spec %d sort columns, grouped=%d", (int)fresh.columns.size(),
                fresh.grouped ? 1 : 0);
        }
    }
    if (state->columns.empty()) {
        AddColumn(state, std::move(current));
        return;
    }
    // Columns are showing: add the new first column hidden, in front, and let
    // ApplyLoaded swap the old ones out once it has its rows.
    if (state->swapOld) {
        CompleteSwap(state);
    }
    size_t old = state->columns.size();
    AddColumn(state, std::move(current), true);
    Column* fresh = state->columns[0].get();
    if (fresh->cell) {
        fresh->cell.Visibility(mux::Visibility::Collapsed);
    }
    state->swapOld = old;
    state->swapGeneration = fresh->generation;
    state->swapDeadline = GetTickCount64() + 3000;
}

// Asks Explorer's own file list to show `folder`, and remembers the request
// so the tick doesn't take it for a navigation by the user.
void NavigateExplorer(WindowState* state, PCIDLIST_ABSOLUTE folder) {
    IShellBrowser* browser = BrowserOf(state->topLevel);
    if (!browser) {
        return;
    }
    Pidl current = CurrentFolder(browser);
    if (current && ILIsEqual(current.get(), folder)) {
        return;
    }
    state->expected.reset(ILClone(folder));
    // Explorer's view takes focus when it navigates. If the keyboard was in
    // a column, the tick gives it back.
    if (state->bridge && state->active >= 0 &&
        state->active < (int)state->columns.size()) {
        HWND panel = (HWND)state->bridge.WindowId().Value;
        HWND focused = GetFocus();
        if (panel && focused && (focused == panel || IsChild(panel, focused))) {
            state->focusColumn = state->columns[state->active].get();
            state->focusUntil = GetTickCount64() + 2500;
        }
    }
    g_ourBrowse = true;
    HRESULT hr = browser->BrowseObject(folder, SBSP_ABSOLUTE | SBSP_SAMEBROWSER);
    g_ourBrowse = false;
    if (FAILED(hr)) {
        Out(L"BrowseObject failed, hr=0x%08lX", hr);
        state->expected.reset();
    }
}

// Explorer follows after a short pause, so a run of quick selections costs
// one navigation.
void ScheduleNavigation(WindowState* state, PCIDLIST_ABSOLUTE folder) {
    if (!g_follow) {
        return;
    }
    if (IsInControlPanel(folder)) {
        // Explorer's view stays where it is. The columns still show it.
        state->pendingNav.reset();
        state->selectFolder.reset();
        state->selectChild.reset();
        return;
    }
    state->pendingNav.reset(ILClone(folder));
    state->navDue = GetTickCount64() + 350;
}

// While the columns to the right of `columnIndex` are stale, the chevron of the
// folder that leads to them is drawn stronger, a quiet thread of the hierarchy.
void SetStaleHint(WindowState* state, int columnIndex, bool on) {
    if (columnIndex < 0 || columnIndex >= (int)state->columns.size()) {
        return;
    }
    Column* column = state->columns[columnIndex].get();
    try {
        if (column->hintEntry >= 0 &&
            column->hintEntry < (int)column->chevrons.size() &&
            column->chevrons[column->hintEntry]) {
            column->chevrons[column->hintEntry].Opacity(0.6);
            column->chevrons[column->hintEntry].FontSize(10);
        }
        column->hintEntry = -1;
        if (!on || columnIndex + 1 >= (int)state->columns.size()) {
            return;
        }
        Pidl leading(ILClone(ILFindLastID(state->columns[columnIndex + 1]->folder.get())));
        if (!leading) {
            return;
        }
        for (size_t i = 0; i < column->entries.size() && i < column->chevrons.size(); i++) {
            if (column->chevrons[i] &&
                ILIsEqual(column->entries[i].child.get(), leading.get())) {
                column->chevrons[i].Opacity(1.0);
                column->chevrons[i].FontSize(13);
                column->hintEntry = (int)i;
                break;
            }
        }
    } catch (...) {
        LogHresult(L"SetStaleHint");
    }
}

// Working in a column, by choosing an item in it or clicking its empty space,
// lifts the stale look of it and of the columns before it, and puts the
// selection of the earlier columns back on the folders that lead here. The
// columns after it stay lit only while the selection here still leads to them.
void LiftStale(WindowState* state, int columnIndex) {
    for (int j = 0; j <= columnIndex; j++) {
        if (state->columns[j]->cell) {
            state->columns[j]->cell.Opacity(1.0);
        }
        if (j < columnIndex) {
            SetStaleHint(state, j, false);
        }
    }
    for (int j = columnIndex; j > 0; j--) {
        Column* previous = state->columns[j - 1].get();
        Column* here = state->columns[j].get();
        if (!ILIsParent(previous->folder.get(), here->folder.get(), TRUE)) {
            break;
        }
        Pidl leading(ILClone(ILFindLastID(here->folder.get())));
        if (leading &&
            (!previous->selectedChild ||
             !ILIsEqual(previous->selectedChild.get(), leading.get()))) {
            SelectChildInColumn(state, previous, leading.get());
        }
    }
    // The columns after this one: lit while the chain holds.
    bool chain = true;
    for (size_t k = columnIndex + 1; k < state->columns.size(); k++) {
        Column* previous = state->columns[k - 1].get();
        Column* here = state->columns[k].get();
        Pidl leading(ILClone(ILFindLastID(here->folder.get())));
        chain = chain && previous->selectedChild && leading &&
                ILIsParent(previous->folder.get(), here->folder.get(), TRUE) &&
                ILIsEqual(previous->selectedChild.get(), leading.get());
        if (here->cell) {
            here->cell.Opacity(chain ? 1.0 : 0.5);
        }
    }
    SetStaleHint(state, columnIndex, false);
}

// The entries of the rows selected in a column, in row order. Group headers
// are left out.
std::vector<size_t> SelectedEntries(Column* column,
                                    muxc::ListView const& list) {
    std::vector<size_t> result;
    auto items = list.Items();
    uint32_t count = items.Size();
    for (uint32_t r = 0; r < count && r < column->rowToEntry.size(); r++) {
        int entry = column->rowToEntry[r];
        if (entry < 0) {
            continue;
        }
        auto item = items.GetAt(r).try_as<muxc::ListViewItem>();
        if (item && item.IsSelected() &&
            item.Visibility() == mux::Visibility::Visible) {
            result.push_back((size_t)entry);
        }
    }
    return result;
}

void OnRowSelected(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || state->building || g_unloading) {
        return;
    }
    try {
        int columnIndex = ColumnIndexOfList(state, list);
        if (columnIndex < 0) {
            return;
        }
        Column* column = state->columns[columnIndex].get();
        column->selectionChangedAt = GetTickCount64();
        state->slowRenameAt = 0;

        LiftStale(state, columnIndex);

        // Group headers and the "more items" row can't be selected, as when
        // Shift-click selects a range across them.
        {
            auto items = list.Items();
            auto selected = list.SelectedItems();
            state->building = true;
            for (uint32_t r = 0; r < items.Size() && r < column->rowToEntry.size();
                 r++) {
                auto object = items.GetAt(r);
                bool hidden = false;
                if (column->rowToEntry[r] >= 0) {
                    auto item = object.try_as<muxc::ListViewItem>();
                    hidden = item && item.Visibility() == mux::Visibility::Collapsed;
                    if (!hidden) {
                        continue;
                    }
                }
                uint32_t at = 0;
                if (selected.IndexOf(object, at)) {
                    selected.RemoveAt(at);
                }
            }
            state->building = false;
        }

        int row = list.SelectedIndex();
        int entryIndex = row >= 0 && row < (int)column->rowToEntry.size()
                             ? column->rowToEntry[row]
                             : -1;
        state->selectFolder.reset();
        state->selectChild.reset();
        state->selectExtra.clear();

        // The columns to the right stay while the selection is a file, several
        // items or nothing, dimmed, until another folder is chosen here.
        auto dimAfter = [&](bool dim) {
            for (size_t j = columnIndex + 1; j < state->columns.size(); j++) {
                if (state->columns[j]->cell) {
                    state->columns[j]->cell.Opacity(dim ? 0.5 : 1.0);
                }
            }
            SetStaleHint(state, columnIndex, dim);
        };

        // Several items selected: no column opens. Explorer's own view gets
        // the same selection, so the preview pane, status bar and command
        // bar act on all of them.
        std::vector<size_t> picked = SelectedEntries(column, list);
        if (picked.size() > 1) {
            column->selectedChild.reset(
                ILClone(column->entries[picked[0]].child.get()));
            SetActiveColumn(state, columnIndex);
            state->selectFolder.reset(ILClone(column->folder.get()));
            state->selectChild.reset(
                ILClone(column->entries[picked[0]].child.get()));
            for (size_t i = 1; i < picked.size(); i++) {
                state->selectExtra.emplace_back(
                    ILClone(column->entries[picked[i]].child.get()));
            }
            state->selectTries = 0;
            ScheduleNavigation(state, column->folder.get());
            dimAfter(true);
            Out(L"%d items selected in column %d", (int)picked.size(),
                columnIndex + 1);
            return;
        }
        if (entryIndex < 0 && picked.size() == 1) {
            entryIndex = (int)picked[0];
        }
        if (entryIndex < 0) {
            column->selectedChild.reset();
            dimAfter(true);
            return;
        }
        const Entry& entry = column->entries[entryIndex];
        column->selectedChild.reset(ILClone(entry.child.get()));
        Pidl absolute(ILCombine(column->folder.get(), entry.child.get()));
        if (!absolute) {
            return;
        }
        if (entry.launcher) {
            // Nothing to show in Explorer's view or in a column. Enter or a
            // double-click runs it.
            dimAfter(true);
            SetActiveColumn(state, columnIndex);
            return;
        }
        if (entry.folder) {
            // Explorer's view shows this column's folder with the item
            // selected, so the toolbar and the panes act on the folder.
            state->selectFolder.reset(ILClone(column->folder.get()));
            state->selectChild.reset(ILClone(entry.child.get()));
            state->selectTries = 0;
            ScheduleNavigation(state, column->folder.get());
            if (columnIndex + 1 < (int)state->columns.size() &&
                ILIsEqual(state->columns[columnIndex + 1]->folder.get(),
                          absolute.get())) {
                // Back on the folder whose columns are still there.
                dimAfter(false);
            } else {
                state->building = true;
                RemoveColumnsFrom(state, columnIndex + 1);
                state->building = false;
                SetStaleHint(state, columnIndex, false);
                AddColumn(state, std::move(absolute));
            }
            // The highlight stays here. It moves to the new column when the
            // user enters it.
            SetActiveColumn(state, columnIndex);
        } else {
            dimAfter(true);
            SetActiveColumn(state, columnIndex);
            // A file: Explorer shows the file's folder with the file selected.
            state->selectFolder.reset(ILClone(column->folder.get()));
            state->selectChild.reset(ILClone(entry.child.get()));
            state->selectTries = 0;
            ScheduleNavigation(state, column->folder.get());
        }
    } catch (...) {
        state->building = false;
        LogHresult(L"OnRowSelected");
    }
}

// ---- Right-click menu ----

// ---- Rename in place and clipboard, which the shell menu can't do here ----

void CancelRename(WindowState* state) {
    if (!state->rename) {
        return;
    }
    auto rename = std::move(state->rename);
    rename->done = true;
    g_renaming = false;
    try {
        if (rename->box) {
            rename->box.KeyDown(rename->keyToken);
            rename->box.LostFocus(rename->lostToken);
        }
        if (rename->grid && rename->box) {
            uint32_t index = 0;
            if (rename->grid.Children().IndexOf(rename->box, index)) {
                rename->grid.Children().RemoveAt(index);
            }
        }
        if (rename->text) {
            rename->text.Visibility(mux::Visibility::Visible);
        }
    } catch (...) {
        LogHresult(L"CancelRename");
    }
}

// ---- Undo for deletes and renames ----

std::wstring FilePathOf(PCIDLIST_ABSOLUTE folder, PCUITEMID_CHILD child) {
    Pidl absolute(ILCombine(folder, child));
    wchar_t path[MAX_PATH] = L"";
    if (absolute && SHGetPathFromIDListW(absolute.get(), path)) {
        return path;
    }
    return L"";
}

void PushUndo(WindowState* state, UndoEntry&& entry) {
    state->undo.push_back(std::move(entry));
    if (state->undo.size() > 20) {
        state->undo.erase(state->undo.begin());
    }
}

// Remember a delete: the paths that are gone now. A delete that didn't happen
// leaves nothing to undo.
void RememberDeletion(WindowState* state, std::vector<std::wstring> const& paths) {
    UndoEntry entry;
    entry.deletion = true;
    for (auto& path : paths) {
        if (!path.empty() && !PathFileExistsW(path.c_str())) {
            entry.paths.push_back(path);
        }
    }
    if (!entry.paths.empty()) {
        PushUndo(state, std::move(entry));
    }
}

// Puts deleted files back from the Recycle Bin: the newest bin item for each
// original path gets the bin's own "undelete" command.
HRESULT RestoreFromRecycleBin(WindowState* state,
                              std::vector<std::wstring> const& paths) {
    PIDLIST_ABSOLUTE bin = nullptr;
    HRESULT hr = SHParseDisplayName(L"shell:RecycleBinFolder", nullptr, &bin, 0,
                                    nullptr);
    if (FAILED(hr) || !bin) {
        return hr;
    }
    IShellFolder* folder = nullptr;
    hr = SHBindToObject(nullptr, bin, nullptr, IID_PPV_ARGS(&folder));
    CoTaskMemFree(bin);
    if (FAILED(hr) || !folder) {
        return hr;
    }
    IShellFolder2* folder2 = nullptr;
    folder->QueryInterface(IID_PPV_ARGS(&folder2));
    static const PROPERTYKEY kDeletedFrom = {
        {0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 2};
    static const PROPERTYKEY kDateDeleted = {
        {0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 3};

    std::vector<Pidl> best(paths.size());
    std::vector<double> bestDate(paths.size(), -1);
    IEnumIDList* list = nullptr;
    if (folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS |
                                         SHCONTF_INCLUDEHIDDEN,
                            &list) == S_OK &&
        list) {
        PITEMID_CHILD child = nullptr;
        int seen = 0;
        while (seen++ < 5000 && list->Next(1, &child, nullptr) == S_OK && child) {
            // The bin names an item by its original path. The original folder
            // and the plain name are the second way to say the same.
            std::wstring normal = DisplayName(folder, child);
            std::wstring name = [&]() {
                STRRET text = {};
                wchar_t buffer[MAX_PATH] = L"";
                if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_INFOLDER, &text)) &&
                    SUCCEEDED(StrRetToBufW(&text, child, buffer, MAX_PATH))) {
                    return std::wstring(buffer);
                }
                return std::wstring();
            }();
            std::wstring from;
            double date = 0;
            if (folder2) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(folder2->GetDetailsEx(child, &kDeletedFrom, &value)) &&
                    value.vt == VT_BSTR && value.bstrVal) {
                    from = value.bstrVal;
                }
                VariantClear(&value);
                if (SUCCEEDED(folder2->GetDetailsEx(child, &kDateDeleted, &value)) &&
                    value.vt == VT_DATE) {
                    date = value.date;
                }
                VariantClear(&value);
            }
            std::wstring joined = from.empty() ? L"" : from + L"\\" + name;
            for (size_t k = 0; k < paths.size(); k++) {
                if ((!normal.empty() && _wcsicmp(normal.c_str(), paths[k].c_str()) == 0) ||
                    (!joined.empty() && _wcsicmp(joined.c_str(), paths[k].c_str()) == 0)) {
                    if (date >= bestDate[k]) {
                        bestDate[k] = date;
                        best[k].reset(ILClone(child));
                    }
                }
            }
            CoTaskMemFree(child);
            child = nullptr;
        }
        list->Release();
    }
    if (folder2) {
        folder2->Release();
    }

    std::vector<PCUITEMID_CHILD> chosen;
    for (auto& item : best) {
        if (item) {
            chosen.push_back(item.get());
        }
    }
    Out(L"undo delete: %d of %d item(s) found in the Recycle Bin", (int)chosen.size(),
        (int)paths.size());
    hr = chosen.empty() ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : S_OK;
    if (!chosen.empty()) {
        IContextMenu* menu = nullptr;
        hr = folder->GetUIObjectOf(state->topLevel, (UINT)chosen.size(),
                                   chosen.data(), IID_IContextMenu, nullptr,
                                   (void**)&menu);
        if (SUCCEEDED(hr) && menu) {
            HMENU hmenu = CreatePopupMenu();
            hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL);
            if (SUCCEEDED(hr)) {
                CMINVOKECOMMANDINFO invoke = {};
                invoke.cbSize = sizeof(invoke);
                invoke.hwnd = state->topLevel;
                invoke.lpVerb = "undelete";
                invoke.nShow = SW_SHOWNORMAL;
                hr = menu->InvokeCommand(&invoke);
            }
            DestroyMenu(hmenu);
            menu->Release();
        }
    }
    folder->Release();
    return hr;
}

// Takes back the last delete or rename this mod knows about. False if there
// is none.
bool UndoLast(WindowState* state) {
    if (state->undo.empty()) {
        return false;
    }
    UndoEntry entry = std::move(state->undo.back());
    state->undo.pop_back();
    HRESULT hr;
    if (entry.deletion) {
        hr = RestoreFromRecycleBin(state, entry.paths);
        Out(L"Undo delete of %d item(s): hr=0x%08lX", (int)entry.paths.size(), hr);
    } else {
        BOOL moved = MoveFileW(entry.renamedTo.c_str(), entry.renamedFrom.c_str());
        hr = moved ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        Out(L"Undo rename \"%s\" back to \"%s\": hr=0x%08lX", entry.renamedTo.c_str(),
            entry.renamedFrom.c_str(), hr);
    }
    return true;
}

void CommitRename(WindowState* state) {
    if (!state->rename || state->rename->done) {
        return;
    }
    std::wstring newName = state->rename->box.Text().c_str();
    Column* column = state->rename->column;
    size_t entryIndex = state->rename->entry;
    CancelRename(state);
    if (newName.empty() || entryIndex >= column->entries.size() ||
        newName == column->entries[entryIndex].name) {
        return;
    }
    IShellFolder* parent = nullptr;
    HRESULT hr = ILIsEmpty(column->folder.get())
                     ? SHGetDesktopFolder(&parent)
                     : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                      IID_PPV_ARGS(&parent));
    if (FAILED(hr) || !parent) {
        return;
    }
    PITEMID_CHILD renamed = nullptr;
    std::wstring oldPath = FilePathOf(column->folder.get(),
                                      column->entries[entryIndex].child.get());
    hr = parent->SetNameOf(state->topLevel, column->entries[entryIndex].child.get(),
                           newName.c_str(), SHGDN_INFOLDER, &renamed);
    Out(L"rename \"%s\" to \"%s\": hr=0x%08lX",
        column->entries[entryIndex].name.c_str(), newName.c_str(), hr);
    parent->Release();
    if (SUCCEEDED(hr) && renamed) {
        // Show the new name now, then read the folder again for the order.
        std::wstring newPath = FilePathOf(column->folder.get(), renamed);
        if (!oldPath.empty() && !newPath.empty()) {
            UndoEntry entry;
            entry.renamedFrom = oldPath;
            entry.renamedTo = newPath;
            PushUndo(state, std::move(entry));
        }
        column->entries[entryIndex].name = newName;
        column->entries[entryIndex].child.reset(renamed);
        renamed = nullptr;
        column->selectedChild.reset(ILClone(column->entries[entryIndex].child.get()));
        if (entryIndex < column->texts.size() && column->texts[entryIndex]) {
            column->texts[entryIndex].Text(
                MiddleEllipsis(newName, NameCapacity(column)));
        }
        RequestLoad(state, column);
    }
    if (renamed) {
        CoTaskMemFree(renamed);
    }
}

void BeginRename(WindowState* state, Column* column, size_t entryIndex) {
    CancelRename(state);
    bool known = false;
    for (auto& c : state->columns) {
        known = known || c.get() == column;
    }
    if (!known || entryIndex >= column->texts.size() ||
        !column->texts[entryIndex]) {
        return;
    }
    auto text = column->texts[entryIndex];
    auto grid = text.Parent().try_as<muxc::Grid>();
    if (!grid) {
        return;
    }
    const std::wstring name = column->entries[entryIndex].name;
    muxc::TextBox box;
    box.Text(name);
    box.VerticalAlignment(mux::VerticalAlignment::Center);
    box.MinHeight(0);
    box.Padding(mux::Thickness{4, 1, 4, 1});
    muxc::Grid::SetColumn(box, 1);
    muxc::Grid::SetColumnSpan(box, 2);

    auto rename = std::make_unique<RenameState>();
    rename->column = column;
    rename->entry = entryIndex;
    rename->grid = grid;
    rename->text = text;
    rename->box = box;
    rename->startedAt = GetTickCount64();
    rename->keyToken = box.KeyDown(
        [](Inspectable const&, mux::Input::KeyRoutedEventArgs const& args) {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->rename) {
                return;
            }
            using winrt::Windows::System::VirtualKey;
            try {
                if (args.Key() == VirtualKey::Enter) {
                    args.Handled(true);
                    CommitRename(state);
                } else if (args.Key() == VirtualKey::Escape) {
                    args.Handled(true);
                    CancelRename(state);
                }
            } catch (...) {
                LogHresult(L"rename key");
            }
        });
    rename->lostToken = box.LostFocus(
        [](Inspectable const&, mux::RoutedEventArgs const&) {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->rename) {
                return;
            }
            try {
                // Closing the flyout hands focus back to the list. Take it
                // again if that happens right after the box opened.
                if (GetTickCount64() - state->rename->startedAt < 400) {
                    state->rename->box.Focus(mux::FocusState::Programmatic);
                } else {
                    CommitRename(state);
                }
            } catch (...) {
                LogHresult(L"rename lost focus");
            }
        });
    text.Visibility(mux::Visibility::Collapsed);
    grid.Children().Append(box);
    state->rename = std::move(rename);
    g_renaming = true;
    box.Focus(mux::FocusState::Programmatic);
    size_t dot = name.rfind(L'.');
    bool keepExtension = !column->entries[entryIndex].folder &&
                         dot != std::wstring::npos && dot > 0;
    box.SelectionStart(0);
    box.SelectionLength((uint32_t)(keepExtension ? dot : name.size()));
    Out(L"rename started for \"%s\"", name.c_str());
}

// After a delete: rows whose file is gone are hidden at once. The reload that
// follows removes them for good, or brings them back if the delete didn't
// happen.
void HideDeletedRows(Column* column, std::vector<size_t> const& entries) {
    try {
        auto items = column->list.Items();
        for (size_t entry : entries) {
            if (entry >= column->entries.size()) {
                continue;
            }
            Pidl absolute(ILCombine(column->folder.get(),
                                    column->entries[entry].child.get()));
            wchar_t path[MAX_PATH] = L"";
            if (!absolute || !SHGetPathFromIDListW(absolute.get(), path) ||
                !path[0] || PathFileExistsW(path)) {
                continue;
            }
            for (size_t r = 0; r < column->rowToEntry.size() && r < items.Size();
                 r++) {
                if (column->rowToEntry[r] == (int)entry) {
                    if (auto item = items.GetAt((uint32_t)r)
                                        .try_as<muxc::ListViewItem>()) {
                        item.Visibility(mux::Visibility::Collapsed);
                    }
                    break;
                }
            }
        }
    } catch (...) {
        LogHresult(L"HideDeletedRows");
    }
}

// Cut and Copy from the shell's menu don't work without a view behind them,
// so the item's data object goes on the clipboard here.
bool CopyOrCut(WindowState* state, Column* column,
               std::vector<size_t> const& entries, bool cut) {
    IShellFolder* parent = nullptr;
    HRESULT hr = ILIsEmpty(column->folder.get())
                     ? SHGetDesktopFolder(&parent)
                     : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                      IID_PPV_ARGS(&parent));
    if (FAILED(hr) || !parent) {
        return false;
    }
    std::vector<PCUITEMID_CHILD> children;
    for (size_t entry : entries) {
        if (entry < column->entries.size()) {
            children.push_back(column->entries[entry].child.get());
        }
    }
    IDataObject* data = nullptr;
    hr = children.empty()
             ? E_INVALIDARG
             : parent->GetUIObjectOf(state->topLevel, (UINT)children.size(),
                                     children.data(), IID_IDataObject, nullptr,
                                     (void**)&data);
    parent->Release();
    if (FAILED(hr) || !data) {
        return false;
    }
    if (cut) {
        static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (memory) {
            if (DWORD* effect = (DWORD*)GlobalLock(memory)) {
                *effect = DROPEFFECT_MOVE;
                GlobalUnlock(memory);
            }
            FORMATETC fmt = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1,
                             TYMED_HGLOBAL};
            STGMEDIUM medium = {};
            medium.tymed = TYMED_HGLOBAL;
            medium.hGlobal = memory;
            if (FAILED(data->SetData(&fmt, &medium, TRUE))) {
                GlobalFree(memory);
            }
        }
    }
    hr = OleSetClipboard(data);
    data->Release();
    if (SUCCEEDED(hr)) {
        state->cutPaths.clear();
        state->cutFolder.reset();
        state->cutFolderPath.clear();
        if (cut) {
            wchar_t folderPath[MAX_PATH] = L"";
            if (SHGetPathFromIDListW(column->folder.get(), folderPath)) {
                state->cutFolderPath = folderPath;
            }
            state->cutFolder.reset(ILClone(column->folder.get()));
            for (PCUITEMID_CHILD child : children) {
                std::wstring path = FilePathOf(column->folder.get(), child);
                if (!path.empty()) {
                    state->cutPaths.push_back(path);
                }
            }
        }
        state->cutSeq = GetClipboardSequenceNumber();
        ApplyCutState(state);
    }
    Out(L"%s %d item(s): hr=0x%08lX", cut ? L"cut" : L"copy",
        (int)children.size(), hr);
    return SUCCEEDED(hr);
}

// Lets go of the shell menu and every handler on the flyout's items.
void CloseContextMenu(WindowState* state) {
    if (!state->context) {
        return;
    }
    auto context = std::move(state->context);
    try {
        for (auto& hook : context->hooks) {
            if (hook.first) {
                hook.first.Click(hook.second);
            }
        }
        if (context->flyout) {
            context->flyout.Closed(context->closedToken);
            context->flyout.Hide();
        }
    } catch (...) {
        LogHresult(L"CloseContextMenu");
    }
    context->hooks.clear();
    context->flyout = nullptr;
    if (context->hmenu) {
        DestroyMenu(context->hmenu);
    }
    if (context->menu) {
        context->menu->Release();
    }
}

std::wstring MenuText(HMENU menu, UINT position, MENUITEMINFOW* info) {
    wchar_t text[256] = L"";
    *info = {};
    info->cbSize = sizeof(*info);
    info->fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID | MIIM_STATE |
                  MIIM_SUBMENU;
    info->dwTypeData = text;
    info->cch = ARRAYSIZE(text) - 1;
    if (!GetMenuItemInfoW(menu, position, TRUE, info)) {
        return L"";
    }
    // Without the & accelerator marks and the shortcut text after the tab.
    std::wstring result;
    for (const wchar_t* p = text; *p && *p != L'\t'; p++) {
        if (*p == L'&') {
            if (p[1] == L'&') {
                result += L'&';
                p++;
            }
            continue;
        }
        result += *p;
    }
    return result;
}

void FillFlyoutItems(WindowState* state, HMENU menu,
                     winrt::Windows::Foundation::Collections::IVector<
                         muxc::MenuFlyoutItemBase> const& items,
                     int depth) {
    int count = GetMenuItemCount(menu);
    bool needSeparator = false;
    for (int i = 0; i < count && i < 80; i++) {
        MENUITEMINFOW info;
        std::wstring text = MenuText(menu, i, &info);
        if (info.fType & MFT_SEPARATOR) {
            needSeparator = items.Size() > 0;
            continue;
        }
        if (text.empty()) {
            continue;
        }
        if (needSeparator) {
            items.Append(muxc::MenuFlyoutSeparator());
            needSeparator = false;
        }
        if (info.hSubMenu) {
            if (depth >= 2) {
                continue;
            }
            muxc::MenuFlyoutSubItem sub;
            sub.Text(text);
            FillFlyoutItems(state, info.hSubMenu, sub.Items(), depth + 1);
            if (sub.Items().Size() > 0) {
                items.Append(sub);
            }
            continue;
        }
        muxc::MenuFlyoutItem item;
        item.Text(text);
        item.IsEnabled(!(info.fState & (MFS_DISABLED | MFS_GRAYED)));
        item.Tag(winrt::box_value((uint32_t)info.wID));
        auto token = item.Click(
            [](Inspectable const& sender, mux::RoutedEventArgs const&) {
                WindowState* state = StateForThisThread(false);
                if (!state || !state->context || g_unloading) {
                    return;
                }
                try {
                    auto item = sender.try_as<muxc::MenuFlyoutItem>();
                    if (!item) {
                        return;
                    }
                    UINT id = winrt::unbox_value<uint32_t>(item.Tag());
                    ContextMenuState* context = state->context.get();
                    // For an undo of a delete: the paths, before they are gone.
                    std::vector<std::wstring> pathsBefore;
                    for (size_t entry : context->entries) {
                        if (context->column && entry < context->column->entries.size()) {
                            pathsBefore.push_back(FilePathOf(
                                context->column->folder.get(),
                                context->column->entries[entry].child.get()));
                        }
                    }
                    wchar_t verb[64] = L"";
                    context->menu->GetCommandString(
                        id - context->idFirst, GCS_VERBW, nullptr,
                        (char*)verb, ARRAYSIZE(verb));
                    Out(L"context menu verb \"%s\"", verb);
                    if (!_wcsicmp(verb, L"copy") || !_wcsicmp(verb, L"cut")) {
                        CopyOrCut(state, context->column, context->entries,
                                  !_wcsicmp(verb, L"cut"));
                        return;
                    }
                    if (!_wcsicmp(verb, L"rename") && state->queue &&
                        context->entries.size() == 1) {
                        Column* column = context->column;
                        size_t entry = context->entries[0];
                        // After the flyout has closed and given focus back.
                        state->queue.TryEnqueue([column, entry]() {
                            WindowState* state = StateForThisThread(false);
                            if (state && state->on && !g_unloading) {
                                try {
                                    BeginRename(state, column, entry);
                                } catch (...) {
                                    LogHresult(L"BeginRename");
                                }
                            }
                        });
                        return;
                    }
                    CMINVOKECOMMANDINFOEX invoke = {};
                    invoke.cbSize = sizeof(invoke);
                    invoke.hwnd = context->owner;
                    invoke.lpVerb = MAKEINTRESOURCEA(id - context->idFirst);
                    invoke.lpVerbW = MAKEINTRESOURCEW(id - context->idFirst);
                    invoke.nShow = SW_SHOWNORMAL;
                    invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                    GetCursorPos(&invoke.ptInvoke);
                    HRESULT hr = context->menu->InvokeCommand(
                        (CMINVOKECOMMANDINFO*)&invoke);
                    Out(L"context menu command %u (%s) -> hr=0x%08lX", id, verb, hr);
                    // The command may have changed the folder. Read it now
                    // instead of waiting for the change notification.
                    if (!_wcsicmp(verb, L"delete")) {
                        HideDeletedRows(context->column, context->entries);
                        if (SUCCEEDED(hr) && !(GetKeyState(VK_SHIFT) & 0x8000)) {
                            RememberDeletion(state, pathsBefore);
                        }
                    }
                    ReloadAll(state);
                } catch (...) {
                    LogHresult(L"running a context menu command");
                }
            });
        state->context->hooks.emplace_back(item, token);
        items.Append(item);
    }
}

HRESULT InvokeVerbOnFolder(WindowState* state, PCIDLIST_ABSOLUTE folder,
                           const char* verb);
HRESULT PasteIntoFolder(WindowState* state, PCIDLIST_ABSOLUTE folder);
void BeginRename(WindowState* state, Column* column, size_t entryIndex);

// Is there something to paste? Files or folders on the clipboard.
bool ClipboardHasFiles() {
    IDataObject* data = nullptr;
    if (FAILED(OleGetClipboard(&data)) || !data) {
        return false;
    }
    static UINT idList = RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
    FORMATETC list = {(CLIPFORMAT)idList, nullptr, DVASPECT_CONTENT, -1,
                      TYMED_HGLOBAL};
    FORMATETC drop = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    bool has = data->QueryGetData(&list) == S_OK ||
               data->QueryGetData(&drop) == S_OK;
    data->Release();
    return has;
}

// A new folder in a column's folder, named like Explorer names it. Only for
// folders on disk. Then the column reloads and the name is put in edit mode.
void CreateNewFolder(WindowState* state, Column* column) {
    wchar_t path[MAX_PATH] = L"";
    if (!SHGetPathFromIDListW(column->folder.get(), path) || !path[0]) {
        Out(L"new folder: this folder isn't on disk");
        return;
    }
    std::wstring name = L"New folder";
    for (int i = 1; i < 100; i++) {
        std::wstring candidate =
            i == 1 ? name : name + L" (" + std::to_wstring(i) + L")";
        std::wstring full = std::wstring(path) + L"\\" + candidate;
        if (!PathFileExistsW(full.c_str())) {
            if (CreateDirectoryW(full.c_str(), nullptr)) {
                state->renameName = candidate;
                state->renameColumn = column;
                RequestLoad(state, column);
                Out(L"new folder \"%s\" created", candidate.c_str());
            } else {
                Out(L"new folder: CreateDirectory failed, gle=%lu",
                    GetLastError());
            }
            return;
        }
    }
}

void RunBackgroundAction(WindowState* state, Column* column,
                         const std::wstring& action) {
    bool known = false;
    for (auto& c : state->columns) {
        known = known || c.get() == column;
    }
    if (!known) {
        return;
    }
    if (action == L"paste") {
        HRESULT hr = PasteIntoFolder(state, column->folder.get());
        Out(L"background Paste: hr=0x%08lX", hr);
    } else if (action == L"newfolder") {
        CreateNewFolder(state, column);
    } else if (action == L"refresh") {
        RequestLoad(state, column);
    } else if (action == L"properties") {
        HRESULT hr = InvokeVerbOnFolder(state, column->folder.get(), "properties");
        Out(L"background Properties: hr=0x%08lX", hr);
    }
}

// Right-click on a column's empty space.
void ShowBackgroundMenu(WindowState* state, Column* column,
                        muxc::ListView const& list,
                        mux::Input::RightTappedRoutedEventArgs const& args) {
    CloseContextMenu(state);
    state->context = std::make_unique<ContextMenuState>();
    state->context->owner = state->topLevel;
    state->context->column = column;

    muxc::MenuFlyout flyout;
    struct Item {
        const wchar_t* id;
        const wchar_t* text;
        bool enabled;
    };
    // The Desktop's properties are the Personalization page of Settings.
    bool desktop = ILIsEmpty(column->folder.get());
    if (!desktop) {
        PIDLIST_ABSOLUTE known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, nullptr,
                                             &known)) &&
            known) {
            desktop = ILIsEqual(known, column->folder.get());
            CoTaskMemFree(known);
        }
    }
    Item items[] = {
        {L"paste", L"Paste", ClipboardHasFiles()},
        {L"newfolder", L"New folder", true},
        {nullptr, nullptr, false},
        {L"refresh", L"Refresh", true},
        {L"properties", desktop ? L"Personalize" : L"Folder properties", true},
    };
    for (auto& item : items) {
        if (!item.id) {
            flyout.Items().Append(muxc::MenuFlyoutSeparator());
            continue;
        }
        muxc::MenuFlyoutItem entry;
        entry.Text(item.text);
        entry.IsEnabled(item.enabled);
        entry.Tag(winrt::box_value(winrt::hstring{item.id}));
        auto token = entry.Click(
            [](Inspectable const& sender, mux::RoutedEventArgs const&) {
                WindowState* state = StateForThisThread(false);
                if (!state || !state->context || g_unloading) {
                    return;
                }
                try {
                    auto item = sender.try_as<muxc::MenuFlyoutItem>();
                    if (!item) {
                        return;
                    }
                    std::wstring action =
                        winrt::unbox_value<winrt::hstring>(item.Tag()).c_str();
                    Column* column = state->context->column;
                    RunBackgroundAction(state, column, action);
                } catch (...) {
                    LogHresult(L"background menu");
                }
            });
        state->context->hooks.emplace_back(entry, token);
        flyout.Items().Append(entry);
    }
    state->context->flyout = flyout;
    state->context->closedToken = flyout.Closed(
        [](Inspectable const&, Inspectable const&) {
            WindowState* state = StateForThisThread(false);
            if (!state || g_unloading || !state->queue) {
                return;
            }
            try {
                state->queue.TryEnqueue([]() {
                    WindowState* state = StateForThisThread(false);
                    if (state && !g_unloading) {
                        CloseContextMenu(state);
                    }
                });
            } catch (...) {
            }
        });
    muxc::Primitives::FlyoutShowOptions options;
    options.Position(args.GetPosition(list));
    flyout.ShowAt(list, options);
    Out(L"background menu shown for \"%s\"", PathOf(column->folder.get()).c_str());
}

void OnRowRightTapped(muxc::ListView const& list,
                      mux::Input::RightTappedRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        int columnIndex = ColumnIndexOfList(state, list);
        if (columnIndex < 0) {
            return;
        }
        Column* column = state->columns[columnIndex].get();

        // The row under the pointer: climb from what was hit to its item.
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        muxc::ListViewItem row{nullptr};
        for (int i = 0; node && i < 20; i++) {
            if (auto found = node.try_as<muxc::ListViewItem>()) {
                row = found;
                break;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
        if (!row) {
            // Empty space: the column's own menu, and the column becomes the
            // active one so Paste and the panes act on its folder.
            args.Handled(true);
            state->building = true;
            list.SelectedItems().Clear();
            state->building = false;
            column->selectedChild.reset();
            SetActiveColumn(state, columnIndex);
            ScheduleNavigation(state, column->folder.get());
            ShowBackgroundMenu(state, column, list, args);
            return;
        }
        int rowIndex = list.IndexFromContainer(row);
        int entryIndex = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size()
                             ? column->rowToEntry[rowIndex]
                             : -1;
        if (entryIndex < 0) {
            return;
        }
        args.Handled(true);
        // Right-click inside a selection acts on all of it. Outside, it
        // selects the item under the pointer first.
        std::vector<size_t> picked;
        if (row.IsSelected()) {
            picked = SelectedEntries(column, list);
        }
        if (picked.empty()) {
            list.SelectedIndex(rowIndex);
            picked.push_back((size_t)entryIndex);
        }

        CloseContextMenu(state);
        IShellFolder* parent = nullptr;
        HRESULT hr = ILIsEmpty(column->folder.get())
                         ? SHGetDesktopFolder(&parent)
                         : SHBindToObject(nullptr, column->folder.get(),
                                          nullptr, IID_PPV_ARGS(&parent));
        if (FAILED(hr) || !parent) {
            Out(L"context menu: can't bind the folder, hr=0x%08lX", hr);
            return;
        }
        std::vector<PCUITEMID_CHILD> children;
        for (size_t entry : picked) {
            children.push_back(column->entries[entry].child.get());
        }
        IContextMenu* menu = nullptr;
        hr = parent->GetUIObjectOf(state->topLevel, (UINT)children.size(),
                                   children.data(), IID_IContextMenu, nullptr,
                                   (void**)&menu);
        parent->Release();
        if (FAILED(hr) || !menu) {
            Out(L"context menu: no IContextMenu, hr=0x%08lX", hr);
            return;
        }
        HMENU hmenu = CreatePopupMenu();
        hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF,
                                    CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME);
        if (FAILED(hr)) {
            DestroyMenu(hmenu);
            menu->Release();
            Out(L"context menu: QueryContextMenu hr=0x%08lX", hr);
            return;
        }

        state->context = std::make_unique<ContextMenuState>();
        state->context->menu = menu;
        state->context->hmenu = hmenu;
        state->context->owner = state->topLevel;
        state->context->column = column;
        state->context->entries = picked;
        muxc::MenuFlyout flyout;
        FillFlyoutItems(state, hmenu, flyout.Items(), 0);
        state->context->flyout = flyout;
        state->context->closedToken = flyout.Closed(
            [](Inspectable const&, Inspectable const&) {
                WindowState* state = StateForThisThread(false);
                if (!state || g_unloading || !state->queue) {
                    return;
                }
                // After the click has been handled.
                try {
                    state->queue.TryEnqueue([]() {
                        WindowState* state = StateForThisThread(false);
                        if (state && !g_unloading) {
                            CloseContextMenu(state);
                        }
                    });
                } catch (...) {
                }
            });
        muxc::Primitives::FlyoutShowOptions options;
        options.Position(args.GetPosition(list));
        flyout.ShowAt(list, options);
        Out(L"context menu shown for %d item(s), first \"%s\", %u entries",
            (int)picked.size(), column->entries[entryIndex].name.c_str(),
            flyout.Items().Size());
    } catch (...) {
        LogHresult(L"OnRowRightTapped");
        if (state) {
            CloseContextMenu(state);
        }
    }
}

void OnRowDoubleTapped(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    state->slowRenameAt = 0;
    state->slowRenameBlockUntil = GetTickCount64() + 600;
    try {
        int columnIndex = ColumnIndexOfList(state, list);
        int row = list.SelectedIndex();
        if (columnIndex < 0 || row < 0) {
            return;
        }
        Column* column = state->columns[columnIndex].get();
        int entryIndex =
            row < (int)column->rowToEntry.size() ? column->rowToEntry[row] : -1;
        if (entryIndex < 0 || column->entries[entryIndex].folder) {
            return;
        }
        Pidl absolute(ILCombine(column->folder.get(),
                                column->entries[entryIndex].child.get()));
        if (absolute) {
            Out(L"open \"%s\"", column->entries[entryIndex].name.c_str());
            OpenItem(state->topLevel, absolute.get());
        }
    } catch (...) {
        LogHresult(L"OnRowDoubleTapped");
    }
}

// Runs a shell verb such as "delete" or "properties" on items of a column.
HRESULT InvokeVerbOnItems(WindowState* state, Column* column,
                          std::vector<size_t> const& entries,
                          const char* verb) {
    IShellFolder* parent = nullptr;
    HRESULT hr = ILIsEmpty(column->folder.get())
                     ? SHGetDesktopFolder(&parent)
                     : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                      IID_PPV_ARGS(&parent));
    if (FAILED(hr) || !parent) {
        return hr;
    }
    std::vector<PCUITEMID_CHILD> children;
    for (size_t entry : entries) {
        if (entry < column->entries.size()) {
            children.push_back(column->entries[entry].child.get());
        }
    }
    IContextMenu* menu = nullptr;
    hr = children.empty()
             ? E_INVALIDARG
             : parent->GetUIObjectOf(state->topLevel, (UINT)children.size(),
                                     children.data(), IID_IContextMenu, nullptr,
                                     (void**)&menu);
    parent->Release();
    if (FAILED(hr) || !menu) {
        return hr;
    }
    HMENU hmenu = CreatePopupMenu();
    hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF,
                                CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME);
    if (SUCCEEDED(hr)) {
        CMINVOKECOMMANDINFO invoke = {};
        invoke.cbSize = sizeof(invoke);
        invoke.hwnd = state->topLevel;
        invoke.lpVerb = verb;
        invoke.nShow = SW_SHOWNORMAL;
        // Shift+Delete deletes for good, with its confirmation.
        if (GetKeyState(VK_SHIFT) & 0x8000) {
            invoke.fMask |= CMIC_MASK_SHIFT_DOWN;
        }
        hr = menu->InvokeCommand(&invoke);
    }
    DestroyMenu(hmenu);
    menu->Release();
    return hr;
}

// Paste into a folder, through that folder's item in its
// parent, which is the route the right-click menu's Paste took.
HRESULT InvokeVerbOnFolder(WindowState* state, PCIDLIST_ABSOLUTE folder,
                           const char* verb) {
    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD child = nullptr;
    HRESULT hr = SHBindToParent(folder, IID_PPV_ARGS(&parent),
                                &child);
    if (FAILED(hr) || !parent) {
        return hr;
    }
    IContextMenu* menu = nullptr;
    hr = parent->GetUIObjectOf(state->topLevel, 1, &child, IID_IContextMenu,
                               nullptr, (void**)&menu);
    parent->Release();
    if (FAILED(hr) || !menu) {
        return hr;
    }
    HMENU hmenu = CreatePopupMenu();
    hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE);
    if (SUCCEEDED(hr)) {
        CMINVOKECOMMANDINFO invoke = {};
        invoke.cbSize = sizeof(invoke);
        invoke.hwnd = state->topLevel;
        invoke.lpVerb = verb;
        invoke.nShow = SW_SHOWNORMAL;
        hr = menu->InvokeCommand(&invoke);
    }
    DestroyMenu(hmenu);
    menu->Release();
    return hr;
}

// Paste the clipboard into a folder the way a drop does, through the folder's
// own drop target. The "paste" verb of the context menu made Explorer's view
// jump to another folder afterwards.
HRESULT PasteIntoFolder(WindowState* state, PCIDLIST_ABSOLUTE folder) {
    // In case Explorer's view jumps anyway, remember where it was.
    state->pasteReturn.reset();
    if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
        Pidl now = CurrentFolder(browser);
        if (now) {
            state->pasteReturn.reset(ILClone(now.get()));
        }
    }
    state->pasteGuardUntil = GetTickCount64() + 3000;

    IShellFolder* target = nullptr;
    HRESULT hr = ILIsEmpty(folder)
                     ? SHGetDesktopFolder(&target)
                     : SHBindToObject(nullptr, folder, nullptr,
                                      IID_PPV_ARGS(&target));
    if (FAILED(hr) || !target) {
        return hr;
    }
    IDropTarget* drop = nullptr;
    hr = target->CreateViewObject(state->topLevel, IID_PPV_ARGS(&drop));
    target->Release();
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && drop) {
        hr = OleGetClipboard(&data);
    }
    if (FAILED(hr) || !drop || !data) {
        if (drop) {
            drop->Release();
        }
        if (data) {
            data->Release();
        }
        Out(L"paste: no drop target or clipboard (hr=0x%08lX), using the verb", hr);
        return InvokeVerbOnFolder(state, folder, "paste");
    }

    // Cut items move, copied items copy.
    DWORD effect = DROPEFFECT_COPY;
    static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    FORMATETC wanted = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1,
                        TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    if (data->GetData(&wanted, &medium) == S_OK) {
        if (medium.hGlobal) {
            if (DWORD* value = (DWORD*)GlobalLock(medium.hGlobal)) {
                if (*value & DROPEFFECT_MOVE) {
                    effect = DROPEFFECT_MOVE;
                }
                GlobalUnlock(medium.hGlobal);
            }
        }
        ReleaseStgMedium(&medium);
    }
    POINTL where = {0, 0};
    DWORD allowed = effect;
    hr = drop->DragEnter(data, MK_LBUTTON, where, &allowed);
    if (SUCCEEDED(hr) && allowed != DROPEFFECT_NONE) {
        hr = drop->Drop(data, 0, where, &allowed);
        if (SUCCEEDED(hr) && effect == DROPEFFECT_MOVE) {
            OleSetClipboard(nullptr);
        }
    } else {
        drop->DragLeave();
    }
    Out(L"paste as %s: hr=0x%08lX", effect == DROPEFFECT_MOVE ? L"move" : L"copy",
        hr);
    data->Release();
    drop->Release();
    ReloadAll(state);  // The target, and the source of a move.
    return hr;
}

// A click on the empty space of a column makes it the active one: Explorer's
// own view moves to its folder, so Paste and the panes act there. The columns
// to its right stay.
// The row of the group header at a point of the list, or -1.
int HeaderRowAt(Column* column, muxc::ListView const& list,
                winrt::Windows::Foundation::Point at) {
    auto items = list.Items();
    for (size_t r = 0; r < column->rowGroup.size() && r < items.Size(); r++) {
        if (column->rowToEntry[r] >= 0 || column->rowGroup[r] < 0) {
            continue;
        }
        auto header = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>();
        if (!header) {
            continue;
        }
        auto bounds = header.TransformToVisual(list).TransformBounds(
            winrt::Windows::Foundation::Rect{0, 0, (float)header.ActualWidth(),
                                              (float)header.ActualHeight()});
        if (at.X >= bounds.X && at.X <= bounds.X + bounds.Width &&
            at.Y >= bounds.Y && at.Y <= bounds.Y + bounds.Height) {
            return (int)r;
        }
    }
    return -1;
}

// Hover on the group headers: the one under the pointer is tinted.
void SetHeaderHover(Column* column, muxc::ListView const& list, int row) {
    if (row == column->hoverHeader) {
        return;
    }
    auto items = list.Items();
    auto paint = [&](int r, bool on) {
        if (r >= 0 && r < (int)items.Size()) {
            if (auto header = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
                header.Background(on ? Brush(64, 150, 150, 150) : Brush(0, 0, 0, 0));
                header.Opacity(on ? 1.0 : 0.92);
            }
        }
    };
    paint(column->hoverHeader, false);
    column->hoverHeader = row;
    paint(row, true);
}

muxc::ToolTip BuildDetailTip(Column* column, size_t entryIndex);
bool OnBandMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args);

void OnColumnPointerMoved(muxc::ListView const& list,
                          mux::Input::PointerRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        Column* column = state->columns[index].get();
        if (OnBandMoved(state, column, list, args)) {
            return;  // A rectangle is being drawn.
        }
        if (column->rowGroup.empty()) {
            return;
        }
        SetHeaderHover(column, list, HeaderRowAt(column, list,
                                                 args.GetCurrentPoint(list).Position()));

        // The first time the pointer is on a row: its detail tooltip.
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        for (int i = 0; node && i < 20; i++) {
            if (auto row = node.try_as<muxc::ListViewItem>()) {
                int rowIndex = list.IndexFromContainer(row);
                int entry = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size()
                                ? column->rowToEntry[rowIndex]
                                : -1;
                if (entry >= 0 && (size_t)entry < column->tipBuilt.size() &&
                    !column->tipBuilt[entry]) {
                    column->tipBuilt[entry] = 1;
                    muxc::ToolTipService::SetToolTip(
                        row, BuildDetailTip(column, (size_t)entry));
                }
                break;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
    } catch (...) {
        LogHresult(L"header hover");
    }
}

void OnColumnPointerExited(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        int index = ColumnIndexOfList(state, list);
        if (index >= 0) {
            SetHeaderHover(state->columns[index].get(), list, -1);
        }
    } catch (...) {
    }
}

// The detail tooltip of a row: the name, then what the shell knows about the
// item, such as its type, size, dates, picture size or length. The labels and
// the values are the shell's own, as the Details view shows them. Read the first
// time the pointer is over the row, not for every row up front.
muxc::ToolTip BuildDetailTip(Column* column, size_t entryIndex) {
    const Entry& entry = column->entries[entryIndex];
    struct Line {
        std::wstring label;
        std::wstring value;
    };
    std::vector<Line> lines;

    IShellFolder* folder = nullptr;
    HRESULT hr = ILIsEmpty(column->folder.get())
                     ? SHGetDesktopFolder(&folder)
                     : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                      IID_PPV_ARGS(&folder));
    IShellFolder2* folder2 = nullptr;
    if (SUCCEEDED(hr) && folder) {
        folder->QueryInterface(IID_PPV_ARGS(&folder2));
    }
    if (folder2) {
        // One pass over the folder's columns finds the number of each property.
        const PROPERTYKEY* wanted[] = {
            &PKEY_ItemTypeText,    &PKEY_Size,         &PKEY_DateModified,
            &PKEY_Image_Dimensions, &PKEY_Media_Duration, &PKEY_Title,
            &PKEY_Author,          &PKEY_Music_Artist, &PKEY_Music_AlbumTitle,
            &PKEY_Keywords,        &PKEY_Comment};
        const int count = ARRAYSIZE(wanted);
        int columnOf[ARRAYSIZE(wanted)];
        for (int& c : columnOf) {
            c = -1;
        }
        int misses = 0;
        for (UINT i = 0; i < 600 && misses < 30; i++) {
            SHCOLUMNID id{};
            if (FAILED(folder2->MapColumnToSCID(i, &id))) {
                misses++;
                continue;
            }
            misses = 0;
            for (int k = 0; k < count; k++) {
                if (columnOf[k] < 0 && IsEqualPropertyKey(id, *wanted[k])) {
                    columnOf[k] = (int)i;
                }
            }
        }
        PCUITEMID_CHILD child = entry.child.get();
        for (int k = 0; k < count && lines.size() < 8; k++) {
            if (columnOf[k] < 0) {
                continue;
            }
            SHELLDETAILS head{}, value{};
            wchar_t headText[128] = L"", valueText[512] = L"";
            if (SUCCEEDED(folder2->GetDetailsOf(nullptr, (UINT)columnOf[k], &head)) &&
                SUCCEEDED(StrRetToBufW(&head.str, nullptr, headText, ARRAYSIZE(headText))) &&
                SUCCEEDED(folder2->GetDetailsOf(child, (UINT)columnOf[k], &value)) &&
                SUCCEEDED(StrRetToBufW(&value.str, child, valueText, ARRAYSIZE(valueText))) &&
                headText[0] && valueText[0]) {
                lines.push_back({headText, valueText});
            }
        }
        folder2->Release();
    }
    if (folder) {
        folder->Release();
    }

    muxc::StackPanel panel;
    muxc::TextBlock name;
    name.Text(entry.name);
    name.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    name.TextWrapping(mux::TextWrapping::Wrap);
    name.MaxWidth(323);
    panel.Children().Append(name);
    if (!lines.empty()) {
        muxc::Grid grid;
        grid.Margin(mux::Thickness{0, 6, 0, 0});
        muxc::ColumnDefinition labels, values;
        labels.Width(mux::GridLength{1, mux::GridUnitType::Auto});
        values.Width(mux::GridLength{1, mux::GridUnitType::Auto});
        grid.ColumnDefinitions().Append(labels);
        grid.ColumnDefinitions().Append(values);
        for (size_t i = 0; i < lines.size(); i++) {
            muxc::RowDefinition row;
            row.Height(mux::GridLength{1, mux::GridUnitType::Auto});
            grid.RowDefinitions().Append(row);
            muxc::TextBlock label;
            label.Text(lines[i].label);
            label.Opacity(0.65);
            label.FontSize(12);
            label.Margin(mux::Thickness{0, 1, 12, 1});
            muxc::Grid::SetRow(label, (int)i);
            muxc::TextBlock value;
            value.Text(lines[i].value);
            value.FontSize(12);
            value.Margin(mux::Thickness{0, 1, 0, 1});
            value.TextWrapping(mux::TextWrapping::Wrap);
            value.MaxWidth(221);
            muxc::Grid::SetRow(value, (int)i);
            muxc::Grid::SetColumn(value, 1);
            grid.Children().Append(label);
            grid.Children().Append(value);
        }
        panel.Children().Append(grid);
    }
    muxc::ToolTip tip;
    tip.Content(panel);
    return tip;
}

// ---- Selection by a rectangle ----

muxc::ScrollViewer FindListScroller(mux::DependencyObject const& node, int depth = 0) {
    if (!node || depth > 8) {
        return nullptr;
    }
    if (auto viewer = node.try_as<muxc::ScrollViewer>()) {
        return viewer;
    }
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        if (auto found = FindListScroller(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1)) {
            return found;
        }
    }
    return nullptr;
}

// Where each row sits in the list's scrolled content. Computed from the row
// heights and corrected by a row that is on screen, since rows off screen may
// not have been laid out.
void MeasureRows(Column* column, muxc::ListView const& list, double offset,
                 std::vector<double>* top, std::vector<double>* bottom) {
    auto items = list.Items();
    size_t count = std::min<size_t>(items.Size(), column->rowToEntry.size());
    std::vector<double> cumulative(count + 1, 0.0);
    std::vector<char> shown(count, 0);
    for (size_t r = 0; r < count; r++) {
        double height = 0;
        if (auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
            if (item.Visibility() == mux::Visibility::Visible) {
                shown[r] = 1;
                height = column->rowToEntry[r] >= 0 ? column->rowHeight
                         : column->rowGroup[r] >= 0 ? column->rowHeight + 4
                                                    : column->rowHeight;
            }
        }
        cumulative[r + 1] = cumulative[r] + height;
    }
    double base = 12;  // The list's top padding, if no row can be measured.
    for (size_t r = 0; r < count; r++) {
        auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>();
        if (item && shown[r] && item.ActualHeight() > 0) {
            double y = item.TransformToVisual(list)
                           .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                           .Y;
            base = y + offset - cumulative[r];
            break;
        }
    }
    top->assign(count, 0);
    bottom->assign(count, 0);
    for (size_t r = 0; r < count; r++) {
        (*top)[r] = shown[r] ? base + cumulative[r] : -1;
        (*bottom)[r] = shown[r] ? base + cumulative[r + 1] : -1;
    }
}

void EndBand(WindowState* state, bool apply) {
    auto& band = state->band;
    bool wasActive = band.active;
    Column* column = band.column;
    band.pending = false;
    band.active = false;
    band.column = nullptr;
    band.top.clear();
    band.bottom.clear();
    band.base.clear();
    if (state->bandBox) {
        state->bandBox.Opacity(0);
    }
    if (wasActive && apply) {
        for (size_t i = 0; i < state->columns.size(); i++) {
            if (state->columns[i].get() == column) {
                // Now that the selection is settled, tell the rest: Explorer's
                // view, the stale look, the new column.
                column->list.ReleasePointerCaptures();
                OnRowSelected(column->list);
                break;
            }
        }
    }
}

void OnBandPressed(muxc::ListView const& list, mux::Input::PointerRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading || state->rename) {
        return;
    }
    try {
        auto point = args.GetCurrentPoint(list);
        if (!point.Properties().IsLeftButtonPressed()) {
            return;
        }
        // On a row, the list selects as usual. Empty space, the gutter and
        // group headers start a rectangle.
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        for (int i = 0; node && i < 20; i++) {
            if (node.try_as<muxc::ListViewItem>()) {
                return;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        Column* column = state->columns[index].get();
        auto scroller = FindListScroller(list);
        auto& band = state->band;
        band.pending = true;
        band.active = false;
        band.additive = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        band.column = column;
        band.start = point.Position();
        band.startOffset = scroller ? scroller.VerticalOffset() : 0;
        band.first = band.last = -1;
    } catch (...) {
        LogHresult(L"OnBandPressed");
    }
}

// Returns true while a rectangle is being drawn, so the hover code stands aside.
bool OnBandMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args) {
    auto& band = state->band;
    if (!band.pending && !band.active) {
        return false;
    }
    if (band.column != column) {
        return band.active;
    }
    auto point = args.GetCurrentPoint(list);
    if (!point.Properties().IsLeftButtonPressed()) {
        EndBand(state, band.active);
        return false;
    }
    auto at = point.Position();
    if (band.pending) {
        double moved = std::abs(at.X - band.start.X) + std::abs(at.Y - band.start.Y);
        if (moved < 6) {
            return false;  // Still a click.
        }
        // A drag: take the pointer, remember the rows, start from the selection
        // the user wants to keep.
        band.pending = false;
        band.active = true;
        list.CapturePointer(args.Pointer());
        auto scroller = FindListScroller(list);
        MeasureRows(column, list, scroller ? scroller.VerticalOffset() : 0, &band.top, &band.bottom);
        band.base.clear();
        auto items = list.Items();
        for (uint32_t r = 0; r < items.Size(); r++) {
            auto item = items.GetAt(r).try_as<muxc::ListViewItem>();
            if (band.additive && item && item.IsSelected()) {
                band.base.push_back((int)r);
            }
        }
        if (!band.additive) {
            bool wasBuilding = state->building;
            state->building = true;
            list.SelectedItems().Clear();
            state->building = wasBuilding;
        }
        LiftStale(state, ColumnIndexOfList(state, list));
        SetActiveColumn(state, ColumnIndexOfList(state, list));
        Out(L"selection rectangle started in column %d", ColumnIndexOfList(state, list) + 1);
    }

    auto scroller = FindListScroller(list);
    double offset = scroller ? scroller.VerticalOffset() : 0;
    double view = scroller ? scroller.ViewportHeight() : list.ActualHeight();
    // Near the top or bottom edge the list scrolls.
    if (scroller) {
        if (at.Y < 24 && offset > 0) {
            scroller.ChangeView(nullptr, std::max(0.0, offset - 22), nullptr, true);
        } else if (at.Y > view - 24) {
            scroller.ChangeView(nullptr, offset + 22, nullptr, true);
        }
    }
    // The rectangle in content coordinates, and the rows it touches.
    double startY = band.start.Y + band.startOffset;
    double nowY = at.Y + offset;
    double yMin = std::min(startY, nowY), yMax = std::max(startY, nowY);
    int first = -1, last = -1;
    for (size_t r = 0; r < band.top.size(); r++) {
        if (band.top[r] < 0 || band.bottom[r] <= yMin || band.top[r] >= yMax) {
            continue;
        }
        if (column->rowToEntry[r] < 0) {
            continue;  // Headers and the "more items" row are not selected.
        }
        if (first < 0) {
            first = (int)r;
        }
        last = (int)r;
    }
    if (first != band.first || last != band.last) {
        bool wasBuilding = state->building;
        state->building = true;
        auto items = list.Items();
        // Between the first and last covered row, the rows that are items.
        auto selected = list.SelectedItems();
        if (!band.additive) {
            selected.Clear();
        } else {
            selected.Clear();
            for (int r : band.base) {
                list.SelectRange(winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(r, 1));
            }
        }
        if (first >= 0) {
            list.SelectRange(winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(
                first, (uint32_t)(last - first + 1)));
            // Headers inside the range are dropped again.
            for (int r = first; r <= last; r++) {
                if (column->rowToEntry[r] < 0 || items.GetAt(r).try_as<muxc::ListViewItem>().Visibility() !=
                                                      mux::Visibility::Visible) {
                    list.DeselectRange(winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(r, 1));
                }
            }
        }
        state->building = wasBuilding;
        band.first = first;
        band.last = last;
    }

    // The rectangle on screen, in the strip's coordinates.
    try {
        double width = list.ActualWidth(), height = list.ActualHeight();
        double startX = band.start.X, nowX = at.X;
        double x0 = std::max(0.0, std::min(startX, nowX));
        double x1 = std::min(width, std::max(startX, nowX));
        double y0 = std::max(0.0, std::min(startY - offset, (double)at.Y));
        double y1 = std::min(height, std::max(startY - offset, (double)at.Y));
        auto origin = list.TransformToVisual(state->strip)
                          .TransformPoint(winrt::Windows::Foundation::Point{(float)x0, (float)y0});
        muxc::Canvas::SetLeft(state->bandBox, origin.X);
        muxc::Canvas::SetTop(state->bandBox, origin.Y);
        state->bandBox.Width(std::max(1.0, x1 - x0));
        state->bandBox.Height(std::max(1.0, y1 - y0));
        state->bandBox.Opacity(1);
    } catch (...) {
        LogHresult(L"drawing the selection rectangle");
    }
    return true;
}

void OnBandReleased(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        if (state->band.pending || state->band.active) {
            EndBand(state, state->band.active);
        }
    } catch (...) {
        state->band = {};
        LogHresult(L"OnBandReleased");
    }
}

// Opens or collapses the group of a header row.
// The end of an opening or closing group: closed rows are taken out of the
// layout, and every row gets its normal height back.
void FinishGroupAnimation(Column* column) {
    if (column->animGroup < 0) {
        return;
    }
    int group = column->animGroup;
    bool collapse = column->animCollapse;
    column->animGroup = -1;
    column->animDoneAt = 0;
    try {
        auto items = column->list.Items();
        for (size_t r = 0; r < column->rowGroup.size() && r < items.Size(); r++) {
            if (column->rowGroup[r] != group || column->rowToEntry[r] < 0) {
                continue;
            }
            if (auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
                item.Visibility(collapse ? mux::Visibility::Collapsed
                                         : mux::Visibility::Visible);
                item.MinHeight(column->rowHeight);
                item.Height(column->rowHeight);
                item.Opacity(1.0);
            }
        }
    } catch (...) {
        LogHresult(L"FinishGroupAnimation");
    }
}

void ToggleGroup(WindowState* state, Column* column, int headerRow) {
    if (column->animGroup >= 0) {
        FinishGroupAnimation(column);  // The last one first.
    }
    if (headerRow < 0 || headerRow >= (int)column->rowGroup.size()) {
        return;
    }
    int group = column->rowGroup[headerRow];
    if (group < 0 || group >= (int)column->groupTitles.size()) {
        return;
    }
    const std::wstring title = column->groupTitles[group];
    auto at = std::find(column->collapsed.begin(), column->collapsed.end(), title);
    bool collapse = at == column->collapsed.end();
    if (collapse) {
        column->collapsed.push_back(title);
    } else {
        column->collapsed.erase(at);
    }
    auto items = column->list.Items();
    auto selected = column->list.SelectedItems();
    std::vector<muxc::ListViewItem> rows;
    bool wasBuilding = state->building;
    state->building = true;
    for (size_t r = 0; r < column->rowGroup.size() && r < items.Size(); r++) {
        if (column->rowGroup[r] != group || column->rowToEntry[r] < 0) {
            continue;
        }
        auto object = items.GetAt((uint32_t)r);
        if (auto item = object.try_as<muxc::ListViewItem>()) {
            rows.push_back(item);
        }
        uint32_t index = 0;
        if (collapse && selected.IndexOf(object, index)) {
            selected.RemoveAt(index);  // A hidden row can't stay selected.
        }
    }
    state->building = wasBuilding;

    // The rows slide shut or open and fade. A very large group just switches.
    namespace anim = mux::Media::Animation;
    bool animate = !rows.empty() && rows.size() <= 60;
    if (!animate) {
        for (auto& item : rows) {
            item.Visibility(collapse ? mux::Visibility::Collapsed
                                     : mux::Visibility::Visible);
        }
    } else {
        try {
            anim::CubicEase ease;
            ease.EasingMode(anim::EasingMode::EaseInOut);
            auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(200));
            anim::Storyboard board;
            double full = column->rowHeight;
            for (auto& item : rows) {
                // Where the row ends up, so the animation can stop on it.
                item.MinHeight(0);
                item.Visibility(mux::Visibility::Visible);
                item.Height(collapse ? 0.0 : full);
                item.Opacity(collapse ? 0.0 : 1.0);

                anim::DoubleAnimation height;
                height.From(collapse ? full : 0.0);
                height.To(collapse ? 0.0 : full);
                height.Duration(length);
                height.EasingFunction(ease);
                height.EnableDependentAnimation(true);
                height.FillBehavior(anim::FillBehavior::Stop);
                anim::Storyboard::SetTarget(height, item);
                anim::Storyboard::SetTargetProperty(height, L"Height");
                board.Children().Append(height);

                anim::DoubleAnimation fade;
                fade.From(collapse ? 1.0 : 0.0);
                fade.To(collapse ? 0.0 : 1.0);
                fade.Duration(length);
                fade.FillBehavior(anim::FillBehavior::Stop);
                anim::Storyboard::SetTarget(fade, item);
                anim::Storyboard::SetTargetProperty(fade, L"Opacity");
                board.Children().Append(fade);
            }
            board.Begin();
            column->animGroup = group;
            column->animCollapse = collapse;
            column->animDoneAt = GetTickCount64() + 260;
        } catch (...) {
            LogHresult(L"animating a group");
            for (auto& item : rows) {
                item.Visibility(collapse ? mux::Visibility::Collapsed
                                         : mux::Visibility::Visible);
                item.MinHeight(column->rowHeight);
                item.Height(column->rowHeight);
                item.Opacity(1.0);
            }
        }
    }

    if (headerRow < (int)column->headerIcons.size() && column->headerIcons[headerRow]) {
        try {
            auto turn = column->headerIcons[headerRow]
                            .RenderTransform()
                            .try_as<mux::Media::RotateTransform>();
            if (turn) {
                double from = turn.Angle();
                double to = collapse ? 0.0 : 90.0;
                turn.Angle(to);  // Where it stays.
                anim::CubicEase ease;
                ease.EasingMode(anim::EasingMode::EaseOut);
                anim::DoubleAnimation spin;
                spin.From(from);
                spin.To(to);
                spin.Duration(mux::DurationHelper::FromTimeSpan(
                    std::chrono::milliseconds(180)));
                spin.EasingFunction(ease);
                spin.FillBehavior(anim::FillBehavior::Stop);
                anim::Storyboard::SetTarget(spin, turn);
                anim::Storyboard::SetTargetProperty(spin, L"Angle");
                anim::Storyboard board;
                board.Children().Append(spin);
                board.Begin();
            }
        } catch (...) {
            LogHresult(L"turning a group chevron");
        }
    }
    Out(L"group \"%s\" %s", title.c_str(), collapse ? L"collapsed" : L"opened");
}

void OnColumnBackgroundTapped(muxc::ListView const& list,
                              mux::Input::TappedRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        // A click on a group header opens or collapses it.
        {
            int hit = ColumnIndexOfList(state, list);
            if (hit >= 0) {
                Column* column = state->columns[hit].get();
                int header = HeaderRowAt(column, list, args.GetPosition(list));
                if (header >= 0) {
                    ToggleGroup(state, column, header);
                    return;
                }
            }
        }
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        for (int i = 0; node && i < 20; i++) {
            if (auto row = node.try_as<muxc::ListViewItem>()) {
                // A row. The selection handler deals with a new selection. A
                // click on the name of the item that was already the only
                // selected one starts a rename, after the double-click time.
                int at = ColumnIndexOfList(state, list);
                if (at >= 0) {
                    Column* column = state->columns[at].get();
                    int rowIndex = list.IndexFromContainer(row);
                    int entry = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size()
                                    ? column->rowToEntry[rowIndex]
                                    : -1;
                    ULONGLONG now = GetTickCount64();
                    bool onName = false;
                    if (entry >= 0 && (size_t)entry < column->texts.size() &&
                        column->texts[entry]) {
                        auto hitText = args.OriginalSource().try_as<muxc::TextBlock>();
                        onName = hitText && hitText == column->texts[entry];
                    }
                    if (entry >= 0 && onName && !state->rename &&
                        now >= state->slowRenameBlockUntil &&
                        now - column->selectionChangedAt > 350 &&
                        !(GetKeyState(VK_CONTROL) & 0x8000) &&
                        !(GetKeyState(VK_SHIFT) & 0x8000)) {
                        std::vector<size_t> picked = SelectedEntries(column, list);
                        if (picked.size() == 1 && picked[0] == (size_t)entry) {
                            state->slowRenameAt = now + GetDoubleClickTime() + 120;
                            state->slowRenameColumn = column;
                            state->slowRenameEntry = (size_t)entry;
                        }
                    }
                }
                return;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        Column* column = state->columns[index].get();
        LiftStale(state, index);
        state->building = true;
        list.SelectedItems().Clear();
        state->building = false;
        column->selectedChild.reset();
        state->selectFolder.reset();
        state->selectChild.reset();
        state->selectExtra.clear();
        SetActiveColumn(state, index);
        ScheduleNavigation(state, column->folder.get());
        state->focusColumn = column;
        state->focusUntil = GetTickCount64() + 1500;
        list.Focus(mux::FocusState::Pointer);
        Out(L"column %d chosen by a click on its empty space", index + 1);
    } catch (...) {
        state->building = false;
        LogHresult(L"OnColumnBackgroundTapped");
    }
}

// Selects the next item whose name starts with what was just typed. Typing
// the same letter again steps through the items with that letter.
void TypeToJump(Column* column, muxc::ListView const& list, WindowState* state,
                wchar_t ch) {
    ULONGLONG now = GetTickCount64();
    if (now - state->typedAt > 1000) {
        state->typed.clear();
    }
    state->typedAt = now;
    state->typed += ch;

    std::wstring needle = state->typed;
    bool repeated = needle.size() > 1 &&
                    needle.find_first_not_of(needle[0]) == std::wstring::npos;
    int current = list.SelectedIndex();
    int start;
    if (needle.size() == 1 || repeated) {
        needle = needle.substr(0, 1);
        start = current < 0 ? 0 : current + 1;
    } else {
        start = current < 0 ? 0 : current;
    }
    int rows = (int)column->rowToEntry.size();
    for (int i = 0; i < rows; i++) {
        int row = (start + i) % rows;
        int entry = column->rowToEntry[row];
        if (entry < 0) {
            continue;
        }
        if (auto item = list.Items().GetAt(row).try_as<muxc::ListViewItem>()) {
            if (item.Visibility() == mux::Visibility::Collapsed) {
                continue;
            }
        }
        if (StrCmpNIW(column->entries[entry].name.c_str(), needle.c_str(),
                      (int)needle.size()) == 0) {
            list.SelectedIndex(row);
            list.ScrollIntoView(list.Items().GetAt(row));
            return;
        }
    }
}

// Right or Enter on a folder enters its column, Left goes back to the parent
// column, Enter on a file opens it.
void OnColumnKey(muxc::ListView const& list,
                 mux::Input::KeyRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    if (state->rename) {
        return;  // Typing into the rename box.
    }
    state->slowRenameAt = 0;
    try {
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        auto key = args.Key();
        using winrt::Windows::System::VirtualKey;
        Column* column = state->columns[index].get();
        int row = list.SelectedIndex();
        int entry = row >= 0 && row < (int)column->rowToEntry.size()
                        ? column->rowToEntry[row]
                        : -1;
        bool onFolder = entry >= 0 && column->entries[entry].folder;

        // Standard file shortcuts, on the selected items.
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        std::vector<size_t> picked = SelectedEntries(column, list);
        auto keep = [&]() {
            state->focusColumn = column;
            state->focusUntil = GetTickCount64() + 1500;
            args.Handled(true);
        };
        if (ctrl && !alt && (key == VirtualKey::C || key == VirtualKey::X)) {
            if (!picked.empty()) {
                CopyOrCut(state, column, picked, key == VirtualKey::X);
            }
            keep();
            return;
        }
        if (ctrl && !alt && key == VirtualKey::V) {
            // A folder selected in the column takes the paste. Otherwise it
            // goes into the column's own folder.
            Pidl target(ILClone(column->folder.get()));
            if (picked.size() == 1 && column->entries[picked[0]].folder) {
                target.reset(ILCombine(column->folder.get(),
                                       column->entries[picked[0]].child.get()));
            }
            HRESULT hr = target ? PasteIntoFolder(state, target.get()) : E_FAIL;
            Out(L"Ctrl+V into %s: hr=0x%08lX", PathOf(target.get()).c_str(), hr);
            keep();
            return;
        }
        if (key == VirtualKey::Delete && !picked.empty()) {
            std::vector<std::wstring> before;
            for (size_t entry : picked) {
                before.push_back(FilePathOf(column->folder.get(),
                                            column->entries[entry].child.get()));
            }
            bool permanent = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HRESULT hr = InvokeVerbOnItems(state, column, picked, "delete");
            Out(L"Delete %d item(s): hr=0x%08lX", (int)picked.size(), hr);
            if (SUCCEEDED(hr) && !permanent) {
                RememberDeletion(state, before);
            }
            HideDeletedRows(column, picked);
            ReloadAll(state);
            keep();
            return;
        }
        if (key == VirtualKey::F2 && picked.size() == 1) {
            BeginRename(state, column, picked[0]);
            args.Handled(true);
            return;
        }
        if (alt && key == VirtualKey::Enter && !picked.empty()) {
            InvokeVerbOnItems(state, column, picked, "properties");
            args.Handled(true);
            return;
        }
        if (ctrl && !alt && (key == VirtualKey::Z || key == VirtualKey::Y)) {
            // The mod's own undo first: deletes and renames done here. Anything
            // else is left to Explorer's view.
            if (key == VirtualKey::Z && UndoLast(state)) {
                ReloadAll(state);
            } else if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
                IShellView* view = nullptr;
                if (SUCCEEDED(browser->QueryActiveShellView(&view)) && view) {
                    MSG msg = {};
                    msg.hwnd = state->topLevel;
                    msg.message = WM_KEYDOWN;
                    msg.wParam = (WPARAM)(key == VirtualKey::Z ? 'Z' : 'Y');
                    msg.lParam = 1;
                    HRESULT hr = view->TranslateAccelerator(&msg);
                    Out(L"%s passed to Explorer's view: hr=0x%08lX",
                        key == VirtualKey::Z ? L"Undo" : L"Redo", hr);
                    view->Release();
                }
            }
            keep();
            return;
        }

        // Letters and digits jump to a name. Ctrl and Alt shortcuts are left
        // alone.
        int code = (int)key;
        wchar_t typedChar = 0;
        if ((code >= 0x30 && code <= 0x39) || (code >= 0x41 && code <= 0x5A)) {
            typedChar = (wchar_t)code;
        } else if (code >= 0x60 && code <= 0x69) {
            typedChar = (wchar_t)(L'0' + (code - 0x60));
        }
        if (typedChar && !(GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_MENU) & 0x8000)) {
            TypeToJump(column, list, state, typedChar);
            keep();
            return;
        }

        if (key == VirtualKey::Left) {
            if (index > 0) {
                SetActiveColumn(state, index - 1);
                state->columns[index - 1]->list.Focus(mux::FocusState::Keyboard);
                state->focusColumn = state->columns[index - 1].get();
                state->focusUntil = GetTickCount64() + 1500;
                args.Handled(true);
            }
            return;
        }
        if (key == VirtualKey::Enter && entry >= 0 && !onFolder) {
            OnRowDoubleTapped(list);
            args.Handled(true);
            return;
        }
        if ((key == VirtualKey::Right || key == VirtualKey::Enter) && onFolder &&
            index + 1 < (int)state->columns.size()) {
            Column* child = state->columns[index + 1].get();
            if (child->list.SelectedIndex() < 0) {
                // Land on the first real row, past any group header.
                for (size_t r = 0; r < child->rowToEntry.size(); r++) {
                    if (child->rowToEntry[r] >= 0) {
                        child->list.SelectedIndex((int)r);
                        break;
                    }
                }
            }
            SetActiveColumn(state, index + 1);
            child->list.Focus(mux::FocusState::Keyboard);
            state->focusColumn = child;
            state->focusUntil = GetTickCount64() + 1500;
            args.Handled(true);
        }
    } catch (...) {
        LogHresult(L"OnColumnKey");
    }
}

void HidePanel(WindowState* state, const wchar_t* why) {
    if (!state->source) {
        state->on = false;
        return;
    }
    Out(L"panel off: %s", why);
    state->on = false;
    CancelRename(state);
    CloseContextMenu(state);
    try {
        if (state->timer) {
            state->timer.Stop();
            state->timer.Tick(state->tickToken);
            state->timer = nullptr;
        }
        state->building = true;
        RemoveColumnsFrom(state, 0);
        state->building = false;
        if (state->rootGrid && state->wheelHandler) {
            state->rootGrid.RemoveHandler(
                mux::UIElement::PointerWheelChangedEvent(),
                state->wheelHandler);
        }
        if (state->strip && state->stripWheelHandler) {
            state->strip.RemoveHandler(
                mux::UIElement::PointerWheelChangedEvent(),
                state->stripWheelHandler);
        }
        state->stripWheelHandler = nullptr;
        state->rootGrid = nullptr;
        state->wheelHandler = nullptr;
        state->source.Content(nullptr);
        state->source.Close();
    } catch (...) {
        LogHresult(L"HidePanel");
    }
    state->source = nullptr;
    state->bridge = nullptr;
    state->scroller = nullptr;
    state->strip = nullptr;
    state->pill = nullptr;
    state->overlay = nullptr;
    state->bandBox = nullptr;
    state->band = {};
    state->lastSeen.reset();
    state->expected.reset();
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    if (state->notifyWindow) {
        DestroyWindow(state->notifyWindow);
        state->notifyWindow = nullptr;
    }
    state->reloadDue = 0;
    state->active = -1;
    state->lastFileList = nullptr;
    state->specCheckAt = 0;
    state->dragColumn = -1;
    state->lastRect = {};
}

// The Explorer window was closed while its panel was showing. Windows has
// already destroyed the panel's window along with it, and closing or
// releasing the panel's XAML objects at that point crashes Explorer. So they
// are let go without being released: a small leak, once per closed window.
void AbandonWindow(WindowState* state) {
    Out(L"window closed with the panel on, letting its objects go");
    state->on = false;
    (void)state->context.release();  // Never destroyed, like the columns.
    (void)state->rename.release();
    g_renaming = false;
    try {
        if (state->timer) {
            state->timer.Stop();
            state->timer.Tick(state->tickToken);
        }
    } catch (...) {
    }
    for (auto& column : state->columns) {
        if (column->notifyId) {
            SHChangeNotifyDeregister(column->notifyId);
            column->notifyId = 0;
        }
        column.release();  // Never destroyed, on purpose.
    }
    state->columns.clear();
    if (state->notifyWindow) {
        DestroyWindow(state->notifyWindow);
        state->notifyWindow = nullptr;
    }
    for (auto& sibling : state->siblings) {
        (void)winrt::detach_abi(sibling.item);
    }
    state->siblings.clear();
    try {
        if (state->entryTimer) {
            state->entryTimer.Stop();
            state->entryTimer.Tick(state->entryToken);
        }
    } catch (...) {
    }
    (void)winrt::detach_abi(state->entryTimer);
    (void)winrt::detach_abi(state->xamlRoot);
    state->entryStopped = false;
    state->entryTries = 0;
    state->restoreSettle = 0;
    (void)winrt::detach_abi(state->timer);
    (void)winrt::detach_abi(state->source);
    (void)winrt::detach_abi(state->bridge);
    (void)winrt::detach_abi(state->scroller);
    (void)winrt::detach_abi(state->rootGrid);
    (void)winrt::detach_abi(state->wheelHandler);
    (void)winrt::detach_abi(state->stripWheelHandler);
    (void)winrt::detach_abi(state->strip);
    (void)winrt::detach_abi(state->pill);
    (void)winrt::detach_abi(state->overlay);
    (void)winrt::detach_abi(state->bandBox);
    (void)winrt::detach_abi(state->menuItem);
    (void)winrt::detach_abi(state->menuItems);
    state->topLevel = nullptr;
    state->lastSeen.reset();
    state->expected.reset();
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    state->lastFileList = nullptr;
    state->reloadDue = 0;
    state->specCheckAt = 0;
    state->active = -1;
    state->dragColumn = -1;
    state->lastRect = {};
}

// Explorer's own view moved to `current` without this mod asking. Keep the
// columns showing the path from the first column down to that folder.
void SyncToFolder(WindowState* state, Pidl const& current) {
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();

    // Already a column: only the active column moves.
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (ILIsEqual(current.get(), state->columns[i]->folder.get())) {
            Out(L"Explorer moved to column %d", (int)i + 1);
            SetActiveColumn(state, (int)i);
            return;
        }
    }
    if (!state->columns.empty()) {
        PCIDLIST_ABSOLUTE root = state->columns[0]->folder.get();

        // Below the first column: open the chain of columns down to it.
        if (ILIsParent(root, current.get(), FALSE)) {
            int deepest = 0;
            for (int i = (int)state->columns.size() - 1; i >= 0; i--) {
                if (ILIsParent(state->columns[i]->folder.get(), current.get(),
                               FALSE)) {
                    deepest = i;
                    break;
                }
            }
            state->building = true;
            RemoveColumnsFrom(state, deepest + 1);
            state->building = false;
            PCUIDLIST_RELATIVE rest =
                ILFindChild((PIDLIST_ABSOLUTE)state->columns[deepest]->folder.get(),
                            (PIDLIST_ABSOLUTE)current.get());
            Pidl walk(ILClone(state->columns[deepest]->folder.get()));
            int parent = deepest;
            while (rest && !ILIsEmpty(rest) && state->columns.size() < 40) {
                Pidl step(ILCloneFirst(rest));
                if (!step) {
                    break;
                }
                SelectChildInColumn(state, state->columns[parent].get(),
                                    step.get());
                Pidl next(ILCombine(walk.get(), step.get()));
                if (!next) {
                    break;
                }
                walk.reset(ILClone(next.get()));
                AddColumn(state, std::move(next));
                parent++;
                rest = ILGetNext(rest);
            }
            Out(L"Explorer moved below the first column, %d columns now",
                (int)state->columns.size());
            SetActiveColumn(state, (int)state->columns.size() - 1);
            return;
        }

        // The parent of the first column, which is what Up does: add it in
        // front, with the old first column selected in it.
        if (ILIsParent(current.get(), root, TRUE)) {
            Pidl childId(ILClone(ILFindLastID(root)));
            AddColumn(state, Pidl(ILClone(current.get())), true);
            state->columns[0]->selectedChild = std::move(childId);
            Out(L"Explorer moved up, parent added as first column");
            SetActiveColumn(state, 0);
            try {
                state->scroller.ChangeView(0.0, nullptr, nullptr);
            } catch (...) {
            }
            return;
        }
    }
    Out(L"Explorer navigated elsewhere (\"%s\"), rebuilding",
        PathOf(current.get()).c_str());
    RebuildFromExplorer(state);
    SetActiveColumn(state, 0);
}

// What Explorer's own view shows changed.
void OnExplorerFolderChanged(WindowState* state, Pidl const& current) {
    state->lastSeen.reset(ILClone(current.get()));
    // A click on a flat entry of the navigation pane starts the columns over
    // at that folder, even when it is inside the current chain.
    if (g_panePidl) {
        bool arrived = GetTickCount64() - g_paneAt < 3000 &&
                       ILIsEqual(current.get(), g_panePidl);
        CoTaskMemFree(g_panePidl);
        g_panePidl = nullptr;
        if (arrived && g_sidebarRoot && IsFlatSidebarTarget(current.get())) {
            Out(L"sidebar click: \"%s\" becomes the first column",
                PathOf(current.get()).c_str());
            state->expected.reset();
            RebuildFromExplorer(state);
            SetActiveColumn(state, 0);
            return;
        }
    }
    if (state->expected && ILIsEqual(current.get(), state->expected.get())) {
        state->expected.reset();
        return;
    }
    state->expected.reset();
    if (state->pasteReturn && GetTickCount64() < state->pasteGuardUntil) {
        Out(L"Explorer moved to \"%s\" right after a paste, sending it back",
            PathOf(current.get()).c_str());
        ScheduleNavigation(state, state->pasteReturn.get());
        return;
    }
    SyncToFolder(state, current);
}

void OnTick() {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || !state->bridge) {
        return;
    }
    if (!IsWindow(state->topLevel)) {
        AbandonWindow(state);
        return;
    }
    if (g_unloading || !g_enabled) {
        HidePanel(state, L"mod unloading or feature off");
        return;
    }
    try {
        if (!IsWindow(state->topLevel)) {
            AbandonWindow(state);
            return;
        }
        state->ticks++;
        ULONGLONG now = GetTickCount64();

        // Keep the panel on the file list. Plain Win32, cheap.
        HWND fileList = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
        RECT rect;
        if (fileList && FileListRect(state->topLevel, fileList, &rect) &&
            !EqualRect(&rect, &state->lastRect)) {
            state->lastRect = rect;
            state->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
                rect.left, rect.top, rect.right - rect.left,
                rect.bottom - rect.top});
        }

        // Explorer follows the active column, after the pause.
        if (state->pendingNav && now >= state->navDue) {
            Pidl target = std::move(state->pendingNav);
            NavigateExplorer(state, target.get());
        }

        // A watched folder changed on disk: read the columns again.
        // At most one reload every two seconds, however busy the folder is.
        size_t shown = 0;
        for (auto& c : state->columns) {
            shown += c->entries.size();
        }
        // Half a second between reloads, two seconds when the columns hold a
        // lot of rows.
        ULONGLONG gap = shown > 3000 ? 2000 : 500;
        if (state->reloadDue && now >= state->reloadDue && !state->rename &&
            now - state->lastReload >= gap) {
            state->reloadDue = 0;
            state->lastReload = now;
            Out(L"a column's folder changed, reloading");
            ReloadAll(state);
        }

        // The resize handle appears once the pointer has rested on an edge.
        for (auto& column : state->columns) {
            if (column->hoverSince && now - column->hoverSince >= 350 &&
                column->pill) {
                column->pill.Opacity(1);
                column->hoverSince = 0;
            }
        }

        // A menu command ran. Take new sort, group, hidden-files or compact
        // settings if any of them changed.
        if (state->specCheckAt && now >= state->specCheckAt) {
            state->specCheckAt = 0;
            bool hidden = ShowHiddenFiles(), compact = IsCompactMode();
            if (hidden != state->shownHidden ||
                compact != state->shownCompact) {
                Out(L"Explorer setting changed: hidden=%d compact=%d",
                    hidden ? 1 : 0, compact ? 1 : 0);
                state->shownHidden = hidden;
                state->shownCompact = compact;
                ReloadAll(state);
            }
            IShellBrowser* browser = BrowserOf(state->topLevel);
            SortSpec after;
            if (browser && ReadSortSpec(browser, &after) &&
                !SameSpec(after, state->specBeforeMenu) &&
                !SameSpec(after, state->spec)) {
                Out(L"sort or group changed in the Sort menu: %d sort columns, grouped=%d",
                    (int)after.columns.size(), after.grouped ? 1 : 0);
                state->spec = after;
                ReloadAll(state);
            }
        }

        if (state->swapOld && now >= state->swapDeadline) {
            Out(L"the new first column didn't load in time, swapping anyway");
            CompleteSwap(state);
        }

        // The slow second click on a selected name: time to rename.
        if (state->slowRenameAt && now >= state->slowRenameAt) {
            state->slowRenameAt = 0;
            Column* column = state->slowRenameColumn;
            bool known = false;
            for (auto& c : state->columns) {
                known = known || c.get() == column;
            }
            if (known && !state->rename) {
                std::vector<size_t> picked = SelectedEntries(column, column->list);
                if (picked.size() == 1 && picked[0] == state->slowRenameEntry) {
                    BeginRename(state, column, picked[0]);
                }
            }
        }

        // Scrollbars of the columns, once a column has its template, and again
        // when the setting changes. The compact style is a transform, so it
        // costs nothing to keep.
        for (auto& c : state->columns) {
            int wanted = g_compactScroll ? 1 : 0;
            if (c->filled && c->scrollStyle != wanted && c->list) {
                try {
                    // Remembered only when a bar was found: the template may
                    // not be built yet.
                    if (StyleScrollBars(c->list, wanted == 1) > 0) {
                        c->scrollStyle = wanted;
                    }
                } catch (...) {
                    LogHresult(L"scrollbar style");
                }
            }
        }

        // A changed setting that affects rows already on screen.
        if (state->styleVersion != g_styleVersion) {
            state->styleVersion = g_styleVersion;
            for (auto& c : state->columns) {
                auto rows = c->list.Items();
                for (uint32_t r = 0; r < rows.Size(); r++) {
                    if (auto item = rows.GetAt(r).try_as<muxc::ListViewItem>()) {
                        ApplyFocusStyle(item);
                    }
                }
            }
        }

        // The theme changed: take the list's colour again.
        if (state->ticks % 20 == 0 && state->rootGrid && fileList &&
            IsDarkMode() != state->shownDark) {
            bool nowDark = IsDarkMode();
            state->shownDark = nowDark;
            state->rootGrid.RequestedTheme(nowDark ? mux::ElementTheme::Dark
                                                   : mux::ElementTheme::Light);
            state->rootGrid.Background(PanelBackground(fileList, nowDark));
            Out(L"theme changed, panel background taken again");
        }

        // A column resized or moved: the pill follows, without animation.
        UpdatePill(state, false);

        // The clipboard changed, by this mod or from outside (Cut in Explorer's
        // toolbar): the dimmed rows follow what is on it.
        {
            DWORD seq = GetClipboardSequenceNumber();
            if (seq != state->clipSeen) {
                state->clipSeen = seq;
                if (seq != state->cutSeq) {
                    SyncCutFromClipboard(state);
                }
            }
        }

        // A group that finished opening or closing.
        for (auto& c : state->columns) {
            if (c->animGroup >= 0 && now >= c->animDoneAt) {
                FinishGroupAnimation(c.get());
            }
        }

        // After a toolbar command: read again, once soon and once later.
        if (state->postActionAt && now >= state->postActionAt) {
            state->postActionAt = 0;
            ReloadAll(state);
        }
        if (state->postActionAt2 && now >= state->postActionAt2) {
            state->postActionAt2 = 0;
            ReloadAll(state);
        }

        // Explorer's own rename box, started from the toolbar, sits under the
        // panel where nobody sees it. Close it and rename in the column.
        if (state->watchEditUntil) {
            if (now >= state->watchEditUntil) {
                state->watchEditUntil = 0;
            } else if (HWND edit = FindNativeRenameBox(state->topLevel)) {
                state->watchEditUntil = 0;
                SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
                SendMessageW(edit, WM_KEYUP, VK_ESCAPE, 0);
                if (state->active >= 0 && state->active < (int)state->columns.size() &&
                    !state->rename) {
                    Column* column = state->columns[state->active].get();
                    std::vector<size_t> picked = SelectedEntries(column, column->list);
                    Out(L"Explorer's own rename box found, renaming in the column instead (%d selected)",
                        (int)picked.size());
                    if (picked.size() == 1) {
                        BeginRename(state, column, picked[0]);
                    }
                }
            }
        }
        // Explorer's view takes focus when it follows a column. Keep it in
        // the column the user is typing in.
        if (state->focusColumn && GetTickCount64() < state->focusUntil &&
            !state->rename && !state->context) {
            bool known = false;
            for (auto& c : state->columns) {
                known = known || c.get() == state->focusColumn;
            }
            // Only when focus has left the panel, and then once: focusing a
            // list again would move its keyboard cursor.
            HWND panel = state->bridge ? (HWND)state->bridge.WindowId().Value
                                       : nullptr;
            HWND focused = GetFocus();
            bool inside = panel && focused &&
                          (focused == panel || IsChild(panel, focused));
            if (known && panel && !inside) {
                state->focusColumn->list.Focus(mux::FocusState::Programmatic);
                Out(L"focus taken back from Explorer's view");
            }
        } else {
            state->focusColumn = nullptr;
        }

        // Ask the shell where Explorer is only when something suggests a
        // change: the file list window was replaced, a request is pending,
        // or as a slow safety net.
        bool selectPending = state->selectChild && !state->pendingNav;
        bool check = fileList != state->lastFileList || state->expected ||
                     selectPending || state->ticks % 20 == 0;
        if (!check) {
            return;
        }
        IShellBrowser* browser = BrowserOf(state->topLevel);
        if (!browser) {
            return;
        }
        Pidl current = CurrentFolder(browser);
        if (!current) {
            return;
        }
        bool listReplaced =
            state->lastFileList && fileList != state->lastFileList;
        state->lastFileList = fileList;
        if (!state->lastSeen ||
            !ILIsEqual(current.get(), state->lastSeen.get())) {
            OnExplorerFolderChanged(state, current);
            if (!state->on) {
                return;
            }
        } else if (listReplaced && !state->expected) {
            // Same folder, new file list window: taken as a Refresh.
            Out(L"file list replaced in the same folder, reloading");
            ReloadAll(state);
        }

        // Mirror the selected file into Explorer's own view. The view fills
        // asynchronously, so this retries for a few seconds.
        if (state->selectChild && state->selectFolder && !state->pendingNav &&
            !state->expected &&
            ILIsEqual(current.get(), state->selectFolder.get())) {
            if (SelectInExplorer(browser, state->selectChild.get())) {
                if (!state->selectExtra.empty()) {
                    AddToSelectionInExplorer(browser, state->selectExtra);
                    state->selectExtra.clear();
                }
                state->selectFolder.reset();
                state->selectChild.reset();
            } else if (++state->selectTries > 40) {
                Out(L"selection could not be mirrored into Explorer's view");
                state->selectFolder.reset();
                state->selectChild.reset();
                state->selectExtra.clear();
            }
        }
    } catch (...) {
        state->building = false;
        LogHresult(L"OnTick");
    }
}

void ShowPanel(WindowState* state) {
    if (state->source || g_unloading || !g_enabled) {
        return;
    }
    HWND fileList = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
    RECT rect;
    if (!fileList || !FileListRect(state->topLevel, fileList, &rect)) {
        Out(L"panel: no visible file list in this window");
        return;
    }
    const wchar_t* step = L"start";
    try {
        step = L"reading sort and group";
        state->spec = SortSpec{};
        if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
            ReadSortSpec(browser, &state->spec);
        }
        Out(L"spec: %d sort columns, first direction %d, grouped=%d",
            (int)state->spec.columns.size(),
            state->spec.columns.empty() ? 0
                                        : (int)state->spec.columns[0].direction,
            state->spec.grouped ? 1 : 0);

        state->shownHidden = ShowHiddenFiles();
        state->shownCompact = IsCompactMode();
        Out(L"Explorer settings read: hidden=%d compact=%d, raw UseCompactMode=%lu",
            state->shownHidden ? 1 : 0, state->shownCompact ? 1 : 0,
            ReadExplorerDword(
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                L"UseCompactMode", 99));
        if (!state->notifyWindow) {
            state->notifyWindow = CreateNotifyWindow(&state->reloadDue);
        }

        step = L"building the content";
        muxc::StackPanel strip;
        strip.Orientation(muxc::Orientation::Horizontal);

        muxc::ScrollViewer scroller;
        scroller.HorizontalScrollMode(muxc::ScrollMode::Auto);
        scroller.HorizontalScrollBarVisibility(
            muxc::ScrollBarVisibility::Auto);
        scroller.VerticalScrollMode(muxc::ScrollMode::Disabled);
        scroller.VerticalScrollBarVisibility(
            muxc::ScrollBarVisibility::Disabled);
        // The strip and, over it, a layer for the pill. The pill scrolls
        // sideways with the columns and stays at the top when a column scrolls.
        bool dark = IsDarkMode();
        muxc::Grid content;
        content.Children().Append(strip);
        muxc::Canvas overlay;
        overlay.IsHitTestVisible(false);
        muxc::Border pill;
        pill.Width(16);
        pill.Height(3);
        pill.CornerRadius(mux::CornerRadius{1.5, 1.5, 1.5, 1.5});
        pill.Opacity(0);
        try {
            // The accent WinUI itself uses for fills: the lighter shade on a
            // dark theme, the darker one on a light theme.
            auto accent = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(
                dark ? winrt::Windows::UI::ViewManagement::UIColorType::AccentLight2
                     : winrt::Windows::UI::ViewManagement::UIColorType::AccentDark1);
            pill.Background(mux::Media::SolidColorBrush(accent));
        } catch (...) {
            pill.Background(Brush(255, 0, 120, 212));
        }
        muxc::Canvas::SetTop(pill, 5);
        muxc::Canvas::SetLeft(pill, 0);
        overlay.Children().Append(pill);
        // The selection rectangle: a soft accent fill with a thin edge.
        muxc::Border bandBox;
        bandBox.IsHitTestVisible(false);
        bandBox.Opacity(0);
        bandBox.CornerRadius(mux::CornerRadius{3, 3, 3, 3});
        bandBox.BorderThickness(mux::Thickness{1, 1, 1, 1});
        try {
            auto accent = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(
                winrt::Windows::UI::ViewManagement::UIColorType::Accent);
            bandBox.Background(Brush(48, accent.R, accent.G, accent.B));
            bandBox.BorderBrush(Brush(170, accent.R, accent.G, accent.B));
        } catch (...) {
            bandBox.Background(Brush(48, 0, 120, 212));
            bandBox.BorderBrush(Brush(170, 0, 120, 212));
        }
        overlay.Children().Append(bandBox);
        state->bandBox = bandBox;
        content.Children().Append(overlay);
        state->overlay = overlay;
        state->pill = pill;
        state->pillLeft = -1;
        state->pillUntil = 0;
        scroller.Content(content);

        muxc::Grid root;
        {
            root.Background(PanelBackground(fileList, dark));
            state->shownDark = dark;
        }
        root.RequestedTheme(dark ? mux::ElementTheme::Dark
                                 : mux::ElementTheme::Light);
        root.Children().Append(scroller);

        // The horizontal mouse wheel scrolls the strip sideways, across the
        // columns. Registered to also see events a column already handled, so
        // the columns' own vertical scrolling can't take it.
        mux::Input::PointerEventHandler wheel(
            [](Inspectable const&,
               mux::Input::PointerRoutedEventArgs const& args) {
                try {
                    WindowState* state = StateForThisThread(false);
                    if (!state || !state->scroller) {
                        return;
                    }
                    auto props =
                        args.GetCurrentPoint(state->scroller).Properties();
                    if (!props.IsHorizontalMouseWheel()) {
                        return;
                    }
                    // Turns that come close together add up and glide to
                    // one target. Starting from the current offset on every
                    // turn would restart the glide each time and look
                    // jerky. After a pause, start from where the strip is.
                    ULONGLONG now = GetTickCount64();
                    double from = (now - state->wheelAt < 300)
                                      ? state->wheelTarget
                                      : state->scroller.HorizontalOffset();
                    double target = from + props.MouseWheelDelta();
                    double maxOffset = state->scroller.ScrollableWidth();
                    target = target < 0 ? 0 : (target > maxOffset ? maxOffset : target);
                    state->wheelTarget = target;
                    state->wheelAt = now;
                    state->scroller.ChangeView(target, nullptr, nullptr, false);
                    args.Handled(true);
                } catch (...) {
                    LogHresult(L"horizontal wheel");
                }
            });
        Inspectable wheelBoxed = winrt::box_value(wheel);
        root.AddHandler(mux::UIElement::PointerWheelChangedEvent(), wheelBoxed,
                        true);
        state->rootGrid = root;
        state->wheelHandler = wheelBoxed;

        // The ordinary wheel only scrolls the column under the pointer. A
        // column that is already at its end, or has nothing to scroll, passes
        // the event up, and the strip's scroll viewer would turn it into a
        // sideways move. This handler sits on the strip, below that scroll
        // viewer and above every column, and marks vertical turns handled.
        mux::Input::PointerEventHandler verticalWheel(
            [](Inspectable const&,
               mux::Input::PointerRoutedEventArgs const& args) {
                try {
                    WindowState* state = StateForThisThread(false);
                    if (!state || !state->scroller) {
                        return;
                    }
                    if (!args.GetCurrentPoint(state->scroller)
                             .Properties()
                             .IsHorizontalMouseWheel()) {
                        args.Handled(true);
                    }
                } catch (...) {
                    LogHresult(L"vertical wheel");
                }
            });
        Inspectable verticalBoxed = winrt::box_value(verticalWheel);
        strip.AddHandler(mux::UIElement::PointerWheelChangedEvent(),
                         verticalBoxed, true);
        state->stripWheelHandler = verticalBoxed;

        step = L"creating the XAML source";
        mux::Hosting::DesktopWindowXamlSource source;
        state->source = source;
        source.Initialize(
            winrt::Microsoft::UI::WindowId{(uint64_t)state->topLevel});
        source.Content(root);

        step = L"placing the panel";
        auto bridge = source.SiteBridge();
        state->bridge = bridge;
        bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
            rect.left, rect.top, rect.right - rect.left,
            rect.bottom - rect.top});
        bridge.Show();
        SetWindowPos((HWND)bridge.WindowId().Value, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        state->lastRect = rect;
        state->lastFileList = fileList;
        state->scroller = scroller;
        state->strip = strip;
        state->on = true;

        step = L"reading the folder";
        RebuildFromExplorer(state);
        SetActiveColumn(state, 0);

        step = L"starting the timer";
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(120));
        state->tickToken = timer.Tick(
            [](mud::DispatcherQueueTimer const&, Inspectable const&) {
                OnTick();
            });
        timer.Start();
        state->timer = timer;
        Out(L"panel on, window=%p", state->topLevel);
    } catch (...) {
        Out(L"panel failed at step: %s", step);
        LogHresult(L"ShowPanel");
        HidePanel(state, L"creation failed");
    }
}

// ---- View menu entry ----

// The last layout the user chose, kept by Windhawk across restarts.
bool LastLayoutWasColumns() {
    return Wh_GetIntValue(L"lastLayoutColumns", 0) != 0;
}

void RememberColumns(bool on) {
    Wh_SetIntValue(L"lastLayoutColumns", on ? 1 : 0);
}

void RemoveMenuEntry(WindowState* state) {
    try {
        for (auto& sibling : state->siblings) {
            if (sibling.item) {
                sibling.item.Click(sibling.token);
            }
        }
        state->siblings.clear();
        if (state->menuItem) {
            state->menuItem.Click(state->menuClickToken);
            uint32_t index = 0;
            if (state->menuItems &&
                state->menuItems.IndexOf(state->menuItem, index)) {
                state->menuItems.RemoveAt(index);
            }
        }
    } catch (...) {
        LogHresult(L"RemoveMenuEntry");
    }
    state->menuItem = nullptr;
    state->menuItems = nullptr;
}

void AddMenuEntry(WindowState* state, ItemsVector const& items) {
    try {
        if (state->menuItem) {
            uint32_t index = 0;
            if (state->menuItems == items &&
                items.IndexOf(state->menuItem, index)) {
                return;
            }
            // The menu was rebuilt. Start again on the new one.
            RemoveMenuEntry(state);
        }

        winrt::hstring groupName;
        if (auto first = items.GetAt(0).try_as<muxc::RadioMenuFlyoutItem>()) {
            groupName = first.GroupName();
        }

        muxc::RadioMenuFlyoutItem item;
        item.Text(L"Columns");
        if (!groupName.empty()) {
            item.GroupName(groupName);
        }
        muxc::FontIcon icon;
        icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        icon.Glyph(L"");
        item.Icon(icon);
        item.Name(L"WindhawkColumnsItem");
        item.IsChecked(state->on);
        state->menuClickToken = item.Click(
            [](Inspectable const&, mux::RoutedEventArgs const&) {
                WindowState* state = StateForThisThread(false);
                if (state && !g_unloading) {
                    Out(L"Columns chosen in the View menu");
                    ShowPanel(state);
                    if (state->on) {
                        RememberColumns(true);
                    }
                }
            });

        // Choosing any of Explorer's eight layouts turns columns off.
        for (uint32_t i = 0; i < 8 && i < items.Size(); i++) {
            auto sibling = items.GetAt(i).try_as<muxc::MenuFlyoutItem>();
            if (!sibling) {
                continue;
            }
            SiblingHook hook;
            hook.item = sibling;
            hook.token = sibling.Click(
                [](Inspectable const&, mux::RoutedEventArgs const&) {
                    WindowState* state = StateForThisThread(false);
                    if (state && state->on) {
                        HidePanel(state, L"another layout chosen");
                    }
                    RememberColumns(false);
                });
            state->siblings.push_back(std::move(hook));
        }

        // Right after the eight layouts, before the separator.
        items.InsertAt(8, item);
        state->menuItem = item;
        state->menuItems = items;
        Out(L"Columns entry added to the View menu, window=%p",
            state->topLevel);
    } catch (...) {
        LogHresult(L"AddMenuEntry");
    }
}

std::wstring ClassOf(Inspectable const& obj) {
    try {
        return std::wstring{winrt::get_class_name(obj).c_str()};
    } catch (...) {
        return L"?";
    }
}

// The View button has no name. Its flyout is the only one that opens with
// eight radio items, the eight layouts. That holds in any language.
void WalkForViewFlyout(WindowState* state, mux::DependencyObject const& node,
                       int depth, int& visited) {
    if (!node || depth > 40 || visited > 3000) {
        return;
    }
    visited++;
    if (auto button = node.try_as<muxc::Button>()) {
        if (auto flyout = button.Flyout()) {
            if (auto menu = flyout.try_as<muxc::MenuFlyout>()) {
                auto items = menu.Items();
                uint32_t radios = 0;
                bool hasOurs = false;
                for (uint32_t i = 0; i < items.Size() && i < 9; i++) {
                    auto radio =
                        items.GetAt(i).try_as<muxc::RadioMenuFlyoutItem>();
                    if (!radio) {
                        break;
                    }
                    radios++;
                    if (radio.Name() == L"WindhawkColumnsItem") {
                        hasOurs = true;
                    }
                }
                if (items.Size() > 8 && (radios == 8 || (radios == 9 && hasOurs))) {
                    if (!hasOurs) {
                        AddMenuEntry(state, items);
                    }
                    visited = 100000;  // Found. Stop the walk.
                    return;
                }
            }
        }
    }
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        WalkForViewFlyout(state, mux::Media::VisualTreeHelper::GetChild(node, i),
                          depth + 1, visited);
    }
}

void OnCommandBarPointer(Inspectable const& sender) {
    if (g_unloading || !g_enabled) {
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (now - g_lastWalk < 1000) {
        return;
    }
    g_lastWalk = now;
    try {
        WindowState* state = StateForThisThread(true);
        if (!state->topLevel || !IsWindow(state->topLevel)) {
            state->topLevel = FindTopLevel();
        }
        if (!state->queue) {
            state->queue = mud::DispatcherQueue::GetForCurrentThread();
        }
        if (!state->topLevel || !state->queue) {
            return;
        }
        auto node = sender.try_as<mux::DependencyObject>();
        if (!node) {
            return;
        }
        // Up to the command bar itself, so the walk stays small.
        mux::DependencyObject root = node;
        for (int i = 0; i < 12; i++) {
            if (ClassOf(root).find(L"CommandBarControl") != std::wstring::npos) {
                break;
            }
            auto parent = mux::Media::VisualTreeHelper::GetParent(root);
            if (!parent) {
                break;
            }
            root = parent;
        }
        int visited = 0;
        WalkForViewFlyout(state, root, 0, visited);
    } catch (...) {
        LogHresult(L"OnCommandBarPointer");
    }
}

// ---- Window creation ----
//
// CommandBarControl_Wave1::OnApplyTemplate runs when a window is built, and
// OnDpiChanged runs right after it, in the same call. OnDpiChanged takes the
// window's XamlRoot by value, which gives the whole island's tree. The View
// flyout is in that tree by the first timer tick after the call. Confirmed
// with probes/probe-window-created.wh.cpp 0.6 on 2026-11-13, build 26100.9974.
// OnDpiChanged is a coroutine: it moves the XamlRoot out of the caller's slot,
// so the slot has to be read before the original runs. `this` of command bar
// methods is never treated as a XAML object. That crashed Explorer in 0.1 and
// 0.2 of the probe.

bool ReadableMemory(const void* p, size_t size) {
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

// `arg` is the caller's temporary: a slot holding the XamlRoot pointer. The
// object's first code address must lie in a package module.
void* XamlRootFromArgument(void* arg) {
    if (!ReadableMemory(arg, sizeof(void*))) {
        return nullptr;
    }
    void* object = *(void**)arg;
    if (!ReadableMemory(object, sizeof(void*))) {
        return nullptr;
    }
    const void* vtable = *(const void* const*)object;
    if (!ReadableMemory(vtable, sizeof(void*) * 3)) {
        return nullptr;
    }
    const void* first = *(const void* const*)vtable;
    HMODULE module = nullptr;
    if (!first || !GetModuleHandleExW(
                      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      (LPCWSTR)first, &module)) {
        return nullptr;
    }
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        return nullptr;
    }
    bool ours = StrStrIW(path, L"Microsoft.UI.Xaml.dll") ||
                StrStrIW(path, L"\\WindowsApps\\");
    return ours ? object : nullptr;
}

BOOL CALLBACK FindAnyCabinetOnThread(HWND hwnd, LPARAM lParam) {
    wchar_t cls[64];
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) &&
        wcscmp(cls, L"CabinetWClass") == 0) {
        *(HWND*)lParam = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND TopLevelOfThisThread() {
    HWND top = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), FindAnyCabinetOnThread, (LPARAM)&top);
    return top;
}

// Stops the timer and revokes its handler. The timer object itself is kept
// until the window's own cleanup, since it can't be released from inside its
// own tick.
void StopEntry(WindowState* state) {
    state->entryStopped = true;
    state->armTab = nullptr;
    try {
        if (state->entryTimer) {
            state->entryTimer.Stop();
            state->entryTimer.Tick(state->entryToken);
        }
    } catch (...) {
        LogHresult(L"StopEntry");
    }
}

// The window's own cleanup, on its thread, while the window still exists.
void ReleaseEntry(WindowState* state) {
    StopEntry(state);
    state->entryTimer = nullptr;
    state->xamlRoot = nullptr;
}

// Every 50 ms after the window is built: put the Columns entry into the View
// menu, then, if Columns was the last layout chosen, turn it on once the file
// list is visible.
void EntryTick() {
    if (g_unloading) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || !state->entryTimer || state->entryStopped) {
        return;
    }
    try {
        state->entryTries++;
        if (!state->topLevel || !IsWindow(state->topLevel)) {
            StopEntry(state);
            return;
        }
        if (!state->menuItem) {
            int visited = 0;
            auto content = state->xamlRoot.Content();
            if (auto root = content.try_as<mux::DependencyObject>()) {
                WalkForViewFlyout(state, root, 0, visited);
            }
            if (state->menuItem) {
                Out(L"Columns entry added at window creation, after %d ticks",
                    state->entryTries);
            } else if (state->entryTries >= 40) {
                Out(L"no View flyout found within 2 s of window creation, giving up");
                StopEntry(state);
            }
            return;
        }
        if (!g_restore || !LastLayoutWasColumns() || state->on) {
            StopEntry(state);
            return;
        }
        // A new tab: wait until it is the visible one, so the panel isn't
        // built on the tab that was showing before.
        if (state->armTab &&
            FindVisibleChild(state->topLevel, L"ShellTabWindowClass") !=
                state->armTab) {
            if (state->entryTries >= 100) {
                Out(L"the new tab never became visible, not restoring Columns");
                StopEntry(state);
            }
            return;
        }
        HWND fileList = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
        RECT rect;
        if (fileList && FileListRect(state->topLevel, fileList, &rect)) {
            // One tick with a visible file list. Three ticks in 0.3.0 left
            // Explorer's own list showing for about 270 ms.
            if (++state->restoreSettle >= 1) {
                Out(L"restoring Columns, window=%p, tick %d", state->topLevel,
                    state->entryTries);
                ShowPanel(state);
                if (state->on && state->menuItem) {
                    state->menuItem.IsChecked(true);
                }
                StopEntry(state);
            }
        } else if (state->entryTries >= 100) {
            Out(L"no visible file list within 5 s, not restoring Columns");
            StopEntry(state);
        }
    } catch (...) {
        LogHresult(L"EntryTick");
        StopEntry(state);
    }
}

// A new tab was created in a window this mod already knows. If Columns was the
// last layout chosen and this window isn't showing it, turn it on for the tab
// once the tab is visible.
void OnTabCreated(HWND tab) {
    if (g_unloading || !g_enabled || !g_restore) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state || !state->topLevel || !IsWindow(state->topLevel) ||
        GetAncestor(tab, GA_PARENT) != state->topLevel || state->on ||
        !state->queue || !LastLayoutWasColumns()) {
        return;
    }
    state->armTab = tab;
    state->entryTries = 0;
    state->restoreSettle = 0;
    if (state->entryTimer && !state->entryStopped) {
        return;  // Still running. It waits for this tab now.
    }
    try {
        auto timer = state->entryTimer ? state->entryTimer
                                       : state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(50));
        state->entryToken = timer.Tick(
            [](mud::DispatcherQueueTimer const&, Inspectable const&) {
                EntryTick();
            });
        state->entryStopped = false;
        timer.Start();
        state->entryTimer = timer;
        Out(L"new tab %p, waiting for it to show Columns", tab);
    } catch (...) {
        LogHresult(L"OnTabCreated");
        state->entryStopped = true;
    }
}

using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int,
                                        int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
CreateWindowExW_t CreateWindowExW_Original;
HWND WINAPI CreateWindowExW_Hook(DWORD exStyle, LPCWSTR className,
                                 LPCWSTR title, DWORD style, int x, int y,
                                 int w, int h, HWND parent, HMENU menu,
                                 HINSTANCE instance, LPVOID param) {
    HWND hwnd = CreateWindowExW_Original(exStyle, className, title, style, x, y,
                                         w, h, parent, menu, instance, param);
    // The tab window is returned after its views exist, on the window's own
    // thread. Confirmed with probes/probe-tabs-virtual.wh.cpp on 2026-11-13.
    if (hwnd && parent && className && !IS_INTRESOURCE(className) &&
        !g_unloading && wcscmp(className, L"ShellTabWindowClass") == 0) {
        try {
            OnTabCreated(hwnd);
        } catch (...) {
            LogHresult(L"CreateWindowExW hook");
        }
    }
    return hwnd;
}

void OnWindowCreated(Inspectable const& rootObject) {
    if (g_unloading || !g_enabled) {
        return;
    }
    auto xamlRoot = rootObject.try_as<mux::XamlRoot>();
    HWND top = TopLevelOfThisThread();
    // At window close the same method runs again, with no tab left.
    if (!xamlRoot || !top ||
        !FindWindowExW(top, nullptr, L"ShellTabWindowClass", nullptr)) {
        return;
    }
    WindowState* state = StateForThisThread(true);
    if (state->topLevel && state->topLevel != top &&
        !IsWindow(state->topLevel)) {
        // The thread's earlier window is gone. Let its objects go.
        AbandonWindow(state);
    }
    if (state->topLevel == top &&
        (state->entryTimer || state->menuItem || state->entryStopped)) {
        return;  // Already handled, this is a later resize or DPI change.
    }
    state->topLevel = top;
    state->queue = mud::DispatcherQueue::GetForCurrentThread();
    state->xamlRoot = xamlRoot;
    state->entryTries = 0;
    state->restoreSettle = 0;
    auto timer = state->queue.CreateTimer();
    timer.Interval(std::chrono::milliseconds(50));
    state->entryToken = timer.Tick(
        [](mud::DispatcherQueueTimer const&, Inspectable const&) {
            EntryTick();
        });
    timer.Start();
    state->entryTimer = timer;
    Out(L"window created, window=%p, looking for the View flyout", top);
}

using DpiChanged_t = void*(WINAPI*)(void* pThis, void* ret, void* root,
                                    void* args);
DpiChanged_t DpiChanged_Original;
void* WINAPI DpiChanged_Hook(void* pThis, void* ret, void* root, void* args) {
    // The reference is taken before the original, which moves the XamlRoot
    // out of the caller's slot.
    Inspectable hold{nullptr};
    if (!g_unloading && g_enabled) {
        try {
            if (void* object = XamlRootFromArgument(root)) {
                winrt::copy_from_abi(hold, object);
            }
        } catch (...) {
            LogHresult(L"OnDpiChanged, before");
        }
    }
    void* result = DpiChanged_Original(pThis, ret, root, args);
    if (hold) {
        try {
            OnWindowCreated(hold);
        } catch (...) {
            LogHresult(L"OnDpiChanged, after");
        }
    }
    return result;
}

using BarHandler_t = void(WINAPI*)(void* pThis, Inspectable const& sender,
                                   void* args);


BarHandler_t PointerPressed_Original;
void WINAPI PointerPressed_Hook(void* pThis, Inspectable const& sender,
                                void* args) {
    // Before the original, so the entry is there when the menu opens.
    OnCommandBarPointer(sender);
    PointerPressed_Original(pThis, sender, args);
}

BarHandler_t PointerPressedWave1_Original;
void WINAPI PointerPressedWave1_Hook(void* pThis, Inspectable const& sender,
                                     void* args) {
    OnCommandBarPointer(sender);
    PointerPressedWave1_Original(pThis, sender, args);
}

// Any command chosen in a command bar menu. The Sort menu's choices come
// through here, so this is where a change of sort or group is noticed. The
// settings are read before the command runs and again shortly after.
using InvokeMenuItem_t = void(WINAPI*)(void* pThis, void* menuItem);
InvokeMenuItem_t InvokeMenuItem_Original;
void WINAPI InvokeMenuItem_Hook(void* pThis, void* menuItem) {
    WindowState* state = g_unloading ? nullptr : StateForThisThread(false);
    if (state && state->on) {
        try {
            state->specBeforeMenu = SortSpec{};
            if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
                ReadSortSpec(browser, &state->specBeforeMenu);
            }
        } catch (...) {
        }
    }
    InvokeMenuItem_Original(pThis, menuItem);
    if (state && state->on) {
        ULONGLONG now = GetTickCount64();
        state->specCheckAt = now + 300;
        state->postActionAt = now + 150;
        state->postActionAt2 = now + 800;
        state->watchEditUntil = now + 1500;
        Out(L"toolbar command invoked");
    }
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
        {{LR"(public: struct winrt::fire_and_forget __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::OnDpiChanged(struct winrt::Microsoft::UI::Xaml::XamlRoot,struct winrt::Microsoft::UI::Xaml::XamlRootChangedEventArgs))"},
         &DpiChanged_Original,
         DpiChanged_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarManager::InvokeMenuItem(struct winrt::WindowsUdk::UI::MenuItem const &))"},
         &InvokeMenuItem_Original,
         InvokeMenuItem_Hook,
         true},
    };
    if (!Wh_SetFunctionHook((void*)TranslateAcceleratorW,
                            (void*)TranslateAcceleratorW_Hook,
                            (void**)&TranslateAcceleratorW_Original)) {
        Out(L"could not hook TranslateAcceleratorW, Backspace in the rename box may not work");
    }
    if (!Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExW_Hook,
                            (void**)&CreateWindowExW_Original)) {
        Out(L"could not hook CreateWindowExW, new tabs won't open in Columns");
    }
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Out(L"HookSymbols returned %d, PointerPressedHandler=%p, Wave1=%p, InvokeMenuItem=%p",
        ok ? 1 : 0, (void*)PointerPressed_Original,
        (void*)PointerPressedWave1_Original, (void*)InvokeMenuItem_Original);
    Out(L"OnDpiChanged=%p", (void*)DpiChanged_Original);
    g_hooked = PointerPressed_Original || PointerPressedWave1_Original ||
               DpiChanged_Original;
}

// Takes the panel and the menu entry out of every window, each on its own UI
// thread, and waits for it.
void CloseEverything() {
    std::vector<std::unique_ptr<WindowState>> states;
    {
        std::lock_guard<std::mutex> lock(g_statesMutex);
        states.swap(g_states);
    }
    for (auto& owned : states) {
        WindowState* state = owned.release();
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool queued = false;
        try {
            if (state->queue) {
                queued = state->queue.TryEnqueue([state, done]() {
                    if (state->topLevel && !IsWindow(state->topLevel)) {
                        AbandonWindow(state);
                    }
                    HidePanel(state, L"mod unloading");
                    RemoveMenuEntry(state);
                    ReleaseEntry(state);
                    state->queue = nullptr;
                    delete state;
                    if (done) {
                        SetEvent(done);
                    }
                });
            }
        } catch (...) {
            LogHresult(L"CloseEverything");
        }
        if (queued && done) {
            DWORD wait = WaitForSingleObject(done, 4000);
            Out(L"window cleanup %s",
                wait == WAIT_OBJECT_0 ? L"done" : L"TIMED OUT");
            if (wait == WAIT_OBJECT_0) {
                CloseHandle(done);
            }
        } else {
            // The window's thread is gone. Its objects can't be released from
            // here, so the state is left alone on purpose.
            Out(L"window cleanup skipped, its thread no longer takes work");
            if (done) {
                CloseHandle(done);
            }
        }
    }
}

#else  // MOD_NO_WINRT: stand-ins for a compile check without WinRT headers

bool g_hooked = false;
void HookExtensions(HMODULE) {}
void CloseEverything() {}
void StartWorker() {}
void StopWorker() {}

#endif

// Explorer can start this mod before it has loaded the module the mod hooks.
// In that case, wait for the module and hook it when it arrives.
std::atomic<bool> g_extensionsHooked{false};

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR name, HANDLE file, DWORD flags) {
    HMODULE module = LoadLibraryExW_Original(name, file, flags);
    const DWORD dataOnly = LOAD_LIBRARY_AS_DATAFILE |
                           LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE |
                           LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    if (module && !(flags & dataOnly) && !g_extensionsHooked && !g_unloading) {
        HMODULE extensions = GetModuleHandleW(L"FileExplorerExtensions.dll");
        if (extensions && !g_extensionsHooked.exchange(true)) {
            Out(L"FileExplorerExtensions.dll has loaded, hooking it now");
            HookExtensions(extensions);
            Wh_ApplyHookOperations();
            if (g_hooked) {
                StartWorker();
                Out(L"ready. Open the View menu of a File Explorer window.");
            }
        }
    }
    return module;
}

void LoadSettings() {
    g_enabled = Wh_GetIntSetting(L"columns.enabled") != 0;
    g_restore = Wh_GetIntSetting(L"columns.restoreOnOpen") != 0;
    g_sidebarRoot = Wh_GetIntSetting(L"columns.sidebarRoot") != 0;
    g_focusRect = Wh_GetIntSetting(L"columns.focusRect") != 0;
    g_compactScroll = Wh_GetIntSetting(L"columns.compactScrollbars") != 0;
    {
        const wchar_t* mode = Wh_GetStringSetting(L"columns.background");
        g_backgroundMode = mode && wcscmp(mode, L"theme") == 0 ? 1
                           : mode && wcscmp(mode, L"transparent") == 0 ? 2
                                                                       : 0;
        if (mode) {
            Wh_FreeStringSetting(mode);
        }
    }
    g_styleVersion++;
    int minWidth = Wh_GetIntSetting(L"columns.minWidth");
    int maxWidth = Wh_GetIntSetting(L"columns.maxWidth");
    minWidth = minWidth < 80 ? 80 : (minWidth > 800 ? 800 : minWidth);
    maxWidth = maxWidth < minWidth ? minWidth : (maxWidth > 1200 ? 1200 : maxWidth);
    g_minWidth = minWidth;
    g_maxWidth = maxWidth;
    g_follow = Wh_GetIntSetting(L"columns.followActiveColumn") != 0;
    int maxItems = Wh_GetIntSetting(L"columns.maxItems");
    g_maxItems = maxItems < 50 ? 50 : (maxItems > 20000 ? 20000 : maxItems);
    g_debugLog = Wh_GetIntSetting(L"debugLog") != 0;
}

}  // namespace

BOOL Wh_ModInit() {
    LoadSettings();
    OutInit();
#if defined(_M_ARM64) || defined(__aarch64__)
    const wchar_t* arch = L"arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif
    Out(L"explorer-extras v0.4.12 start, compiled as %s, pid=%lu, columns=%d width=%d..%d follow=%d",
        arch, GetCurrentProcessId(), g_enabled ? 1 : 0, g_minWidth.load(),
        g_maxWidth.load(), g_follow ? 1 : 0);

    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
        void* loadLibrary = kernelBase
            ? (void*)GetProcAddress(kernelBase, "LoadLibraryExW")
            : nullptr;
        BOOL waiting = loadLibrary &&
            Wh_SetFunctionHook(loadLibrary, (void*)LoadLibraryExW_Hook,
                               (void**)&LoadLibraryExW_Original);
        Out(L"FileExplorerExtensions.dll isn't loaded yet, waiting for it: %d",
            waiting ? 1 : 0);
        return TRUE;
    }
    g_extensionsHooked = true;
    HookExtensions(module);
    if (g_hooked) {
        StartWorker();
        Out(L"ready. Open the View menu of a File Explorer window.");
    } else {
        Out(L"no hook could be installed. The mod does nothing.");
    }
    return TRUE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    Out(L"settings changed: columns=%d width=%d..%d follow=%d maxItems=%d",
        g_enabled ? 1 : 0, g_minWidth.load(), g_maxWidth.load(),
        g_follow ? 1 : 0, g_maxItems.load());
    if (!g_enabled) {
        // Off means gone: take the entry and the panel out of every window.
        CloseEverything();
    }
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    // The worker first, so no result is queued after the windows are cleaned.
    StopWorker();
    CloseEverything();
}

void Wh_ModUninit() {
    UnregisterNotifyClass();
    Out(L"explorer-extras end");
}
