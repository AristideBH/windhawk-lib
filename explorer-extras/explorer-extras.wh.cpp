// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Optional additions to File Explorer. First one: a Columns layout in the View menu, Miller columns drawn with Explorer's own WinUI 3 controls.
// @version         0.27.5
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
  - desktopRealOnly: false
    $name: Desktop column shows only real items
    $description: Hides the virtual items of the Desktop column (This PC, Recycle Bin, Control Panel, Libraries and so on) and keeps the files, folders and shortcuts that are on the Desktop.
  - remoteMarks: true
    $name: Mark cloud, network and device items
    $description: A small glyph at the end of a row for a cloud file that is not downloaded, an item on a network location, or an item on a phone or camera.
  - rememberSort: true
    $name: Remember sort and group per folder
    $description: Each column keeps its own sort and group, taken from Explorer's view of that folder, and shows it again next time. When off, all columns share the sort of the first folder.
  - rememberWidth: true
    $name: Remember column width per folder
    $description: A column dragged to a width reopens at that width the next time the folder is shown.
  - rememberLayout: true
    $name: Remember Columns per folder
    $description: Choosing another layout in a folder, or Columns in it, is remembered for that folder. Going to a folder left in another layout turns Columns off for the tab, and going to a folder shown in Columns turns it on.
  - springLoad: true
    $name: Open a folder you hover on while dragging
    $description: Holding dragged items on a folder row for about two thirds of a second opens the folder in the next column, so a drop can go deeper without letting go.
  - harmonizeWidths: true
    $name: Shift and double-click a border to even the widths
    $description: Holding Shift while double-clicking the border of a column sets every column to the average of the widths their names would fit. The widths then stay, as if dragged by hand, until a border is double-clicked on its own.
  - keepStale: true
    $name: Keep the columns after a file is chosen
    $description: When a file, several items or nothing is chosen in a column, the columns to its right stay, dimmed, and come back when the folder is chosen again. Switch it off to close them instead.
  - dragOut: true
    $name: Drag items out of the panel
    $description: Dragging rows past the edge of the panel starts a real drag, so they can be dropped on the desktop, in another window or in another application. Switch it off if rows should never leave the panel.
  - archives: false
    $name: Browse archives in columns
    $description: A zip, 7z, rar or tar file opens as a folder in the next column, read by the shell's own archive support. Only on local fixed drives, and not above 512 MB. Archives that Windows cannot open as folders stay files.
  - headerKeyboard: false
    $name: Keyboard on group headers
    $description: Up from the first item of a group goes to its header, and Left, Right and Enter collapse and open it. Off, the arrow keys only move between items.
  - doubleClickUp: false
    $name: Double-click on empty space goes up
    $description: A double-click on the empty space of a column goes up one folder, as in the "Explorer double-click up" mod. In the first column the parent folder is added in front.
  - compactScrollbars: false
    $name: Compact scrollbars
    $description: Keeps the scrollbars of the columns thin, also when the pointer is over them. They can still be dragged and clicked.
  - focusRect: true
    $name: Show the focus outline on the active item
    $description: The outline around the item under the keyboard cursor while you navigate with the arrow keys. Off hides it, the selection highlight stays.
  - background: explorer
    $name: Panel background
    $description: Explorer takes the colour of Explorer's own file list. Theme uses the standard dark or light colour. Transparent makes Explorer's own list invisible, not removed, and lets the window's own backdrop show through the panel. Test mode.
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
  - maxItems: 5000
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
#include <commctrl.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <oleauto.h>

#undef GetCurrentTime

#include <propkey.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cwchar>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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
#include <winrt/Microsoft.UI.Xaml.Markup.h>
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


// ---- Settings ----
//
// Every setting is declared once in settings.schema.json. build.py writes the
// block Windhawk reads and the table below from it. The rest of the mod reads a
// setting by its id, never by Windhawk's key, so the window that comes next can put
// its own value in front of Windhawk's in one place.

enum class SettingKind { Bool, Int, Enum, String };

struct SettingInfo {
    const wchar_t* id;            // "dragOut".
    const wchar_t* key;           // Windhawk's name for it: "columns.dragOut", "debugLog".
    SettingKind kind;
    int defaultInt;               // Bool and Int.
    const wchar_t* defaultText;   // Enum and String.
    int minValue;                 // Int: the limits, both 0 for none.
    int maxValue;
    const wchar_t* name;
    const wchar_t* description;
    const wchar_t* page;          // "home", "columns", or "" for none.
    const wchar_t* card;          // The card of the page it sits in.
    bool windhawkOnly;            // Not shown in the settings window.
    const wchar_t* options;       // Enum: "value=Label|value=Label".
    bool rebuild;                 // A change needs the columns read or fitted again.
};

struct SettingCard {
    const wchar_t* id;
    const wchar_t* name;
};

constexpr SettingInfo kSettings[] = {
    {L"enabled", L"columns.enabled", SettingKind::Bool, 1, L"", 0, 0,
     L"Enabled",
     L"Adds the Columns entry to the View menu. Off removes it.",
     L"home", L"feature", false, L"", false},
    {L"restoreOnOpen", L"columns.restoreOnOpen", SettingKind::Bool, 1, L"", 0, 0,
     L"Open new windows in Columns",
     L"When the last layout you chose was Columns, a new Explorer window opens in Columns too. Choosing any other layout in the View menu turns this off.",
     L"columns", L"behaviour", false, L"", false},
    {L"desktopRealOnly", L"columns.desktopRealOnly", SettingKind::Bool, 0, L"", 0, 0,
     L"Desktop column shows only real items",
     L"Hides the virtual items of the Desktop column (This PC, Recycle Bin, Control Panel, Libraries and so on) and keeps the files, folders and shortcuts that are on the Desktop.",
     L"columns", L"content", false, L"", true},
    {L"remoteMarks", L"columns.remoteMarks", SettingKind::Bool, 1, L"", 0, 0,
     L"Mark cloud, network and device items",
     L"A small glyph at the end of a row for a cloud file that is not downloaded, an item on a network location, or an item on a phone or camera.",
     L"columns", L"content", false, L"", true},
    {L"rememberSort", L"columns.rememberSort", SettingKind::Bool, 1, L"", 0, 0,
     L"Remember sort and group per folder",
     L"Each column keeps its own sort and group, taken from Explorer's view of that folder, and shows it again next time. When off, all columns share the sort of the first folder.",
     L"columns", L"dragmemory", false, L"", false},
    {L"rememberWidth", L"columns.rememberWidth", SettingKind::Bool, 1, L"", 0, 0,
     L"Remember column width per folder",
     L"A column dragged to a width reopens at that width the next time the folder is shown.",
     L"columns", L"dragmemory", false, L"", false},
    {L"rememberLayout", L"columns.rememberLayout", SettingKind::Bool, 1, L"", 0, 0,
     L"Remember Columns per folder",
     L"Choosing another layout in a folder, or Columns in it, is remembered for that folder. Going to a folder left in another layout turns Columns off for the tab, and going to a folder shown in Columns turns it on.",
     L"columns", L"dragmemory", false, L"", false},
    {L"springLoad", L"columns.springLoad", SettingKind::Bool, 1, L"", 0, 0,
     L"Open a folder you hover on while dragging",
     L"Holding dragged items on a folder row for about two thirds of a second opens the folder in the next column, so a drop can go deeper without letting go.",
     L"columns", L"dragmemory", false, L"", false},
    {L"harmonizeWidths", L"columns.harmonizeWidths", SettingKind::Bool, 1, L"", 0, 0,
     L"Shift and double-click a border to even the widths",
     L"Holding Shift while double-clicking the border of a column sets every column to the average of the widths their names would fit. The widths then stay, as if dragged by hand, until a border is double-clicked on its own.",
     L"columns", L"layout", false, L"", true},
    {L"keepStale", L"columns.keepStale", SettingKind::Bool, 1, L"", 0, 0,
     L"Keep the columns after a file is chosen",
     L"When a file, several items or nothing is chosen in a column, the columns to its right stay, dimmed, and come back when the folder is chosen again. Switch it off to close them instead.",
     L"columns", L"behaviour", false, L"", false},
    {L"dragOut", L"columns.dragOut", SettingKind::Bool, 1, L"", 0, 0,
     L"Drag items out of the panel",
     L"Dragging rows past the edge of the panel starts a real drag, so they can be dropped on the desktop, in another window or in another application. Switch it off if rows should never leave the panel.",
     L"columns", L"dragmemory", false, L"", false},
    {L"archives", L"columns.archives", SettingKind::Bool, 0, L"", 0, 0,
     L"Browse archives in columns",
     L"A zip, 7z, rar or tar file opens as a folder in the next column, read by the shell's own archive support. Only on local fixed drives, and not above 512 MB. Archives that Windows cannot open as folders stay files.",
     L"columns", L"content", false, L"", true},
    {L"headerKeyboard", L"columns.headerKeyboard", SettingKind::Bool, 0, L"", 0, 0,
     L"Keyboard on group headers",
     L"Up from the first item of a group goes to its header, and Left, Right and Enter collapse and open it. Off, the arrow keys only move between items.",
     L"columns", L"behaviour", false, L"", false},
    {L"doubleClickUp", L"columns.doubleClickUp", SettingKind::Bool, 0, L"", 0, 0,
     L"Double-click on empty space goes up",
     L"A double-click on the empty space of a column goes up one folder, as in the \"Explorer double-click up\" mod. In the first column the parent folder is added in front.",
     L"columns", L"behaviour", false, L"", false},
    {L"compactScrollbars", L"columns.compactScrollbars", SettingKind::Bool, 0, L"", 0, 0,
     L"Compact scrollbars",
     L"Keeps the scrollbars of the columns thin, also when the pointer is over them. They can still be dragged and clicked.",
     L"columns", L"layout", false, L"", true},
    {L"focusRect", L"columns.focusRect", SettingKind::Bool, 1, L"", 0, 0,
     L"Show the focus outline on the active item",
     L"The outline around the item under the keyboard cursor while you navigate with the arrow keys. Off hides it, the selection highlight stays.",
     L"columns", L"layout", false, L"", true},
    {L"background", L"columns.background", SettingKind::Enum, 0, L"explorer", 0, 0,
     L"Panel background",
     L"Explorer takes the colour of Explorer's own file list. Theme uses the standard dark or light colour. Transparent makes Explorer's own list invisible, not removed, and lets the window's own backdrop show through the panel. Test mode.",
     L"columns", L"layout", false, L"explorer=Explorer's file list colour|theme=Theme colour|transparent=Transparent (test)", true},
    {L"sidebarRoot", L"columns.sidebarRoot", SettingKind::Bool, 0, L"", 0, 0,
     L"Sidebar sets the first column",
     L"A click on Home, Gallery, OneDrive or a pinned folder in the navigation pane makes that folder the first column. The expandable tree (This PC, drives, Network) keeps following the columns as before.",
     L"columns", L"behaviour", false, L"", false},
    {L"minWidth", L"columns.minWidth", SettingKind::Int, 160, L"", 80, 800,
     L"Minimum column width",
     L"",
     L"columns", L"layout", false, L"", true},
    {L"maxWidth", L"columns.maxWidth", SettingKind::Int, 420, L"", 80, 1200,
     L"Maximum column width",
     L"Columns fit their longest name up to this width. Longer names are cut in the middle.",
     L"columns", L"layout", false, L"", true},
    {L"followActiveColumn", L"columns.followActiveColumn", SettingKind::Bool, 1, L"", 0, 0,
     L"Explorer follows the active column",
     L"Explorer's own file list navigates to the folder of the column you are in, so the address bar, Back, the preview pane and the status bar stay in step.",
     L"columns", L"behaviour", false, L"", false},
    {L"maxItems", L"columns.maxItems", SettingKind::Int, 5000, L"", 50, 20000,
     L"Maximum items per column",
     L"",
     L"columns", L"content", false, L"", true},
    {L"debugLog", L"debugLog", SettingKind::Bool, 1, L"", 0, 0,
     L"Write a debug log",
     L"Writes to Code\\windhawk-lib\\probes\\logs\\explorer-extras.log in your user folder, if that folder exists.",
     L"", L"", true, L"", false},
};

constexpr SettingCard kCards[] = {
    {L"layout", L"Layout and look"},
    {L"behaviour", L"Behaviour"},
    {L"dragmemory", L"Drag and drop and memory"},
    {L"content", L"Content and advanced"},
};

constexpr const wchar_t* kWhatsNew[] = {
    L"Extras in the View menu opens this window. Settings apply at once, and Reset goes back to Windhawk\u2019s values.",
    L"Hold Shift and double-click a column border to give every column the same width.",
    L"Drag rows to another window, the desktop, the navigation pane or the tab strip. Hold them on a folder to open it.",
    L"Archives can be dragged out of, and a phone\u2019s folders and OneDrive syncing show their own marks.",
    L"The toolbar\u2019s Select all, Invert selection and Select none reach the columns.",
};
constexpr size_t kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);
constexpr size_t kCardCount = sizeof(kCards) / sizeof(kCards[0]);
constexpr size_t kWhatsNewCount = sizeof(kWhatsNew) / sizeof(kWhatsNew[0]);

const SettingInfo* FindSetting(const wchar_t* id) {
    for (size_t i = 0; i < kSettingCount; i++) {
        if (wcscmp(kSettings[i].id, id) == 0) {
            return &kSettings[i];
        }
    }
    return nullptr;
}

// ---- Where a value comes from ----
//
// Windhawk's page holds the default. The settings window writes an override, kept
// in the mod's own storage as text under "ui.<id>", together with the value
// Windhawk's page had at that moment under "ui.seen.<id>". The value in use is the
// override, unless Windhawk's page has been edited since: that difference is a new
// edit on the page, and it wins and clears the override.

std::wstring WindhawkValueText(const SettingInfo* info) {
    if (info->kind == SettingKind::Bool || info->kind == SettingKind::Int) {
        return std::to_wstring(Wh_GetIntSetting(info->key));
    }
    std::wstring result = info->defaultText;
    if (const wchar_t* value = Wh_GetStringSetting(info->key)) {
        result = value;
        Wh_FreeStringSetting(value);
    }
    return result;
}

bool ReadStored(const std::wstring& name, std::wstring* out) {
    wchar_t buffer[256];
    size_t length = Wh_GetStringValue(name.c_str(), buffer, ARRAYSIZE(buffer));
    if (!length) {
        return false;
    }
    *out = buffer;
    return true;
}

std::wstring EffectiveText(const SettingInfo* info) {
    std::wstring windhawk = WindhawkValueText(info);
    std::wstring override;
    if (ReadStored(std::wstring(L"ui.") + info->id, &override)) {
        std::wstring seen;
        if (ReadStored(std::wstring(L"ui.seen.") + info->id, &seen) && seen != windhawk) {
            Wh_DeleteValue((std::wstring(L"ui.") + info->id).c_str());
            Wh_DeleteValue((std::wstring(L"ui.seen.") + info->id).c_str());
            return windhawk;  // Edited on Windhawk's page since: the page wins.
        }
        return override;
    }
    return windhawk;
}

bool SettingIsOverridden(const SettingInfo* info) {
    std::wstring stored;
    return ReadStored(std::wstring(L"ui.") + info->id, &stored);
}

// The window's choice. The same as Windhawk's value needs no override, and the
// override that was there goes.
void WriteSettingOverride(const SettingInfo* info, const std::wstring& text) {
    std::wstring name = std::wstring(L"ui.") + info->id;
    std::wstring seenName = std::wstring(L"ui.seen.") + info->id;
    std::wstring windhawk = WindhawkValueText(info);
    if (text == windhawk) {
        Wh_DeleteValue(name.c_str());
        Wh_DeleteValue(seenName.c_str());
        return;
    }
    Wh_SetStringValue(name.c_str(), text.c_str());
    Wh_SetStringValue(seenName.c_str(), windhawk.c_str());
}

void ClearAllOverrides() {
    for (size_t i = 0; i < kSettingCount; i++) {
        Wh_DeleteValue((std::wstring(L"ui.") + kSettings[i].id).c_str());
        Wh_DeleteValue((std::wstring(L"ui.seen.") + kSettings[i].id).c_str());
    }
}

int SettingInt(const wchar_t* id) {
    const SettingInfo* info = FindSetting(id);
    if (!info) {
        return 0;
    }
    int value = _wtoi(EffectiveText(info).c_str());
    if (info->minValue || info->maxValue) {
        value = std::clamp(value, info->minValue, info->maxValue);
    }
    return value;
}

bool SettingBool(const wchar_t* id) {
    return SettingInt(id) != 0;
}

std::wstring SettingText(const wchar_t* id) {
    const SettingInfo* info = FindSetting(id);
    return info ? EffectiveText(info) : std::wstring();
}
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

// The window of the panel, while it is on. Backspace goes to the columns, not
// to Explorer's own Back accelerator, when the keyboard is in it.
thread_local HWND g_panelWindow = nullptr;

using TranslateAcceleratorW_t = int(WINAPI*)(HWND, HACCEL, LPMSG);
TranslateAcceleratorW_t TranslateAcceleratorW_Original;
int WINAPI TranslateAcceleratorW_Hook(HWND hwnd, HACCEL accel, LPMSG msg) {
    if (g_renaming && msg &&
        (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP ||
         msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP)) {
        return 0;  // Not translated: the message goes on to the focused control.
    }
    // Keys the panel takes for itself while the keyboard is in it: Backspace,
    // and Ctrl+Shift+N, which Explorer would use to make a folder in its own,
    // hidden view.
    bool panelKey = false;
    if (g_panelWindow && msg && (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP)) {
        panelKey = msg->wParam == VK_BACK ||
                   (msg->wParam == 'N' && (GetKeyState(VK_CONTROL) & 0x8000) &&
                    (GetKeyState(VK_SHIFT) & 0x8000));
    }
    if (panelKey) {
        HWND focus = GetFocus();
        if (focus && (focus == g_panelWindow || IsChild(g_panelWindow, focus))) {
            return 0;
        }
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

// A file the shell can open as a folder, that is small enough and on a local
// fixed drive: such archives are read in the next column.
bool IsBrowsableArchive(IShellFolder* folder, PCUITEMID_CHILD child, const std::wstring& name) {
    if (!g_archives) {
        return false;
    }
    SFGAOF attributes = SFGAO_FOLDER | SFGAO_STREAM;
    if (FAILED(folder->GetAttributesOf(1, &child, &attributes)) ||
        !(attributes & SFGAO_FOLDER) || !(attributes & SFGAO_STREAM)) {
        return false;
    }
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    static const wchar_t* kinds[] = {L".zip", L".7z", L".rar", L".tar", L".tgz",
                                     L".gz", L".bz2", L".tbz", L".xz", L".zst"};
    bool known = false;
    for (auto kind : kinds) {
        known = known || _wcsicmp(name.c_str() + dot, kind) == 0;
    }
    if (!known) {
        return false;
    }
    STRRET text = {};
    wchar_t path[MAX_PATH] = L"";
    if (FAILED(folder->GetDisplayNameOf(child, SHGDN_FORPARSING, &text)) ||
        FAILED(StrRetToBufW(&text, child, path, MAX_PATH)) || !path[0] || path[1] != L':') {
        return false;  // Not a plain local path.
    }
    wchar_t root[4] = {path[0], L':', L'\\', 0};
    if (GetDriveTypeW(root) != DRIVE_FIXED) {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
        return false;
    }
    ULONGLONG size = ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    return size <= 512ULL * 1024 * 1024;
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

// Whether a cloud item is transferring now: the storage provider (OneDrive)
// publishes System.StorageProviderTransferProgress while it uploads or downloads
// and drops it when it is done.
bool IsSyncing(IShellFolder* folder, PCUITEMID_CHILD child) {
    IShellItem2* item = nullptr;
    if (FAILED(SHCreateItemWithParent(nullptr, folder, child, IID_PPV_ARGS(&item))) || !item) {
        return false;
    }
    static const PROPERTYKEY transfer = {{0xE77E90DF, 0x6271, 0x4F5B, {0x83, 0x4F, 0x2D, 0xD1, 0xF2, 0x45, 0xDD, 0xA4}}, 4};
    ULONGLONG value = 0;
    bool syncing = SUCCEEDED(item->GetUInt64(transfer, &value)) && value != 0;
    item->Release();
    return syncing;
}

// Whether an item is not plainly on this disk: a cloud file that is not
// downloaded, a network location, or a phone or camera. Read on the worker thread.
int RemoteKind(IShellFolder* folder, PCUITEMID_CHILD child) {
    std::wstring name = ParsingName(folder, child);
    if (name.empty()) {
        return 0;
    }
    if (StrStrIW(name.c_str(), L"\\\\?\\usb#") || StrStrIW(name.c_str(), L"\\\\?\\swd#") ||
        StrStrIW(name.c_str(), L"\\\\?\\wpdbusenum") || StrStrIW(name.c_str(), L"\\CrossDevice\\")) {
        return 3;  // A phone, by its device path or by Phone Link's folder.
    }
    if (name.size() > 2 && name[0] == L'\\' && name[1] == L'\\') {
        return 2;
    }
    if (name.size() > 2 && name[1] == L':') {
        static std::atomic<int> kinds[26];  // 0 unknown, 1 local, 2 remote
        wchar_t letter = (wchar_t)towupper(name[0]);
        if (letter >= L'A' && letter <= L'Z') {
            int kind = kinds[letter - L'A'];
            if (!kind) {
                wchar_t root[4] = {letter, L':', L'\\', 0};
                kind = GetDriveTypeW(root) == DRIVE_REMOTE ? 2 : 1;
                kinds[letter - L'A'] = kind;
            }
            if (kind == 2) {
                return 2;
            }
        }
        // A synced (OneDrive style) item: not downloaded, always kept, or
        // downloaded. The reparse tag tells a cloud file from a plain link.
        WIN32_FIND_DATAW found;
        HANDLE find = FindFirstFileW(name.c_str(), &found);
        if (find != INVALID_HANDLE_VALUE) {
            FindClose(find);
            DWORD attributes = found.dwFileAttributes;
            bool cloud = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                         (found.dwReserved0 & 0xF000FFFF) == 0x9000001A;
            bool placeholder = cloud || (attributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
                                                       FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE));
            if (placeholder && IsSyncing(folder, child)) {
                return 6;
            }
            if (attributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN |
                              FILE_ATTRIBUTE_OFFLINE)) {
                return 1;
            }
            if (cloud && (attributes & 0x00080000)) {  // FILE_ATTRIBUTE_PINNED
                return 4;
            }
            if (cloud && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                return 5;
            }
        }
    }
    return 0;
}

// For a shortcut to a folder: the folder it points to, as an absolute pidl.
Pidl FolderLinkTarget(IShellFolder* folder, PCUITEMID_CHILD child) {
    Pidl result;
    SFGAOF attributes = SFGAO_LINK | SFGAO_FOLDER;
    if (FAILED(folder->GetAttributesOf(1, &child, &attributes)) ||
        !(attributes & SFGAO_LINK) || (attributes & SFGAO_FOLDER)) {
        return result;
    }
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemWithParent(nullptr, folder, child, IID_PPV_ARGS(&item))) && item) {
        IShellItem* target = nullptr;
        if (SUCCEEDED(item->BindToHandler(nullptr, BHID_LinkTargetItem, IID_PPV_ARGS(&target))) &&
            target) {
            SFGAOF kind = 0;
            if (SUCCEEDED(target->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &kind)) &&
                (kind & SFGAO_FOLDER) && !(kind & SFGAO_STREAM)) {
                PIDLIST_ABSOLUTE pidl = nullptr;
                if (SUCCEEDED(SHGetIDListFromObject(target, &pidl)) && pidl) {
                    result.reset(pidl);
                }
            }
            target->Release();
        }
        item->Release();
    }
    return result;
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
    size_t total = 0;  // How many items the folder has, up to the reading cap.
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

constexpr int kReadCap = 20000;  // The most items read from one folder.

// Reads a folder, sorted and grouped as `spec` says. Runs on the worker
// thread, with shell objects of its own.
void LoadFolder(PCIDLIST_ABSOLUTE folderPidl, const SortSpec& spec,
                bool includeHidden, int maxItems, FolderResult* result) {
    ULONGLONG loadStarted = GetTickCount64();
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
            // The folder is read whole, up to a safety cap, and cut to the
            // limit after it is sorted, so the items kept are the first ones
            // in sort order, not whichever the shell listed first.
            if ((int)result->entries.size() >= kReadCap) {
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
            if (g_desktopRealOnly && ILIsEmpty(folderPidl)) {
                SFGAOF real = SFGAO_FILESYSTEM;
                if (FAILED(folder->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&child, &real)) ||
                    !(real & SFGAO_FILESYSTEM)) {
                    CoTaskMemFree(child);
                    child = nullptr;
                    continue;
                }
            }
            Entry entry;
            entry.name = DisplayName(folder, child);
            // One call for what the row needs to know: each one is a round trip to a
            // phone's driver, and a camera folder holds thousands of items.
            SFGAOF attributes = SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM | SFGAO_LINK;
            bool known = SUCCEEDED(folder->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&child, &attributes));
            entry.folder = known && (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
            if (!entry.folder && IsBrowsableArchive(folder, child, entry.name)) {
                entry.folder = true;
                entry.archive = true;
            }
            if (!entry.folder && known && (attributes & SFGAO_LINK) && !(attributes & SFGAO_FOLDER)) {
                entry.linkTarget = FolderLinkTarget(folder, child);
                if (entry.linkTarget) {
                    entry.folder = true;
                }
            }
            entry.launcher = !entry.folder && known && !(attributes & (SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM));
            if (g_remoteMarks) {
                entry.remote = RemoteKind(folder, child);
            }
            entry.child.reset(child);
            result->entries.push_back(std::move(entry));
            child = nullptr;
        }
        list->Release();
    }
    result->ok = SUCCEEDED(hr);
    {
        // A folder that takes a while is worth a line: a phone is read item by item.
        ULONGLONG took = GetTickCount64() - loadStarted;
        if (took >= 500 || !result->ok) {
            PWSTR where = nullptr;
            std::wstring key;
            if (SUCCEEDED(SHGetNameFromIDList(folderPidl, SIGDN_DESKTOPABSOLUTEPARSING, &where)) && where) {
                key = where;
                CoTaskMemFree(where);
            }
            Out(L"load: \"%s\" read %d items in %d ms, enumeration hr=0x%08lX", key.c_str(),
                (int)result->entries.size(), (int)took, hr);
        }
    }
    if (g_remoteMarks) {
        int marked = 0, syncing = 0;
        for (auto& entry : result->entries) {
            marked += entry.remote != 0;
            syncing += entry.remote == 6;
        }
        // One line per folder, again only when the numbers change.
        [[clang::no_destroy]] static std::mutex lock;
        [[clang::no_destroy]] static std::map<std::wstring, int> last;
        std::wstring key;
        PWSTR text = nullptr;
        if (SUCCEEDED(SHGetNameFromIDList(folderPidl, SIGDN_DESKTOPABSOLUTEPARSING, &text)) && text) {
            key = text;
            CoTaskMemFree(text);
        }
        std::lock_guard<std::mutex> guard(lock);
        int summary = marked * 10000 + syncing;
        auto at = last.find(key);
        if ((at == last.end() ? marked != 0 || syncing != 0 : at->second != summary)) {
            Out(L"marks in \"%s\": %d cloud, network or device, %d syncing", key.c_str(), marked, syncing);
        }
        last[key] = summary;
    }

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

    result->total = result->entries.size();
    if ((int)result->entries.size() > maxItems) {
        result->entries.erase(result->entries.begin() + maxItems, result->entries.end());
        result->truncated = true;
    }

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

// The items selected in Explorer's own view, as child ids.
std::vector<Pidl> ExplorerSelection(IShellBrowser* browser) {
    std::vector<Pidl> result;
    IFolderView* folderView = FolderViewOf(browser);
    if (!folderView) {
        return result;
    }
    IShellItemArray* array = nullptr;
    if (SUCCEEDED(folderView->Items(SVGIO_SELECTION, IID_PPV_ARGS(&array))) && array) {
        DWORD count = 0;
        array->GetCount(&count);
        for (DWORD i = 0; i < count; i++) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(array->GetItemAt(i, &item)) && item) {
                PIDLIST_ABSOLUTE absolute = nullptr;
                if (SUCCEEDED(SHGetIDListFromObject(item, &absolute)) && absolute) {
                    result.emplace_back(ILClone(ILFindLastID(absolute)));
                    CoTaskMemFree(absolute);
                }
                item->Release();
            }
        }
        array->Release();
    }
    folderView->Release();
    return result;
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
    // A file on disk is opened by its path: an item of a synced folder such as
    // OneDrive has a pidl that ShellExecute handles badly (it does nothing).
    wchar_t path[MAX_PATH] = L"";
    bool byPath = SHGetPathFromIDListW(pidl, path) && path[0];
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.hwnd = owner;
    info.nShow = SW_SHOWNORMAL;
    if (byPath) {
        info.fMask = SEE_MASK_DEFAULT;
        info.lpFile = path;
    } else {
        info.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
        info.lpIDList = (void*)pidl;
    }
    bool ok = ShellExecuteExW(&info);
    Out(L"OpenItem %s: %s, gle=%lu, hInstApp=%llu", byPath ? L"by path" : L"by pidl",
        ok ? L"ok" : L"failed", ok ? 0 : GetLastError(), (unsigned long long)(ULONG_PTR)info.hInstApp);
}


// ---- What is remembered per folder ----
//
// Sort and group, the width of a column and whether the folder is shown in
// Columns. Kept across restarts in one value of the mod's storage, as lines of
// "path TAB sort TAB width TAB columns". The most recent 150 folders are kept.

struct FolderMemory {
    bool hasSpec = false;
    SortSpec spec;
    double width = 0;
    int columns = -1;  // -1 not known, 0 not shown in Columns, 1 shown in Columns.
};

std::mutex g_memoryMutex;
[[clang::no_destroy]] std::map<std::wstring, FolderMemory> g_memory;
[[clang::no_destroy]] std::vector<std::wstring> g_memoryOrder;  // Oldest first.
bool g_memoryLoaded = false;
constexpr size_t kMemoryLimit = 150;

std::wstring MemoryKey(std::wstring const& path) {
    std::wstring key = path;
    for (auto& c : key) {
        c = (wchar_t)towlower(c);
    }
    return key;
}

std::wstring GuidText(GUID const& guid) {
    wchar_t text[64] = L"";
    StringFromGUID2(guid, text, ARRAYSIZE(text));
    return text;
}

std::wstring SpecToText(SortSpec const& spec) {
    std::wstring text;
    for (auto& column : spec.columns) {
        text += GuidText(column.propkey.fmtid) + L"," + std::to_wstring(column.propkey.pid) + L"," +
                std::to_wstring((int)column.direction) + L";";
    }
    text += L"/";
    if (spec.grouped) {
        text += GuidText(spec.groupKey.fmtid) + L"," + std::to_wstring(spec.groupKey.pid) + L"," +
                (spec.groupAscending ? L"1" : L"0");
    }
    return text;
}

// One "{guid},pid,number" field.
bool ParseKeyField(std::wstring const& field, PROPERTYKEY* key, int* number) {
    size_t first = field.find(L',');
    size_t second = first == std::wstring::npos ? first : field.find(L',', first + 1);
    if (second == std::wstring::npos) {
        return false;
    }
    GUID guid;
    if (FAILED(CLSIDFromString(field.substr(0, first).c_str(), &guid))) {
        return false;
    }
    key->fmtid = guid;
    key->pid = (DWORD)wcstoul(field.substr(first + 1, second - first - 1).c_str(), nullptr, 10);
    *number = (int)wcstol(field.substr(second + 1).c_str(), nullptr, 10);
    return true;
}

bool SpecFromText(std::wstring const& text, SortSpec* spec) {
    size_t slash = text.find(L'/');
    if (slash == std::wstring::npos) {
        return false;
    }
    SortSpec result;
    size_t start = 0;
    std::wstring columns = text.substr(0, slash);
    while (start < columns.size()) {
        size_t end = columns.find(L';', start);
        if (end == std::wstring::npos) {
            end = columns.size();
        }
        PROPERTYKEY key{};
        int direction = 0;
        if (ParseKeyField(columns.substr(start, end - start), &key, &direction)) {
            SORTCOLUMN sort{};
            sort.propkey = key;
            sort.direction = (SORTDIRECTION)direction;
            result.columns.push_back(sort);
        }
        start = end + 1;
    }
    std::wstring group = text.substr(slash + 1);
    if (!group.empty()) {
        PROPERTYKEY key{};
        int ascending = 1;
        if (ParseKeyField(group, &key, &ascending)) {
            result.grouped = true;
            result.groupKey = key;
            result.groupAscending = ascending != 0;
        }
    }
    *spec = result;
    return true;
}

void SaveFolderMemoryLocked() {
    std::wstring text;
    for (auto& key : g_memoryOrder) {
        auto found = g_memory.find(key);
        if (found == g_memory.end()) {
            continue;
        }
        auto& memory = found->second;
        text += key + L"\t" + (memory.hasSpec ? SpecToText(memory.spec) : L"") + L"\t" +
                std::to_wstring((int)memory.width) + L"\t" + std::to_wstring(memory.columns) + L"\n";
    }
    Wh_SetStringValue(L"folderMemory", text.c_str());
}

void LoadFolderMemoryLocked() {
    if (g_memoryLoaded) {
        return;
    }
    g_memoryLoaded = true;
    std::vector<wchar_t> buffer(120000);
    size_t length = Wh_GetStringValue(L"folderMemory", buffer.data(), buffer.size());
    if (!length) {
        return;
    }
    std::wstring text(buffer.data(), length);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring line = text.substr(start, end - start);
        start = end + 1;
        size_t a = line.find(L'\t');
        size_t b = a == std::wstring::npos ? a : line.find(L'\t', a + 1);
        size_t c = b == std::wstring::npos ? b : line.find(L'\t', b + 1);
        if (c == std::wstring::npos) {
            continue;
        }
        std::wstring key = line.substr(0, a);
        FolderMemory memory;
        std::wstring spec = line.substr(a + 1, b - a - 1);
        if (!spec.empty() && SpecFromText(spec, &memory.spec)) {
            memory.hasSpec = true;
        }
        memory.width = wcstod(line.substr(b + 1, c - b - 1).c_str(), nullptr);
        memory.columns = (int)wcstol(line.substr(c + 1).c_str(), nullptr, 10);
        g_memory[key] = memory;
        g_memoryOrder.push_back(key);
    }
}

FolderMemory GetFolderMemory(std::wstring const& path) {
    std::lock_guard<std::mutex> lock(g_memoryMutex);
    LoadFolderMemoryLocked();
    auto found = g_memory.find(MemoryKey(path));
    FolderMemory memory = found == g_memory.end() ? FolderMemory() : found->second;
    // What the settings turn off is not given out.
    if (!g_rememberSort) {
        memory.hasSpec = false;
    }
    if (!g_rememberWidth) {
        memory.width = 0;
    }
    if (!g_rememberLayout) {
        memory.columns = -1;
    }
    return memory;
}

// Updates one folder's entry and writes the store when something changed.
template <typename Change>
void ChangeFolderMemory(std::wstring const& path, Change change) {
    if (path.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_memoryMutex);
    LoadFolderMemoryLocked();
    std::wstring key = MemoryKey(path);
    FolderMemory memory;
    auto found = g_memory.find(key);
    if (found != g_memory.end()) {
        memory = found->second;
    }
    FolderMemory before = memory;
    change(memory);
    bool same = before.hasSpec == memory.hasSpec && before.width == memory.width &&
                before.columns == memory.columns &&
                (!memory.hasSpec || SameSpec(before.spec, memory.spec));
    if (same && found != g_memory.end()) {
        return;
    }
    g_memory[key] = memory;
    g_memoryOrder.erase(std::remove(g_memoryOrder.begin(), g_memoryOrder.end(), key),
                        g_memoryOrder.end());
    g_memoryOrder.push_back(key);
    while (g_memoryOrder.size() > kMemoryLimit) {
        g_memory.erase(g_memoryOrder.front());
        g_memoryOrder.erase(g_memoryOrder.begin());
    }
    SaveFolderMemoryLocked();
}

void RememberFolderSpec(std::wstring const& path, SortSpec const& spec) {
    if (!g_rememberSort) {
        return;
    }
    ChangeFolderMemory(path, [&](FolderMemory& memory) {
        memory.hasSpec = true;
        memory.spec = spec;
    });
}

void RememberFolderWidth(std::wstring const& path, double width) {
    if (!g_rememberWidth) {
        return;
    }
    ChangeFolderMemory(path, [&](FolderMemory& memory) { memory.width = width; });
}

void RememberFolderColumns(std::wstring const& path, int columns) {
    if (!g_rememberLayout) {
        return;
    }
    ChangeFolderMemory(path, [&](FolderMemory& memory) { memory.columns = columns; });
}

int RememberedColumns(std::wstring const& path) {
    return GetFolderMemory(path).columns;
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

struct IconBits;

struct Column {
    Pidl folder;
    std::vector<Entry> entries;
    std::vector<int> rowToEntry;         // Entry index per row, -1 for a header.
    std::vector<muxc::TextBlock> texts;  // Name text per entry.
    std::vector<muxc::Grid> iconHosts;   // Icon cell per entry.
    std::vector<muxc::FontIcon> chevrons;  // Chevron per entry, folders only.
    int hintEntry = -1;  // The entry whose chevron wears the badge.
    muxc::Border hintBadge{nullptr};  // The small rounded background behind that chevron.
    std::vector<uint32_t> shapedRows;  // Rows whose corners and gaps were changed for a run of selected rows.
    int hoverHeader = -1;  // The group header under the pointer.
    int focusHeader = -1;  // The group header the keyboard is on.
    winrt::event_token moveToken{};
    winrt::event_token pressToken{};
    mux::Input::PointerEventHandler stopScrollHandler{nullptr};  // A press stops a scroll in flight.
    winrt::event_token releaseToken{};
    winrt::event_token captureLostToken{};
    winrt::event_token exitToken{};
    bool filled = false;  // Has shown a listing at least once.
    std::shared_ptr<std::atomic<bool>> cancel;  // Set when this column's load is not wanted.
    int scrollStyle = -1;  // The scrollbar style these rows were given.
    double gutterRight = 10;  // The strip kept free on the right, in px.
    std::vector<char> tipBuilt;  // Per entry: the detail tooltip exists.
    // Rows are built when they come near the screen. Every entry has a row
    // shell from the start; its content is made later.
    std::vector<int> entryToRow;   // The row of each entry.
    std::vector<char> iconAsked;   // Per entry: an icon was asked for beyond the first batch.
    std::vector<size_t> iconWanted;  // Entries built since, waiting to be asked for.
    std::vector<char> built;       // Per entry: the row has its content.
    std::map<size_t, std::shared_ptr<IconBits>> pendingIcons;  // Icons that came before their row.
    muxc::ScrollViewer viewer{nullptr};  // The list's own scroller, watched for scrolling.
    winrt::event_token viewToken{};
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
    SortSpec spec;                      // This folder's own sort and group,
    bool hasSpec = false;               // when it has one. Otherwise the window's.
    double restoreScroll = -1;          // A scroll offset to put back once the rows are in.
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
    // Vertical wheel turns are scrolled by this mod, so a press can end a glide.
    ULONGLONG lastWheelAt = 0;
    bool wheelSuppress = false;  // After a press: wheel turns are dropped until a gap.
    double wheelTarget = 0;
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
    bool insideArchive = false;  // The items are inside an archive that is being browsed.
    muxc::Primitives::FlyoutBase flyout{nullptr};
    winrt::event_token closedToken{};
    std::vector<std::pair<muxc::MenuFlyoutItem, winrt::event_token>> hooks;
    std::vector<std::pair<muxc::Button, winrt::event_token>> buttonHooks;
};

// What the user did in the columns that Ctrl+Z can take back. Explorer's own
// undo is out of reach from here, so deletes and renames are kept by the mod.
struct UndoEntry {
    bool deletion = false;
    std::vector<std::wstring> paths;  // Deleted paths, to restore.
    std::wstring renamedFrom;         // A rename: the old path
    std::wstring renamedTo;           // and the new one.
    bool created = false;             // A new folder, whose path is in renamedTo.
    std::vector<std::pair<std::wstring, std::wstring>> moves;  // Items moved into it, old and new path.
    std::vector<std::pair<std::wstring, std::wstring>> renames;  // Several renames at once, old and new.
};

// An item being renamed in place.
struct RenameState {
    Column* column = nullptr;
    size_t entry = 0;
    muxc::Grid grid{nullptr};
    muxc::TextBlock text{nullptr};
    muxc::TextBox box{nullptr};
    std::vector<size_t> multi;  // Several items renamed together: all their entries.
    winrt::event_token keyToken{};
    winrt::event_token lostToken{};
    ULONGLONG startedAt = 0;
    bool done = false;
};

struct Worker;

// A drag of rows from one column to a folder, followed by the mod itself.
// What the drag label shows: the name or the count, and the icons of the first
// items. Kept apart from the columns, so the label can be shown again, when a
// drag that left the panel comes back to it.
struct GhostSpec {
    std::wstring name;
    size_t count = 0;
    std::vector<mux::Media::ImageSource> icons;  // Up to three, a null one when not loaded.
};

struct DragState {
    bool pending = false;  // Pressed on a row, the pointer has not moved far yet.
    bool active = false;   // Dragging.
    Column* column = nullptr;
    int pressedEntry = -1;
    std::vector<size_t> entries;
    winrt::Windows::Foundation::Point start{};  // In the source list's coordinates.
    Column* targetColumn = nullptr;
    int targetEntry = -1;  // The folder row under the pointer, or -1 for a column.
    bool targetOk = false;
    bool lastCtrl = false;  // Ctrl as it was when the target was last judged.
    bool fromArchive = false;  // The dragged items are inside an archive: a drop extracts them.
    muxc::Border frame{nullptr};  // The accent frame around the row or column a drop would go to.
    mux::FrameworkElement ghost{nullptr};
    muxc::TextBlock ghostText{nullptr};
    muxc::TextBlock ghostAction{nullptr};  // What a drop would do, under the name.
    muxc::FontIcon ghostIcon{nullptr};     // The pictogram before it.
    muxc::StackPanel ghostRow{nullptr};    // The pictogram and the line.
    std::wstring labelName;                // The first line of the label.
};

// The columns of a tab that is not showing, kept to put back when it is.
struct TabColumnSnapshot {
    double scroll = 0;  // The column's vertical scroll offset.
    Pidl folder;
    Pidl selected;
    double width = 0;
    bool userSized = false;
};
struct TabSnapshot {
    HWND tab = nullptr;
    std::vector<TabColumnSnapshot> columns;
    int active = 0;
};

struct WindowState {
    Worker* worker = nullptr;  // This window's folder and icon reader.
    DWORD thread = 0;
    std::unique_ptr<ContextMenuState> context;
    std::unique_ptr<RenameState> rename;
    std::vector<UndoEntry> undo;
    std::vector<UndoEntry> redo;
    // After an extraction: the names to select in a column once it has loaded.
    std::vector<std::wstring> selectNames;
    Column* selectNamesColumn = nullptr;
    // Open in a new tab: the folder to show, until when to wait, and the tab
    // Explorer made for it.
    Pidl openTabFolder;
    ULONGLONG openTabUntil = 0;
    HWND newTab = nullptr;
    int styleVersion = -1;  // The row style the rows were last given.
    winrt::Windows::UI::Color pillColor{};    // The accent shades the panel was last drawn with.
    winrt::Windows::UI::Color accentColor{};
    bool colorsSet = false;
    ULONGLONG scrollQuietUntil = 0;  // A scroll of the strip is under way: no refocusing meanwhile.
    ULONGLONG cursorFixUntil = 0;  // While Explorer navigates, a busy cursor over the panel is put right.
    bool shownDark = false;  // The theme the panel background was made for.
    int shownMode = 0;       // And the background mode.
    // The native lists made invisible, in transparent mode, and whether this mod
    // gave each the layered style. A tab can hold more than one view window
    // after layouts change, so every visible one is cleared.
    std::vector<std::pair<HWND, bool>> cleared;
    // A click on the name of the item that is already selected starts a
    // rename once the double-click time has passed.
    // After a command of Explorer's own toolbar: read the columns again, and
    // watch for Explorer opening its own rename box, which the panel hides.
    ULONGLONG postActionAt = 0;
    ULONGLONG syncSelAt = 0;  // When to take Explorer's selection after a toolbar command.
    ULONGLONG postActionAt2 = 0;
    ULONGLONG watchEditUntil = 0;
    DWORD clipSeen = 0;
    ULONGLONG slowRenameAt = 0;
    ULONGLONG slowRenameBlockUntil = 0;
    Column* slowRenameColumn = nullptr;
    size_t slowRenameEntry = 0;
    HWND topLevel = nullptr;
    mud::DispatcherQueue queue{nullptr};
    // Columns per tab: the tab that is showing, and the stacks of the others.
    DragState drag;
    HWND currentTab = nullptr;
    ULONGLONG specSyncDue = 0;  // When to read the sort of the folder Explorer shows.
    std::wstring hiddenFolder;  // The folder a tab that is not in Columns showed last.
    std::vector<TabSnapshot> tabSnapshots;
    // Which tabs show Columns. The panel is one per window, so it is hidden
    // while a tab that is not in Columns is showing.
    std::vector<HWND> columnsTabs;
    std::vector<HWND> seenTabs;
    bool softHidden = false;

    // View menu entry.
    ItemsVector menuItems{nullptr};
    muxc::RadioMenuFlyoutItem menuItem{nullptr};
    muxc::AppBarButton settingsItem{nullptr};  // "Extras", last in the toolbar's "..." menu.
    muxc::CommandBar overflowBar{nullptr};     // The toolbar that holds it.
    winrt::event_token settingsClickToken{};
    // The View button of the toolbar: while Columns shows, its icon is ours.
    muxc::Button viewButton{nullptr};
    mux::UIElement viewIconOriginal{nullptr};
    mux::UIElement viewIconOurs{nullptr};
    int viewIconWhere = 0;  // 1: the button's content, 2: a child of a panel, 3: the child of a border.
    bool viewIconShown = false;
    bool viewIconLogged = false;
    // While the keyboard is in use, rows do not show the hover of a pointer that
    // has not moved.
    bool kbHoverOff = false;
    POINT kbCursor{};
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
    // Drops coming in from other windows and applications, and the scroll that
    // follows a drag (inbound or of rows) near the edge of a column or the strip.
    IDropTarget* panelDrop = nullptr;
    GhostSpec outSpec;               // The label of a drag that left the panel,
    bool outDragActive = false;      // shown again when it comes back to it.
    bool outFromArchive = false;     // The drag that left the panel carries items of an archive.
    // A folder row held under dragged items opens after a pause.
    Column* springColumn = nullptr;
    int springEntry = -1;
    ULONGLONG springSince = 0;
    bool springDone = false;
    HWND panelDropWindow = nullptr;
    winrt::Windows::Foundation::Point autoPoint{};  // The pointer, host coordinates.
    bool autoOn = false;
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

bool OnDragMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args);
void OnDragPressed(WindowState* state, muxc::ListView const& list, muxc::ListViewItem const& row,
                   mux::Input::PointerRoutedEventArgs const& args);
bool OnDragReleased(WindowState* state);
void CancelDrag(WindowState* state);

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

// Transparent mode: Explorer's own file list stays, but is drawn fully
// transparent, so nothing shows through the panel except the window's backdrop.
// Undone whenever the panel goes away.
BOOL CALLBACK CollectViewsProc(HWND hwnd, LPARAM lParam) {
    wchar_t name[32];
    if (GetClassNameW(hwnd, name, ARRAYSIZE(name)) &&
        wcscmp(name, L"SHELLDLL_DefView") == 0 && IsWindowVisible(hwnd)) {
        ((std::vector<HWND>*)lParam)->push_back(hwnd);
    }
    return TRUE;
}

void ClearOne(WindowState* state, HWND list) {
    LONG_PTR ex = GetWindowLongPtrW(list, GWL_EXSTYLE);
    bool added = !(ex & WS_EX_LAYERED);
    if (added) {
        SetWindowLongPtrW(list, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    }
    BOOL ok = SetLayeredWindowAttributes(list, 0, 0, LWA_ALPHA);
    DWORD error = ok ? 0 : GetLastError();
    bool known = false;
    for (auto& entry : state->cleared) {
        if (entry.first == list) {
            entry.second = entry.second || added;
            known = true;
        }
    }
    if (!known) {
        state->cleared.emplace_back(list, added);
    }
    Out(L"the native file list is made transparent (window %p, %s, result %d, error %lu)",
        list, ClassNameOf(list).c_str(), ok ? 1 : 0, error);
}

// Every visible view window of this window is made transparent, and one that
// something else made visible again is cleared again.
void ClearNativeLists(WindowState* state) {
    std::vector<HWND> views;
    EnumChildWindows(state->topLevel, CollectViewsProc, (LPARAM)&views);
    for (HWND view : views) {
        bool known = false;
        for (auto& entry : state->cleared) {
            known = known || entry.first == view;
        }
        if (!known) {
            ClearOne(state, view);
            continue;
        }
        // The style or the alpha can be put back by Explorer, or by a mod that
        // fades the view in when the layout changes.
        BYTE alpha = 255;
        DWORD flags = 0;
        bool intact = (GetWindowLongPtrW(view, GWL_EXSTYLE) & WS_EX_LAYERED) &&
                      GetLayeredWindowAttributes(view, nullptr, &alpha, &flags) &&
                      (flags & LWA_ALPHA) && alpha == 0;
        if (!intact) {
            Out(L"the native list %p was made visible again by something else (alpha %d), clearing it again",
                view, (int)alpha);
            ClearOne(state, view);
        }
    }
    // Windows that are gone are forgotten.
    for (size_t i = 0; i < state->cleared.size();) {
        if (!IsWindow(state->cleared[i].first)) {
            state->cleared.erase(state->cleared.begin() + i);
        } else {
            i++;
        }
    }
}

void RestoreNativeList(WindowState* state) {
    auto cleared = std::move(state->cleared);
    state->cleared.clear();
    for (auto& entry : cleared) {
        HWND list = entry.first;
        if (!list || !IsWindow(list)) {
            continue;
        }
        SetLayeredWindowAttributes(list, 0, 255, LWA_ALPHA);
        if (entry.second) {
            LONG_PTR ex = GetWindowLongPtrW(list, GWL_EXSTYLE);
            SetWindowLongPtrW(list, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
        }
        RedrawWindow(list, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    if (!cleared.empty()) {
        Out(L"the native file list is visible again");
    }
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
    std::shared_ptr<std::atomic<bool>> cancel;  // The column no longer wants this.
    // A job for icons only: rows that came on screen after the first batch.
    bool iconsOnly = false;
    std::vector<size_t> iconIndices;
    std::vector<Pidl> iconChildren;
};

// One reader thread per window, so a slow folder in one window never holds up
// the folders and icons of another. A worker is stopped when its window's panel
// goes away and cleaned up later; all of them end when the mod unloads.
struct Worker {
    std::mutex mutex;
    std::deque<std::unique_ptr<Job>> jobs;
    HANDLE event = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
};

std::mutex g_workersMutex;
[[clang::no_destroy]] std::vector<std::unique_ptr<Worker>> g_workers;

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

// Icons read with the first load. The rows beyond are asked for when they are
// built, so the ones on screen come first even far down a long folder.
constexpr size_t kMaxIcons = 120;
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
               std::vector<size_t> const& indices, size_t limit,
               unsigned generation, mud::DispatcherQueue const& queue,
               std::atomic<bool> const& stop, std::atomic<bool> const* cancel) {
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
    size_t count = std::min(std::min(children.size(), indices.size()), limit);
    std::vector<size_t> missed;
    // The first pass asks only for what the shell already has in its cache, which
    // is quick, so the rows on screen fill at once. What is not cached is made in
    // the second pass, in the same order.
    auto readOne = [&](size_t i, bool cachedOnly) -> bool {
        IShellItem* item = nullptr;
        if (FAILED(SHCreateItemWithParent(nullptr, folder, children[i].get(), IID_PPV_ARGS(&item))) || !item) {
            return true;  // Nothing more to try.
        }
        bool done = false;
        IShellItemImageFactory* factory = nullptr;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) && factory) {
            HBITMAP bitmap = nullptr;
            HRESULT got = factory->GetImage(SIZE{32, 32}, cachedOnly ? SIIGBF_INCACHEONLY : 0, &bitmap);
            if (SUCCEEDED(got) && bitmap) {
                IconBits bits;
                bits.index = indices[i];
                if (ReadBitmapBits(bitmap, &bits)) {
                    batch->icons.push_back(std::move(bits));
                }
                DeleteObject(bitmap);
                done = true;
            } else if (!cachedOnly) {
                done = true;  // Asked in full and failed: not again.
            }
            factory->Release();
        } else {
            done = true;
        }
        item->Release();
        return done;
    };
    for (size_t i = 0; i < count && !stop && !(cancel && *cancel); i++) {
        if (!readOne(i, true)) {
            missed.push_back(i);
        }
        if (batch->icons.size() >= kIconBatch) {
            flush();
        }
    }
    flush();
    for (size_t k = 0; k < missed.size() && !stop && !(cancel && *cancel); k++) {
        readOne(missed[k], false);
        if (batch->icons.size() >= kIconBatch) {
            flush();
        }
    }
    flush();
    folder->Release();
}

DWORD WINAPI WorkerProc(LPVOID param) {
    Worker* worker = (Worker*)param;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (!worker->stop) {
        MsgWaitForMultipleObjects(1, &worker->event, FALSE, INFINITE, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        for (;;) {
            std::unique_ptr<Job> job;
            {
                std::lock_guard<std::mutex> lock(worker->mutex);
                if (worker->jobs.empty()) {
                    break;
                }
                job = std::move(worker->jobs.front());
                worker->jobs.pop_front();
            }
            if (worker->stop) {
                break;
            }
            // A column that has been removed or asked again doesn't need this.
            if (job->cancel && *job->cancel) {
                continue;
            }
            if (job->iconsOnly) {
                try {
                    LoadIcons(job->folder.get(), job->iconChildren, job->iconIndices,
                              job->iconChildren.size(), job->generation, job->queue,
                              worker->stop, job->cancel.get());
                } catch (...) {
                }
                continue;
            }
            auto loaded = std::make_shared<Loaded>();
            loaded->generation = job->generation;
            LoadFolder(job->folder.get(), job->spec, job->hidden,
                       job->maxItems, &loaded->data);
            if (job->cancel && *job->cancel) {
                continue;
            }
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
                std::vector<size_t> indices(children.size());
                for (size_t i = 0; i < indices.size(); i++) {
                    indices[i] = i;
                }
                LoadIcons(job->folder.get(), children, indices, kMaxIcons,
                          job->generation, job->queue, worker->stop,
                          job->cancel.get());
            } catch (...) {
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->jobs.clear();
    }
    CoUninitialize();
    worker->finished = true;
    return 0;
}

// A new worker for a window. Workers that have ended are cleaned up here.
Worker* CreateWorker() {
    std::lock_guard<std::mutex> lock(g_workersMutex);
    for (size_t i = 0; i < g_workers.size();) {
        Worker* old = g_workers[i].get();
        if (old->finished && WaitForSingleObject(old->thread, 0) == WAIT_OBJECT_0) {
            CloseHandle(old->thread);
            CloseHandle(old->event);
            g_workers.erase(g_workers.begin() + i);
        } else {
            i++;
        }
    }
    auto worker = std::make_unique<Worker>();
    worker->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    worker->thread = CreateThread(nullptr, 0, WorkerProc, worker.get(), 0, nullptr);
    Worker* raw = worker.get();
    g_workers.push_back(std::move(worker));
    Out(L"a reader thread was started for a window, %d running", (int)g_workers.size());
    return raw;
}

// The window's panel is gone: its worker ends after the job it is on.
void ReleaseWorker(Worker* worker) {
    if (!worker) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_workersMutex);
    for (auto& known : g_workers) {
        if (known.get() == worker) {
            worker->stop = true;
            SetEvent(worker->event);
            break;
        }
    }
}

void StartWorker() {}  // Workers start with the first load of a window.

// At unload: every worker ends, and is waited for.
void StopWorker() {
    std::vector<std::unique_ptr<Worker>> all;
    {
        std::lock_guard<std::mutex> lock(g_workersMutex);
        all.swap(g_workers);
    }
    for (auto& worker : all) {
        worker->stop = true;
        SetEvent(worker->event);
    }
    for (auto& worker : all) {
        WaitForSingleObject(worker->thread, 10000);
        CloseHandle(worker->thread);
        CloseHandle(worker->event);
    }
}

// Where a job goes in a worker's queue: a folder is needed to fill a column, so it
// goes before every icon job (and after the folders asked earlier); the newest
// icon request goes first among the icon jobs, since it is for what is on screen
// now, and older ones are for rows that may have scrolled away.
void QueueJob(Worker* worker, std::unique_ptr<Job> job) {
    std::lock_guard<std::mutex> lock(worker->mutex);
    auto at = worker->jobs.begin();
    while (at != worker->jobs.end() && !(*at)->iconsOnly) {
        ++at;
    }
    if (job->iconsOnly) {
        worker->jobs.insert(at, std::move(job));
    } else {
        worker->jobs.insert(at, std::move(job));
    }
}

void RequestLoad(WindowState* state, Column* column) {
    if (g_unloading) {
        return;
    }
    if (!state->worker) {
        state->worker = CreateWorker();
    }
    // The load this column asked before is not wanted any more.
    if (column->cancel) {
        *column->cancel = true;
    }
    column->cancel = std::make_shared<std::atomic<bool>>(false);
    column->generation = ++state->nextGeneration;
    auto job = std::make_unique<Job>();
    job->generation = column->generation;
    job->folder.reset(ILClone(column->folder.get()));
    job->spec = column->hasSpec ? column->spec : state->spec;
    job->hidden = ShowHiddenFiles();
    job->maxItems = g_maxItems;
    job->queue = state->queue;
    job->cancel = column->cancel;
    QueueJob(state->worker, std::move(job));
    SetEvent(state->worker->event);
}

// Asks the worker for the icons of rows that were built after the first batch.
void RequestIcons(WindowState* state, Column* column) {
    if (g_unloading || !state->worker || column->iconWanted.empty() ||
        !column->cancel) {
        return;
    }
    auto job = std::make_unique<Job>();
    job->iconsOnly = true;
    job->generation = column->generation;
    job->folder.reset(ILClone(column->folder.get()));
    job->queue = state->queue;
    job->cancel = column->cancel;
    // The rows built last are the ones on screen: take those, newest first.
    size_t take = std::min<size_t>(column->iconWanted.size(), 200);
    for (size_t i = 0; i < take; i++) {
        size_t entry = column->iconWanted[column->iconWanted.size() - 1 - i];
        if (entry < column->entries.size()) {
            job->iconIndices.push_back(entry);
            job->iconChildren.emplace_back(ILClone(column->entries[entry].child.get()));
        }
    }
    column->iconWanted.erase(column->iconWanted.end() - take, column->iconWanted.end());
    QueueJob(state->worker, std::move(job));
    SetEvent(state->worker->event);
}

void LogRowTree(mux::DependencyObject const& node, int depth);
void RestoreViewButtonIcon(WindowState* state);
void HoverOn(WindowState* state);
void HoverOff(WindowState* state);
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
                if (column->stopScrollHandler) {
                    column->list.RemoveHandler(mux::UIElement::PointerPressedEvent(),
                                               winrt::box_value(column->stopScrollHandler));
                    column->stopScrollHandler = nullptr;
                }
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
            if (column->cancel) {
                *column->cancel = true;
            }
            if (column->viewer) {
                column->viewer.ViewChanged(column->viewToken);
                column->viewer = nullptr;
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
        if (now < state->pillUntil) {
            // A move is still going: the new one starts from where the pill is
            // drawn now, not from where the old one was heading.
            try {
                from = state->pill.TransformToVisual(state->overlay)
                           .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                           .X;
            } catch (...) {
            }
        }
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
            state->scrollQuietUntil = GetTickCount64() + 600;
        }
    } catch (...) {
    }
}

// Scrolls the strip, smoothly, so that column `index` is in the middle of the
// view, as far as the ends of the strip allow.
void CenterColumn(WindowState* state, int index) {
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
        double view = state->scroller.ViewportWidth();
        double target = left + width / 2 - view / 2;
        double most = state->scroller.ScrollableWidth();
        target = std::max(0.0, std::min(target, most));
        if (std::abs(target - state->scroller.HorizontalOffset()) > 1) {
            state->scroller.ChangeView(target, nullptr, nullptr, false);
            state->scrollQuietUntil = GetTickCount64() + 700;
        }
    } catch (...) {
    }
}

void SetActiveColumn(WindowState* state, int index, bool ensureVisible = true) {
    state->active = index;
    if (ensureVisible) {
        EnsureColumnVisible(state, index);
    }
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

// The width a column's longest names ask for, inside the limits.
double FittedWidth(Column* column) {
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
    return width;
}

// Sets the column's width from its longest names, unless the user sized it.
void FitWidth(Column* column) {
    if (column->userSized) {
        return;
    }
    column->list.Width(FittedWidth(column));
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
        // The little arrow buttons at the ends would be squeezed by the
        // transform. A compact bar has none: the thumb and the track stay.
        std::function<void(mux::DependencyObject const&, int)> arrows =
            [&](mux::DependencyObject const& node, int level) {
                if (!node || level > 12) {
                    return;
                }
                if (auto element = node.try_as<mux::FrameworkElement>()) {
                    std::wstring name = element.Name().c_str();
                    if (name.find(L"SmallDecrease") != std::wstring::npos ||
                        name.find(L"SmallIncrease") != std::wstring::npos) {
                        if (compact) {
                            element.Width(0);
                            element.Height(0);
                            element.MinWidth(0);
                            element.MinHeight(0);
                            element.Opacity(0);
                            element.IsHitTestVisible(false);
                        } else {
                            element.ClearValue(mux::FrameworkElement::WidthProperty());
                            element.ClearValue(mux::FrameworkElement::HeightProperty());
                            element.ClearValue(mux::FrameworkElement::MinWidthProperty());
                            element.ClearValue(mux::FrameworkElement::MinHeightProperty());
                            element.Opacity(1);
                            element.IsHitTestVisible(true);
                        }
                    }
                }
                int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
                for (int i = 0; i < count; i++) {
                    arrows(mux::Media::VisualTreeHelper::GetChild(node, i), level + 1);
                }
            };
        arrows(bar, 0);
        // A narrower bar, not a squeezed one: the thumb keeps its round corners.
        if (compact) {
            bar.RenderTransform(nullptr);
            if (vertical) {
                bar.Width(7);
                bar.HorizontalAlignment(mux::HorizontalAlignment::Right);
                // A negative left margin takes the bar's width out of the layout,
                // so it floats over the right edge instead of narrowing the rows.
                bar.Margin(mux::Thickness{-7, 0, 0, 0});
            } else {
                bar.Height(7);
                bar.VerticalAlignment(mux::VerticalAlignment::Bottom);
            }
        } else {
            bar.RenderTransform(nullptr);
            bar.ClearValue(mux::FrameworkElement::WidthProperty());
            bar.ClearValue(mux::FrameworkElement::HeightProperty());
            bar.ClearValue(mux::FrameworkElement::MarginProperty());
            bar.HorizontalAlignment(vertical ? mux::HorizontalAlignment::Right
                                             : mux::HorizontalAlignment::Stretch);
            bar.VerticalAlignment(vertical ? mux::VerticalAlignment::Stretch
                                           : mux::VerticalAlignment::Bottom);
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

// A row without content: only the item, sized. Its content is built when the
// row comes near the screen, so a long folder opens at once.
muxc::ListViewItem MakeRowShell(double rowHeight) {
    muxc::ListViewItem row;
    row.MinHeight(rowHeight);
    row.Height(rowHeight);
    row.Padding(mux::Thickness{10, 0, 8, 0});
    ApplyFocusStyle(row);
    row.HorizontalContentAlignment(mux::HorizontalAlignment::Stretch);
    return row;
}

void ApplyIconBits(Column* column, IconBits const& icon);

// Builds the content of one row.
void BuildRowContent(Column* column, size_t entryIndex, muxc::ListViewItem const& row) {
    const Entry& entry = column->entries[entryIndex];
    size_t capacity = NameCapacity(column);
    muxc::Grid grid;
    muxc::ColumnDefinition iconColumn, nameColumn, markColumn, chevronColumn;
    iconColumn.Width(mux::GridLength{24, mux::GridUnitType::Pixel});
    nameColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
    chevronColumn.Width(mux::GridLength{16, mux::GridUnitType::Pixel});
    grid.ColumnDefinitions().Append(iconColumn);
    markColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
    grid.ColumnDefinitions().Append(nameColumn);
    grid.ColumnDefinitions().Append(markColumn);
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
    column->iconHosts[entryIndex] = iconHost;  // The worker swaps in the real icon later.

    muxc::TextBlock name;
    std::wstring shown = MiddleEllipsis(entry.name, capacity);
    name.Text(shown);
    name.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    name.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid::SetColumn(name, 1);
    grid.Children().Append(name);
    column->texts[entryIndex] = name;

    if (entry.remote) {
        muxc::FontIcon mark;
        mark.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        // Cloud, network, device, always kept (green check), downloaded (check).
        mark.Glyph(entry.remote == 1   ? L"\uE753"
                   : entry.remote == 2 ? L"\uE839"
                   : entry.remote == 3 ? L"\uE8EA"
                   : entry.remote == 6 ? L"\uE895"
                                       : L"\uE73E");
        if (entry.remote == 6) {  // Syncing now: the blue of OneDrive.
            mark.Foreground(mux::Media::SolidColorBrush(
                winrt::Windows::UI::ColorHelper::FromArgb(255, 0, 120, 212)));
        }
        if (entry.remote == 4) {
            mark.Foreground(mux::Media::SolidColorBrush(
                winrt::Windows::UI::ColorHelper::FromArgb(255, 16, 124, 16)));
            mark.Opacity(1.0);
        }
        mark.FontSize(11);
        mark.Opacity(0.6);
        mark.Margin(mux::Thickness{6, 0, 4, 0});
        mark.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(mark, 2);
        grid.Children().Append(mark);
    }
    if (entry.folder) {
        muxc::FontIcon chevron;
        chevron.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        chevron.Glyph(L"");
        chevron.FontSize(10);
        chevron.Opacity(0.6);
        chevron.HorizontalAlignment(mux::HorizontalAlignment::Right);
        chevron.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(chevron, 3);
        grid.Children().Append(chevron);
        column->chevrons[entryIndex] = chevron;
    }

    row.Content(grid);
    // The full name, for names that were cut. The detail tooltip replaces it
    // the first time the pointer is on the row.
    muxc::ToolTipService::SetToolTip(row, winrt::box_value(winrt::hstring{entry.name}));
    column->built[entryIndex] = 1;
    // Beyond the first batch the worker reads icons for the rows that get built.
    if (entryIndex >= kMaxIcons && entryIndex < column->iconAsked.size() &&
        !column->iconAsked[entryIndex]) {
        column->iconAsked[entryIndex] = 1;
        column->iconWanted.push_back(entryIndex);
    }
    auto pending = column->pendingIcons.find(entryIndex);
    if (pending != column->pendingIcons.end()) {
        ApplyIconBits(column, *pending->second);
        column->pendingIcons.erase(pending);
    }
}

// Builds the rows between two row numbers that don't have their content yet.
void BuildRowRange(Column* column, int firstRow, int lastRow) {
    auto items = column->list.Items();
    int count = (int)std::min<size_t>(items.Size(), column->rowToEntry.size());
    firstRow = std::max(0, firstRow);
    lastRow = std::min(count - 1, lastRow);
    int made = 0;
    for (int r = firstRow; r <= lastRow && made < 200; r++) {
        int entry = column->rowToEntry[r];
        if (entry < 0 || (size_t)entry >= column->built.size() || column->built[entry]) {
            continue;
        }
        if (auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
            BuildRowContent(column, (size_t)entry, item);
            made++;
        }
    }
}

// The rows on screen, and a few more each side, get their content.
void BuildVisibleRows(Column* column) {
    auto panel = column->list.ItemsPanelRoot().try_as<muxc::ItemsStackPanel>();
    if (!panel) {
        return;
    }
    int first = panel.FirstCacheIndex(), last = panel.LastCacheIndex();
    if (first < 0 || last < 0) {
        return;
    }
    BuildRowRange(column, first - 10, last + 10);
}

void EnsureRowBuilt(Column* column, size_t entryIndex) {
    if (entryIndex < column->entryToRow.size() && column->entryToRow[entryIndex] >= 0) {
        int row = column->entryToRow[entryIndex];
        BuildRowRange(column, row, row);
    }
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
muxc::ScrollViewer FindListScroller(mux::DependencyObject const& node, int depth = 0);
void OnRowSelected(muxc::ListView const& list);

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
    column->entryToRow.assign(column->entries.size(), -1);
    column->iconAsked.assign(column->entries.size(), 0);
    column->iconWanted.clear();
    column->built.assign(column->entries.size(), 0);
    column->pendingIcons.clear();
    column->chevrons.clear();
    column->chevrons.resize(column->entries.size(), muxc::FontIcon{nullptr});
    column->hintEntry = -1;
    column->focusHeader = -1;
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
    std::vector<winrt::Windows::Foundation::IInspectable> made;  // Set in one go at the end.
    made.reserve(column->entries.size() + 16);
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
            made.push_back(MakeHeaderRow(title, rowHeight, isCollapsed(group), &icon));
            column->rowToEntry.push_back(-1);
            column->rowGroup.push_back(group);
            column->headerIcons.push_back(icon);
        }
        if (column->selectedChild &&
            ILIsEqual(column->entries[i].child.get(),
                      column->selectedChild.get())) {
            selectRow = (int)column->rowToEntry.size();
        }
        auto row = MakeRowShell(rowHeight);
        if (isCollapsed(group)) {
            row.Visibility(mux::Visibility::Collapsed);
        }
        made.push_back(row);
        column->entryToRow[i] = (int)column->rowToEntry.size();
        column->rowToEntry.push_back((int)i);
        column->rowGroup.push_back(group);
        column->headerIcons.push_back(muxc::FontIcon{nullptr});
    }
    if (data.truncated) {
        muxc::ListViewItem more;
        size_t hidden = data.total > column->entries.size()
                            ? data.total - column->entries.size()
                            : 0;
        std::wstring text = hidden > 0
                                ? std::to_wstring(hidden) + (data.total >= (size_t)kReadCap ? L"+" : L"") +
                                      L" more items not shown"
                                : std::wstring(L"More items not shown");
        more.Content(winrt::box_value(winrt::hstring{text}));
        more.IsEnabled(false);
        made.push_back(more);
        column->rowToEntry.push_back(-1);
        column->rowGroup.push_back(-1);
        column->headerIcons.push_back(muxc::FontIcon{nullptr});
    }
    items.ReplaceAll(made);
    // The first rows, and the ones around the selection, have their content
    // now. The rest is built as the list scrolls.
    BuildRowRange(column, 0, 60);
    if (selectRow >= 0) {
        BuildRowRange(column, selectRow - 30, selectRow + 30);
        column->list.SelectedIndex(selectRow);
    }
    column->filled = true;
    column->list.IsTabStop(column->entries.empty());
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
    if (!state->selectNames.empty() && state->selectNamesColumn == column) {
        // Select what was just extracted, and tell the rest (Explorer's view,
        // the columns) after the column has been laid out.
        int first = -1;
        bool wasBuilding = state->building;
        state->building = true;
        for (size_t i = 0; i < column->entries.size(); i++) {
            for (auto& wanted : state->selectNames) {
                if (_wcsicmp(column->entries[i].name.c_str(), wanted.c_str()) == 0 &&
                    i < column->entryToRow.size() && column->entryToRow[i] >= 0) {
                    int row = column->entryToRow[i];
                    if (first < 0) {
                        first = row;
                        column->list.SelectedIndex(row);
                    } else {
                        column->list.SelectRange(
                            winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(row, 1));
                    }
                    break;
                }
            }
        }
        state->building = wasBuilding;
        state->selectNames.clear();
        state->selectNamesColumn = nullptr;
        if (first >= 0 && state->queue) {
            state->queue.TryEnqueue([column, first]() {
                WindowState* state = StateForThisThread(false);
                if (!state || !state->on || g_unloading) {
                    return;
                }
                try {
                    bool known = false;
                    for (auto& c : state->columns) {
                        known = known || c.get() == column;
                    }
                    if (known) {
                        column->list.ScrollIntoView(column->list.Items().GetAt((uint32_t)first));
                        // Then centre the first extracted row in the column.
                        column->list.UpdateLayout();
                        auto scroller = FindListScroller(column->list, 0);
                        auto container = column->list.ContainerFromIndex(first)
                                             .try_as<mux::FrameworkElement>();
                        if (scroller && container) {
                            double top = container.TransformToVisual(scroller)
                                             .TransformPoint({0, 0}).Y;
                            double offset = scroller.VerticalOffset() + top -
                                            (scroller.ViewportHeight() - container.ActualHeight()) / 2;
                            scroller.ChangeView(nullptr, std::max(0.0, offset), nullptr, false);
                        }
                        OnRowSelected(column->list);
                    }
                } catch (...) {
                    LogHresult(L"selecting the extracted items");
                }
            });
        }
    }
    if (column->restoreScroll >= 0 && state->queue) {
        // A tab shown again: the column goes back to where it was scrolled.
        double wanted = column->restoreScroll;
        column->restoreScroll = -1;
        state->queue.TryEnqueue([column, wanted]() {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->on || g_unloading) {
                return;
            }
            try {
                bool known = false;
                for (auto& c : state->columns) {
                    known = known || c.get() == column;
                }
                if (!known) {
                    return;
                }
                column->list.UpdateLayout();
                auto scroller = FindListScroller(column->list, 0);
                if (scroller) {
                    scroller.ChangeView(nullptr, std::min(wanted, scroller.ScrollableHeight()),
                                        nullptr, true);
                }
            } catch (...) {
                LogHresult(L"restoring a column's scroll");
            }
        });
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
            data.entries[i].folder != column.entries[i].folder) {
            return false;
        }
        // The same item read again has the same bytes. ILIsEqual asks the shell,
        // which is slow for thousands of items on the UI thread.
        PCUITEMID_CHILD a = data.entries[i].child.get();
        PCUITEMID_CHILD b = column.entries[i].child.get();
        UINT sizeA = ILGetSize(a), sizeB = ILGetSize(b);
        if (!(sizeA == sizeB && memcmp(a, b, sizeA) == 0) && !ILIsEqual(a, b)) {
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

void LiftStale(WindowState* state, int columnIndex);
void ScheduleNavigation(WindowState* state, PCIDLIST_ABSOLUTE folder, ULONGLONG delay = 350);
void SetActiveColumn(WindowState* state, int index, bool ensureVisible);

// A folder on disk that is no longer there: deleted, renamed or moved.
// Folders that aren't on disk (This PC, Control Panel) are never "gone" here.
bool FolderGone(PCIDLIST_ABSOLUTE folder) {
    wchar_t path[MAX_PATH] = L"";
    if (!folder || ILIsEmpty(folder) || !SHGetPathFromIDListW(folder, path) || !path[0]) {
        return false;
    }
    return !PathFileExistsW(path);
}

void ApplyLoaded(std::shared_ptr<Loaded> const& loaded) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    for (size_t position = 0; position < state->columns.size(); position++) {
        auto& column = state->columns[position];
        if (column->generation == loaded->generation) {
            // The column's folder is gone: the column and those after it are
            // removed, and the one before it shows its folder again, without
            // the row that led here.
            if (position > 0 && FolderGone(column->folder.get())) {
                Out(L"a column's folder is gone, its columns are removed");
                try {
                    bool wasBuilding = state->building;
                    state->building = true;
                    RemoveColumnsFrom(state, position);
                    Column* previous = state->columns[position - 1].get();
                    previous->selectedChild.reset();
                    previous->list.SelectedItems().Clear();
                    state->building = wasBuilding;
                    LiftStale(state, (int)position - 1);
                    SetActiveColumn(state, (int)position - 1);
                    ScheduleNavigation(state, previous->folder.get());
                    RequestLoad(state, previous);
                } catch (...) {
                    state->building = false;
                    LogHresult(L"pruning a column");
                }
                return;
            }
            bool wasBuilding = state->building;
            state->building = true;
            try {
                // Only a column that shows its first listing scrolls itself into
                // view. A reload of one that is already there must not pull the
                // strip back to the last column, which undid the centring of
                // the column Explorer's Up had just gone to.
                bool firstListing = !column->filled;
                bool changed = !SameListing(loaded->data, *column);
                if (changed) {
                    ULONGLONG fillStarted = GetTickCount64();
                    size_t fillCount = loaded->data.entries.size();
                    FillColumn(state, column.get(), std::move(loaded->data));
                    ULONGLONG fillTook = GetTickCount64() - fillStarted;
                    if (fillTook >= 40) {
                        Out(L"fill: %d rows set up in %d ms on the UI thread", (int)fillCount, (int)fillTook);
                    }
                } else if (column->cancel) {
                    // The rows and their icons are already there: the icon pass
                    // that follows this load is not needed.
                    *column->cancel = true;
                    column->cancel = std::make_shared<std::atomic<bool>>(false);
                }
                // A synchronous layout pass, so only when something changed:
                // the reloads after a toolbar command came in the middle of a
                // scroll and made it stutter.
                if (changed || firstListing) {
                    state->scroller.UpdateLayout();
                }
                if (state->swapOld && column->generation == state->swapGeneration) {
                    CompleteSwap(state);
                } else if (firstListing && state->columns.back().get() == column.get()) {
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
// Puts an icon into its row's icon cell.
void ApplyIconBits(Column* column, IconBits const& icon) {
    try {
        mux::Media::Imaging::WriteableBitmap bitmap(icon.width, icon.height);
        auto buffer = bitmap.PixelBuffer();
        winrt::com_ptr<IBufferByteAccessLocal> access;
        winrt::check_hresult(
            ((::IUnknown*)winrt::get_abi(buffer))
                ->QueryInterface(kIidBufferByteAccess, access.put_void()));
        uint8_t* pixels = nullptr;
        winrt::check_hresult(access->Buffer(&pixels));
        if (!pixels || buffer.Capacity() < icon.bgra.size()) {
            return;
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
        if (host.Children().Size() > 0 && host.Children().GetAt(0).try_as<muxc::Image>()) {
            return;  // Already has its icon.
        }
        host.Children().Clear();
        host.Children().Append(image);
    } catch (...) {
        static int logged = 0;
        if (logged++ < 3) {
            LogHresult(L"applying an icon");
        }
    }
}

// Runs on the window's UI thread when the worker has read some icons. An icon
// whose row has no content yet waits for it.
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
            if (icon.index >= column->iconHosts.size()) {
                continue;
            }
            if (!column->iconHosts[icon.index]) {
                column->pendingIcons[icon.index] = std::make_shared<IconBits>(icon);
                continue;
            }
            ApplyIconBits(column.get(), icon);
        }
        return;
    }
}

void OnRowSelected(muxc::ListView const& list);
void SetHeaderFocus(Column* column, muxc::ListView const& list, int row);
void OnRowDoubleTapped(muxc::ListView const& list);
void OnEmptyDoubleTapped(muxc::ListView const& list,
                         mux::Input::DoubleTappedRoutedEventArgs const& args);
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
// ---- The handle on a column's border: it grows where the pointer is, follows it
// up and down the border, and shrinks away when the pointer leaves.

void PillGoTo(muxc::Border const& pill, double scaleTo, double offsetTo, int milliseconds, bool hideAtEnd) {
    try {
        auto motion = pill.RenderTransform().try_as<mux::Media::CompositeTransform>();
        if (!motion) {
            return;
        }
        namespace anim = mux::Media::Animation;
        anim::CubicEase ease;
        ease.EasingMode(anim::EasingMode::EaseOut);
        auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(milliseconds));
        anim::Storyboard board;
        auto add = [&](const wchar_t* property, double from, double to) {
            anim::DoubleAnimation step;
            step.From(from);
            step.To(to);
            step.Duration(length);
            step.EasingFunction(ease);
            step.EnableDependentAnimation(true);
            step.FillBehavior(anim::FillBehavior::HoldEnd);
            anim::Storyboard::SetTarget(step, pill);
            anim::Storyboard::SetTargetProperty(step, property);
            board.Children().Append(step);
        };
        add(L"(UIElement.RenderTransform).(CompositeTransform.ScaleY)", motion.ScaleY(), scaleTo);
        add(L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)", motion.TranslateY(), offsetTo);
        if (hideAtEnd) {
            board.Completed([pill](Inspectable const&, Inspectable const&) {
                if (!winrt::unbox_value_or<bool>(pill.Tag(), false)) {
                    pill.Opacity(0);
                }
            });
        }
        board.Begin();
    } catch (...) {
        LogHresult(L"animating the border handle");
    }
}

// Where the handle sits for a pointer at `y` in the grip: its middle follows
// the pointer, kept inside the grip.
double PillOffset(Column* column, double y) {
    double height = column->grip.ActualHeight();
    double half = column->pill.Height() / 2;
    double centre = y < half ? half : (y > height - half ? height - half : y);
    return centre - height / 2;
}

void PillShow(Column* column, double y) {
    if (!column->pill || !column->grip) {
        return;
    }
    try {
        auto motion = column->pill.RenderTransform().try_as<mux::Media::CompositeTransform>();
        if (!motion) {
            return;
        }
        bool wasShown = winrt::unbox_value_or<bool>(column->pill.Tag(), false) && column->pill.Opacity() > 0;
        double offset = PillOffset(column, y);
        column->pill.Tag(winrt::box_value(true));
        column->pill.Opacity(1);
        if (!wasShown) {
            motion.TranslateY(offset);  // It appears at the pointer, then grows.
        }
        PillGoTo(column->pill, 1.0, offset, wasShown ? 80 : 150, false);
    } catch (...) {
        LogHresult(L"showing the border handle");
    }
}

void PillFollow(Column* column, double y) {
    if (!column->pill || !column->grip || !winrt::unbox_value_or<bool>(column->pill.Tag(), false)) {
        return;
    }
    PillGoTo(column->pill, 1.0, PillOffset(column, y), 35, false);
}

void PillHide(Column* column) {
    if (!column->pill || !winrt::unbox_value_or<bool>(column->pill.Tag(), false)) {
        return;
    }
    column->pill.Tag(winrt::box_value(false));
    auto motion = column->pill.RenderTransform().try_as<mux::Media::CompositeTransform>();
    PillGoTo(column->pill, 0.0, motion ? motion.TranslateY() : 0.0, 110, true);
}

// Shift and double-click on a border: every column takes the average of the
// widths their names would fit, gliding there, and keeps it like a width
// dragged by hand.
void HarmonizeWidths(WindowState* state) {
    double total = 0;
    int counted = 0;
    for (auto& column : state->columns) {
        if (column->list && column->filled && !column->entries.empty()) {
            total += FittedWidth(column.get());
            counted++;
        }
    }
    if (counted == 0) {
        return;
    }
    double target = std::round(total / counted);
    target = std::clamp(target, (double)g_minWidth, (double)g_maxWidth);
    namespace anim = mux::Media::Animation;
    anim::CubicEase ease;
    ease.EasingMode(anim::EasingMode::EaseInOut);
    auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(260));
    anim::Storyboard board;
    for (auto& column : state->columns) {
        if (!column->list) {
            continue;
        }
        double from = column->list.ActualWidth();
        column->userSized = true;
        column->list.Width(target);
        if (std::abs(from - target) < 1) {
            continue;
        }
        anim::DoubleAnimation glide;
        glide.From(from);
        glide.To(target);
        glide.Duration(length);
        glide.EasingFunction(ease);
        glide.EnableDependentAnimation(true);
        glide.FillBehavior(anim::FillBehavior::Stop);
        anim::Storyboard::SetTarget(glide, column->list);
        anim::Storyboard::SetTargetProperty(glide, L"Width");
        board.Children().Append(glide);
    }
    board.Begin();
    // The active marker follows the widths while they change; the names are cut
    // again for the new width at the end.
    auto timer = state->queue.CreateTimer();
    timer.Interval(std::chrono::milliseconds(16));
    auto frames = std::make_shared<int>(0);
    timer.Tick([frames](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
        WindowState* state = StateForThisThread(false);
        if (!state || !state->on) {
            sender.Stop();
            return;
        }
        UpdatePill(state, false);
        if (++*frames >= 20) {
            sender.Stop();
            for (auto& column : state->columns) {
                if (column->list) {
                    RefreshNames(column.get());
                    if (column->folder) {
                        RememberFolderWidth(PathOf(column->folder.get()), column->list.Width());
                    }
                }
            }
            UpdatePill(state, false);
        }
    });
    timer.Start();
    Out(L"widths harmonized to %d across %d columns", (int)target, (int)state->columns.size());
}

void AddColumn(WindowState* state, Pidl folder, bool atFront = false, bool archive = false) {
    auto column = std::make_unique<Column>();
    column->folder = std::move(folder);
    column->notifyId = WatchFolder(state->notifyWindow, column->folder.get());

    muxc::ListView list;
    list.Width(g_minWidth);
    {
        FolderMemory memory = GetFolderMemory(PathOf(column->folder.get()));
        if (memory.hasSpec) {
            column->spec = memory.spec;
            column->hasSpec = true;
        }
        if (memory.width >= g_minWidth && memory.width <= g_maxWidth) {
            list.Width(memory.width);
            column->userSized = true;
        }
    }
    if (archive) {
        // The shell reads the archive on the worker thread, which can take a while.
        // A spinner shows meanwhile; the listing replaces it.
        muxc::ProgressRing spinner;
        spinner.IsActive(true);
        spinner.Width(28);
        spinner.Height(28);
        spinner.HorizontalAlignment(mux::HorizontalAlignment::Center);
        spinner.Margin(mux::Thickness{0, 24, 0, 0});
        muxc::ListViewItem reading;
        reading.Content(spinner);
        reading.IsHitTestVisible(false);
        reading.IsTabStop(false);
        reading.HorizontalContentAlignment(mux::HorizontalAlignment::Center);
        list.Items().Append(reading);
    }
    // A list with rows hands the keyboard to its rows. An empty one has none,
    // so it takes the keyboard itself, without a focus outline.
    list.IsTabStop(true);
    list.UseSystemFocusVisuals(false);
    list.FocusVisualPrimaryThickness(mux::Thickness{0, 0, 0, 0});
    list.FocusVisualSecondaryThickness(mux::Thickness{0, 0, 0, 0});
    list.SelectionMode(muxc::ListViewSelectionMode::Extended);
    list.Padding(mux::Thickness{5, 12, 10, 4});
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
                if (auto list = sender.try_as<muxc::ListView>()) {
                    OnEmptyDoubleTapped(list, args);
                }
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
    // A click stops a scroll that is still gliding. A row marks its own press as
    // handled, so this is added to see handled presses too.
    column->stopScrollHandler = mux::Input::PointerEventHandler(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& pressArgs) {
            try {
                if (auto list = sender.try_as<muxc::ListView>()) {
                    auto scroller = FindListScroller(list, 0);
                    WindowState* pressState = StateForThisThread(false);
                    // A row marks its own press as handled, so the list's ordinary
                    // press handler never sees it. A drag of rows starts here.
                    if (pressState && pressState->on && !g_unloading && !pressState->rename) {
                        mux::DependencyObject node =
                            pressArgs.OriginalSource().try_as<mux::DependencyObject>();
                        for (int i = 0; node && i < 20; i++) {
                            if (auto row = node.try_as<muxc::ListViewItem>()) {
                                OnDragPressed(pressState, list, row, pressArgs);
                                break;
                            }
                            node = mux::Media::VisualTreeHelper::GetParent(node);
                        }
                    }
                    if (pressState) {
                        int at = ColumnIndexOfList(pressState, list);
                        if (at >= 0) {
                            Column* c = pressState->columns[at].get();
                            // Turns that keep coming right after the press are a glide.
                            c->wheelSuppress = GetTickCount64() - c->lastWheelAt < 600;
                            c->wheelTarget = scroller ? scroller.VerticalOffset() : 0;
                        }
                    }
                    if (scroller) {
                        double offset = scroller.VerticalOffset();
                        // Move by half a pixel and back, without animation: a change to
                        // the same offset is ignored while the list glides.
                        double nudge = offset + 0.5 <= scroller.ScrollableHeight() ? 0.5 : -0.5;
                        scroller.ChangeView(nullptr, offset + nudge, nullptr, true);
                        scroller.ChangeView(nullptr, offset, nullptr, true);
                        bool inertia = scroller.IsScrollInertiaEnabled();
                        scroller.IsScrollInertiaEnabled(false);
                        scroller.IsScrollInertiaEnabled(inertia);
                        // A ChangeView to where the list already is can be ignored
                        // while it glides. Turning vertical scrolling off and on
                        // again ends the glide.
                        auto mode = scroller.VerticalScrollMode();
                        scroller.VerticalScrollMode(muxc::ScrollMode::Disabled);
                        scroller.VerticalScrollMode(mode);
                        static int logged = 0;
                        if (logged++ < 6) {
                            Out(L"a press stops the scroll of a list, offset %.1f", offset);
                        }
                    }
                }
            } catch (...) {
            }
        });
    list.AddHandler(mux::UIElement::PointerPressedEvent(),
                    winrt::box_value(column->stopScrollHandler), true);
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
                            if (!element) {
                                return;
                            }
                            auto props = args.GetCurrentPoint(element).Properties();
                            if (props.IsHorizontalMouseWheel()) {
                                args.Handled(true);
                                return;
                            }
                            // A vertical turn: scrolled here, not by the list, so that a
                            // press can stop it. A touchpad keeps sending turns while it
                            // glides, and a press ends that by dropping the turns until
                            // there is a pause.
                            WindowState* state = StateForThisThread(false);
                            if (!state || !state->on || g_unloading) {
                                return;
                            }
                            for (auto& c : state->columns) {
                                if (c->panel != element) {
                                    continue;
                                }
                                auto scroller = FindListScroller(c->list, 0);
                                if (!scroller) {
                                    return;
                                }
                                ULONGLONG now = GetTickCount64();
                                if (c->wheelSuppress) {
                                    if (now - c->lastWheelAt > 120) {
                                        c->wheelSuppress = false;
                                    } else {
                                        c->lastWheelAt = now;
                                        args.Handled(true);
                                        return;
                                    }
                                }
                                int delta = props.MouseWheelDelta();
                                bool notch = delta % 120 == 0;
                                double from = (notch && now - c->lastWheelAt < 300)
                                                  ? c->wheelTarget
                                                  : scroller.VerticalOffset();
                                double target = from - delta * 0.8;
                                target = std::max(0.0, std::min(target, scroller.ScrollableHeight()));
                                c->wheelTarget = target;
                                c->lastWheelAt = now;
                                scroller.ChangeView(nullptr, target, nullptr, !notch);
                                args.Handled(true);
                                return;
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
    {
        mux::Media::CompositeTransform motion;
        motion.ScaleY(0);
        pill.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
        pill.RenderTransform(motion);
        pill.Tag(winrt::box_value(false));
    }
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
           mux::Input::PointerRoutedEventArgs const& args) {
            WindowState* state = StateForThisThread(false);
            int index = state ? ColumnIndexOfGrip(state, sender) : -1;
            if (index >= 0) {
                Column* column = state->columns[index].get();
                column->hoverSince = GetTickCount64();
                if (state->dragColumn < 0) {
                    PillShow(column, args.GetCurrentPoint(column->grip).Position().Y);
                }
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
                    PillHide(state->columns[index].get());
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
            if (!state || !state->on) {
                return;
            }
            try {
                int index = ColumnIndexOfGrip(state, sender);
                if (index >= 0) {
                    Column* column = state->columns[index].get();
                    PillFollow(column, args.GetCurrentPoint(column->grip).Position().Y);
                }
                if (state->dragColumn < 0 || index != state->dragColumn) {
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
                    PillHide(state->columns[index].get());
                }
                RefreshNames(state->columns[index].get());
                RememberFolderWidth(PathOf(state->columns[index]->folder.get()),
                                    state->columns[index]->list.Width());
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
                if (g_harmonize && (GetKeyState(VK_SHIFT) & 0x8000)) {
                    HarmonizeWidths(state);
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
            RememberFolderSpec(PathOf(current.get()), fresh);
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
    if (FolderGone(folder)) {
        Out(L"not navigating Explorer to a folder that is gone");
        return;
    }
    state->cursorFixUntil = GetTickCount64() + 2500;
    g_ourBrowse = true;
    // Without taking the keyboard: Explorer's view used to take it, and giving
    // it back later flashed the selection and scrolled the strip.
    HRESULT hr = browser->BrowseObject(
        folder, SBSP_ABSOLUTE | SBSP_SAMEBROWSER | SBSP_ACTIVATE_NOFOCUS);
    g_ourBrowse = false;
    if (FAILED(hr)) {
        Out(L"BrowseObject failed, hr=0x%08lX", hr);
        state->expected.reset();
    } else {
        state->specSyncDue = GetTickCount64() + 700;
    }
}

// Explorer follows after a short pause, so a run of quick selections costs
// one navigation. A selection in the folder Explorer already shows needs no
// navigation: it is mirrored at once, so a previewer that asks Explorer for the
// selected item (QuickLook on Space) finds it there.
void ScheduleNavigation(WindowState* state, PCIDLIST_ABSOLUTE folder, ULONGLONG delay) {
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
    if (state->lastSeen && !state->expected && ILIsEqual(state->lastSeen.get(), folder)) {
        state->pendingNav.reset();
        if (state->selectChild && state->selectFolder && ILIsEqual(state->selectFolder.get(), folder)) {
            if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
                if (SelectInExplorer(browser, state->selectChild.get())) {
                    if (!state->selectExtra.empty()) {
                        AddToSelectionInExplorer(browser, state->selectExtra);
                        state->selectExtra.clear();
                    }
                    state->selectFolder.reset();
                    state->selectChild.reset();
                }
            }
        }
        return;
    }
    state->pendingNav.reset(ILClone(folder));
    state->navDue = GetTickCount64() + delay;
}

// While the columns to the right of `columnIndex` are stale, the chevron of the
// folder that leads to them sits on a small rounded badge, a quiet thread of the
// hierarchy.
void SetStaleHint(WindowState* state, int columnIndex, bool on) {
    if (columnIndex < 0 || columnIndex >= (int)state->columns.size()) {
        return;
    }
    Column* column = state->columns[columnIndex].get();
    try {
        if (column->hintBadge) {
            if (auto parent = mux::Media::VisualTreeHelper::GetParent(column->hintBadge).try_as<muxc::Panel>()) {
                uint32_t index = 0;
                if (parent.Children().IndexOf(column->hintBadge, index)) {
                    parent.Children().RemoveAt(index);
                }
            }
            column->hintBadge = nullptr;
        }
        if (column->hintEntry >= 0 &&
            column->hintEntry < (int)column->chevrons.size() &&
            column->chevrons[column->hintEntry]) {
            column->chevrons[column->hintEntry].Opacity(0.6);
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
            if (ILIsEqual(column->entries[i].child.get(), leading.get())) {
                EnsureRowBuilt(column, i);
                auto chevron = column->chevrons[i];
                if (!chevron) {
                    break;
                }
                if (auto grid = mux::Media::VisualTreeHelper::GetParent(chevron).try_as<muxc::Panel>()) {
                    uint32_t at = 0;
                    if (grid.Children().IndexOf(chevron, at)) {
                        bool dark = chevron.ActualTheme() == mux::ElementTheme::Dark;
                        muxc::Border badge;
                        // A circle whose middle is the chevron's middle.
                        double glyph = chevron.ActualWidth() > 1 ? chevron.ActualWidth() : 10;
                        badge.Width(15);
                        badge.Height(15);
                        badge.CornerRadius(mux::CornerRadius{7.5, 7.5, 7.5, 7.5});
                        badge.Margin(mux::Thickness{0, 0, glyph / 2 - 7.5, 0});
                        badge.Background(dark ? Brush(13, 255, 255, 255) : Brush(10, 0, 0, 0));
                        badge.HorizontalAlignment(mux::HorizontalAlignment::Right);
                        badge.VerticalAlignment(mux::VerticalAlignment::Center);
                        badge.IsHitTestVisible(false);
                        muxc::Grid::SetColumn(badge, 3);
                        grid.Children().InsertAt(at, badge);  // Under the chevron.
                        column->hintBadge = badge;
                    }
                }
                chevron.Opacity(0.9);
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

// The background of a selected row is a border inside its presenter, inset by
// 4 px at the sides and 2.3 px above and below. Rows that are selected next to
// each other share one background, as in Finder: the border loses the inset
// where it meets the next one, and its corners there are square. Only the rows
// touched by the last pass are put back, so a long column costs nothing when
// the selection does not change.
muxc::Border SelectionBorderOf(muxc::ListViewItem const& item) {
    try {
        if (mux::Media::VisualTreeHelper::GetChildrenCount(item) > 0) {
            auto presenter = mux::Media::VisualTreeHelper::GetChild(item, 0);
            if (presenter && mux::Media::VisualTreeHelper::GetChildrenCount(presenter) > 0) {
                return mux::Media::VisualTreeHelper::GetChild(presenter, 0).try_as<muxc::Border>();
            }
        }
    } catch (...) {
    }
    return nullptr;
}

void ShapeSelectionRuns(Column* column, muxc::ListView const& list) {
    static bool known = false;
    static mux::Thickness defaultMargin{4, 2.3, 4, 2.3};
    static mux::CornerRadius defaultCorner{4, 4, 4, 4};
    try {
        auto items = list.Items();
        uint32_t count = items.Size();
        auto rowAt = [&](uint32_t r) -> muxc::ListViewItem {
            return r < count ? items.GetAt(r).try_as<muxc::ListViewItem>() : muxc::ListViewItem{nullptr};
        };
        auto selectedAt = [&](uint32_t r) {
            if (r >= count || r >= column->rowToEntry.size() || column->rowToEntry[r] < 0) {
                return false;
            }
            auto item = rowAt(r);
            return item && item.IsSelected() && item.Visibility() == mux::Visibility::Visible;
        };
        for (uint32_t r : column->shapedRows) {
            if (auto item = rowAt(r)) {
                if (auto border = SelectionBorderOf(item)) {
                    border.Margin(defaultMargin);
                    border.CornerRadius(defaultCorner);
                }
            }
        }
        column->shapedRows.clear();
        auto selected = list.SelectedItems();
        for (uint32_t i = 0; i < selected.Size(); i++) {
            auto item = selected.GetAt(i).try_as<muxc::ListViewItem>();
            if (!item) {
                continue;
            }
            int index = list.IndexFromContainer(item);
            if (index < 0) {
                continue;
            }
            auto border = SelectionBorderOf(item);
            if (!border) {
                continue;
            }
            if (!known) {
                known = true;
                defaultMargin = border.Margin();
                defaultCorner = border.CornerRadius();
            }
            uint32_t r = (uint32_t)index;
            bool up = r > 0 && selectedAt(r - 1);
            bool down = selectedAt(r + 1);
            if (!up && !down) {
                continue;
            }
            border.Margin(mux::Thickness{defaultMargin.Left, up ? 0.0 : defaultMargin.Top, defaultMargin.Right,
                                         down ? 0.0 : defaultMargin.Bottom});
            double top = up ? 0 : defaultCorner.TopLeft, bottom = down ? 0 : defaultCorner.BottomLeft;
            border.CornerRadius(mux::CornerRadius{top, top, bottom, bottom});
            column->shapedRows.push_back(r);
        }
    } catch (...) {
        LogHresult(L"shaping runs of selected rows");
    }
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
        ShapeSelectionRuns(column, list);
        if (column->focusHeader >= 0) {
            SetHeaderFocus(column, list, -1);  // A row was chosen: the keyboard leaves the header.
        }
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
            if (dim && !g_keepStale) {
                // The setting asks for them to go instead of waiting, dimmed.
                state->building = true;
                RemoveColumnsFrom(state, columnIndex + 1);
                state->building = false;
                SetStaleHint(state, columnIndex, false);
                return;
            }
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
            ScheduleNavigation(state, column->folder.get(), 120);
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
        Pidl absolute(entry.linkTarget ? ILClone(entry.linkTarget.get())
                                       : ILCombine(column->folder.get(), entry.child.get()));
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
        if (entry.folder && FolderGone(absolute.get())) {
            // The row is stale: the folder was deleted, renamed or moved. Read
            // the column again instead of opening a column on nothing.
            Out(L"\"%s\" is gone, the column is read again", entry.name.c_str());
            RequestLoad(state, column);
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
                AddColumn(state, std::move(absolute), false, entry.archive);
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
            ScheduleNavigation(state, column->folder.get(), 120);
        }
    } catch (...) {
        state->building = false;
        LogHresult(L"OnRowSelected");
    }
}

bool TryShowNewTab(WindowState* state);
void OnTick();
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
    state->redo.clear();  // A new action ends the redo history.
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
    if (entry.created) {
        // The items that were moved into it go back first.
        for (size_t i = entry.moves.size(); i-- > 0;) {
            MoveFileW(entry.moves[i].second.c_str(), entry.moves[i].first.c_str());
        }
        // A new folder is taken back to the Recycle Bin, with what is in it.
        std::wstring names = entry.renamedTo;
        names.push_back(L'\0');
        names.push_back(L'\0');
        SHFILEOPSTRUCTW op = {};
        op.hwnd = state->topLevel;
        op.wFunc = FO_DELETE;
        op.pFrom = names.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
        int result = SHFileOperationW(&op);
        hr = result == 0 && !op.fAnyOperationsAborted ? S_OK : E_FAIL;
        Out(L"Undo new folder \"%s\": result %d", entry.renamedTo.c_str(), result);
    } else if (!entry.renames.empty()) {
        hr = S_OK;
        for (size_t i = entry.renames.size(); i-- > 0;) {
            if (!MoveFileW(entry.renames[i].second.c_str(), entry.renames[i].first.c_str())) {
                hr = HRESULT_FROM_WIN32(GetLastError());
            }
        }
        Out(L"Undo of %d renames: hr=0x%08lX", (int)entry.renames.size(), hr);
    } else if (entry.deletion) {
        hr = RestoreFromRecycleBin(state, entry.paths);
        Out(L"Undo delete of %d item(s): hr=0x%08lX", (int)entry.paths.size(), hr);
    } else {
        BOOL moved = MoveFileW(entry.renamedTo.c_str(), entry.renamedFrom.c_str());
        hr = moved ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        Out(L"Undo rename \"%s\" back to \"%s\": hr=0x%08lX", entry.renamedTo.c_str(),
            entry.renamedFrom.c_str(), hr);
    }
    if (SUCCEEDED(hr)) {
        state->redo.push_back(std::move(entry));
    }
    return true;
}

// Does again what Ctrl+Z took back: the delete goes to the Recycle Bin again,
// the rename is made again. False if there is nothing to redo.
bool RedoLast(WindowState* state) {
    if (state->redo.empty()) {
        return false;
    }
    UndoEntry entry = std::move(state->redo.back());
    state->redo.pop_back();
    HRESULT hr = S_OK;
    if (entry.created) {
        hr = CreateDirectoryW(entry.renamedTo.c_str(), nullptr)
                 ? S_OK
                 : HRESULT_FROM_WIN32(GetLastError());
        for (auto& pair : entry.moves) {
            MoveFileW(pair.first.c_str(), pair.second.c_str());
        }
        Out(L"Redo new folder \"%s\": hr=0x%08lX", entry.renamedTo.c_str(), hr);
    } else if (!entry.renames.empty()) {
        for (auto& pair : entry.renames) {
            if (!MoveFileW(pair.first.c_str(), pair.second.c_str())) {
                hr = HRESULT_FROM_WIN32(GetLastError());
            }
        }
        Out(L"Redo of %d renames: hr=0x%08lX", (int)entry.renames.size(), hr);
    } else if (entry.deletion) {
        std::wstring names;
        for (auto& path : entry.paths) {
            names += path;
            names.push_back(L'\0');
        }
        names.push_back(L'\0');
        SHFILEOPSTRUCTW op = {};
        op.hwnd = state->topLevel;
        op.wFunc = FO_DELETE;
        op.pFrom = names.c_str();
        op.fFlags = FOF_ALLOWUNDO;
        int result = SHFileOperationW(&op);
        hr = result == 0 && !op.fAnyOperationsAborted ? S_OK : E_FAIL;
        Out(L"Redo delete of %d item(s): result %d", (int)entry.paths.size(), result);
    } else {
        BOOL moved = MoveFileW(entry.renamedFrom.c_str(), entry.renamedTo.c_str());
        hr = moved ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        Out(L"Redo rename \"%s\" to \"%s\": hr=0x%08lX", entry.renamedFrom.c_str(),
            entry.renamedTo.c_str(), hr);
    }
    if (SUCCEEDED(hr)) {
        state->undo.push_back(std::move(entry));  // Straight, so the redo history stays.
    }
    return true;
}

// The selected items' paths on the clipboard as text, each in quotes on its
// own line, like Explorer's "Copy as path".
void CopyAsPath(WindowState* state, Column* column, std::vector<size_t> const& entries) {
    std::wstring text;
    for (size_t entry : entries) {
        if (entry >= column->entries.size()) {
            continue;
        }
        std::wstring path = FilePathOf(column->folder.get(), column->entries[entry].child.get());
        if (path.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += L"\r\n";
        }
        text += L"\"" + path + L"\"";
    }
    if (text.empty() || !OpenClipboard(state->topLevel)) {
        return;
    }
    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (memory) {
        if (wchar_t* target = (wchar_t*)GlobalLock(memory)) {
            memcpy(target, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                GlobalFree(memory);
            }
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    Out(L"copied %d path(s) as text", (int)entries.size());
}

void CommitRename(WindowState* state) {
    if (!state->rename || state->rename->done) {
        return;
    }
    std::wstring newName = state->rename->box.Text().c_str();
    Column* column = state->rename->column;
    size_t entryIndex = state->rename->entry;
    std::vector<size_t> together = state->rename->multi;
    CancelRename(state);

    // Several items: the typed name, then a number, and each keeps its own
    // extension, as Explorer names them: "Name (1).txt", "Name (2).jpg".
    if (together.size() > 1 && !newName.empty()) {
        auto extensionOf = [](std::wstring const& name) {
            size_t dot = name.rfind(L'.');
            return dot != std::wstring::npos && dot > 0 ? name.substr(dot) : std::wstring();
        };
        std::wstring firstExtension =
            entryIndex < column->entries.size() && !column->entries[entryIndex].folder
                ? extensionOf(column->entries[entryIndex].name)
                : std::wstring();
        std::wstring base = newName;
        if (!firstExtension.empty() && base.size() > firstExtension.size() &&
            _wcsicmp(base.c_str() + base.size() - firstExtension.size(), firstExtension.c_str()) == 0) {
            base.resize(base.size() - firstExtension.size());
        }
        IShellFolder* parent = nullptr;
        HRESULT bound = ILIsEmpty(column->folder.get())
                            ? SHGetDesktopFolder(&parent)
                            : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                             IID_PPV_ARGS(&parent));
        if (FAILED(bound) || !parent) {
            return;
        }
        UndoEntry undo;
        int number = 0;
        for (size_t entry : together) {
            if (entry >= column->entries.size()) {
                continue;
            }
            number++;
            std::wstring name = base + L" (" + std::to_wstring(number) + L")" +
                                (column->entries[entry].folder
                                     ? std::wstring()
                                     : extensionOf(column->entries[entry].name));
            std::wstring oldPath = FilePathOf(column->folder.get(), column->entries[entry].child.get());
            PITEMID_CHILD renamed = nullptr;
            HRESULT hr = parent->SetNameOf(state->topLevel, column->entries[entry].child.get(),
                                           name.c_str(), SHGDN_INFOLDER, &renamed);
            Out(L"rename %d of %d, \"%s\" to \"%s\": hr=0x%08lX", number, (int)together.size(),
                column->entries[entry].name.c_str(), name.c_str(), hr);
            if (SUCCEEDED(hr) && renamed) {
                std::wstring newPath = FilePathOf(column->folder.get(), renamed);
                if (!oldPath.empty() && !newPath.empty()) {
                    undo.renames.emplace_back(oldPath, newPath);
                }
            }
            if (renamed) {
                CoTaskMemFree(renamed);
            }
        }
        parent->Release();
        if (!undo.renames.empty()) {
            PushUndo(state, std::move(undo));
        }
        RequestLoad(state, column);
        return;
    }
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

void BeginRename(WindowState* state, Column* column, size_t entryIndex);

// Several selected items are renamed together: the box opens on the first, and
// what is typed names all of them.
void BeginMultiRename(WindowState* state, Column* column, std::vector<size_t> const& entries) {
    if (entries.empty()) {
        return;
    }
    BeginRename(state, column, entries[0]);
    if (state->rename && entries.size() > 1) {
        state->rename->multi = entries;
        Out(L"renaming %d items together", (int)entries.size());
    }
}

void BeginRename(WindowState* state, Column* column, size_t entryIndex) {
    CancelRename(state);
    bool known = false;
    for (auto& c : state->columns) {
        known = known || c.get() == column;
    }
    if (known) {
        EnsureRowBuilt(column, entryIndex);  // A row that was never near the screen.
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
        for (auto& hook : context->buttonHooks) {
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
    context->buttonHooks.clear();
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

int ArchiveParentColumn(WindowState* state, Column* column);

// Runs one command of the shell's menu for the items the menu was made for.
void RunContextCommand(UINT id) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->context || g_unloading) {
        return;
    }
    try {
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
}

void FillFlyoutItems(WindowState* state, HMENU menu,
                     winrt::Windows::Foundation::Collections::IVector<
                         muxc::MenuFlyoutItemBase> const& items,
                     int depth, std::set<int> const* skip = nullptr) {
    // Inside an archive only Copy is offered. Properties on such items took
    // Explorer down in the 0.7.2 test, and Delete, Cut, Rename, Send to and
    // the like have no meaning there.
    bool restricted = state->context && state->context->insideArchive;
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
        if (skip && skip->count(i)) {
            continue;  // Shown elsewhere in the menu.
        }
        if (needSeparator) {
            items.Append(muxc::MenuFlyoutSeparator());
            needSeparator = false;
        }
        if (restricted) {
            if (info.hSubMenu) {
                continue;
            }
            wchar_t verb[64] = L"";
            if (state->context->menu) {
                state->context->menu->GetCommandString(info.wID - state->context->idFirst,
                                                       GCS_VERBW, nullptr, (char*)verb,
                                                       ARRAYSIZE(verb));
            }
            if (_wcsicmp(verb, L"copy") != 0) {
                continue;
            }
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
                auto item = sender.try_as<muxc::MenuFlyoutItem>();
                if (item) {
                    RunContextCommand(winrt::unbox_value<uint32_t>(item.Tag()));
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
                UndoEntry undo;
                undo.created = true;
                undo.renamedTo = full;
                PushUndo(state, std::move(undo));
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

// Makes a folder in the column's folder and moves the selected items into it,
// then opens its name for editing. The move is the shell's own, so conflicts
// and progress behave as in Explorer.
void NewFolderWithSelection(WindowState* state, Column* column,
                            std::vector<size_t> const& entries) {
    wchar_t path[MAX_PATH] = L"";
    if (entries.empty() || !SHGetPathFromIDListW(column->folder.get(), path) || !path[0]) {
        Out(L"new folder with selection: not a folder on disk, or nothing selected");
        return;
    }
    std::vector<std::wstring> from;
    for (size_t entry : entries) {
        if (entry < column->entries.size()) {
            std::wstring item = FilePathOf(column->folder.get(), column->entries[entry].child.get());
            if (!item.empty()) {
                from.push_back(item);
            }
        }
    }
    if (from.empty()) {
        return;
    }
    std::wstring name = L"New folder";
    std::wstring candidate, full;
    for (int i = 1; i < 100; i++) {
        candidate = i == 1 ? name : name + L" (" + std::to_wstring(i) + L")";
        full = std::wstring(path) + L"\\" + candidate;
        if (!PathFileExistsW(full.c_str())) {
            break;
        }
    }
    if (!CreateDirectoryW(full.c_str(), nullptr)) {
        Out(L"new folder with selection: CreateDirectory failed, gle=%lu", GetLastError());
        return;
    }
    std::wstring names;
    for (auto& item : from) {
        names += item;
        names.push_back(L'\0');
    }
    names.push_back(L'\0');
    std::wstring target = full;
    target.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.hwnd = state->topLevel;
    op.wFunc = FO_MOVE;
    op.pFrom = names.c_str();
    op.pTo = target.c_str();
    op.fFlags = FOF_ALLOWUNDO;
    int result = SHFileOperationW(&op);
    Out(L"new folder \"%s\" with %d item(s): result %d", candidate.c_str(), (int)from.size(), result);

    UndoEntry undo;
    undo.created = true;
    undo.renamedTo = full;
    for (auto& item : from) {
        const wchar_t* leaf = wcsrchr(item.c_str(), L'\\');
        std::wstring moved = full + L"\\" + (leaf ? leaf + 1 : item.c_str());
        if (PathFileExistsW(moved.c_str())) {
            undo.moves.emplace_back(item, moved);
        }
    }
    PushUndo(state, std::move(undo));
    state->renameName = candidate;
    state->renameColumn = column;
    RequestLoad(state, column);
}

// Opens a folder in a new tab of this window: Ctrl+T makes the tab, and once
// Explorer has built it, the folder is shown in it.
void OpenFolderInNewTab(WindowState* state, PCIDLIST_ABSOLUTE folder) {
    if (!folder || FolderGone(folder)) {
        return;
    }
    state->openTabFolder.reset(ILClone(folder));
    state->openTabUntil = GetTickCount64() + 4000;
    state->newTab = nullptr;
    INPUT keys[4] = {};
    keys[0].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_CONTROL;
    keys[1].type = INPUT_KEYBOARD;
    keys[1].ki.wVk = 'T';
    keys[2] = keys[1];
    keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3] = keys[0];
    keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, keys, sizeof(INPUT));
    Out(L"open in a new tab: \"%s\"", PathOf(folder).c_str());
    // Watched every 10 ms, not at the tick's pace: the folder is shown in the tab
    // and the columns follow the moment it exists.
    try {
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(10));
        timer.Tick([](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
            WindowState* now = StateForThisThread(false);
            if (!now || g_unloading || TryShowNewTab(now)) {
                sender.Stop();
                if (now && !g_unloading) {
                    OnTick();  // Notices the new folder at once.
                }
            }
        });
        timer.Start();
    } catch (...) {
        LogHresult(L"watching for the new tab");
    }
}

// Extracts an archive into a folder next to it, named like the archive, with the
// shell's own copy: conflicts and progress behave as in Explorer.
void ExtractHere(WindowState* state, Column* column, size_t entryIndex) {
    if (entryIndex >= column->entries.size() || !column->entries[entryIndex].archive) {
        return;
    }
    Pidl archive(ILCombine(column->folder.get(), column->entries[entryIndex].child.get()));
    std::wstring archivePath = FilePathOf(column->folder.get(), column->entries[entryIndex].child.get());
    if (!archive || archivePath.empty()) {
        return;
    }
    std::wstring target = archivePath;
    size_t dot = target.rfind(L'.');
    if (dot != std::wstring::npos && dot > target.rfind(L'\\')) {
        target.resize(dot);
    }
    for (int i = 2; PathFileExistsW(target.c_str()) && i < 100; i++) {
        target = archivePath.substr(0, archivePath.rfind(L'.')) + L" (" + std::to_wstring(i) + L")";
    }
    if (!CreateDirectoryW(target.c_str(), nullptr)) {
        Out(L"extract: can't make \"%s\", gle=%lu", target.c_str(), GetLastError());
        return;
    }
    IShellFolder* source = nullptr;
    HRESULT hr = SHBindToObject(nullptr, archive.get(), nullptr, IID_PPV_ARGS(&source));
    IShellItem* destination = nullptr;
    if (SUCCEEDED(hr) && source) {
        hr = SHCreateItemFromParsingName(target.c_str(), nullptr, IID_PPV_ARGS(&destination));
    }
    IFileOperation* operation = nullptr;
    if (SUCCEEDED(hr) && destination) {
        hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    }
    int copied = 0;
    if (SUCCEEDED(hr) && operation) {
        operation->SetOwnerWindow(state->topLevel);
        operation->SetOperationFlags(FOFX_NOCOPYHOOKS | FOF_NOCONFIRMMKDIR);
        IEnumIDList* list = nullptr;
        if (source->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &list) == S_OK && list) {
            PITEMID_CHILD child = nullptr;
            while (list->Next(1, &child, nullptr) == S_OK && child) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(SHCreateItemWithParent(nullptr, source, child, IID_PPV_ARGS(&item))) && item) {
                    if (SUCCEEDED(operation->CopyItem(item, destination, nullptr, nullptr))) {
                        copied++;
                    }
                    item->Release();
                }
                CoTaskMemFree(child);
                child = nullptr;
            }
            list->Release();
        }
        if (copied > 0) {
            hr = operation->PerformOperations();
        }
    }
    Out(L"extract \"%s\": %d item(s), hr=0x%08lX", target.c_str(), copied, hr);
    if (copied > 0) {
        const wchar_t* leaf = wcsrchr(target.c_str(), L'\\');
        state->selectNames.assign(1, leaf ? leaf + 1 : target.c_str());
        state->selectNamesColumn = column;
    }
    if (operation) {
        operation->Release();
    }
    if (destination) {
        destination->Release();
    }
    if (source) {
        source->Release();
    }
    RequestLoad(state, column);
}

// For a column that shows the inside of an archive: the column before the
// archive, on disk, which holds it. -1 if this column is not inside one.
int ArchiveParentColumn(WindowState* state, Column* column) {
    int index = -1;
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (state->columns[i].get() == column) {
            index = (int)i;
        }
    }
    if (index <= 0) {
        return -1;
    }
    // The shell gives a folder inside an archive a path too ("C:\x\a.zip\sub"),
    // so look for a part of the path that is a file, not a folder.
    std::wstring path = PathOf(column->folder.get());
    if (path.size() < 3 || path[1] != L':') {
        return -1;
    }
    std::wstring archive;
    for (std::wstring part = path; part.size() > 3;) {
        DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            archive = part;
            break;
        }
        size_t slash = part.rfind(L'\\');
        if (slash == std::wstring::npos) {
            break;
        }
        part.resize(slash);
    }
    if (archive.empty()) {
        return -1;
    }
    // The column that shows the archive itself; the one before it holds the
    // archive (its own name can be a library name, not a path).
    for (int j = index; j >= 1; j--) {
        std::wstring shown = PathOf(state->columns[j]->folder.get());
        if (_wcsicmp(shown.c_str(), archive.c_str()) == 0) {
            return j - 1;
        }
    }
    return -1;
}

// Copies the picked items of an archive that is open in a column into the
// folder that holds the archive, with the shell's own copy, and selects them.
void ExtractSelected(WindowState* state, Column* column, std::vector<size_t> const& entries) {
    int parentIndex = ArchiveParentColumn(state, column);
    if (parentIndex < 0 || entries.empty()) {
        return;
    }
    Column* parent = state->columns[parentIndex].get();
    IShellFolder* source = nullptr;
    HRESULT hr = SHBindToObject(nullptr, column->folder.get(), nullptr, IID_PPV_ARGS(&source));
    IShellItem* destination = nullptr;
    if (SUCCEEDED(hr) && source) {
        hr = SHCreateItemFromIDList(parent->folder.get(), IID_PPV_ARGS(&destination));
    }
    IFileOperation* operation = nullptr;
    if (SUCCEEDED(hr) && destination) {
        hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    }
    int copied = 0;
    std::vector<std::wstring> names;
    if (SUCCEEDED(hr) && operation) {
        operation->SetOwnerWindow(state->topLevel);
        operation->SetOperationFlags(FOFX_NOCOPYHOOKS | FOF_NOCONFIRMMKDIR);
        for (size_t entry : entries) {
            if (entry >= column->entries.size()) {
                continue;
            }
            IShellItem* item = nullptr;
            if (SUCCEEDED(SHCreateItemWithParent(nullptr, source, column->entries[entry].child.get(),
                                                 IID_PPV_ARGS(&item))) &&
                item) {
                if (SUCCEEDED(operation->CopyItem(item, destination, nullptr, nullptr))) {
                    copied++;
                    // The name the copy will have: the item's own file name.
                    PWSTR leaf = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &leaf)) && leaf) {
                        names.push_back(leaf);
                        CoTaskMemFree(leaf);
                    }
                }
                item->Release();
            }
        }
        if (copied > 0) {
            hr = operation->PerformOperations();
        }
    }
    Out(L"extract selected: %d item(s) beside the archive, hr=0x%08lX", copied, hr);
    if (operation) {
        operation->Release();
    }
    if (destination) {
        destination->Release();
    }
    if (source) {
        source->Release();
    }
    if (copied > 0 && SUCCEEDED(hr)) {
        state->selectNames = names;
        state->selectNamesColumn = parent;
        RequestLoad(state, parent);
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

void ShowBackgroundMenu(WindowState* state, Column* column, muxc::ListView const& list,
                        mux::Input::RightTappedRoutedEventArgs const& args);

// The item's flyout: the shell's own commands for the picked items, at a point
// of the list. Used by the right-click and by the Menu key.
// One entry of the shell's menu for the picked items.
struct MenuRow {
    int pos = 0;
    UINT id = 0;
    std::wstring text;
    std::wstring verb;
    HMENU sub = nullptr;
    bool enabled = true;
};

// A Fluent glyph for a shell verb, or an empty string.
const wchar_t* GlyphForVerb(std::wstring const& verb) {
    static const struct {
        const wchar_t* verb;
        const wchar_t* glyph;
    } table[] = {
        {L"cut", L""},        {L"copy", L""},
        {L"rename", L""},     {L"delete", L""},
        {L"open", L""},       {L"openas", L""},
        {L"properties", L""}, {L"pintohome", L""},
        {L"copyaspath", L""}, {L"edit", L""},
        {L"print", L""},      {L"setdesktopwallpaper", L""},
    };
    for (auto& entry : table) {
        if (_wcsicmp(verb.c_str(), entry.verb) == 0) {
            return entry.glyph;
        }
    }
    if (StrStrIW(verb.c_str(), L"share")) {
        return L"";
    }
    return L"";
}

// The shortcut shown at the right of a list entry.
const wchar_t* ShortcutForVerb(std::wstring const& verb) {
    if (_wcsicmp(verb.c_str(), L"open") == 0) {
        return L"Enter";
    }
    if (_wcsicmp(verb.c_str(), L"properties") == 0) {
        return L"Alt+Enter";
    }
    if (_wcsicmp(verb.c_str(), L"copyaspath") == 0) {
        return L"Ctrl+Shift+C";
    }
    return L"";
}

// The name of the icon file Windows ships for a shell verb, among the two-tone
// SVGs of Explorer's own toolbar and menus. Empty when there is none.
const wchar_t* OfficialIconName(std::wstring const& verb) {
    static const struct {
        const wchar_t* verb;
        const wchar_t* file;
    } table[] = {
        {L"cut", L"windows.cut"},
        {L"copy", L"windows.copy"},
        {L"rename", L"windows.rename"},
        {L"delete", L"windows.ribbondelete"},
        {L"openas", L"windows.openwith"},
        {L"properties", L"windows.properties"},
        {L"pintohome", L"windows.pintohome"},
        {L"pintohomefile", L"windows.pintohomefile"},
        {L"copyaspath", L"windows.copyaspath"},
        {L"setdesktopwallpaper", L"windows.setdesktopwallpaper"},
        {L"link", L"link"},
        {L"print", L"print"},
    };
    for (auto& entry : table) {
        if (_wcsicmp(verb.c_str(), entry.verb) == 0) {
            return entry.file;
        }
    }
    if (StrStrIW(verb.c_str(), L"share")) {
        return L"Windows.ModernShare";
    }
    return L"";
}

// The icon Windows draws for that file, in the colours of the current theme. The
// files are read straight from the File Explorer package.
mux::UIElement OfficialIcon(const wchar_t* name, double size) {
    if (!name || !name[0]) {
        return nullptr;
    }
    try {
        std::wstring path =
            std::wstring(L"C:/Windows/SystemApps/MicrosoftWindows.Client.FileExp_cw5n1h2txyewy/"
                         L"FileExplorerExtensions/Assets/images/contrast-standard/") +
            (IsDarkMode() ? L"theme-dark/" : L"theme-light/") + name + L".svg";
        if (!PathFileExistsW(path.c_str())) {
            return nullptr;
        }
        mux::Media::Imaging::SvgImageSource source{
            winrt::Windows::Foundation::Uri(winrt::hstring(L"file:///" + path))};
        source.RasterizePixelWidth(size * 3);
        source.RasterizePixelHeight(size * 3);
        muxc::ImageIcon icon;
        icon.Source(source);
        icon.Width(size);
        icon.Height(size);
        return icon;
    } catch (...) {
        LogHresult(L"OfficialIcon");
    }
    return nullptr;
}

// A plain glyph icon. The accent part of a two-tone icon comes from the official
// files; a glyph has no such part to colour.
mux::UIElement MakeTwoToneIcon(WindowState* state, const wchar_t* glyph, double size, bool enabled) {
    muxc::Grid host;
    host.Width(size + 2);
    host.Height(size + 2);
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(glyph);
    icon.FontSize(size);
    icon.Opacity(enabled ? 1.0 : 0.5);
    icon.HorizontalAlignment(mux::HorizontalAlignment::Center);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    host.Children().Append(icon);
    return host;
}

// The icon Explorer's toolbar uses for a command with this label: on a button of
// the bar, or on an entry of one of its menus.
mux::Controls::IconElement FindToolbarIcon(mux::DependencyObject const& node, const wchar_t* label,
                                           int depth, int& visited);

mux::Controls::IconElement IconInMenuItems(
    winrt::Windows::Foundation::Collections::IVector<muxc::MenuFlyoutItemBase> const& items,
    const wchar_t* label, int depth) {
    for (uint32_t i = 0; i < items.Size() && i < 60; i++) {
        auto base = items.GetAt(i);
        if (auto item = base.try_as<muxc::MenuFlyoutItem>()) {
            if (item.Icon() && _wcsicmp(item.Text().c_str(), label) == 0) {
                return item.Icon();
            }
        } else if (auto sub = base.try_as<muxc::MenuFlyoutSubItem>()) {
            if (sub.Icon() && _wcsicmp(sub.Text().c_str(), label) == 0) {
                return sub.Icon();
            }
            if (depth < 2) {
                if (auto found = IconInMenuItems(sub.Items(), label, depth + 1)) {
                    return found;
                }
            }
        }
    }
    return nullptr;
}

mux::Controls::IconElement FindToolbarIcon(mux::DependencyObject const& node, const wchar_t* label,
                                           int depth, int& visited) {
    if (!node || depth > 40 || visited > 3000) {
        return nullptr;
    }
    visited++;
    if (auto button = node.try_as<muxc::AppBarButton>()) {
        if (button.Icon() && _wcsicmp(button.Label().c_str(), label) == 0) {
            return button.Icon();
        }
    }
    if (auto button = node.try_as<muxc::Button>()) {
        if (auto flyout = button.Flyout()) {
            if (auto menu = flyout.try_as<muxc::MenuFlyout>()) {
                if (auto found = IconInMenuItems(menu.Items(), label, 0)) {
                    return found;
                }
            }
        }
    }
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        if (auto found = FindToolbarIcon(mux::Media::VisualTreeHelper::GetChild(node, i), label,
                                         depth + 1, visited)) {
            return found;
        }
    }
    return nullptr;
}

// A copy of the icon Explorer's toolbar uses for a command, so the menu matches
// the toolbar. Null when the toolbar has no such command or the icon is of a kind
// that is not copied.
mux::UIElement ToolbarIcon(WindowState* state, const wchar_t* label, double size) {
    try {
        if (!state->xamlRoot) {
            return nullptr;
        }
        auto content = state->xamlRoot.Content();
        auto start = content ? content.try_as<mux::DependencyObject>() : nullptr;
        int visited = 0;
        auto icon = FindToolbarIcon(start, label, 0, visited);
        if (!icon) {
            return nullptr;
        }
        if (auto image = icon.try_as<muxc::ImageIcon>()) {
            muxc::ImageIcon copy;
            copy.Source(image.Source());
            copy.Width(size);
            copy.Height(size);
            return copy;
        }
        if (auto font = icon.try_as<muxc::FontIcon>()) {
            muxc::FontIcon copy;
            copy.FontFamily(font.FontFamily());
            copy.Glyph(font.Glyph());
            copy.FontSize(size);
            return copy;
        }
        Out(L"toolbar icon of \"%s\" is a kind that is not copied", label);
    } catch (...) {
        LogHresult(L"ToolbarIcon");
    }
    return nullptr;
}

// ---- The two flyouts, built from the same parts ----
//
// The item menu and the folder (background) menu are one look: a header row of
// quick actions with labels, then separated groups of rows with an icon, a name,
// a right-aligned shortcut hint and, for a submenu, an arrow. Everything that
// decides how it looks is in `MenuStyle`, so a change of spacing, icon size or
// colour is made in one place.

struct MenuStyle {
    double minWidth = 300;
    mux::Thickness rootMargin{-12, -11, -12, -13};  // Takes the flyout's own padding back.
    double rowIcon = 18;
    mux::Thickness rowPadding{5, 4, 5, 4};
    double iconColumn = 28;
    double arrowColumn = 18;
    double hintOpacity = 0.6;
    double hintGap = 24;
    double quickIcon = 20;
    double quickMinWidth = 56;
    mux::Thickness quickPadding{1, 3, 1, 3};
    double quickCaptionSize = 11;
    mux::Thickness separatorMargin{6, 4, 6, 4};
    winrt::Windows::UI::Color separatorColor{50, 128, 128, 128};
    double corner = 4;
};

// A quick action of the header row.
struct MenuQuick {
    std::wstring label;  // Under the icon.
    std::wstring tip;
    const wchar_t* glyph = L"";
    std::wstring icon;   // The official icon file, without its extension.
    bool enabled = true;
    std::function<void()> action;
};

// A row of the list.
struct MenuRowSpec {
    std::wstring text;
    const wchar_t* glyph = L"";
    std::wstring icon;
    std::wstring shortcut;
    bool enabled = true;
    std::function<void()> action;
    muxc::MenuFlyout submenu{nullptr};
};

struct MenuBuilder {
    WindowState* state;
    MenuStyle style;
    muxc::StackPanel root;
    bool anyRows = false;
    bool separatorWanted = false;
    bool toolbarIcons = true;  // Look for the toolbar's own icon of a label before drawing a glyph.

    MenuBuilder(WindowState* owner, double minWidth, bool toolbarFallback)
        : state(owner), toolbarIcons(toolbarFallback) {
        style.minWidth = minWidth;
        root.Orientation(muxc::Orientation::Vertical);
        root.MinWidth(style.minWidth);
        root.Margin(style.rootMargin);
        root.XYFocusKeyboardNavigation(mux::Input::XYFocusKeyboardNavigationMode::Enabled);
    }

    muxc::Button PlainButton() {
        muxc::Button button;
        button.Background(mux::Media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
        button.BorderThickness(mux::Thickness{0, 0, 0, 0});
        button.CornerRadius(mux::CornerRadius{style.corner, style.corner, style.corner, style.corner});
        return button;
    }

    // Runs `action` when the button is clicked, then closes the menu.
    void Clickable(muxc::Button const& button, std::function<void()> action, bool closeAfter) {
        auto token = button.Click([action, closeAfter](Inspectable const&, mux::RoutedEventArgs const&) {
            try {
                action();
                if (closeAfter) {
                    WindowState* open = StateForThisThread(false);
                    if (open && open->context && open->context->flyout) {
                        open->context->flyout.Hide();
                    }
                }
            } catch (...) {
                LogHresult(L"a flyout button");
            }
        });
        state->context->buttonHooks.emplace_back(button, token);
    }

    mux::UIElement IconFor(const std::wstring& file, const std::wstring& label, const wchar_t* glyph, double size,
                           bool enabled, bool glyphOnlyIfGiven) {
        mux::UIElement icon = OfficialIcon(file.c_str(), (int)size);
        if (!icon && toolbarIcons) {
            icon = ToolbarIcon(state, label.c_str(), (int)size);
        }
        if (!icon && (!glyphOnlyIfGiven || (glyph && glyph[0]))) {
            icon = MakeTwoToneIcon(state, glyph, (int)size, enabled);
        }
        return icon;
    }

    // A thin line between groups of rows. It is drawn only before a row that follows
    // other rows.
    void Separator() {
        muxc::Border line;
        line.Height(1);
        line.Margin(style.separatorMargin);
        line.Background(mux::Media::SolidColorBrush(style.separatorColor));
        root.Children().Append(line);
    }

    // The header row: icons with labels, as many as there are.
    void QuickRow(std::vector<MenuQuick> const& quick) {
        if (quick.empty()) {
            return;
        }
        muxc::Grid header;
        for (size_t q = 0; q < quick.size(); q++) {
            muxc::ColumnDefinition definition;
            definition.Width(mux::GridLength{1, mux::GridUnitType::Star});
            header.ColumnDefinitions().Append(definition);
            auto button = PlainButton();
            button.HorizontalAlignment(mux::HorizontalAlignment::Stretch);
            button.MinWidth(style.quickMinWidth);
            button.Padding(style.quickPadding);
            muxc::StackPanel stack;
            stack.Orientation(muxc::Orientation::Vertical);
            stack.HorizontalAlignment(mux::HorizontalAlignment::Center);
            mux::UIElement icon = IconFor(quick[q].icon, quick[q].label, quick[q].glyph, style.quickIcon,
                                          quick[q].enabled, false);
            icon.as<mux::FrameworkElement>().HorizontalAlignment(mux::HorizontalAlignment::Center);
            stack.Children().Append(icon);
            muxc::TextBlock caption;
            caption.Text(quick[q].label);
            caption.FontSize(style.quickCaptionSize);
            caption.Margin(mux::Thickness{0, 4, 0, 0});
            caption.TextTrimming(mux::TextTrimming::CharacterEllipsis);
            caption.HorizontalAlignment(mux::HorizontalAlignment::Center);
            stack.Children().Append(caption);
            button.Content(stack);
            button.IsEnabled(quick[q].enabled);
            if (!quick[q].tip.empty()) {
                muxc::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring{quick[q].tip}));
            }
            muxc::Grid::SetColumn(button, (int)q);
            header.Children().Append(button);
            if (quick[q].action) {
                Clickable(button, quick[q].action, true);
            }
        }
        root.Children().Append(header);
        anyRows = true;
    }

    // A list row: icon, name, shortcut and, for a submenu, an arrow.
    void Row(MenuRowSpec const& spec) {
        if (separatorWanted && anyRows) {
            Separator();
        }
        separatorWanted = false;
        auto button = PlainButton();
        button.HorizontalAlignment(mux::HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(mux::HorizontalAlignment::Stretch);
        button.Padding(style.rowPadding);
        button.IsEnabled(spec.enabled);
        muxc::Grid content;
        muxc::ColumnDefinition iconColumn, nameColumn, hintColumn, arrowColumn;
        iconColumn.Width(mux::GridLength{style.iconColumn, mux::GridUnitType::Pixel});
        nameColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        hintColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
        arrowColumn.Width(mux::GridLength{style.arrowColumn, mux::GridUnitType::Pixel});
        content.ColumnDefinitions().Append(iconColumn);
        content.ColumnDefinitions().Append(nameColumn);
        content.ColumnDefinitions().Append(hintColumn);
        content.ColumnDefinitions().Append(arrowColumn);
        if (mux::UIElement icon = IconFor(spec.icon, spec.text, spec.glyph, style.rowIcon, spec.enabled, toolbarIcons)) {
            muxc::Grid::SetColumn(icon.as<mux::FrameworkElement>(), 0);
            content.Children().Append(icon);
        }
        muxc::TextBlock label;
        label.Text(spec.text);
        label.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(label, 1);
        content.Children().Append(label);
        if (!spec.shortcut.empty()) {
            muxc::TextBlock hint;
            hint.Text(spec.shortcut);
            hint.Opacity(style.hintOpacity);
            hint.Margin(mux::Thickness{style.hintGap, 0, 0, 0});
            hint.VerticalAlignment(mux::VerticalAlignment::Center);
            muxc::Grid::SetColumn(hint, 2);
            content.Children().Append(hint);
        }
        if (spec.submenu) {
            muxc::FontIcon arrow;
            arrow.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            arrow.Glyph(L"");
            arrow.FontSize(10);
            arrow.Opacity(style.hintOpacity);
            arrow.HorizontalAlignment(mux::HorizontalAlignment::Right);
            arrow.VerticalAlignment(mux::VerticalAlignment::Center);
            muxc::Grid::SetColumn(arrow, 3);
            content.Children().Append(arrow);
            button.Flyout(spec.submenu);
            spec.submenu.Closed([](Inspectable const&, Inspectable const&) {
                // A choice made in the submenu closes the whole menu.
                WindowState* open = StateForThisThread(false);
                if (open && open->context && open->context->flyout) {
                    open->context->flyout.Hide();
                }
            });
        }
        button.Content(content);
        root.Children().Append(button);
        if (spec.action) {
            Clickable(button, spec.action, true);
        }
        anyRows = true;
    }

    // Puts the content in the flyout, hands the flyout to the state so it can be
    // closed, and shows it at `position` of `anchor`.
    void Show(muxc::Flyout const& flyout, mux::UIElement const& anchor, winrt::Windows::Foundation::Point position) {
        flyout.Content(root);
        state->context->flyout = flyout;
        state->context->closedToken = flyout.Closed([](Inspectable const&, Inspectable const&) {
            WindowState* open = StateForThisThread(false);
            if (!open || g_unloading || !open->queue) {
                return;
            }
            // After the click has been handled.
            try {
                open->queue.TryEnqueue([]() {
                    WindowState* now = StateForThisThread(false);
                    if (now && !g_unloading) {
                        CloseContextMenu(now);
                    }
                });
            } catch (...) {
            }
        });
        muxc::Primitives::FlyoutShowOptions options;
        options.Position(position);
        flyout.ShowAt(anchor, options);
    }
};

// The shell's context menu for one item, shown the way Explorer's own is: a row
// of quick actions with icons on top, the usual commands below, and the rest
// of the shell's entries behind "Show more options". The shell's entries are
// copied into XAML, since a native popup would run a modal loop inside a XAML
// handler, which crashed Explorer before.
void ShowItemFlyout(WindowState* state, Column* column, muxc::ListView const& list,
                    std::vector<size_t> const& picked,
                    winrt::Windows::Foundation::Point position) {
    try {
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
        bool insideArchive = ArchiveParentColumn(state, column) >= 0;
        state->context->insideArchive = insideArchive;

        // The shell's entries, with the verb of each.
        std::vector<MenuRow> rows;
        int count = GetMenuItemCount(hmenu);
        for (int i = 0; i < count && i < 80; i++) {
            MENUITEMINFOW info;
            std::wstring text = MenuText(hmenu, i, &info);
            if ((info.fType & MFT_SEPARATOR) || text.empty()) {
                continue;
            }
            MenuRow row;
            row.pos = i;
            row.id = info.wID;
            row.text = text;
            row.sub = info.hSubMenu;
            row.enabled = !(info.fState & (MFS_DISABLED | MFS_GRAYED));
            wchar_t verb[64] = L"";
            menu->GetCommandString(info.wID - state->context->idFirst, GCS_VERBW, nullptr,
                                   (char*)verb, ARRAYSIZE(verb));
            row.verb = verb;
            Out(L"menu entry %d: id=%u verb=\"%s\" text=\"%s\"%s", i, row.id, verb, text.c_str(),
                row.sub ? L" (submenu)" : L"");
            rows.push_back(std::move(row));
        }

        muxc::Flyout flyout;
        MenuBuilder builder(state, 300, true);
        auto& root = builder.root;
        std::set<int> used;  // Positions shown above "Show more options".

        auto clickable = [&](muxc::Button const& button, std::function<void()> action, bool closeAfter) {
            builder.Clickable(button, action, closeAfter);
        };
        auto plainButton = [&]() { return builder.PlainButton(); };

        // Top row: the quick actions as icons, with this mod's own after them.
        struct Quick {
            std::wstring label;  // Under the icon.
            std::wstring tip;
            const wchar_t* glyph;
            bool enabled;
            std::function<void()> action;
            std::wstring icon;  // The official icon file, without its extension.
        };
        std::vector<Quick> quick;
        const wchar_t* quickVerbs[] = {L"cut", L"copy", L"rename", L"share", L"delete"};
        for (int q = 0; q < 5; q++) {
            if (insideArchive && q != 1) {
                continue;
            }
            for (auto& row : rows) {
                bool match = q == 3 ? (StrStrIW(row.verb.c_str(), L"share") && !row.sub)
                                    : _wcsicmp(row.verb.c_str(), quickVerbs[q]) == 0;
                if (!match || used.count(row.pos)) {
                    continue;
                }
                used.insert(row.pos);
                UINT id = row.id;
                quick.push_back({row.text, row.text, GlyphForVerb(q == 3 ? L"share" : quickVerbs[q]),
                                 row.enabled, [id]() { RunContextCommand(id); }});
                quick.back().icon = OfficialIconName(row.verb);
                break;
            }
        }
        wchar_t folderPath[MAX_PATH] = L"";
        bool onDisk = !insideArchive && SHGetPathFromIDListW(column->folder.get(), folderPath) &&
                      folderPath[0];
        {
            auto addMod = [&](const wchar_t* text, const wchar_t* tip, const wchar_t* glyph,
                              const wchar_t* iconFile, std::function<void()> action) {
                quick.push_back({text, tip, glyph, true, [action]() {
                                     WindowState* now = StateForThisThread(false);
                                     if (now && !g_unloading) {
                                         action();
                                     }
                                 }});
                quick.back().icon = iconFile;
            };
            if (onDisk) {
                addMod(L"New folder", L"New folder with selection", L"", L"newfolder", [column, picked]() {
                    NewFolderWithSelection(StateForThisThread(false), column, picked);
                });
            }
            if (picked.size() == 1 && column->entries[picked[0]].folder) {
                addMod(L"New tab", L"Open in new tab", L"", L"windows.opennewtab", [column, picked]() {
                    auto& picked0 = column->entries[picked[0]];
                    Pidl target(picked0.linkTarget
                                    ? ILClone(picked0.linkTarget.get())
                                    : ILCombine(column->folder.get(), picked0.child.get()));
                    OpenFolderInNewTab(StateForThisThread(false), target.get());
                });
            }
        }
        bool& anyRows = builder.anyRows;
        {
            std::vector<MenuQuick> header;
            for (auto& q : quick) {
                MenuQuick item;
                item.label = q.label;
                item.tip = q.tip;
                item.glyph = q.glyph;
                item.icon = q.icon;
                item.enabled = q.enabled;
                item.action = q.action;
                header.push_back(std::move(item));
            }
            builder.QuickRow(header);
        }

        bool& separatorWanted = builder.separatorWanted;
        auto addRow = [&](const wchar_t* text, const wchar_t* glyph, const wchar_t* shortcut,
                          bool enabled, std::function<void()> action,
                          muxc::MenuFlyout const& submenu, const wchar_t* iconFile) {
            MenuRowSpec spec;
            spec.text = text;
            spec.glyph = glyph ? glyph : L"";
            spec.icon = iconFile ? iconFile : L"";
            spec.shortcut = shortcut ? shortcut : L"";
            spec.enabled = enabled;
            spec.action = std::move(action);
            spec.submenu = submenu;
            builder.Row(spec);
        };

        // Extracting from an archive.
        if (insideArchive) {
            addRow(L"Extract selected here", L"", L"", true,
                   [column, picked]() { ExtractSelected(StateForThisThread(false), column, picked); },
                   muxc::MenuFlyout{nullptr}, L"windows.compressedfile.extract");
        }
        if (onDisk && picked.size() == 1 && column->entries[picked[0]].archive) {
            addRow(L"Extract here", L"", L"", true,
                   [column, picked]() { ExtractHere(StateForThisThread(false), column, picked[0]); },
                   muxc::MenuFlyout{nullptr}, L"windows.compressedfile.extract");
        }

        // The usual commands, in Explorer's order.
        if (!insideArchive) {
            const wchar_t* mainVerbs[] = {L"open", L"openas", L"pintohome", L"copyaspath",
                                          L"setdesktopwallpaper", L"edit", L"properties"};
            separatorWanted = anyRows;
            for (auto verb : mainVerbs) {
                for (auto& row : rows) {
                    if (_wcsicmp(row.verb.c_str(), verb) != 0 || used.count(row.pos)) {
                        continue;
                    }
                    used.insert(row.pos);
                    UINT id = row.id;
                    muxc::MenuFlyout subMenu{nullptr};
                    if (row.sub) {
                        subMenu = muxc::MenuFlyout();
                        FillFlyoutItems(state, row.sub, subMenu.Items(), 1, nullptr);
                    }
                    addRow(row.text.c_str(), GlyphForVerb(row.verb), ShortcutForVerb(row.verb),
                           row.enabled, row.sub ? std::function<void()>() : [id]() { RunContextCommand(id); },
                           subMenu, OfficialIconName(row.verb));
                    break;
                }
            }

            // Everything else the shell offers.
            muxc::MenuFlyout more;
            FillFlyoutItems(state, hmenu, more.Items(), 0, &used);
            if (more.Items().Size() > 0) {
                separatorWanted = anyRows;
                addRow(L"Show more options", L"", L"", true, nullptr, more, L"");
            }
        }

        builder.Show(flyout, list, position);
        Out(L"context menu shown for %d item(s), first \"%s\": %d quick, %u rows",
            (int)picked.size(), column->entries[picked[0]].name.c_str(), (int)quick.size(),
            root.Children().Size());
    } catch (...) {
        LogHresult(L"ShowItemFlyout");
        CloseContextMenu(state);
    }
}

// Right-click on a column's empty space: the same look as the item menu, a row
// of quick actions on top and the rest listed below.
void ShowBackgroundMenu(WindowState* state, Column* column,
                        muxc::ListView const& list,
                        mux::Input::RightTappedRoutedEventArgs const& args) {
    CloseContextMenu(state);
    state->context = std::make_unique<ContextMenuState>();
    state->context->owner = state->topLevel;
    state->context->column = column;

    // The Desktop's properties are the Personalization page of Settings.
    bool desktop = ILIsEmpty(column->folder.get());
    if (!desktop) {
        PIDLIST_ABSOLUTE known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, nullptr, &known)) && known) {
            desktop = ILIsEqual(known, column->folder.get());
            CoTaskMemFree(known);
        }
    }
    // Inside an archive being browsed, writing and properties make no sense.
    bool inArchive = ArchiveParentColumn(state, column) >= 0;

    muxc::Flyout flyout;
    MenuBuilder builder(state, 260, false);
    auto& root = builder.root;

    auto plainButton = [&]() { return builder.PlainButton(); };
    auto onClick = [&](muxc::Button const& button, std::wstring action) {
        builder.Clickable(button, [action]() {
            WindowState* now = StateForThisThread(false);
            if (!now || !now->context || g_unloading) {
                return;
            }
            RunBackgroundAction(now, now->context->column, action);
        }, true);
    };

    // The quick actions.
    struct Quick {
        const wchar_t* action;
        const wchar_t* label;
        const wchar_t* glyph;
        const wchar_t* icon;
        bool enabled;
    };
    Quick quick[] = {
        {L"paste", L"Paste", L"", L"windows.paste", ClipboardHasFiles() && !inArchive},
        {L"newfolder", L"New folder", L"", L"newfolder", !inArchive},
        {L"refresh", L"Refresh", L"", L"windows.refresh", true},
    };
    {
        std::vector<MenuQuick> header;
        for (auto& q : quick) {
            MenuQuick item;
            item.label = q.label;
            item.glyph = q.glyph;
            item.icon = q.icon;
            item.enabled = q.enabled;
            std::wstring action = q.action;
            item.action = [action]() {
                WindowState* now = StateForThisThread(false);
                if (now && now->context && !g_unloading) {
                    RunBackgroundAction(now, now->context->column, action);
                }
            };
            header.push_back(std::move(item));
        }
        builder.QuickRow(header);
    }

    // The list: the folder's properties.
    {
        builder.separatorWanted = true;
        MenuRowSpec spec;
        spec.text = desktop ? L"Personalize" : L"Folder properties";
        spec.icon = desktop ? L"Personalize" : L"windows.properties";
        spec.enabled = !inArchive;
        spec.action = []() {
            WindowState* now = StateForThisThread(false);
            if (now && now->context && !g_unloading) {
                RunBackgroundAction(now, now->context->column, L"properties");
            }
        };
        builder.Row(spec);
    }

    builder.Show(flyout, list, args.GetPosition(list));
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

        ShowItemFlyout(state, column, list, picked, args.GetPosition(list));
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

// One group header in its state: normal, under the pointer, or the one the
// keyboard is on, which is tinted a little stronger.
void PaintHeader(muxc::ListViewItem const& header, bool hover, bool focused) {
    header.Background(focused ? Brush(96, 150, 150, 150)
                              : hover ? Brush(64, 150, 150, 150) : Brush(0, 0, 0, 0));
    header.Opacity(focused || hover ? 1.0 : 0.92);
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
                PaintHeader(header, on, r == column->focusHeader);
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

// The keyboard on a group header: it is tinted, and Left and Right collapse and
// open it. Headers cannot take real keyboard focus, so the mod keeps this
// itself.
void SetHeaderFocus(Column* column, muxc::ListView const& list, int row) {
    int old = column->focusHeader;
    column->focusHeader = row;
    auto items = list.Items();
    auto paint = [&](int r, bool on) {
        if (r >= 0 && r < (int)items.Size()) {
            if (auto header = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
                PaintHeader(header, !on && r == column->hoverHeader, on);
            }
        }
    };
    paint(old, false);
    paint(row, true);
    if (row >= 0 && row < (int)items.Size()) {
        list.ScrollIntoView(items.GetAt((uint32_t)row));
    }
}

// The next row in a direction that is a group header or a visible item.
int NextHeaderOrItemRow(Column* column, muxc::ListView const& list, int from, int step) {
    auto items = list.Items();
    for (int r = from + step; r >= 0 && r < (int)column->rowToEntry.size() && r < (int)items.Size();
         r += step) {
        bool header = column->rowToEntry[r] < 0 && column->rowGroup[r] >= 0;
        bool item = column->rowToEntry[r] >= 0;
        if (!header && !item) {
            continue;  // The "more items" row.
        }
        auto row = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>();
        if (row && row.Visibility() == mux::Visibility::Visible) {
            return r;
        }
    }
    return -1;
}

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
        if (OnDragMoved(state, column, list, args)) {
            return;  // Rows are being dragged.
        }
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

    // The tooltip is never wider than the column of the row it describes.
    double columnWidth = column->list ? column->list.ActualWidth() : 0;
    if (columnWidth < 160) {
        columnWidth = 160;
    }
    const double padding = 24;  // The tooltip's own padding and border.
    double room = columnWidth - padding;
    muxc::StackPanel panel;
    panel.MaxWidth(room);
    muxc::TextBlock name;
    name.Text(entry.name);
    name.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    name.TextWrapping(mux::TextWrapping::Wrap);
    name.MaxWidth(std::min(323.0, room));
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
            value.MaxWidth(std::max(60.0, std::min(221.0, room - 96)));
            muxc::Grid::SetRow(value, (int)i);
            muxc::Grid::SetColumn(value, 1);
            grid.Children().Append(label);
            grid.Children().Append(value);
        }
        panel.Children().Append(grid);
    }
    muxc::ToolTip tip;
    tip.MaxWidth(columnWidth);
    tip.Content(panel);
    return tip;
}

// ---- Selection by a rectangle ----

muxc::ScrollViewer FindListScroller(mux::DependencyObject const& node, int depth) {
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

// The list scrolled: the rows that came near the screen get their content.
void OnListViewChanged(Inspectable const& sender) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        auto viewer = sender.try_as<muxc::ScrollViewer>();
        for (auto& c : state->columns) {
            if (viewer && c->viewer && c->viewer == viewer && c->filled) {
                BuildVisibleRows(c.get());
                return;
            }
        }
    } catch (...) {
        LogHresult(L"OnListViewChanged");
    }
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
        // The middle button on a folder opens it in a new tab.
        if (point.Properties().IsMiddleButtonPressed()) {
            mux::DependencyObject node =
                args.OriginalSource().try_as<mux::DependencyObject>();
            for (int i = 0; node && i < 20; i++) {
                if (auto row = node.try_as<muxc::ListViewItem>()) {
                    int at = ColumnIndexOfList(state, list);
                    int rowIndex = list.IndexFromContainer(row);
                    if (at >= 0 && rowIndex >= 0) {
                        Column* column = state->columns[at].get();
                        int entry = rowIndex < (int)column->rowToEntry.size()
                                        ? column->rowToEntry[rowIndex]
                                        : -1;
                        if (entry >= 0 && column->entries[entry].folder) {
                            auto& hit = column->entries[entry];
                            Pidl target(hit.linkTarget
                                            ? ILClone(hit.linkTarget.get())
                                            : ILCombine(column->folder.get(), hit.child.get()));
                            OpenFolderInNewTab(state, target.get());
                            args.Handled(true);
                        }
                    }
                    return;
                }
                node = mux::Media::VisualTreeHelper::GetParent(node);
            }
            return;
        }
        if (!point.Properties().IsLeftButtonPressed()) {
            return;
        }
        // On a row, the list selects as usual. Empty space, the gutter and
        // group headers start a rectangle.
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        for (int i = 0; node && i < 20; i++) {
            if (auto row = node.try_as<muxc::ListViewItem>()) {
                OnDragPressed(state, list, row, args);
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
        {
            // For the log: what was pressed, and where, so a strip that doesn't
            // start a rectangle can be traced.
            static int logged = 0;
            if (logged++ < 30) {
                auto at = point.Position();
                std::wstring source = L"?";
                try {
                    source = winrt::get_class_name(args.OriginalSource()).c_str();
                } catch (...) {
                }
                Out(L"press on empty space of column %d: x=%.0f of %.0f, y=%.0f, source=%s",
                    index + 1, (double)at.X, list.ActualWidth(), (double)at.Y,
                    source.c_str());
            }
        }
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
        if (OnDragReleased(state)) {
            return;
        }
        if (state->band.pending || state->band.active) {
            EndBand(state, state->band.active);
        }
    } catch (...) {
        state->band = {};
        state->drag = DragState();
        LogHresult(L"OnBandReleased");
    }
}

// A double-click on the empty space of a column goes up one folder: the column
// before it becomes the active one, or, in the first column, Explorer is taken
// to the parent folder, which is then added in front of the columns.
void OnEmptyDoubleTapped(muxc::ListView const& list,
                         mux::Input::DoubleTappedRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading || !g_doubleClickUp) {
        return;
    }
    try {
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        Column* column = state->columns[index].get();
        // A group header takes the double-click for itself.
        if (HeaderRowAt(column, list, args.GetPosition(list)) >= 0) {
            return;
        }
        // Up from the active column, whichever column was double-clicked.
        int from = state->active >= 0 && state->active < (int)state->columns.size()
                       ? state->active
                       : index;
        if (from > 0) {
            Column* previous = state->columns[from - 1].get();
            LiftStale(state, from - 1);
            SetActiveColumn(state, from - 1, false);
            ScheduleNavigation(state, previous->folder.get());
            state->focusColumn = previous;
            state->focusUntil = GetTickCount64() + 1500;
            previous->list.Focus(mux::FocusState::Pointer);
            // The active column goes to the middle of the view.
            CenterColumn(state, from - 1);
            state->cursorFixUntil = GetTickCount64() + 2500;
            Out(L"double-click on empty space: back to column %d", from);
            return;
        }
        column = state->columns[0].get();
        if (ILIsEmpty(column->folder.get())) {
            return;  // The top of the namespace.
        }
        Pidl parent(ILClone(column->folder.get()));
        ILRemoveLastID(parent.get());
        if (!parent || IsInControlPanel(parent.get())) {
            return;
        }
        NavigateExplorer(state, parent.get());
        Out(L"double-click on empty space: up from the first column");
    } catch (...) {
        LogHresult(L"OnEmptyDoubleTapped");
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
        if (key != VirtualKey::Control && key != VirtualKey::Shift && key != VirtualKey::Menu &&
            key != VirtualKey::LeftControl && key != VirtualKey::RightControl &&
            key != VirtualKey::LeftShift && key != VirtualKey::RightShift &&
            key != VirtualKey::LeftMenu && key != VirtualKey::RightMenu) {
            HoverOff(state);
        }
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
        // The keyboard on a group header, or on its way there. A setting, off by
        // default. Turned off while a header has it, the header is let go.
        if (!g_headerKeyboard && column->focusHeader >= 0) {
            SetHeaderFocus(column, list, -1);
        }
        if (g_headerKeyboard) {
            int header = column->focusHeader;
            if (header >= 0 && header < (int)column->rowGroup.size()) {
                int group = column->rowGroup[header];
                bool closed = group >= 0 && group < (int)column->groupTitles.size() &&
                              std::find(column->collapsed.begin(), column->collapsed.end(),
                                        column->groupTitles[group]) != column->collapsed.end();
                auto goTo = [&](int target) {
                    if (target < 0) {
                        return;
                    }
                    if (column->rowToEntry[target] < 0) {
                        SetHeaderFocus(column, list, target);
                    } else {
                        SetHeaderFocus(column, list, -1);
                        list.SelectedIndex(target);
                        list.ScrollIntoView(list.Items().GetAt((uint32_t)target));
                    }
                };
                if (key == VirtualKey::Left && !closed) {
                    ToggleGroup(state, column, header);
                    keep();
                    return;
                }
                if (key == VirtualKey::Right) {
                    if (closed) {
                        ToggleGroup(state, column, header);
                    } else {
                        goTo(NextHeaderOrItemRow(column, list, header, 1));
                    }
                    keep();
                    return;
                }
                if (key == VirtualKey::Enter || key == VirtualKey::Space) {
                    ToggleGroup(state, column, header);
                    keep();
                    return;
                }
                if (key == VirtualKey::Down || key == VirtualKey::Up) {
                    goTo(NextHeaderOrItemRow(column, list, header,
                                             key == VirtualKey::Down ? 1 : -1));
                    keep();
                    return;
                }
                // Any other key leaves the header and does its usual job.
                SetHeaderFocus(column, list, -1);
            } else if (key == VirtualKey::Up && !ctrl && !alt &&
                       !(GetKeyState(VK_SHIFT) & 0x8000) && picked.size() == 1 && row > 0 &&
                       column->rowToEntry[row - 1] < 0 && column->rowGroup[row - 1] >= 0) {
                // Up from the first item of a group lands on its header.
                bool wasBuilding = state->building;
                state->building = true;
                list.SelectedItems().Clear();
                state->building = wasBuilding;
                SetHeaderFocus(column, list, row - 1);
                keep();
                return;
            }
        }
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // Everyday keys.
        if (key == VirtualKey::Escape && state->drag.active) {
            CancelDrag(state);
            args.Handled(true);
            return;
        }
        if (key == VirtualKey::F5 && !ctrl && !alt) {
            ReloadAll(state);
            Out(L"F5: the columns are read again");
            keep();
            return;
        }
        if (key == VirtualKey::Escape && !ctrl && !alt && !picked.empty()) {
            list.SelectedItems().Clear();
            keep();
            return;
        }
        if (key == VirtualKey::Back && !ctrl && !alt && !shift) {
            // Up one column; in the first one, Explorer goes to the parent folder.
            if (index > 0) {
                SetActiveColumn(state, index - 1);
                state->columns[index - 1]->list.Focus(mux::FocusState::Keyboard);
                state->focusColumn = state->columns[index - 1].get();
                state->focusUntil = GetTickCount64() + 1500;
                args.Handled(true);
            } else if (!ILIsEmpty(column->folder.get())) {
                Pidl parent(ILClone(column->folder.get()));
                ILRemoveLastID(parent.get());
                if (parent && !IsInControlPanel(parent.get())) {
                    NavigateExplorer(state, parent.get());
                }
                args.Handled(true);
            }
            return;
        }
        if (((key == VirtualKey::Application) || (key == VirtualKey::F10 && shift)) &&
            !picked.empty()) {
            // The flyout of the selected item, below its row.
            auto selectedRow = list.Items().GetAt((uint32_t)row).try_as<muxc::ListViewItem>();
            winrt::Windows::Foundation::Point where{40, 40};
            if (selectedRow) {
                where = selectedRow.TransformToVisual(list).TransformPoint(
                    winrt::Windows::Foundation::Point{40, (float)selectedRow.ActualHeight()});
            }
            ShowItemFlyout(state, column, list, picked, where);
            args.Handled(true);
            return;
        }
        if (ctrl && shift && !alt && key == VirtualKey::N) {
            CreateNewFolder(state, column);
            keep();
            return;
        }
        if (ctrl && alt && !shift && key == VirtualKey::N && !picked.empty()) {
            NewFolderWithSelection(state, column, picked);
            keep();
            return;
        }
        if (ctrl && shift && !alt && key == VirtualKey::C) {
            if (picked.empty()) {
                // Nothing selected: the column's own folder.
                wchar_t folder[MAX_PATH] = L"";
                if (SHGetPathFromIDListW(column->folder.get(), folder) && folder[0]) {
                    std::wstring text = std::wstring(L"\"") + folder + L"\"";
                    if (OpenClipboard(state->topLevel)) {
                        EmptyClipboard();
                        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
                        if (memory) {
                            if (wchar_t* target = (wchar_t*)GlobalLock(memory)) {
                                memcpy(target, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
                                GlobalUnlock(memory);
                                if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                                    GlobalFree(memory);
                                }
                            } else {
                                GlobalFree(memory);
                            }
                        }
                        CloseClipboard();
                        Out(L"copied the column's folder path as text");
                    }
                }
            } else {
                CopyAsPath(state, column, picked);
            }
            keep();
            return;
        }
        if (ctrl && !alt && (key == VirtualKey::Y || (shift && key == VirtualKey::Z)) &&
            RedoLast(state)) {
            ReloadAll(state);
            keep();
            return;
        }
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
                auto& into = column->entries[picked[0]];
                target.reset(into.linkTarget
                                 ? ILClone(into.linkTarget.get())
                                 : ILCombine(column->folder.get(), into.child.get()));
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
        if (key == VirtualKey::F2 && !picked.empty()) {
            BeginMultiRename(state, column, picked);
            args.Handled(true);
            return;
        }
        if (alt && key == VirtualKey::Enter && !picked.empty()) {
            InvokeVerbOnItems(state, column, picked, "properties");
            args.Handled(true);
            return;
        }
        if (ctrl && !alt && !(shift && key == VirtualKey::Z) &&
            (key == VirtualKey::Z || key == VirtualKey::Y)) {
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

void ChooseColumns(WindowState* state);
void LeaveColumns(WindowState* state);
void RegisterPanelDropTarget(WindowState* state);
void RevokePanelDropTarget(WindowState* state);
void DragAutoScroll(WindowState* state);
void HoverTick(WindowState* state);
void SpringLoadTick(WindowState* state);
void PillHide(Column* column);
void UpdateLastEdge(WindowState* state);
void SyncViewButtonIcon(WindowState* state);

void HidePanel(WindowState* state, const wchar_t* why) {
    if (!state->source) {
        state->on = false;
        return;
    }
    Out(L"panel off: %s", why);
    state->on = false;
    RestoreViewButtonIcon(state);
    HoverOn(state);
    g_panelWindow = nullptr;
    RevokePanelDropTarget(state);
    RestoreNativeList(state);
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
    ReleaseWorker(state->worker);
    state->worker = nullptr;
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
    state->columnsTabs.clear();
    state->seenTabs.clear();
    state->tabSnapshots.clear();
    state->currentTab = nullptr;
    state->softHidden = false;
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
void CloseSettingsWindowOfThread();

void AbandonWindow(WindowState* state) {
    Out(L"window closed with the panel on, letting its objects go");
    CloseSettingsWindowOfThread();
    state->on = false;
    (void)state->context.release();  // Never destroyed, like the columns.
    (void)state->rename.release();
    g_renaming = false;
    g_panelWindow = nullptr;
    try {
        if (state->timer) {
            state->timer.Stop();
            state->timer.Tick(state->tickToken);
        }
    } catch (...) {
    }
    ReleaseWorker(state->worker);
    state->worker = nullptr;
    for (auto& column : state->columns) {
        if (column->notifyId) {
            SHChangeNotifyDeregister(column->notifyId);
            column->notifyId = 0;
        }
        if (column->cancel) {
            *column->cancel = true;
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
// Explorer moved to a column's folder on its own (the toolbar's Up, Back). That
// column's selected item is selected in Explorer's view too, so the toolbar and
// the panes still act on what the column shows as selected.
void MirrorColumnSelection(WindowState* state, Column* column) {
    if (column && column->selectedChild) {
        state->selectFolder.reset(ILClone(column->folder.get()));
        state->selectChild.reset(ILClone(column->selectedChild.get()));
        state->selectExtra.clear();
        state->selectTries = 0;
    }
}

void SyncToFolder(WindowState* state, Pidl const& current) {
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();

    // Already a column: only the active column moves.
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (ILIsEqual(current.get(), state->columns[i]->folder.get())) {
            Out(L"Explorer moved to column %d", (int)i + 1);
            LiftStale(state, (int)i);
            // One scroll only, to the middle: a second one started by
            // SetActiveColumn would cut the first short and make it stutter.
            SetActiveColumn(state, (int)i, false);
            MirrorColumnSelection(state, state->columns[i].get());
            // Up and Back in the toolbar: the column Explorer went to is put
            // in the middle of the view, like a double-click up does.
            CenterColumn(state, (int)i);
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
            MirrorColumnSelection(state, state->columns[0].get());
            try {
                state->scroller.ChangeView(0.0, nullptr, nullptr);
            } catch (...) {
            }
            return;
        }
    }
    if (RememberedColumns(PathOf(current.get())) == 0) {
        Out(L"Explorer went to a folder that was left in its own layout, Columns off for the tab");
        LeaveColumns(state);
        return;
    }
    Out(L"Explorer navigated elsewhere (\"%s\"), rebuilding",
        PathOf(current.get()).c_str());
    RebuildFromExplorer(state);
    SetActiveColumn(state, 0);
}

// What Explorer's own view shows changed.
void OnExplorerFolderChanged(WindowState* state, Pidl const& current) {
    state->lastSeen.reset(ILClone(current.get()));
    state->specSyncDue = GetTickCount64() + 700;
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
            if (RememberedColumns(PathOf(current.get())) == 0) {
                Out(L"this folder was left in Explorer's own layout, Columns off for the tab");
                LeaveColumns(state);
                return;
            }
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
    SyncToFolder(state, current);
}

bool LastLayoutWasColumns();

bool TabListHas(std::vector<HWND> const& tabs, HWND tab) {
    return std::find(tabs.begin(), tabs.end(), tab) != tabs.end();
}

void TabListRemove(std::vector<HWND>& tabs, HWND tab) {
    tabs.erase(std::remove(tabs.begin(), tabs.end(), tab), tabs.end());
}

// Tabs that were closed are forgotten.
void PruneTabs(WindowState* state) {
    for (auto* list : {&state->columnsTabs, &state->seenTabs}) {
        for (size_t i = 0; i < list->size();) {
            if (!IsWindow((*list)[i])) {
                list->erase(list->begin() + i);
            } else {
                i++;
            }
        }
    }
}

// The panel is one per window. It is hidden, not torn down, while a tab that
// is not in Columns is showing.
void SoftHidePanel(WindowState* state) {
    if (!state->bridge || state->softHidden) {
        return;
    }
    CancelRename(state);
    CloseContextMenu(state);
    RestoreNativeList(state);
    try {
        state->bridge.Hide();
        if (state->menuItem) {
            state->menuItem.IsChecked(false);
        }
    } catch (...) {
        LogHresult(L"SoftHidePanel");
    }
    state->softHidden = true;
    g_panelWindow = nullptr;
    {
        IShellBrowser* browser = BrowserOf(state->topLevel);
        Pidl where = browser ? CurrentFolder(browser) : Pidl();
        state->hiddenFolder = where ? PathOf(where.get()) : std::wstring();
    }
    Out(L"panel hidden: this tab is not in Columns");
}

void SoftShowPanel(WindowState* state) {
    if (!state->bridge || !state->softHidden) {
        return;
    }
    try {
        state->bridge.Show();
        HWND bridgeWindow = (HWND)state->bridge.WindowId().Value;
        SetWindowPos(bridgeWindow, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        g_panelWindow = bridgeWindow;
        if (state->menuItem) {
            state->menuItem.IsChecked(true);
        }
    } catch (...) {
        LogHresult(L"SoftShowPanel");
    }
    state->softHidden = false;
    state->lastRect = {};  // The tick places it again.
    Out(L"panel shown again: this tab is in Columns");
}

// The columns of the tab that is leaving are kept, so they come back with it.
void SaveTabSnapshot(WindowState* state, HWND tab) {
    // Tabs that were closed are forgotten.
    for (size_t i = 0; i < state->tabSnapshots.size();) {
        if (!IsWindow(state->tabSnapshots[i].tab) || state->tabSnapshots[i].tab == tab) {
            state->tabSnapshots.erase(state->tabSnapshots.begin() + i);
        } else {
            i++;
        }
    }
    if (!tab || !IsWindow(tab) || state->columns.empty()) {
        return;
    }
    TabSnapshot snapshot;
    snapshot.tab = tab;
    snapshot.active = state->active;
    for (auto& column : state->columns) {
        TabColumnSnapshot saved;
        saved.folder.reset(ILClone(column->folder.get()));
        if (column->selectedChild) {
            saved.selected.reset(ILClone(column->selectedChild.get()));
        }
        saved.width = column->list ? column->list.Width() : 0;
        saved.userSized = column->userSized;
        {
            auto scroller = column->list ? FindListScroller(column->list, 0) : nullptr;
            saved.scroll = scroller ? scroller.VerticalOffset() : 0;
        }
        snapshot.columns.push_back(std::move(saved));
    }
    Out(L"tab left: %d column(s) kept", (int)snapshot.columns.size());
    state->tabSnapshots.push_back(std::move(snapshot));
}

// Puts the columns of a tab back, when Explorer's view of that tab is still
// on the folder its active column showed. Returns false if there is nothing
// to put back, so the caller builds the columns from Explorer's folder.
bool RestoreTabSnapshot(WindowState* state, HWND tab, IShellBrowser* browser,
                        Pidl const& current) {
    for (size_t i = 0; i < state->tabSnapshots.size(); i++) {
        if (state->tabSnapshots[i].tab != tab) {
            continue;
        }
        TabSnapshot snapshot = std::move(state->tabSnapshots[i]);
        state->tabSnapshots.erase(state->tabSnapshots.begin() + i);
        if (snapshot.columns.empty() || snapshot.active < 0 ||
            snapshot.active >= (int)snapshot.columns.size() ||
            !snapshot.columns[0].folder || FolderGone(snapshot.columns[0].folder.get())) {
            return false;
        }
        auto& activeFolder = snapshot.columns[snapshot.active].folder;
        if (!ILIsEqual(activeFolder.get(), current.get()) &&
            _wcsicmp(PathOf(activeFolder.get()).c_str(), PathOf(current.get()).c_str()) != 0) {
            Out(L"tab shown again, but its folder changed: the columns start over");
            return false;
        }
        state->lastSeen.reset(ILClone(current.get()));
        state->expected.reset();
        state->pendingNav.reset();
        state->selectFolder.reset();
        state->selectChild.reset();
        {
            SortSpec fresh;
            if (ReadSortSpec(browser, &fresh)) {
                state->spec = fresh;
            }
        }
        if (state->swapOld) {
            CompleteSwap(state);
        }
        state->building = true;
        RemoveColumnsFrom(state, 0);
        state->building = false;
        for (auto& saved : snapshot.columns) {
            AddColumn(state, std::move(saved.folder));
            Column* column = state->columns.back().get();
            if (saved.selected) {
                column->selectedChild = std::move(saved.selected);
            }
            column->restoreScroll = saved.scroll;
            if (saved.userSized && saved.width > 0 && column->list) {
                column->list.Width(saved.width);
                column->userSized = true;
            }
        }
        SetActiveColumn(state, snapshot.active);
        Out(L"tab shown again: %d column(s) put back", (int)state->columns.size());
        return true;
    }
    return false;
}

// Another tab came to the front. Returns true when the tick has nothing more to
// do for this round.
bool HandleTabSwitch(WindowState* state, HWND tab, HWND fileList) {
    HWND old = state->currentTab;
    state->currentTab = tab;
    // What was pending belongs to the tab that left: a navigation after a
    // pause, a selection to mirror, a folder Explorer was asked to open.
    state->pendingNav.reset();
    state->expected.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    state->selectExtra.clear();
    state->focusColumn = nullptr;
    PruneTabs(state);
    bool known = TabListHas(state->seenTabs, tab);
    if (!known) {
        state->seenTabs.push_back(tab);
        // A tab seen for the first time takes the layout chosen last.
        if (g_restore && LastLayoutWasColumns()) {
            state->columnsTabs.push_back(tab);
        }
    }
    bool newIsColumns = TabListHas(state->columnsTabs, tab);
    Out(L"tab switch: %p to %p (%s)", old, tab, newIsColumns ? L"columns" : L"not columns");
    if (old && TabListHas(state->columnsTabs, old) && !state->softHidden) {
        SaveTabSnapshot(state, old);
    }
    if (!newIsColumns) {
        SoftHidePanel(state);
        return true;
    }
    bool wasHidden = state->softHidden;
    if (wasHidden) {
        SoftShowPanel(state);
    }
    IShellBrowser* browser = BrowserOf(state->topLevel);
    Pidl shown = browser ? CurrentFolder(browser) : Pidl();
    if (shown && RestoreTabSnapshot(state, tab, browser, shown)) {
        state->lastFileList = fileList;
        return true;
    }
    // No columns kept for this tab: start from the folder it shows. The old
    // columns are cleared first, since a rebuild would keep them on screen
    // until the new ones are in, and they belong to another tab.
    state->building = true;
    RemoveColumnsFrom(state, 0);
    state->building = false;
    RebuildFromExplorer(state);
    SetActiveColumn(state, 0);
    state->lastFileList = fileList;
    return true;
}

// A toolbar command that changes the selection of Explorer's own view, Select
// all, Select none, Invert selection or a Paste that selects what it made, acts
// on the view under the panel. Some time after a command, the active column
// takes that selection when Explorer shows the column's folder and nothing else
// is changing the selection.
void SyncSelectionFromExplorer(WindowState* state) {
    if (!state->on || state->active < 0 || state->active >= (int)state->columns.size()) {
        return;
    }
    Column* column = state->columns[state->active].get();
    if (!column->list || !column->filled || state->selectChild || state->pendingNav || state->rename ||
        state->expected || GetTickCount64() - column->selectionChangedAt < 1000) {
        return;
    }
    IShellBrowser* browser = BrowserOf(state->topLevel);
    if (!browser) {
        return;
    }
    Pidl current = CurrentFolder(browser);
    if (!current || !ILIsEqual(current.get(), column->folder.get())) {
        return;
    }
    auto selection = ExplorerSelection(browser);
    std::vector<size_t> wanted;
    if (!column->entries.empty() && selection.size() == column->entries.size()) {
        for (size_t i = 0; i < column->entries.size(); i++) {
            wanted.push_back(i);
        }
    } else if (!selection.empty()) {
        if (selection.size() * column->entries.size() > 4000000) {
            return;  // Too much to match here.
        }
        for (auto& child : selection) {
            for (size_t i = 0; i < column->entries.size(); i++) {
                if (ILIsEqual(column->entries[i].child.get(), child.get())) {
                    wanted.push_back(i);
                    break;
                }
            }
        }
        std::sort(wanted.begin(), wanted.end());
    }
    auto shown = SelectedEntries(column, column->list);
    std::sort(shown.begin(), shown.end());
    if (shown == wanted) {
        return;
    }
    try {
        auto selected = column->list.SelectedItems();
        auto items = column->list.Items();
        state->building = true;
        selected.Clear();
        for (size_t index : wanted) {
            int row = column->entryToRow[index];
            if (row >= 0 && (uint32_t)row < items.Size()) {
                selected.Append(items.GetAt((uint32_t)row));
            }
        }
        state->building = false;
        Out(L"the toolbar changed Explorer's selection: %d items now selected in column %d", (int)wanted.size(),
            state->active + 1);
        OnRowSelected(column->list);
    } catch (...) {
        state->building = false;
        LogHresult(L"taking Explorer's selection after a toolbar command");
    }
}

// "Open in a new tab": once the new tab exists, shows the folder in it. True
// when there is nothing left to wait for.
bool TryShowNewTab(WindowState* state) {
    if (!state->openTabFolder) {
        return true;
    }
    if (GetTickCount64() >= state->openTabUntil) {
        Out(L"the new tab did not open in time");
        state->openTabFolder.reset();
        state->newTab = nullptr;
        return true;
    }
    if (state->newTab && IsWindow(state->newTab)) {
        DWORD_PTR result = 0;
        if (SendMessageTimeoutW(state->newTab, WM_USER + 7, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500,
                                &result) &&
            result) {
            IShellBrowser* tabBrowser = (IShellBrowser*)result;
            g_ourBrowse = true;
            HRESULT hr = tabBrowser->BrowseObject(state->openTabFolder.get(),
                                                  SBSP_ABSOLUTE | SBSP_SAMEBROWSER | SBSP_ACTIVATE_NOFOCUS);
            g_ourBrowse = false;
            Out(L"the new tab shows its folder: hr=0x%08lX", hr);
            if (SUCCEEDED(hr)) {
                state->openTabFolder.reset();
                state->newTab = nullptr;
                return true;
            }
        }
    }
    return false;
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
    DragAutoScroll(state);
    try {
        if (!IsWindow(state->topLevel)) {
            AbandonWindow(state);
            return;
        }
        state->ticks++;
        ULONGLONG now = GetTickCount64();
        HoverTick(state);
        SpringLoadTick(state);
        SyncViewButtonIcon(state);
        UpdateLastEdge(state);

        HWND fileList = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");

        // Columns per tab: another tab came to the front.
        HWND tab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
        if (tab && tab != state->currentTab && HandleTabSwitch(state, tab, fileList)) {
            return;
        }
        if (state->softHidden) {
            // This tab is not in Columns. Only watch where it goes: a folder that
            // was last shown in Columns turns Columns on again.
            if (state->ticks % 4 == 0) {
                IShellBrowser* hiddenBrowser = BrowserOf(state->topLevel);
                Pidl where = hiddenBrowser ? CurrentFolder(hiddenBrowser) : Pidl();
                if (where) {
                    std::wstring path = PathOf(where.get());
                    if (_wcsicmp(path.c_str(), state->hiddenFolder.c_str()) != 0) {
                        state->hiddenFolder = path;
                        if (RememberedColumns(path) == 1) {
                            Out(L"this folder was shown in Columns last time, Columns on for the tab");
                            ChooseColumns(state);
                        }
                    }
                }
            }
            return;
        }

        // A tab made after the panel is a window above it, and covers it: the
        // panel would show but get no mouse. Keep it on top.
        {
            HWND panelWindow = (HWND)state->bridge.WindowId().Value;
            if (panelWindow && GetWindow(panelWindow, GW_HWNDPREV)) {
                SetWindowPos(panelWindow, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                static int logged = 0;
                if (logged++ < 8) {
                    Out(L"the panel was covered by a sibling window, raised again");
                }
            }
        }

        // Keep the panel on the file list. Plain Win32, cheap.
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

        // Explorer shows its busy cursor while it navigates, and a pointer that
        // stands still over the panel keeps it. Put the arrow back.
        if (state->cursorFixUntil && now < state->cursorFixUntil) {
            CURSORINFO info = {sizeof(info)};
            if (GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) &&
                (info.hCursor == LoadCursorW(nullptr, IDC_APPSTARTING) ||
                 info.hCursor == LoadCursorW(nullptr, IDC_WAIT))) {
                HWND under = WindowFromPoint(info.ptScreenPos);
                if (under && GetAncestor(under, GA_ROOT) == state->topLevel) {
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                }
            }
        } else {
            state->cursorFixUntil = 0;
        }

        // The resize handle appears once the pointer has rested on an edge. It
        // goes away when the pointer is no longer on that edge, also when the
        // columns moved under a pointer that stood still, which sends no event.
        {
            HWND bridgeWindow = state->bridge ? (HWND)state->bridge.WindowId().Value : nullptr;
            POINT cursor = {};
            bool haveCursor = bridgeWindow && GetCursorPos(&cursor) &&
                              ScreenToClient(bridgeWindow, &cursor);
            double scale = 1.0;
            try {
                if (state->rootGrid && state->rootGrid.XamlRoot()) {
                    scale = state->rootGrid.XamlRoot().RasterizationScale();
                }
            } catch (...) {
            }
            for (auto& column : state->columns) {
                if (!column->pill || !column->grip) {
                    continue;
                }
                bool shown = column->pill.Opacity() > 0 || column->hoverSince != 0;
                if (!shown || state->dragColumn >= 0) {
                    continue;
                }
                bool over = false;
                if (haveCursor && scale > 0) {
                    try {
                        auto box = column->grip.TransformToVisual(state->rootGrid)
                                       .TransformBounds(winrt::Windows::Foundation::Rect{
                                           0, 0, (float)column->grip.ActualWidth(),
                                           (float)column->grip.ActualHeight()});
                        double x = cursor.x / scale, y = cursor.y / scale;
                        over = x >= box.X && x <= box.X + box.Width && y >= box.Y &&
                               y <= box.Y + box.Height;
                    } catch (...) {
                    }
                }
                if (!over) {
                    PillHide(column.get());  // The pointer left without the event.
                    column->hoverSince = 0;
                }
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
            Column* activeColumn = state->active >= 0 && state->active < (int)state->columns.size()
                                       ? state->columns[state->active].get()
                                       : nullptr;
            SortSpec const& effective =
                activeColumn && activeColumn->hasSpec ? activeColumn->spec : state->spec;
            if (browser && ReadSortSpec(browser, &after) &&
                !SameSpec(after, state->specBeforeMenu) && !SameSpec(after, effective)) {
                Out(L"sort or group changed in the Sort menu: %d sort columns, grouped=%d",
                    (int)after.columns.size(), after.grouped ? 1 : 0);
                if (activeColumn && g_rememberSort) {
                    // Only the column the user is in changes, and it is remembered.
                    activeColumn->spec = after;
                    activeColumn->hasSpec = true;
                    RememberFolderSpec(PathOf(activeColumn->folder.get()), after);
                    RequestLoad(state, activeColumn);
                } else {
                    state->spec = after;
                    ReloadAll(state);
                }
            }
        }

        // Explorer showed the active column's folder a moment ago: its sort
        // for that folder is the one to show, and to remember.
        if (state->specSyncDue && now >= state->specSyncDue) {
            state->specSyncDue = 0;
            Column* activeColumn = state->active >= 0 && state->active < (int)state->columns.size()
                                       ? state->columns[state->active].get()
                                       : nullptr;
            IShellBrowser* browser = BrowserOf(state->topLevel);
            if (activeColumn && browser && g_rememberSort) {
                Pidl shown = CurrentFolder(browser);
                SortSpec read;
                std::wstring path = PathOf(activeColumn->folder.get());
                if (shown && _wcsicmp(PathOf(shown.get()).c_str(), path.c_str()) == 0 &&
                    ReadSortSpec(browser, &read)) {
                    SortSpec const& effective =
                        activeColumn->hasSpec ? activeColumn->spec : state->spec;
                    if (!SameSpec(read, effective)) {
                        Out(L"column \"%s\" takes the sort Explorer has for it", path.c_str());
                        activeColumn->spec = read;
                        activeColumn->hasSpec = true;
                        RequestLoad(state, activeColumn);
                    }
                    RememberFolderSpec(path, read);
                }
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
            // A column that never had the compact style is left exactly as
            // Explorer's template made it.
            if (c->scrollStyle == -1 && wanted == 0) {
                c->scrollStyle = 0;
            }
            if (c->filled && c->scrollStyle != wanted && c->list) {
                try {
                    // Remembered only when a bar was found: the template may
                    // not be built yet.
                    if (StyleScrollBars(c->list, wanted == 1) > 0) {
                        c->scrollStyle = wanted;
                        c->list.Padding(mux::Thickness{5, 12, 10.0, 4});
                        c->gutterRight = 10;
                    }
                } catch (...) {
                    LogHresult(L"scrollbar style");
                }
            }
        }

        // Rows near the screen have their content: the scroller of each list is
        // watched once it exists, and the rows are topped up here as well, since
        // the list lays out a moment after it is filled.
        for (auto& c : state->columns) {
            if (!c->filled || !c->list) {
                continue;
            }
            if (!c->viewer) {
                auto viewer = FindListScroller(c->list);
                if (viewer) {
                    c->viewer = viewer;
                    c->viewToken = viewer.ViewChanged(
                        [](Inspectable const& sender,
                           muxc::ScrollViewerViewChangedEventArgs const&) {
                            OnListViewChanged(sender);
                        });
                }
            }
            BuildVisibleRows(c.get());
            RequestIcons(state, c.get());
        }

        // A compact scrollbar floats over the right edge. While it shows, the
        // free strip for the selection rectangle is 7 px wider, so 10 px stay
        // free beside it. With no bar there is no extra gap.
        if (g_compactScroll) {
            for (auto& c : state->columns) {
                if (!c->list || !c->filled) {
                    continue;
                }
                auto viewer = FindListScroller(c->list);
                bool barShown = viewer && viewer.ComputedVerticalScrollBarVisibility() ==
                                              mux::Visibility::Visible;
                double wanted = barShown ? 23.0 : 10.0;  // 7 for the bar, 16 free beside it
                if (c->gutterRight != wanted) {
                    c->gutterRight = wanted;
                    c->list.Padding(mux::Thickness{5, 12, wanted, 4});
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
        // Transparent mode: the native list is kept invisible, also after
        // Explorer replaces it. Any other mode gives it back.
        if (g_backgroundMode == 2) {
            ClearNativeLists(state);
        } else if (!state->cleared.empty()) {
            RestoreNativeList(state);
        }
        if (state->rootGrid && fileList &&
            (state->shownMode != g_backgroundMode ||
             (state->ticks % 20 == 0 && IsDarkMode() != state->shownDark))) {
            state->shownMode = g_backgroundMode;
            bool nowDark = IsDarkMode();
            state->shownDark = nowDark;
            state->rootGrid.RequestedTheme(nowDark ? mux::ElementTheme::Dark
                                                   : mux::ElementTheme::Light);
            state->rootGrid.Background(PanelBackground(fileList, nowDark));
            Out(L"theme changed, panel background taken again");
        }

        // The system accent or the theme changed: the pill and the selection
        // rectangle take the new colour.
        if (state->ticks % 20 == 0 && state->pill && state->bandBox) {
            try {
                bool darkNow = IsDarkMode();
                winrt::Windows::UI::ViewManagement::UISettings colours;
                auto pillShade = colours.GetColorValue(
                    darkNow ? winrt::Windows::UI::ViewManagement::UIColorType::AccentLight2
                            : winrt::Windows::UI::ViewManagement::UIColorType::AccentDark1);
                auto accent = colours.GetColorValue(
                    winrt::Windows::UI::ViewManagement::UIColorType::Accent);
                auto same = [](winrt::Windows::UI::Color const& a, winrt::Windows::UI::Color const& b) {
                    return a.R == b.R && a.G == b.G && a.B == b.B;
                };
                if (!state->colorsSet || !same(pillShade, state->pillColor) ||
                    !same(accent, state->accentColor)) {
                    if (state->colorsSet) {
                        Out(L"accent or theme changed, the pill and the selection rectangle follow");
                    }
                    state->colorsSet = true;
                    state->pillColor = pillShade;
                    state->accentColor = accent;
                    state->pill.Background(mux::Media::SolidColorBrush(pillShade));
                    state->bandBox.Background(Brush(48, accent.R, accent.G, accent.B));
                    state->bandBox.BorderBrush(Brush(170, accent.R, accent.G, accent.B));
                }
            } catch (...) {
            }
        }

        // Open in a new tab: the tab is there, show the folder in it.
        TryShowNewTab(state);

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
        if (state->syncSelAt && now >= state->syncSelAt) {
            state->syncSelAt = 0;
            SyncSelectionFromExplorer(state);
        }
        if (state->postActionAt && now >= state->postActionAt) {
            state->postActionAt = 0;
            ReloadAll(state);
        }
        if (state->postActionAt2 && now >= state->postActionAt2) {
            state->postActionAt2 = 0;
            SyncSelectionFromExplorer(state);
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
                    if (!picked.empty()) {
                        BeginMultiRename(state, column, picked);
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
            // Not while the strip is scrolling: giving the list the keyboard
            // brings it into view, which cut the centring short.
            if (known && panel && !inside && now >= state->scrollQuietUntil) {
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
            state->shownMode = g_backgroundMode;
            // Transparent mode: the native list is cleared before the panel
            // shows, so it never flashes.
            if (g_backgroundMode == 2) {
                ClearNativeLists(state);
            }
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
                    HoverOn(state);
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
        g_panelWindow = (HWND)bridge.WindowId().Value;
        RegisterPanelDropTarget(state);
        state->lastFileList = fileList;
        state->scroller = scroller;
        state->strip = strip;
        state->on = true;
        state->softHidden = false;
        state->currentTab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
        if (state->currentTab) {
            if (!TabListHas(state->columnsTabs, state->currentTab)) {
                state->columnsTabs.push_back(state->currentTab);
            }
            if (!TabListHas(state->seenTabs, state->currentTab)) {
                state->seenTabs.push_back(state->currentTab);
            }
        }

        step = L"reading the folder";
        RebuildFromExplorer(state);
        SetActiveColumn(state, 0);

        step = L"starting the timer";
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(60));
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

// "Columns" chosen in the View menu of the tab that is showing.
void ChooseColumns(WindowState* state) {
    if (!state->source) {
        ShowPanel(state);
        // Choosing Columns in a folder is remembered for it, also when this
        // builds the panel. Without this, a folder left in another layout
        // earlier stayed marked that way.
        if (state->on && state->lastSeen) {
            RememberFolderColumns(PathOf(state->lastSeen.get()), 1);
        }
        return;
    }
    HWND tab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
    if (tab) {
        if (!TabListHas(state->columnsTabs, tab)) {
            state->columnsTabs.push_back(tab);
        }
        if (!TabListHas(state->seenTabs, tab)) {
            state->seenTabs.push_back(tab);
        }
    }
    state->currentTab = tab;
    SoftShowPanel(state);
    RebuildFromExplorer(state);
    SetActiveColumn(state, 0);
    if (state->lastSeen) {
        RememberFolderColumns(PathOf(state->lastSeen.get()), 1);
    }
    if (state->menuItem) {
        state->menuItem.IsChecked(true);
    }
}

// One of Explorer's own layouts chosen for the tab that is showing.
void LeaveColumns(WindowState* state) {
    {
        IShellBrowser* browser = BrowserOf(state->topLevel);
        Pidl where = browser ? CurrentFolder(browser) : Pidl();
        if (where) {
            RememberFolderColumns(PathOf(where.get()), 0);
        }
    }
    HWND tab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
    TabListRemove(state->columnsTabs, tab);
    for (size_t i = 0; i < state->tabSnapshots.size();) {
        if (state->tabSnapshots[i].tab == tab) {
            state->tabSnapshots.erase(state->tabSnapshots.begin() + i);
        } else {
            i++;
        }
    }
    PruneTabs(state);
    if (state->columnsTabs.empty()) {
        HidePanel(state, L"another layout chosen");
    } else {
        SoftHidePanel(state);
    }
}

// ---- Drag and drop between the columns ----
//
// Rows are dragged with the pointer inside the panel. The mod follows the
// pointer itself, shows a label and lights the folder it would drop on, then
// hands the dragged items to that folder's own drop target, so the shell does the
// move, copy or link, with its own conflict and progress dialogs, as it does
// for Explorer. Ctrl copies, Shift moves and Alt links; without a key, the
// shell chooses (a move on the same drive, a copy to another). Escape cancels.

// The column and folder row under a point of the panel, or false.
bool DragTargetAt(WindowState* state, winrt::Windows::Foundation::Point where, Column** column,
                  int* entry) {
    auto elements = mux::Media::VisualTreeHelper::FindElementsInHostCoordinates(where, state->rootGrid);
    for (auto element : elements) {
        muxc::ListViewItem row{nullptr};
        mux::DependencyObject node = element;
        for (int i = 0; node && i < 40; i++) {
            if (!row) {
                if (auto found = node.try_as<muxc::ListViewItem>()) {
                    row = found;
                }
            }
            if (auto list = node.try_as<muxc::ListView>()) {
                int at = ColumnIndexOfList(state, list);
                if (at < 0) {
                    break;
                }
                *column = state->columns[at].get();
                *entry = -1;
                if (row) {
                    int rowIndex = list.IndexFromContainer(row);
                    if (rowIndex >= 0 && rowIndex < (int)(*column)->rowToEntry.size()) {
                        *entry = (*column)->rowToEntry[rowIndex];
                    }
                }
                return true;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
    }
    return false;
}

// The absolute folder a drop on this column or row would go to.
Pidl DragTargetFolder(Column* column, int entry) {
    if (entry >= 0 && (size_t)entry < column->entries.size()) {
        auto& hit = column->entries[entry];
        return Pidl(hit.linkTarget ? ILClone(hit.linkTarget.get())
                                   : ILCombine(column->folder.get(), hit.child.get()));
    }
    return Pidl(ILClone(column->folder.get()));
}

// Whether the dragged items can be dropped there.
bool DragTargetAllowed(WindowState* state, Column* column, int entry) {
    auto& d = state->drag;
    if (!d.column) {
        return false;
    }
    if (entry >= 0) {
        if ((size_t)entry >= column->entries.size()) {
            return false;
        }
        auto& hit = column->entries[entry];
        if (!hit.folder || hit.archive || hit.launcher) {
            return false;
        }
        if (column == d.column &&
            std::find(d.entries.begin(), d.entries.end(), (size_t)entry) != d.entries.end()) {
            return false;  // A folder onto itself.
        }
    } else {
        if (column == d.column && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            return false;  // Already there. With Ctrl it is a copy in the same folder.
        }
        if (ArchiveParentColumn(state, column) >= 0) {
            return false;
        }
    }
    Pidl target = DragTargetFolder(column, entry);
    wchar_t path[MAX_PATH] = L"";
    if (!target || !SHGetPathFromIDListW(target.get(), path) || !path[0]) {
        return false;  // Not a folder on disk.
    }
    // A folder cannot go into itself or what is inside it.
    for (size_t index : d.entries) {
        if (index >= d.column->entries.size() || !d.column->entries[index].folder) {
            continue;
        }
        std::wstring source = FilePathOf(d.column->folder.get(), d.column->entries[index].child.get());
        if (!source.empty() && _wcsnicmp(path, source.c_str(), source.size()) == 0 &&
            (path[source.size()] == L'\0' || path[source.size()] == L'\\')) {
            return false;
        }
    }
    return true;
}

void DragClearTarget(WindowState* state) {
    auto& d = state->drag;
    try {
        if (d.frame) {
            d.frame.Visibility(mux::Visibility::Collapsed);
        }
    } catch (...) {
        LogHresult(L"clearing the drop target");
    }
    d.targetColumn = nullptr;
    d.targetEntry = -1;
    d.targetOk = false;
}

// The label comes in and goes out the way a flyout does: a short fade with a
// slight scale from the middle. On the way out it is removed when it has faded.
void AnimateGhost(mux::FrameworkElement const& ghost, bool entering) {
    try {
        namespace anim = mux::Media::Animation;
        mux::Media::ScaleTransform scale;
        ghost.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
        ghost.RenderTransform(scale);
        anim::CubicEase ease;
        ease.EasingMode(entering ? anim::EasingMode::EaseOut : anim::EasingMode::EaseIn);
        auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(entering ? 150 : 100));
        anim::Storyboard board;
        auto add = [&](const wchar_t* property, double from, double to) {
            anim::DoubleAnimation step;
            step.From(from);
            step.To(to);
            step.Duration(length);
            step.EasingFunction(ease);
            step.EnableDependentAnimation(true);
            step.FillBehavior(entering ? anim::FillBehavior::Stop : anim::FillBehavior::HoldEnd);
            anim::Storyboard::SetTarget(step, ghost);
            anim::Storyboard::SetTargetProperty(step, property);
            board.Children().Append(step);
        };
        add(L"Opacity", entering ? 0.0 : 1.0, entering ? 1.0 : 0.0);
        add(L"(UIElement.RenderTransform).(ScaleTransform.ScaleX)", entering ? 0.92 : 1.0,
            entering ? 1.0 : 0.96);
        add(L"(UIElement.RenderTransform).(ScaleTransform.ScaleY)", entering ? 0.92 : 1.0,
            entering ? 1.0 : 0.96);
        board.Begin();
    } catch (...) {
        LogHresult(L"animating the drag label");
    }
}

void FadeOutGhost(muxc::Canvas const& overlay, mux::FrameworkElement const& ghost) {
    auto remove = [overlay, ghost]() {
        try {
            uint32_t index = 0;
            if (overlay.Children().IndexOf(ghost, index)) {
                overlay.Children().RemoveAt(index);
            }
        } catch (...) {
        }
    };
    if (g_unloading) {
        remove();
        return;
    }
    try {
        AnimateGhost(ghost, false);
        auto timer = overlay.DispatcherQueue().CreateTimer();
        timer.Interval(std::chrono::milliseconds(130));
        timer.IsRepeating(false);
        timer.Tick([remove](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
            sender.Stop();
            remove();
        });
        timer.Start();
    } catch (...) {
        remove();
    }
}

void DragEnd(WindowState* state) {
    auto& d = state->drag;
    DragClearTarget(state);
    state->autoOn = false;
    try {
        if (d.ghost && state->overlay) {
            FadeOutGhost(state->overlay, d.ghost);
        }
        if (d.frame && state->overlay) {
            uint32_t index = 0;
            if (state->overlay.Children().IndexOf(d.frame, index)) {
                state->overlay.Children().RemoveAt(index);
            }
        }
        if (d.active && d.column && d.column->list) {
            d.column->list.ReleasePointerCaptures();
        }
    } catch (...) {
        LogHresult(L"ending a drag");
    }
    d = DragState();
}

void CancelDrag(WindowState* state) {
    if (state->drag.pending || state->drag.active) {
        DragEnd(state);
    }
}

void StartOutDrag(WindowState* state);

// A press on a row: a drag may start from it once the pointer moves.
void OnDragPressed(WindowState* state, muxc::ListView const& list, muxc::ListViewItem const& row,
                   mux::Input::PointerRoutedEventArgs const& args) {
    if (!args.GetCurrentPoint(list).Properties().IsLeftButtonPressed()) {
        return;
    }
    int at = ColumnIndexOfList(state, list);
    if (at < 0) {
        return;
    }
    Column* column = state->columns[at].get();
    int rowIndex = list.IndexFromContainer(row);
    int entry = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size() ? column->rowToEntry[rowIndex] : -1;
    if (entry < 0) {
        return;
    }
    DragEnd(state);
    auto& d = state->drag;
    d.pending = true;
    d.column = column;
    d.fromArchive = ArchiveParentColumn(state, column) >= 0;
    d.pressedEntry = entry;
    d.start = args.GetCurrentPoint(list).Position();
    static int logged = 0;
    if (logged++ < 6) {
        Out(L"drag may start: pressed on row %d (entry %d) of column %d", rowIndex, entry, at + 1);
    }
}

// Puts the accent frame around the folder row, or the column, a drop would go
// to. Run again whenever the pointer moves or the columns scroll, since the row
// moves under a frame that stays still.
void DragPlaceFrame(WindowState* state) {
    auto& d = state->drag;
    if (!d.targetOk || !d.targetColumn || !state->overlay) {
        if (d.frame) {
            d.frame.Visibility(mux::Visibility::Collapsed);
        }
        return;
    }
    try {
        Column* column = d.targetColumn;
        mux::FrameworkElement target{nullptr};
        if (d.targetEntry >= 0) {
            int rowIndex = column->entryToRow[d.targetEntry];
            target = column->list.Items().GetAt((uint32_t)rowIndex).try_as<muxc::ListViewItem>();
        } else {
            target = column->cell;
        }
        if (!target) {
            if (d.frame) {
                d.frame.Visibility(mux::Visibility::Collapsed);
            }
            return;
        }
        auto at = target.TransformToVisual(state->overlay).TransformPoint(winrt::Windows::Foundation::Point{0, 0});
        double left = at.X, top = at.Y, width = target.ActualWidth(), height = target.ActualHeight();
        if (d.targetEntry >= 0) {
            // A row that is half scrolled out of its column is framed only where it shows.
            auto listAt = column->list.TransformToVisual(state->overlay)
                              .TransformPoint(winrt::Windows::Foundation::Point{0, 0});
            double listTop = listAt.Y, listBottom = listAt.Y + column->list.ActualHeight();
            if (top < listTop) {
                height -= listTop - top;
                top = listTop;
            }
            if (top + height > listBottom) {
                height = listBottom - top;
            }
        }
        if (width < 4 || height < 4) {
            if (d.frame) {
                d.frame.Visibility(mux::Visibility::Collapsed);
            }
            return;
        }
        if (!d.frame) {
            winrt::Windows::UI::Color tint = state->colorsSet ? state->pillColor
                                                              : winrt::Windows::UI::Color{255, 76, 194, 255};
            muxc::Border frame;
            frame.BorderBrush(Brush(255, tint.R, tint.G, tint.B));
            frame.BorderThickness(mux::Thickness{1.5, 1.5, 1.5, 1.5});
            frame.Background(Brush(12, tint.R, tint.G, tint.B));
            frame.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
            frame.IsHitTestVisible(false);
            state->overlay.Children().InsertAt(0, frame);  // Under the label.
            d.frame = frame;
        }
        muxc::Canvas::SetLeft(d.frame, left);
        muxc::Canvas::SetTop(d.frame, top);
        d.frame.Width(width);
        d.frame.Height(height);
        d.frame.Visibility(mux::Visibility::Visible);
    } catch (...) {
        LogHresult(L"framing the drop target");
    }
}

// Lights the folder row, or the column, a drop would go to.
void DragLight(WindowState* state, Column*, int) {
    DragPlaceFrame(state);
}

// Finds the target under a point of the panel (host coordinates) for the drag
// of rows, and lights it when the drop is allowed. Also run by the tick while
// a column scrolls under a pointer that does not move.
void DragRefreshTarget(WindowState* state, winrt::Windows::Foundation::Point where) {
    auto& d = state->drag;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    Column* targetColumn = nullptr;
    int targetEntry = -1;
    bool found = false;
    try {
        found = DragTargetAt(state, where, &targetColumn, &targetEntry);
    } catch (...) {
        LogHresult(L"finding the drop target");
    }
    if (!found || targetColumn != d.targetColumn || targetEntry != d.targetEntry ||
        ctrl != d.lastCtrl) {
        d.lastCtrl = ctrl;
        DragClearTarget(state);
        if (found) {
            d.targetColumn = targetColumn;
            d.targetEntry = targetEntry;
            d.targetOk = DragTargetAllowed(state, targetColumn, targetEntry);
            if (d.targetOk) {
                DragLight(state, targetColumn, targetEntry);
            }
        }
    }
    if (d.targetOk) {
        DragPlaceFrame(state);
    }
    if (d.ghost) {
        d.ghost.Opacity(d.targetOk || !found ? 1.0 : 0.8);
    }
}

// The label's content, as it is now.
GhostSpec SpecOf(Column* column, std::vector<size_t> const& entries) {
    GhostSpec spec;
    spec.count = entries.size();
    spec.name = entries.size() == 1 && entries[0] < column->entries.size()
                    ? column->entries[entries[0]].name
                    : std::to_wstring(entries.size()) + L" items";
    for (size_t at = 0; at < std::min<size_t>(3, entries.size()); ++at) {
        mux::Media::ImageSource source{nullptr};
        size_t entry = entries[at];
        try {
            if (entry < column->iconHosts.size() && column->iconHosts[entry] &&
                column->iconHosts[entry].Children().Size() > 0) {
                if (auto image = column->iconHosts[entry].Children().GetAt(0).try_as<muxc::Image>()) {
                    source = image.Source();
                }
            }
        } catch (...) {
        }
        spec.icons.push_back(source);
    }
    return spec;
}

// Builds the label that follows the pointer, on the surface of a WinUI tooltip
// (no stroke of its own): up to three icons of what is dragged, stacked, the
// name or the count, and a line saying what a drop would do there. It goes into
// the drag state and the overlay; the caller keeps the rest.
void ShowGhost(WindowState* state, GhostSpec const& spec) {
    auto& d = state->drag;
    muxc::ToolTip ghost;
    ghost.BorderThickness(mux::Thickness{0, 0, 0, 0});
    ghost.Padding(mux::Thickness{10, 8, 14, 8});
    ghost.IsHitTestVisible(false);
    muxc::StackPanel content;
    content.Orientation(muxc::Orientation::Horizontal);
    content.Spacing(10);
    size_t shown = std::max<size_t>(1, std::min<size_t>(3, spec.count));
    const double iconSize = 32;  // As big as the icon of Explorer's own drag image.
    muxc::Grid stack;
    stack.Width(iconSize + 6.0 * (shown - 1));
    stack.Height(iconSize + 4.0 * (shown - 1));
    stack.VerticalAlignment(mux::VerticalAlignment::Center);
    for (size_t at = shown; at-- > 0;) {  // Back to front.
        mux::UIElement piece{nullptr};
        if (at < spec.icons.size() && spec.icons[at]) {
            muxc::Image image;
            image.Source(spec.icons[at]);
            image.Width(iconSize);
            image.Height(iconSize);
            image.Stretch(mux::Media::Stretch::Uniform);
            piece = image;
        } else {
            muxc::FontIcon glyph;
            glyph.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            glyph.Glyph(L"");
            glyph.FontSize(26);
            glyph.Width(iconSize);
            glyph.Height(iconSize);
            piece = glyph;
        }
        auto frame = piece.as<mux::FrameworkElement>();
        if (shown == 1) {
            frame.HorizontalAlignment(mux::HorizontalAlignment::Center);
            frame.VerticalAlignment(mux::VerticalAlignment::Center);
        } else {
            frame.HorizontalAlignment(mux::HorizontalAlignment::Left);
            frame.VerticalAlignment(mux::VerticalAlignment::Bottom);
            frame.Margin(mux::Thickness{6.0 * at, 0, 0, 4.0 * at});
            piece.Opacity(at == 0 ? 1.0 : 0.85);
        }
        stack.Children().Append(piece);
    }
    content.Children().Append(stack);
    muxc::StackPanel texts;
    texts.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::TextBlock text;
    text.FontSize(12);
    text.MaxWidth(240);
    text.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    texts.Children().Append(text);
    muxc::StackPanel row;
    row.Orientation(muxc::Orientation::Horizontal);
    row.Spacing(5);
    row.Opacity(0.7);
    row.Visibility(mux::Visibility::Collapsed);
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.FontSize(11);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    row.Children().Append(icon);
    muxc::TextBlock action;
    action.FontSize(11);
    action.MaxWidth(220);
    action.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    row.Children().Append(action);
    texts.Children().Append(row);
    content.Children().Append(texts);
    ghost.Content(content);
    d.ghost = ghost;
    d.ghostText = text;
    d.ghostAction = action;
    d.ghostIcon = icon;
    d.ghostRow = row;
    d.labelName = spec.name;
    state->overlay.Children().Append(ghost);
    AnimateGhost(ghost, true);
}

// The label goes out, with its animation, and the drag state forgets it.
void RemoveGhost(WindowState* state) {
    auto& d = state->drag;
    if (d.ghost && state->overlay) {
        FadeOutGhost(state->overlay, d.ghost);
    }
    d.ghost = nullptr;
    d.ghostText = nullptr;
    d.ghostAction = nullptr;
    d.ghostIcon = nullptr;
    d.ghostRow = nullptr;
}

// The name of a folder, as the shell shows it.
std::wstring FolderDisplayName(PCIDLIST_ABSOLUTE pidl) {
    std::wstring result;
    PWSTR text = nullptr;
    if (pidl && SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &text)) && text) {
        result = text;
        CoTaskMemFree(text);
    }
    return result;
}

std::wstring Shortened(std::wstring text, size_t most) {
    if (text.size() > most) {
        text = text.substr(0, most - 1) + L"…";
    }
    return text;
}

// The two lines of the label: what is dragged, and what a drop would do.
void DragUpdateLabel(WindowState* state) {
    auto& d = state->drag;
    if (!d.ghostText) {
        return;
    }
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    d.ghostText.Text(Shortened(d.labelName, 40).c_str());
    if (!d.ghostAction) {
        return;
    }
    std::wstring line;
    const wchar_t* glyph = L"";
    if (d.targetColumn) {
        if (!d.targetOk) {
            line = L"Can’t drop here";
            glyph = L"";  // Forbidden.
        } else {
            std::wstring target =
                d.targetEntry >= 0 && (size_t)d.targetEntry < d.targetColumn->entries.size()
                    ? d.targetColumn->entries[d.targetEntry].name
                    : FolderDisplayName(d.targetColumn->folder.get());
            const wchar_t* verb = L"Drop in ";
            bool extract = d.fromArchive || state->outFromArchive;
            glyph = L"";  // Move to a folder.
            if (ctrl && !shift) {
                verb = L"Copy to ";
                glyph = L"";
            } else if (alt) {
                verb = L"Link in ";
                glyph = L"";
            } else if (shift) {
                verb = L"Move to ";
            }
            if (extract) {
                verb = L"Extract to ";
                glyph = L"\uE8C8";
            }
            line = std::wstring(verb) + Shortened(target, 28);
        }
    }
    if (line.empty()) {
        d.ghostRow.Visibility(mux::Visibility::Collapsed);
    } else {
        d.ghostIcon.Glyph(glyph);
        d.ghostAction.Text(line.c_str());
        d.ghostRow.Visibility(mux::Visibility::Visible);
    }
}

// Moves of the pointer. True while a drag is under way, so the hover code
// stands aside.
bool OnDragMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args) {
    auto& d = state->drag;
    if (!d.pending && !d.active) {
        return false;
    }
    if (d.column != column) {
        return d.active;
    }
    auto point = args.GetCurrentPoint(list);
    if (!point.Properties().IsLeftButtonPressed()) {
        DragEnd(state);
        return false;
    }
    auto at = point.Position();
    if (d.pending) {
        if (std::abs(at.X - d.start.X) + std::abs(at.Y - d.start.Y) < 8) {
            return false;  // Still a click.
        }
        std::vector<size_t> picked = SelectedEntries(column, list);
        if (std::find(picked.begin(), picked.end(), (size_t)d.pressedEntry) == picked.end()) {
            picked.assign(1, (size_t)d.pressedEntry);
        }
        d.entries = picked;
        d.pending = false;
        d.active = true;
        state->slowRenameAt = 0;
        Out(L"drag: moved far enough, taking the pointer");
        list.CapturePointer(args.Pointer());
        Out(L"drag: pointer taken, making the label");
        ShowGhost(state, SpecOf(column, d.entries));
        Out(L"drag started from column %d with %d item(s)", ColumnIndexOfList(state, list) + 1,
            (int)d.entries.size());
    }

    // Where the label goes, and the target under the pointer.
    try {
        auto overlayAt = args.GetCurrentPoint(state->overlay).Position();
        muxc::Canvas::SetLeft(d.ghost, overlayAt.X + 16);
        muxc::Canvas::SetTop(d.ghost, overlayAt.Y + 16);
    } catch (...) {
    }
    state->autoPoint = args.GetCurrentPoint(nullptr).Position();
    state->autoOn = true;
    DragRefreshTarget(state, state->autoPoint);
    DragUpdateLabel(state);
    // Past the edge of the panel the drag goes on as a real one.
    if (g_dragOut && state->rootGrid) {
        auto inRoot = args.GetCurrentPoint(state->rootGrid).Position();
        if (inRoot.X < 0 || inRoot.Y < 0 || inRoot.X > state->rootGrid.ActualWidth() ||
            inRoot.Y > state->rootGrid.ActualHeight()) {
            StartOutDrag(state);
            return true;
        }
    }
    return true;
}

// Hands the items to the folder's drop target. All the arguments are copies, so
// nothing depends on the columns still being as they were.
void DropItems(WindowState* state, Pidl sourceFolder, std::vector<Pidl> children, Pidl targetFolder,
               DWORD keyState) {
    IShellFolder* source = nullptr;
    HRESULT hr = ILIsEmpty(sourceFolder.get())
                     ? SHGetDesktopFolder(&source)
                     : SHBindToObject(nullptr, sourceFolder.get(), nullptr, IID_PPV_ARGS(&source));
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && source) {
        std::vector<PCUITEMID_CHILD> items;
        for (auto& child : children) {
            items.push_back(child.get());
        }
        hr = source->GetUIObjectOf(state->topLevel, (UINT)items.size(), items.data(), IID_IDataObject,
                                   nullptr, (void**)&data);
    }
    if (source) {
        source->Release();
    }
    IShellFolder* target = nullptr;
    if (SUCCEEDED(hr) && data) {
        hr = SHBindToObject(nullptr, targetFolder.get(), nullptr, IID_PPV_ARGS(&target));
    }
    IDropTarget* drop = nullptr;
    if (SUCCEEDED(hr) && target) {
        hr = target->CreateViewObject(state->topLevel, IID_PPV_ARGS(&drop));
    }
    if (target) {
        target->Release();
    }
    if (FAILED(hr) || !drop || !data) {
        Out(L"drop: no data object or drop target, hr=0x%08lX", hr);
        if (drop) {
            drop->Release();
        }
        if (data) {
            data->Release();
        }
        return;
    }
    POINT cursor;
    GetCursorPos(&cursor);
    POINTL where = {cursor.x, cursor.y};
    DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    hr = drop->DragEnter(data, keyState, where, &effect);
    if (SUCCEEDED(hr) && effect != DROPEFFECT_NONE) {
        DWORD offered = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
        drop->DragOver(keyState, where, &offered);
        DWORD result = offered;
        hr = drop->Drop(data, keyState, where, &result);
        Out(L"dropped %d item(s) on \"%s\": effect %lu, hr=0x%08lX", (int)children.size(),
            PathOf(targetFolder.get()).c_str(), (unsigned long)result, hr);
    } else {
        drop->DragLeave();
        Out(L"the folder takes no drop, hr=0x%08lX effect=%lu", hr, (unsigned long)effect);
    }
    data->Release();
    drop->Release();
    ReloadAll(state);
}

// Puts the effect a target of the shell takes when no key says otherwise into the
// dragged data.
void SetPreferredEffect(IDataObject* data, DWORD effect) {
    static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!memory) {
        return;
    }
    if (DWORD* value = (DWORD*)GlobalLock(memory)) {
        *value = effect;
        GlobalUnlock(memory);
    }
    FORMATETC wanted = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = memory;
    if (FAILED(data->SetData(&wanted, &medium, TRUE))) {
        GlobalFree(memory);
    }
}

// The drag source of a real drag. It ends the drag on Escape or when the button
// goes up, and notes what the target under the pointer last offered and which
// window it was, for the log.
struct OutDropSource : IDropSource {
    LONG refs = 1;
    DWORD lastEffect = 0;
    DWORD dropKeys = 0;
    DWORD graceKeys = 0;  // The keys of a moment ago that still count at the release.
    IDataObject* data = nullptr;  // The dragged data, not owned.
    ULONGLONG ctrlAt = 0, shiftAt = 0, altAt = 0;  // When each key was last seen down.
    HWND dropWindow = nullptr;
    POINT dropPoint{};
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDropSource) {
            *out = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG now = InterlockedDecrement(&refs);
        if (now == 0) {
            delete this;
        }
        return (ULONG)now;
    }
    STDMETHODIMP QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) {
            return DRAGDROP_S_CANCEL;
        }
        ULONGLONG now = GetTickCount64();
        if (keys & MK_CONTROL) {
            ctrlAt = now;
        }
        if (keys & MK_SHIFT) {
            shiftAt = now;
        }
        if (keys & MK_ALT) {
            altAt = now;
        }
        if (!(keys & MK_LBUTTON)) {
            // A key let go a moment before the button still counts: the two
            // rarely come up in the same instant, and the wrong one would turn
            // a copy into a move.
            const ULONGLONG grace = 400;
            DWORD wanted = DROPEFFECT_NONE;
            if (now - altAt < grace) {
                wanted = DROPEFFECT_LINK;
            } else if (now - ctrlAt < grace && now - shiftAt >= grace) {
                wanted = DROPEFFECT_COPY;
            } else if (now - shiftAt < grace) {
                wanted = DROPEFFECT_MOVE;
            }
            graceKeys = wanted == DROPEFFECT_COPY   ? MK_CONTROL
                        : wanted == DROPEFFECT_MOVE ? MK_SHIFT
                        : wanted == DROPEFFECT_LINK ? (MK_CONTROL | MK_SHIFT)
                                                    : 0;
            if (wanted != DROPEFFECT_NONE && data) {
                SetPreferredEffect(data, wanted);
                Out(L"drag out: the keys of a moment ago ask for effect %lu", (unsigned long)wanted);
            }
            POINT cursor;
            GetCursorPos(&cursor);
            dropPoint = cursor;
            dropKeys = keys;
            dropWindow = WindowFromPoint(cursor);
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }
    HWND probeWindow = nullptr;
    DWORD probeEffect = 0xFFFFFFFF;
    // Probe (0.13.7): logs when the window under the pointer or the effect it
    // offers changes, and the tree item under the pointer over a tree view.
    int feedbackCalls = 0;
    void Probe(DWORD effect) {
        POINT cursor;
        GetCursorPos(&cursor);
        HWND under = WindowFromPoint(cursor);
        if (under == probeWindow && effect == probeEffect) {
            return;
        }
        probeWindow = under;
        probeEffect = effect;
        std::wstring cls = under ? ClassNameOf(under) : L"?";
        std::wstring item;
        if (under && cls == L"SysTreeView32") {
            TVHITTESTINFO hit = {};
            hit.pt = cursor;
            ScreenToClient(under, &hit.pt);
            HTREEITEM node = (HTREEITEM)SendMessageW(under, TVM_HITTEST, 0, (LPARAM)&hit);
            if (node) {
                wchar_t text[128] = L"";
                TVITEMW info = {};
                info.mask = TVIF_TEXT;
                info.hItem = node;
                info.pszText = text;
                info.cchTextMax = ARRAYSIZE(text);
                SendMessageW(under, TVM_GETITEMW, 0, (LPARAM)&info);
                item = text;
            } else {
                item = L"(no item)";
            }
        }
        Out(L"drag probe: over \"%s\" %p%s%s, effect offered %lu", cls.c_str(), under,
            item.empty() ? L"" : L" item ", item.c_str(), (unsigned long)effect);
    }
    STDMETHODIMP GiveFeedback(DWORD effect) override {
        lastEffect = effect;
        feedbackCalls++;
        Probe(effect);
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// The child of a shell item whose display name is `name`, or null. The caller
// releases it.
IShellItem* FindChildByName(IShellItem* parent, const std::wstring& name) {
    IEnumShellItems* items = nullptr;
    if (FAILED(parent->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))) || !items) {
        return nullptr;
    }
    IShellItem* found = nullptr;
    IShellItem* child = nullptr;
    ULONG fetched = 0;
    while (!found && items->Next(1, &child, &fetched) == S_OK && fetched == 1) {
        PWSTR text = nullptr;
        if (SUCCEEDED(child->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
            if (_wcsicmp(text, name.c_str()) == 0) {
                found = child;
                found->AddRef();
            }
            CoTaskMemFree(text);
        }
        child->Release();
        child = nullptr;
    }
    items->Release();
    return found;
}

// A folder called `name` among the places a pinned folder usually comes from: the
// known folders themselves, then what lies in the Desktop, the user's profile and
// This PC. The caller releases it.
IShellItem* FindInUsualPlaces(const std::wstring& name) {
    const KNOWNFOLDERID* ids[] = {&FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Pictures,
                                  &FOLDERID_Music,     &FOLDERID_Videos,    &FOLDERID_Desktop,
                                  &FOLDERID_Profile};
    std::vector<IShellItem*> places;
    for (const KNOWNFOLDERID* id : ids) {
        IShellItem* place = nullptr;
        if (SUCCEEDED(SHGetKnownFolderItem(*id, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&place))) && place) {
            places.push_back(place);
        }
    }
    PIDLIST_ABSOLUTE computer = nullptr;
    if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &computer)) && computer) {
        IShellItem* place = nullptr;
        if (SUCCEEDED(SHCreateItemFromIDList(computer, IID_PPV_ARGS(&place))) && place) {
            places.push_back(place);
        }
        CoTaskMemFree(computer);
    }
    IShellItem* found = nullptr;
    for (IShellItem* place : places) {
        if (found) {
            break;
        }
        PWSTR text = nullptr;
        if (SUCCEEDED(place->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
            if (_wcsicmp(text, name.c_str()) == 0) {
                found = place;
                found->AddRef();
            }
            CoTaskMemFree(text);
        }
        if (!found) {
            found = FindChildByName(place, name);
        }
    }
    for (IShellItem* place : places) {
        place->Release();
    }
    return found;
}

// The folder a navigation pane tree item stands for, found by walking the names
// of the item and its parents from the root: the tree keeps no pidl we can read.
// A root that is not under the Desktop is taken to be Home (Quick access).
Pidl FolderOfNavItem(HWND tree, HTREEITEM node) {
    std::vector<std::wstring> names;
    for (HTREEITEM walk = node; walk;
         walk = (HTREEITEM)SendMessageW(tree, TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)walk)) {
        wchar_t text[256] = L"";
        TVITEMW info = {};
        info.mask = TVIF_TEXT;
        info.hItem = walk;
        info.pszText = text;
        info.cchTextMax = ARRAYSIZE(text);
        SendMessageW(tree, TVM_GETITEMW, 0, (LPARAM)&info);
        names.insert(names.begin(), text);
    }
    if (names.empty()) {
        return Pidl();
    }
    IShellItem* current = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&current))) ||
        !current) {
        return Pidl();
    }
    size_t index = 0;
    if (IShellItem* root = FindChildByName(current, names[0])) {
        current->Release();
        current = root;
        index = 1;
    } else {
        current->Release();
        current = nullptr;
        // Home: the pinned folders are roots of the tree themselves, or children of
        // a root named Home, so look for the first name in Home, else skip it.
        if (FAILED(SHCreateItemFromParsingName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}", nullptr,
                                               IID_PPV_ARGS(&current))) ||
            !current) {
            Out(L"drag out: no folder found for the navigation pane item \"%s\"", names.back().c_str());
            return Pidl();
        }
        if (IShellItem* pinned = FindChildByName(current, names[0])) {
            current->Release();
            current = pinned;
        } else if (names.size() < 2) {
            current->Release();
            Out(L"drag out: no folder found for the navigation pane item \"%s\"", names.back().c_str());
            return Pidl();
        }
        index = 1;
    }
    for (; index < names.size(); ++index) {
        IShellItem* next = FindChildByName(current, names[index]);
        if (!next) {
            next = FindInUsualPlaces(names[index]);
        }
        current->Release();
        current = next;
        if (!current) {
            Out(L"drag out: no folder found for the navigation pane item \"%s\" (stopped at \"%s\", chain of %d, first \"%s\")",
                names.back().c_str(), names[index].c_str(), (int)names.size(), names[0].c_str());
            return Pidl();
        }
    }
    Pidl result;
    PWSTR path = nullptr;
    if (SUCCEEDED(current->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path, nullptr, &pidl, 0, nullptr)) && pidl) {
            result.reset(pidl);
        }
        CoTaskMemFree(path);
    }
    if (!result) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(current, &pidl)) && pidl) {
            result.reset(pidl);
        }
    }
    current->Release();
    return result;
}

// The shell's drag loop is driven by mouse move messages, and the panel's input
// (a XAML island) takes them as pointer messages, so the loop never sees the
// pointer move: a target hears of the release point only, and the navigation
// pane, the views of other windows and other applications show no hover and
// answer nothing until then. This thread posts the missing moves to the drag
// loop's thread while the drag runs, with the position and the keys as they are.
struct MoveNudger {
    std::atomic<bool> stop{false};
    std::atomic<int> sent{0};
    std::thread worker;
    void Start(DWORD threadId) {
        worker = std::thread([this, threadId]() {
            POINT last = {LONG_MIN, LONG_MIN};
            while (!stop.load()) {
                POINT now;
                if (GetCursorPos(&now) && (now.x != last.x || now.y != last.y)) {
                    last = now;
                    WPARAM keys = 0;
                    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) keys |= MK_LBUTTON;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keys |= MK_CONTROL;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keys |= MK_SHIFT;
                    if (PostThreadMessageW(threadId, WM_MOUSEMOVE, keys,
                                           MAKELPARAM((WORD)now.x, (WORD)now.y))) {
                        sent++;
                    }
                }
                Sleep(15);
            }
        });
    }
    void Stop() {
        stop = true;
        if (worker.joinable()) {
            worker.join();
        }
    }
    ~MoveNudger() { Stop(); }
};

// A real drag, for when the pointer leaves the panel: the shell's own drag loop
// carries the items to whatever is under the pointer, another Explorer window,
// the desktop or another application. Started after the event that noticed it,
// and with the panel's own drag already ended.
void DoOutDrag(WindowState* state, Pidl sourceFolder, std::vector<Pidl> children,
               DWORD preferred) {
    IShellFolder* source = nullptr;
    HRESULT hr = ILIsEmpty(sourceFolder.get())
                     ? SHGetDesktopFolder(&source)
                     : SHBindToObject(nullptr, sourceFolder.get(), nullptr, IID_PPV_ARGS(&source));
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && source) {
        std::vector<PCUITEMID_CHILD> items;
        for (auto& child : children) {
            items.push_back(child.get());
        }
        hr = source->GetUIObjectOf(state->topLevel, (UINT)items.size(), items.data(), IID_IDataObject,
                                   nullptr, (void**)&data);
    }
    if (source) {
        source->Release();
    }
    if (FAILED(hr) || !data) {
        Out(L"drag out: no data object, hr=0x%08lX", hr);
        return;
    }
    DWORD effect = DROPEFFECT_NONE;
    // The keys held as the pointer left the panel decide the default effect: a
    // target of the shell reads it from the data when no key says otherwise. The
    // drag loop does not see a key pressed after it has started.
    if (state->outFromArchive) {
        preferred = DROPEFFECT_COPY;
    }
    if (preferred != DROPEFFECT_NONE) {
        SetPreferredEffect(data, preferred);
    }
    Out(L"drag out: %d item(s) leave the panel, starting the shell's drag, default effect %lu",
        (int)children.size(), (unsigned long)preferred);
    // 0.13.9: the panel's pointer capture (XAML or Win32) may still hold the mouse,
    // so the drag loop would see no moves until the release.
    ReleaseCapture();
    OutDropSource* dropSource = new OutDropSource();
    dropSource->data = data;
    MoveNudger nudger;
    nudger.Start(GetCurrentThreadId());
    state->outDragActive = true;
    hr = SHDoDragDrop(state->topLevel, data, dropSource,
                      state->outFromArchive ? DROPEFFECT_COPY : (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK),
                      &effect);
    state->outDragActive = false;
    RemoveGhost(state);
    nudger.Stop();
    Out(L"drag out: the drag loop asked for feedback %d time(s), %d pointer move(s) posted to it",
        dropSource->feedbackCalls, nudger.sent.load());
    {
        wchar_t title[80] = L"";
        DWORD processId = 0;
        if (dropSource->dropWindow) {
            GetWindowTextW(dropSource->dropWindow, title, ARRAYSIZE(title));
            GetWindowThreadProcessId(dropSource->dropWindow, &processId);
        }
        Out(L"drag out ended: effect %lu, hr=0x%08lX; keys at the drop 0x%lX; the target last offered effect %lu; dropped over a \"%s\" window \"%s\" of process %lu",
            (unsigned long)effect, hr, (unsigned long)dropSource->dropKeys,
            (unsigned long)dropSource->lastEffect,
            dropSource->dropWindow ? ClassNameOf(dropSource->dropWindow).c_str() : L"?", title,
            (unsigned long)processId);
    }
    if (dropSource->dropWindow) {
        HWND root = GetAncestor(dropSource->dropWindow, GA_ROOT);
        RECT rect{};
        wchar_t rootTitle[80] = L"";
        if (root) {
            GetWindowRect(root, &rect);
            GetWindowTextW(root, rootTitle, ARRAYSIZE(rootTitle));
        }
        Out(L"drag out: released at %ld,%ld over top-level \"%s\" \"%s\" at %ld,%ld to %ld,%ld",
            dropSource->dropPoint.x, dropSource->dropPoint.y,
            root ? ClassNameOf(root).c_str() : L"?", rootTitle, rect.left, rect.top, rect.right,
            rect.bottom);
    }
    // 0.14.0: the drag loop gets no pointer moves from the panel's input, so it only
    // tells a target about the release point, and the navigation pane's tree needs a
    // hovered item to take a drop. Dropped on that tree, the drop is done here, to
    // the folder the item under the pointer stands for, with the folder's own drop
    // target like a drop between columns.
    HWND treeWindow = dropSource->dropWindow;
    POINT treePoint = dropSource->dropPoint;
    DWORD treeKeys = dropSource->dropKeys & (MK_CONTROL | MK_SHIFT | MK_ALT);
    treeKeys |= dropSource->graceKeys;
    DWORD lastOffered = dropSource->lastEffect;
    if (state->outFromArchive) {
        treeKeys = MK_CONTROL;
    }
    // A target that answered the drag with one effect has taken the drop itself;
    // only one that kept the whole offer (all three effects) has heard nothing.
    bool unanswered = (dropSource->lastEffect & (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK)) ==
                      (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK);
    dropSource->Release();
    data->Release();
    DWORD treeProcess = 0;
    if (treeWindow && ClassNameOf(treeWindow) == L"SysTreeView32") {
        GetWindowThreadProcessId(treeWindow, &treeProcess);
    }
    // A folder dropped on the tab strip opens in a new tab. The strip is Explorer's
    // own XAML and took no drop from this drag, so the mod does it: the release is
    // in the top band of the window, left of the caption buttons, and nothing
    // answered the drag.
    if (!children.empty() && lastOffered == 0 && treeWindow && !state->outFromArchive) {
        HWND top = GetAncestor(treeWindow, GA_ROOT);
        DWORD topProcess = 0;
        if (top) {
            GetWindowThreadProcessId(top, &topProcess);
        }
        RECT box{};
        if (top && topProcess == GetCurrentProcessId() && ClassNameOf(top) == L"CabinetWClass" &&
            GetWindowRect(top, &box)) {
            double scale = GetDpiForWindow(top) / 96.0;
            bool inStrip = treePoint.y >= box.top && treePoint.y <= box.top + (LONG)(46 * scale) &&
                           treePoint.x >= box.left && treePoint.x <= box.right - (LONG)(140 * scale);
            if (inStrip) {
                IShellFolder* sourceFolderObject = nullptr;
                HRESULT bound = ILIsEmpty(sourceFolder.get())
                                    ? SHGetDesktopFolder(&sourceFolderObject)
                                    : SHBindToObject(nullptr, sourceFolder.get(), nullptr,
                                                     IID_PPV_ARGS(&sourceFolderObject));
                if (SUCCEEDED(bound) && sourceFolderObject) {
                    for (auto& child : children) {
                        PCUITEMID_CHILD one = child.get();
                        ULONG attributes = SFGAO_FOLDER | SFGAO_STREAM;
                        if (SUCCEEDED(sourceFolderObject->GetAttributesOf(1, &one, &attributes)) &&
                            (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM)) {
                            Pidl folder(ILCombine(sourceFolder.get(), child.get()));
                            Out(L"drag out: a folder dropped on the tab strip opens in a new tab");
                            OpenFolderInNewTab(state, folder.get());
                            break;  // One tab per drop.
                        }
                    }
                    sourceFolderObject->Release();
                }
            }
        }
    }
    if (treeProcess == GetCurrentProcessId() && !children.empty() && !unanswered) {
        Out(L"drag out: the navigation pane answered the drag itself, nothing to finish");
    } else if (treeProcess == GetCurrentProcessId() && !children.empty()) {
        TVHITTESTINFO hit = {};
        hit.pt = treePoint;
        ScreenToClient(treeWindow, &hit.pt);
        HTREEITEM node = (HTREEITEM)SendMessageW(treeWindow, TVM_HITTEST, 0, (LPARAM)&hit);
        if (!node) {
            Out(L"drag out: released on the navigation pane, but not on an item");
        } else {
            Pidl target = FolderOfNavItem(treeWindow, node);
            if (target) {
                // The shell may have done the move itself: look before doing it again.
                IShellFolder* sourceFolderObject = nullptr;
                HRESULT bound = ILIsEmpty(sourceFolder.get())
                                    ? SHGetDesktopFolder(&sourceFolderObject)
                                    : SHBindToObject(nullptr, sourceFolder.get(), nullptr,
                                                     IID_PPV_ARGS(&sourceFolderObject));
                bool stillThere = true;
                if (SUCCEEDED(bound) && sourceFolderObject) {
                    PCUITEMID_CHILD one = children[0].get();
                    ULONG attributes = SFGAO_VALIDATE;
                    stillThere = SUCCEEDED(sourceFolderObject->GetAttributesOf(1, &one, &attributes));
                    sourceFolderObject->Release();
                }
                std::wstring targetPath = PathOf(target.get());
                if (!stillThere) {
                    Out(L"drag out: the items are gone from their folder, nothing left to drop on the navigation pane item");
                } else if (_wcsicmp(targetPath.c_str(), PathOf(sourceFolder.get()).c_str()) == 0) {
                    Out(L"drag out: the navigation pane item is the folder the items came from, nothing to do");
                } else {
                    Out(L"drag out: finishing the drop on the navigation pane item, folder \"%s\", keys 0x%lX",
                        targetPath.c_str(), (unsigned long)treeKeys);
                    DropItems(state, std::move(sourceFolder), std::move(children), std::move(target),
                              treeKeys);
                }
            }
        }
    }
    ReloadAll(state);
}

void StartOutDrag(WindowState* state) {
    auto& d = state->drag;
    if (!d.active || !d.column) {
        return;
    }
    Pidl sourceFolder(ILClone(d.column->folder.get()));
    std::vector<Pidl> children;
    for (size_t index : d.entries) {
        if (index < d.column->entries.size()) {
            children.emplace_back(ILClone(d.column->entries[index].child.get()));
        }
    }
    Out(L"drag out: the pointer left the panel, ending the panel's own drag");
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    DWORD preferred = alt ? DROPEFFECT_LINK : (ctrl && !shift) ? DROPEFFECT_COPY
                      : shift ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
    state->outSpec = SpecOf(d.column, d.entries);
    state->outFromArchive = d.fromArchive;
    DragEnd(state);
    if (children.empty()) {
        return;
    }
    auto shared = std::make_shared<std::pair<Pidl, std::vector<Pidl>>>(std::move(sourceFolder),
                                                                       std::move(children));
    state->queue.TryEnqueue([shared, preferred]() {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->on || g_unloading) {
            return;
        }
        try {
            DoOutDrag(now, std::move(shared->first), std::move(shared->second), preferred);
        } catch (...) {
            LogHresult(L"dragging out");
        }
    });
}

// The button went up. True if it ended a drag.
bool OnDragReleased(WindowState* state) {
    auto& d = state->drag;
    if (d.pending) {
        DragEnd(state);
        return false;
    }
    if (!d.active) {
        return false;
    }
    Column* sourceColumn = d.column;
    Column* targetColumn = d.targetColumn;
    int targetEntry = d.targetEntry;
    // Judged again with the keys as they are at the release.
    bool ok = d.targetColumn && DragTargetAllowed(state, d.targetColumn, d.targetEntry);
    std::vector<size_t> entries = d.entries;
    bool fromArchive = d.fromArchive;
    DragEnd(state);
    if (!ok || !targetColumn || !sourceColumn) {
        Out(L"drag ended outside a folder that takes it");
        return true;
    }
    Pidl sourceFolder(ILClone(sourceColumn->folder.get()));
    std::vector<Pidl> children;
    for (size_t index : entries) {
        if (index < sourceColumn->entries.size()) {
            children.emplace_back(ILClone(sourceColumn->entries[index].child.get()));
        }
    }
    Pidl targetFolder = DragTargetFolder(targetColumn, targetEntry);
    if (children.empty() || !targetFolder) {
        return true;
    }
    DWORD keyState = MK_LBUTTON;
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        keyState |= MK_CONTROL;
    }
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        keyState |= MK_SHIFT;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        keyState |= MK_ALT;
    }
    if (fromArchive) {
        keyState = MK_LBUTTON | MK_CONTROL;  // Taken out of an archive: always a copy.
    }
    // After the event has finished, since the shell may show a dialog.
    auto shared = std::make_shared<std::tuple<Pidl, std::vector<Pidl>, Pidl>>(
        std::move(sourceFolder), std::move(children), std::move(targetFolder));
    state->queue.TryEnqueue([shared, keyState]() {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->on || g_unloading) {
            return;
        }
        try {
            DropItems(now, std::move(std::get<0>(*shared)), std::move(std::get<1>(*shared)),
                      std::move(std::get<2>(*shared)), keyState);
        } catch (...) {
            LogHresult(L"dropping items");
        }
    });
    return true;
}

// ---- Drops coming into the columns, and the scroll while dragging ----
//
// Files dragged in from another Explorer window, the desktop or another
// application land on the panel's window. The panel registers its own OLE drop
// target there and hands everything to the folder under the pointer: the folder
// row, or the column's own folder. The folder's own drop target decides what
// the pointer shows (move, copy, link, nothing) and does the work, with its own
// conflict and progress dialogs, exactly as for a drop in an Explorer view, so
// virtual items and long operations need nothing from us. The row or column it
// would land on is lit as in the drag of rows between columns.

// Whether a drop may go to this column or this folder row.
bool InboundTargetAllowed(WindowState* state, Column* column, int entry, const Pidl& target) {
    if (entry >= 0) {
        if ((size_t)entry >= column->entries.size()) {
            return false;
        }
        auto& hit = column->entries[entry];
        if (!hit.folder || hit.archive || hit.launcher) {
            return false;
        }
    } else if (ArchiveParentColumn(state, column) >= 0) {
        return false;
    }
    wchar_t path[MAX_PATH] = L"";
    return target && SHGetPathFromIDListW(target.get(), path) && path[0];  // A folder on disk.
}

// A point of the screen in the panel's host coordinates.
winrt::Windows::Foundation::Point HostPointOf(WindowState* state, POINTL pt) {
    HWND bridgeWindow = (HWND)state->bridge.WindowId().Value;
    POINT client = {pt.x, pt.y};
    ScreenToClient(bridgeWindow, &client);
    double scale = 1.0;
    if (auto xamlRoot = state->rootGrid.XamlRoot()) {
        scale = xamlRoot.RasterizationScale();
    }
    if (scale <= 0) {
        scale = 1.0;
    }
    return winrt::Windows::Foundation::Point{(float)(client.x / scale), (float)(client.y / scale)};
}

struct PanelDropTarget : IDropTarget {
    LONG refs = 1;
    IDataObject* data = nullptr;
    IDropTarget* folder = nullptr;  // The folder's own target, between its DragEnter and DragLeave.
    Pidl folderPidl;
    DWORD lastKeys = 0;
    DWORD lastButtons = 0;  // The button of the drag, as the last move saw it held.
    POINTL lastPoint{};
    DWORD lastWanted = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG now = InterlockedDecrement(&refs);
        if (now == 0) {
            delete this;
        }
        return (ULONG)now;
    }

    // Ends the folder's part of the drag, without a drop.
    void LeaveFolder() {
        if (folder) {
            folder->DragLeave();
            folder->Release();
            folder = nullptr;
        }
        folderPidl.reset();
    }
    void DropData() {
        if (data) {
            data->Release();
            data = nullptr;
        }
    }

    // The pointer is at `pt` (screen). Settles the target and gives the effect.
    void Settle(DWORD keys, POINTL pt, DWORD* effect) {
        lastKeys = keys;
        if (keys & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) {
            lastButtons = keys & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON);
        }
        lastPoint = pt;
        DWORD wanted = *effect;
        lastWanted = wanted;
        *effect = DROPEFFECT_NONE;
        WindowState* state = StateForThisThread(false);
        if (!state || !state->on || !state->bridge || !state->rootGrid || !data) {
            LeaveFolder();
            return;
        }
        Column* column = nullptr;
        int entry = -1;
        bool found = false;
        try {
            winrt::Windows::Foundation::Point where = HostPointOf(state, pt);
            state->autoPoint = where;
            state->autoOn = true;
            found = DragTargetAt(state, where, &column, &entry);
        } catch (...) {
            LogHresult(L"finding the drop target of a drop coming in");
        }
        Pidl target;
        if (found && column) {
            target = DragTargetFolder(column, entry);
            if (!InboundTargetAllowed(state, column, entry, target)) {
                target.reset();
            }
        }
        if (!target) {
            LeaveFolder();
            DragClearTarget(state);
            return;
        }
        if (!folder || !folderPidl || !ILIsEqual(folderPidl.get(), target.get())) {
            LeaveFolder();
            IShellFolder* shellFolder = nullptr;
            IDropTarget* created = nullptr;
            if (SUCCEEDED(SHBindToObject(nullptr, target.get(), nullptr, IID_PPV_ARGS(&shellFolder))) &&
                shellFolder) {
                shellFolder->CreateViewObject(state->topLevel, IID_PPV_ARGS(&created));
                shellFolder->Release();
            }
            if (!created) {
                DragClearTarget(state);
                return;
            }
            DWORD offered = wanted;
            if (FAILED(created->DragEnter(data, keys, pt, &offered))) {
                created->Release();
                DragClearTarget(state);
                return;
            }
            folder = created;
            folderPidl = std::move(target);
            *effect = offered;
        } else {
            DWORD offered = wanted;
            if (SUCCEEDED(folder->DragOver(keys, pt, &offered))) {
                *effect = offered;
            }
        }
        auto& d = state->drag;
        bool ok = *effect != DROPEFFECT_NONE;
        if (column != d.targetColumn || entry != d.targetEntry || ok != d.targetOk) {
            DragClearTarget(state);
            d.targetColumn = column;
            d.targetEntry = entry;
            d.targetOk = ok;
            if (ok) {
                DragLight(state, column, entry);
            }
        } else if (ok) {
            DragPlaceFrame(state);
        }
    }

    IDropTargetHelper* helper = nullptr;  // Draws the image of a drag that comes from elsewhere.
    bool helperActive = false;

    // The label: our own drag coming back gets the label it had in the panel, a
    // drag from elsewhere gets the shell's image, by its helper.
    void Over(DWORD keys, POINTL pt, DWORD* effect) {
        Settle(keys, pt, effect);
        WindowState* state = StateForThisThread(false);
        if (!state || !state->on || !state->bridge || !state->rootGrid) {
            return;
        }
        try {
            if (state->outDragActive) {
                if (!state->drag.ghost) {
                    ShowGhost(state, state->outSpec);
                }
                auto where = HostPointOf(state, pt);
                muxc::Canvas::SetLeft(state->drag.ghost, where.X + 16);
                muxc::Canvas::SetTop(state->drag.ghost, where.Y + 16);
                DragUpdateLabel(state);
                state->drag.ghost.Opacity(state->drag.targetOk || !state->drag.targetColumn ? 1.0 : 0.8);
            } else if (helperActive && helper) {
                POINT p = {pt.x, pt.y};
                helper->DragOver(&p, *effect);
            }
        } catch (...) {
            LogHresult(L"the label of a drag coming in");
        }
    }
    void HideLabel() {
        if (helper && helperActive) {
            helper->DragLeave();
        }
        helperActive = false;
        if (WindowState* state = StateForThisThread(false)) {
            if (state->drag.ghost) {
                RemoveGhost(state);
            }
        }
    }

    // After a scroll under a pointer that did not move: judge the target again.
    void Refresh() {
        if (!data) {
            return;
        }
        DWORD effect = lastWanted;
        Over(lastKeys, lastPoint, &effect);
    }

    STDMETHODIMP DragEnter(IDataObject* object, DWORD keys, POINTL pt, DWORD* effect) override {
        DropData();
        lastButtons = 0;
        data = object;
        if (data) {
            data->AddRef();
        }
        WindowState* state = StateForThisThread(false);
        if (state && state->on && !state->outDragActive && object) {
            if (!helper) {
                CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper));
            }
            if (helper && state->bridge) {
                POINT p = {pt.x, pt.y};
                helperActive = SUCCEEDED(helper->DragEnter((HWND)state->bridge.WindowId().Value, object, &p,
                                                           *effect));
            }
        }
        Over(keys, pt, effect);
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD keys, POINTL pt, DWORD* effect) override {
        Over(keys, pt, effect);
        return S_OK;
    }
    STDMETHODIMP DragLeave() override {
        HideLabel();
        LeaveFolder();
        DropData();
        if (WindowState* state = StateForThisThread(false)) {
            DragClearTarget(state);
            state->autoOn = false;
        }
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* object, DWORD keys, POINTL pt, DWORD* effect) override {
        if (!data && object) {
            data = object;
            data->AddRef();
        }
        DWORD wanted = *effect;
        Over(keys, pt, effect);
        WindowState* state = StateForThisThread(false);
        if (helper && helperActive) {
            POINT p = {pt.x, pt.y};
            helper->Drop(data, &p, *effect);
            helperActive = false;
        }
        if (state && state->drag.ghost) {
            RemoveGhost(state);
        }
        if (folder && *effect != DROPEFFECT_NONE) {
            // At the drop the button is already up, and a folder's drop target takes a
            // key state with no button for a drag of the right button: it asks
            // "Copy here, Move here, Cancel". The button of the drag is the one the
            // last move had; the modifiers are those of the drop.
            DWORD buttons = lastButtons;
            DWORD dropKeys = (keys & ~(DWORD)(MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) | (buttons ? buttons : MK_LBUTTON);
            DWORD result = wanted;
            HRESULT hr = folder->Drop(data, dropKeys, pt, &result);
            Out(L"drop coming in on \"%s\": effect %lu, hr=0x%08lX", PathOf(folderPidl.get()).c_str(),
                (unsigned long)result, hr);
            *effect = result;
            folder->Release();  // The drop ends the folder's part: no DragLeave.
            folder = nullptr;
            folderPidl.reset();
        } else {
            LeaveFolder();
            *effect = DROPEFFECT_NONE;
        }
        DropData();
        if (state) {
            DragClearTarget(state);
            state->autoOn = false;
            ReloadAll(state);
        }
        return S_OK;
    }
};

void RegisterPanelDropTarget(WindowState* state) {
    if (state->panelDrop || !state->bridge) {
        return;
    }
    HWND window = (HWND)state->bridge.WindowId().Value;
    auto* target = new PanelDropTarget();
    HRESULT hr = RegisterDragDrop(window, target);
    if (hr == DRAGDROP_E_ALREADYREGISTERED) {
        RevokeDragDrop(window);  // The XAML island's own, which nothing here uses.
        hr = RegisterDragDrop(window, target);
    }
    if (FAILED(hr)) {
        Out(L"the panel could not take drops coming in, hr=0x%08lX", hr);
        target->Release();
        return;
    }
    state->panelDrop = target;
    state->panelDropWindow = window;
    Out(L"the panel takes drops coming in, window %p", window);
}

void RevokePanelDropTarget(WindowState* state) {
    if (!state->panelDrop) {
        return;
    }
    auto* target = static_cast<PanelDropTarget*>(state->panelDrop);
    state->panelDrop = nullptr;
    if (IsWindow(state->panelDropWindow)) {
        RevokeDragDrop(state->panelDropWindow);
    }
    state->panelDropWindow = nullptr;
    target->HideLabel();
    target->LeaveFolder();
    target->DropData();
    if (target->helper) {
        target->helper->Release();
        target->helper = nullptr;
    }
    target->Release();
}

// While something is dragged over the panel, a column near its top or bottom
// edge scrolls, and the strip near its left or right edge. Run by the tick; the
// speed grows as the pointer nears (or passes) the edge.
void DragAutoScroll(WindowState* state) {
    if (!state->autoOn || !state->rootGrid || !state->scroller) {
        return;
    }
    const double edge = 48, fastest = 28;
    auto at = state->autoPoint;
    bool scrolled = false;
    try {
        double width = state->rootGrid.ActualWidth();
        double dx = 0;
        if (at.X < edge) {
            dx = -std::min(1.0, (edge - at.X) / edge) * fastest;
        } else if (at.X > width - edge) {
            dx = std::min(1.0, (at.X - (width - edge)) / edge) * fastest;
        }
        if (dx != 0) {
            double offset = state->scroller.HorizontalOffset();
            double target = std::clamp(offset + dx, 0.0, state->scroller.ScrollableWidth());
            if (std::abs(target - offset) > 0.5) {
                state->scroller.ChangeView(target, nullptr, nullptr, true);
                scrolled = true;
            }
        }
        Column* column = nullptr;
        int entry = -1;
        if (DragTargetAt(state, at, &column, &entry) && column && column->list) {
            auto scroller = FindListScroller(column->list, 0);
            if (scroller) {
                double top = column->list.TransformToVisual(state->rootGrid)
                                 .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                                 .Y;
                double height = column->list.ActualHeight();
                double dy = 0;
                if (at.Y < top + edge) {
                    dy = -std::min(1.0, (top + edge - at.Y) / edge) * fastest;
                } else if (at.Y > top + height - edge) {
                    dy = std::min(1.0, (at.Y - (top + height - edge)) / edge) * fastest;
                }
                if (dy != 0) {
                    double offset = scroller.VerticalOffset();
                    double target = std::clamp(offset + dy, 0.0, scroller.ScrollableHeight());
                    if (std::abs(target - offset) > 0.5) {
                        scroller.ChangeView(nullptr, target, nullptr, true);
                        scrolled = true;
                    }
                }
            }
        }
    } catch (...) {
        LogHresult(L"scrolling while dragging");
        return;
    }
    if (scrolled) {
        // What is under the pointer has moved.
        if (state->drag.active) {
            DragRefreshTarget(state, at);
        } else if (state->panelDrop) {
            static_cast<PanelDropTarget*>(state->panelDrop)->Refresh();
        }
    }
}

// A folder row held under dragged items, rows of the panel or files from
// elsewhere, opens in the next column after a pause, so a drop can go deeper
// without letting go. One opening per hover.
void SpringLoadTick(WindowState* state) {
    auto& d = state->drag;
    bool dragging = state->autoOn && g_springLoad;
    if (!dragging || !d.targetOk || !d.targetColumn || d.targetEntry < 0) {
        state->springColumn = nullptr;
        state->springEntry = -1;
        state->springDone = false;
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (state->springColumn != d.targetColumn || state->springEntry != d.targetEntry) {
        state->springColumn = d.targetColumn;
        state->springEntry = d.targetEntry;
        state->springSince = now;
        state->springDone = false;
        return;
    }
    if (state->springDone || now - state->springSince < 700) {
        return;
    }
    state->springDone = true;
    Column* column = d.targetColumn;
    int entry = d.targetEntry;
    try {
        if ((size_t)entry >= column->entries.size() || !column->entries[entry].folder ||
            column->entries[entry].archive || column->entries[entry].launcher) {
            return;
        }
        int index = -1;
        for (size_t i = 0; i < state->columns.size(); i++) {
            if (state->columns[i].get() == column) {
                index = (int)i;
            }
        }
        if (index < 0) {
            return;
        }
        Pidl folder = DragTargetFolder(column, entry);
        if (index + 1 < (int)state->columns.size() && folder &&
            ILIsEqual(state->columns[index + 1]->folder.get(), folder.get())) {
            return;  // Already open there.
        }
        Out(L"a folder held under a drag opens: \"%s\"", column->entries[entry].name.c_str());
        SelectChildInColumn(state, column, column->entries[entry].child.get());
        OnRowSelected(column->list);
    } catch (...) {
        LogHresult(L"opening a folder held under a drag");
    }
}
// ---- View menu entry ----

// The icon of Columns, drawn from assets/columns.svg (16 x 12, one filled
// shape), as a path icon that takes the colour of the toolbar's text.
muxc::IconElement MakeColumnsIcon() {
    return winrt::Microsoft::UI::Xaml::Markup::XamlReader::Load(
               L"<PathIcon xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
               L"HorizontalAlignment='Center' VerticalAlignment='Center' Foreground='{ThemeResource TextFillColorSecondaryBrush}' Data='F1 M13.0625 0C13.4531 0 13.8255 0.0807292 14.1797 0.242188C14.5391 0.398438 14.8516 0.611979 15.1172 0.882812C15.388 1.14844 15.6016 1.46094 15.7578 1.82031C15.9193 2.17448 16 2.54688 16 2.9375V9.0625C16 9.45312 15.9193 9.82812 15.7578 10.1875C15.6016 10.5417 15.388 10.8542 15.1172 11.125C14.8516 11.3906 14.5391 11.6042 14.1797 11.7656C13.8255 11.9219 13.4531 12 13.0625 12H2.9375C2.54688 12 2.17188 11.9219 1.8125 11.7656C1.45833 11.6042 1.14583 11.3906 0.875 11.125C0.609375 10.8542 0.395833 10.5417 0.234375 10.1875C0.078125 9.82812 0 9.45312 0 9.0625V2.9375C0 2.54688 0.078125 2.17448 0.234375 1.82031C0.395833 1.46094 0.609375 1.14844 0.875 0.882812C1.14583 0.611979 1.45833 0.398438 1.8125 0.242188C2.17188 0.0807292 2.54688 0 2.9375 0H13.0625ZM11 11H13.0391C13.2995 11 13.5469 10.9479 13.7812 10.8438C14.0208 10.7344 14.2292 10.5911 14.4062 10.4141C14.5885 10.2318 14.7318 10.0234 14.8359 9.78906C14.9453 9.54948 15 9.29948 15 9.03906V2.96094C15 2.70052 14.9453 2.45312 14.8359 2.21875C14.7318 1.97917 14.5885 1.77083 14.4062 1.59375C14.2292 1.41146 14.0208 1.26823 13.7812 1.16406C13.5469 1.05469 13.2995 1 13.0391 1H11V11ZM6 11H10V1H6V11ZM2.96094 1C2.70052 1 2.45052 1.05469 2.21094 1.16406C1.97656 1.26823 1.76823 1.41146 1.58594 1.59375C1.40885 1.77083 1.26562 1.97917 1.15625 2.21875C1.05208 2.45312 1 2.70052 1 2.96094V9.03906C1 9.29948 1.05208 9.54948 1.15625 9.78906C1.26562 10.0234 1.40885 10.2318 1.58594 10.4141C1.76823 10.5911 1.97656 10.7344 2.21094 10.8438C2.45052 10.9479 2.70052 11 2.96094 11H5V1H2.96094Z' />")
        .as<muxc::IconElement>();
}

void OpenSettingsWindow(WindowState* state);

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
        if (state->settingsItem) {
            state->settingsItem.Click(state->settingsClickToken);
            uint32_t at = 0;
            if (state->overflowBar && state->overflowBar.SecondaryCommands().IndexOf(state->settingsItem, at)) {
                state->overflowBar.SecondaryCommands().RemoveAt(at);
            }
            state->settingsItem = nullptr;
            state->overflowBar = nullptr;
        }
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
        bool columnsOn = g_enabled;
        auto mine = state->menuItem;
        if (mine) {
            uint32_t index = 0;
            if (state->menuItems == items && items.IndexOf(mine, index)) {
                return;
            }
            // The menu was rebuilt. Start again on the new one.
            RemoveMenuEntry(state);
        }

        winrt::hstring groupName;
        if (auto first = items.GetAt(0).try_as<muxc::RadioMenuFlyoutItem>()) {
            groupName = first.GroupName();
        }

        if (columnsOn) {
            muxc::RadioMenuFlyoutItem item;
            item.Text(L"Columns");
            if (!groupName.empty()) {
                item.GroupName(groupName);
            }
            item.Icon(MakeColumnsIcon());
            item.Name(L"WindhawkColumnsItem");
            item.IsChecked(state->on);
            state->menuClickToken = item.Click(
                [](Inspectable const&, mux::RoutedEventArgs const&) {
                    WindowState* state = StateForThisThread(false);
                    if (state && !g_unloading) {
                        Out(L"Columns chosen in the View menu");
                        ChooseColumns(state);
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
                            LeaveColumns(state);
                        }
                        RememberColumns(false);
                    });
                state->siblings.push_back(std::move(hook));
            }

            // Right after the eight layouts, before the separator.
            items.InsertAt(8, item);
            state->menuItem = item;
            state->menuItems = items;

        }

        Out(L"Columns entry added to the View menu, window=%p", state->topLevel);
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
                    if (state->viewButton != button) {
                        RestoreViewButtonIcon(state);
                        state->viewButton = button;
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

// ---- The "..." menu ----
//
// The toolbar's "..." button has no flyout: it opens the command bar's overflow, the
// secondary commands of the CommandBar named FileExplorerCommandBar (Select all, Select
// none, Invert selection, Properties, Options). "Extras" is one more secondary command,
// last, under Options.

muxc::CommandBar FindCommandBar(mux::DependencyObject const& node, int depth, int& visited) {
    if (!node || depth > 40 || visited > 3000) {
        return nullptr;
    }
    visited++;
    if (auto bar = node.try_as<muxc::CommandBar>()) {
        return bar;
    }
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        if (auto found = FindCommandBar(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1, visited)) {
            return found;
        }
    }
    return nullptr;
}

void AddOverflowEntry(WindowState* state, mux::DependencyObject const& root) {
    try {
        muxc::CommandBar bar = state->overflowBar;
        if (!bar) {
            int visited = 0;
            bar = FindCommandBar(root, 0, visited);
        }
        if (!bar) {
            return;
        }
        auto commands = bar.SecondaryCommands();
        uint32_t at = 0;
        if (state->settingsItem && state->overflowBar == bar && commands.IndexOf(state->settingsItem, at)) {
            return;  // Already there.
        }
        if (!state->settingsItem) {
            muxc::AppBarButton item;
            item.Label(L"Extras");
            muxc::FontIcon icon;
            icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            icon.Glyph(L"");  // ExploitProtection.
            item.Icon(icon);
            item.Name(L"WindhawkSettingsItem");
            state->settingsClickToken = item.Click([](Inspectable const&, mux::RoutedEventArgs const&) {
                WindowState* now = StateForThisThread(false);
                if (!now || g_unloading || !now->queue) {
                    return;
                }
                // After the menu has closed and the click has been handled.
                try {
                    now->queue.TryEnqueue([]() {
                        WindowState* later = StateForThisThread(false);
                        if (later && !g_unloading) {
                            OpenSettingsWindow(later);
                        }
                    });
                } catch (...) {
                }
            });
            state->settingsItem = item;
        }
        commands.Append(state->settingsItem);
        state->overflowBar = bar;
        Out(L"the Extras entry is in the \"...\" menu, window=%p", state->topLevel);
    } catch (...) {
        LogHresult(L"AddOverflowEntry");
    }
}

void OnCommandBarPointer(Inspectable const& sender) {
    if (g_unloading) {
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
        if (g_enabled) {
            int visited = 0;
            WalkForViewFlyout(state, root, 0, visited);
        }
        AddOverflowEntry(state, root);
    } catch (...) {
        LogHresult(L"OnCommandBarPointer");
    }
}


// ---- The icon of the View button ----

// The icon the button shows now: the first icon element in its content.
mux::UIElement FindIconElement(mux::DependencyObject const& node, int depth, int& visited) {
    if (!node || depth > 12 || visited > 400) {
        return nullptr;
    }
    visited++;
    std::wstring name = ClassOf(node);
    if (name.find(L"FontIcon") != std::wstring::npos || name.find(L"PathIcon") != std::wstring::npos ||
        name.find(L"ImageIcon") != std::wstring::npos || name.find(L"BitmapIcon") != std::wstring::npos) {
        return node.try_as<mux::UIElement>();
    }
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        if (auto found = FindIconElement(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1, visited)) {
            return found;
        }
    }
    return nullptr;
}

void LogTree(mux::DependencyObject const& node, int depth) {
    if (!node || depth > 8) {
        return;
    }
    Out(L"view button tree: %*s%s", depth * 2, L"", ClassOf(node).c_str());
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        LogTree(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1);
    }
}

// Puts the original icon back.
void RestoreViewButtonIcon(WindowState* state) {
    if (!state->viewIconShown) {
        return;
    }
    state->viewIconShown = false;
    try {
        auto original = state->viewIconOriginal;
        auto ours = state->viewIconOurs;
        if (original && ours) {
            if (state->viewIconWhere == 4) {
                if (auto bar = state->viewButton.try_as<muxc::AppBarButton>()) {
                    bar.Icon(original.as<muxc::IconElement>());
                }
            } else if (state->viewIconWhere == 1 && state->viewButton) {
                state->viewButton.Content(original);
            } else if (auto parent = mux::Media::VisualTreeHelper::GetParent(ours)) {
                if (auto panel = parent.try_as<muxc::Panel>()) {
                    uint32_t index = 0;
                    if (panel.Children().IndexOf(ours, index)) {
                        panel.Children().SetAt(index, original);
                    }
                } else if (auto border = parent.try_as<muxc::Border>()) {
                    border.Child(original);
                }
            }
        }
    } catch (...) {
        LogHresult(L"restoring the View button icon");
    }
    state->viewIconOriginal = nullptr;
    state->viewIconOurs = nullptr;
}

// While Columns shows, the View button wears the Columns icon: Explorer does not
// know the layout and keeps the icon of the last one it set.
void SyncViewButtonIcon(WindowState* state) {
    bool want = state->on && !state->softHidden && state->viewButton;
    if (state->viewIconShown) {
        // The button may have been rebuilt, taking our icon with it.
        bool gone = true;
        try {
            gone = !state->viewIconOurs ||
                   (state->viewIconWhere != 1 && state->viewIconWhere != 4 &&
                    !mux::Media::VisualTreeHelper::GetParent(state->viewIconOurs));
            if (state->viewIconWhere == 1 && state->viewButton) {
                gone = state->viewButton.Content() != state->viewIconOurs;
            } else if (state->viewIconWhere == 4) {
                auto bar = state->viewButton.try_as<muxc::AppBarButton>();
                gone = !bar || !(bar.Icon() == state->viewIconOurs.try_as<muxc::IconElement>());
            }
        } catch (...) {
        }
        if (gone) {
            state->viewIconShown = false;
            state->viewIconOriginal = nullptr;
            state->viewIconOurs = nullptr;
        }
    }
    if (!want) {
        RestoreViewButtonIcon(state);
        return;
    }
    if (state->viewIconShown) {
        return;
    }
    try {
        auto button = state->viewButton;
        mux::UIElement original{nullptr};
        int where = 0;
        auto content = button.Content();
        if (auto bar = button.try_as<muxc::AppBarButton>(); bar && bar.Icon()) {
            // The toolbar's button: its Icon property is what shows.
            original = bar.Icon();
            where = 4;
        } else if (content && ClassOf(content).find(L"Icon") != std::wstring::npos) {
            original = content.try_as<mux::UIElement>();
            where = 1;
        } else {
            int visited = 0;
            original = FindIconElement(button, 0, visited);
            if (original) {
                auto parent = mux::Media::VisualTreeHelper::GetParent(original);
                where = parent.try_as<muxc::Panel>() ? 2 : parent.try_as<muxc::Border>() ? 3 : 0;
            }
        }
        if (!original || !where) {
            if (!state->viewIconLogged) {
                state->viewIconLogged = true;
                Out(L"the View button has no icon this mod can replace, its tree:");
                LogTree(button, 0);
            }
            return;
        }
        auto ours = MakeColumnsIcon();
        auto frame = ours.as<mux::FrameworkElement>();
        if (auto source = original.try_as<mux::FrameworkElement>()) {
            frame.Margin(source.Margin());
            frame.HorizontalAlignment(source.HorizontalAlignment());
            frame.VerticalAlignment(source.VerticalAlignment());
        }
        if (where == 4) {
            // As wide as the icon it replaces, and as high as its shape asks.
            double width = 16;
            if (auto source = original.try_as<mux::FrameworkElement>()) {
                if (source.ActualWidth() > 1) {
                    width = source.ActualWidth();
                }
                Out(L"the View button's icon is %.1f x %.1f", source.ActualWidth(), source.ActualHeight());
            }
            // The toolbar scales the icon to fill its box, so the box must be the
            // one the original has (square); the drawing is then made smaller inside
            // it, to the width of the glyphs around it.
            frame.Width(width);
            frame.Height(width);
            // The drawing is 16 x 12 in a square box and sits at the top of it:
            // moved down by half the difference, so it is centred like the others.
            mux::Media::CompositeTransform inside;
            inside.ScaleX(0.86);
            inside.ScaleY(0.86);
            inside.TranslateY(2.5);
            frame.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
            frame.RenderTransform(inside);
            button.as<muxc::AppBarButton>().Icon(ours);
        } else if (where == 1) {
            button.Content(ours);
        } else {
            auto parent = mux::Media::VisualTreeHelper::GetParent(original);
            if (where == 2) {
                auto panel = parent.as<muxc::Panel>();
                uint32_t index = 0;
                if (!panel.Children().IndexOf(original, index)) {
                    return;
                }
                panel.Children().SetAt(index, ours);
            } else {
                parent.as<muxc::Border>().Child(ours);
            }
        }
        state->viewIconOriginal = original;
        state->viewIconOurs = ours;
        state->viewIconWhere = where;
        state->viewIconShown = true;
        Out(L"the View button shows the Columns icon (replaced a %s)", ClassOf(original).c_str());
    } catch (...) {
        LogHresult(L"showing the Columns icon on the View button");
    }
}

// ---- Hover while the keyboard is in use ----

// The rows ignore the pointer, so none keeps a hover look because the pointer
// rests where it was. The tick gives them back when the pointer moves.
void SetRowsHitTest(WindowState* state, bool on) {
    for (auto& column : state->columns) {
        if (!column->list) {
            continue;
        }
        try {
            auto items = column->list.Items();
            for (uint32_t i = 0; i < items.Size(); i++) {
                if (auto row = items.GetAt(i).try_as<muxc::ListViewItem>()) {
                    row.IsHitTestVisible(on);
                }
            }
        } catch (...) {
        }
    }
}

void HoverOff(WindowState* state) {
    if (state->kbHoverOff) {
        return;
    }
    state->kbHoverOff = true;
    GetCursorPos(&state->kbCursor);
    SetRowsHitTest(state, false);
}

void HoverOn(WindowState* state) {
    if (!state->kbHoverOff) {
        return;
    }
    state->kbHoverOff = false;
    SetRowsHitTest(state, true);
}

void HoverTick(WindowState* state) {
    if (!state->kbHoverOff) {
        return;
    }
    POINT now;
    if (GetCursorPos(&now) && (std::abs(now.x - state->kbCursor.x) > 2 || std::abs(now.y - state->kbCursor.y) > 2)) {
        HoverOn(state);
    }
}

// The last column, when it is empty, has no right edge and no handle to drag:
// there is nothing beside it to resize against.
void UpdateLastEdge(WindowState* state) {
    for (size_t i = 0; i < state->columns.size(); i++) {
        auto& column = state->columns[i];
        if (!column->grip) {
            continue;
        }
        bool last = i + 1 == state->columns.size();
        bool hide = last && column->filled && column->entries.empty();
        auto wanted = hide ? mux::Visibility::Collapsed : mux::Visibility::Visible;
        try {
            if (column->grip.Visibility() != wanted) {
                column->grip.Visibility(wanted);
            }
            // The last column has no separator line to its right, whatever it holds;
            // the handle to resize it still shows when the pointer is on the edge.
            if (auto content = column->grip.Child().try_as<muxc::Panel>()) {
                if (content.Children().Size() > 0) {
                    content.Children().GetAt(0).Opacity(last ? 0.0 : 1.0);
                }
            }
        } catch (...) {
        }
    }
}

void LogRowTree(mux::DependencyObject const& node, int depth) {
    if (!node || depth > 6) {
        return;
    }
    std::wstring extra;
    if (auto element = node.try_as<mux::FrameworkElement>()) {
        wchar_t text[96];
        swprintf(text, 96, L" %.0fx%.0f margin %.1f,%.1f,%.1f,%.1f", element.ActualWidth(), element.ActualHeight(),
                 element.Margin().Left, element.Margin().Top, element.Margin().Right, element.Margin().Bottom);
        extra = text;
    }
    Out(L"row tree: %*s%s%s", depth * 2, L"", ClassOf(node).c_str(), extra.c_str());
    int children = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < children; i++) {
        LogRowTree(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1);
    }
}

// ---- The settings window ----
//
// A top level window on the UI thread of the Explorer window that opened it, with
// a XAML island over it: a sidebar of pages on the left, a scrolling page on the
// right with a header, a description and cards of options. Mica behind it, as in
// Windows Settings. One window only. Spec: SPEC-settings-shell.md.
//
// In this version the Columns page is read only: it shows the settings as they are.

extern bool g_hooked;  // Defined with the hooks, later.
void LoadSettings();
void CloseEverything();
void ReloadAll(WindowState* state);

constexpr wchar_t kSettingsClass[] = L"ExplorerExtrasSettingsWindow";

struct SettingsUi {
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    muxc::Grid root{nullptr};
    muxc::ListView nav{nullptr};
    muxc::ScrollViewer content{nullptr};
    int page = 0;
    bool abandon = false;  // Let the XAML objects go without releasing them.
};

HWND g_settingsWindow = nullptr;
DWORD g_settingsThread = 0;
SettingsUi* g_settingsUi = nullptr;
bool g_settingsClassMade = false;
std::atomic<bool> g_settingsAbandon{false};  // The mod is unloading.
bool g_settingsBuilding = false;  // A page is being built: its controls change nothing.

constexpr wchar_t kXamlNs[] = L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'";

winrt::Windows::Foundation::IInspectable LoadXaml(std::wstring const& markup) {
    return winrt::Microsoft::UI::Xaml::Markup::XamlReader::Load(markup);
}

muxc::TextBlock MakeText(std::wstring const& text, double size, bool semibold, bool secondary) {
    std::wstring markup = L"<TextBlock " + std::wstring(kXamlNs) + L" TextWrapping='Wrap'";
    if (secondary) {
        markup += L" Foreground='{ThemeResource TextFillColorSecondaryBrush}'";
    }
    markup += L"/>";
    auto block = LoadXaml(markup).as<muxc::TextBlock>();
    block.Text(text);
    block.FontSize(size);
    if (semibold) {
        block.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    }
    return block;
}

muxc::Border MakeCard() {
    return LoadXaml(std::wstring(L"<Border ") + kXamlNs +
                    L" Background='{ThemeResource CardBackgroundFillColorDefaultBrush}'"
                    L" BorderBrush='{ThemeResource CardStrokeColorDefaultBrush}'"
                    L" BorderThickness='1' CornerRadius='8' Padding='16,12'/>")
        .as<muxc::Border>();
}

// A line of a card: a name with its description on the left, a control on the right.
muxc::Grid MakeSettingRow(std::wstring const& name, std::wstring const& description,
                          mux::UIElement const& control) {
    muxc::Grid row;
    muxc::ColumnDefinition textColumn, controlColumn;
    textColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
    controlColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
    row.ColumnDefinitions().Append(textColumn);
    row.ColumnDefinitions().Append(controlColumn);
    row.ColumnSpacing(24);
    row.Padding(mux::Thickness{0, 8, 0, 8});
    muxc::StackPanel texts;
    texts.VerticalAlignment(mux::VerticalAlignment::Center);
    texts.Children().Append(MakeText(name, 14, false, false));
    if (!description.empty()) {
        auto detail = MakeText(description, 12, false, true);
        detail.Margin(mux::Thickness{0, 2, 0, 0});
        texts.Children().Append(detail);
    }
    muxc::Grid::SetColumn(texts, 0);
    row.Children().Append(texts);
    if (control) {
        auto frame = control.as<mux::FrameworkElement>();
        frame.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(frame, 1);
        row.Children().Append(control);
    }
    return row;
}

// ---- Applying a setting chosen in the window ----

// What a window does when a setting changed that affects columns already built:
// the widths the user has not set are fitted again, and the folders are read again.
void RebuildForSettings() {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        for (auto& column : state->columns) {
            if (column->list && column->filled && !column->userSized) {
                FitWidth(column.get());
                RefreshNames(column.get());
            }
        }
        ReloadAll(state);
        UpdatePill(state, false);
    } catch (...) {
        LogHresult(L"rebuilding after a setting");
    }
}

void RebuildEverywhere() {
    std::vector<mud::DispatcherQueue> queues;
    {
        std::lock_guard<std::mutex> lock(g_statesMutex);
        for (auto& state : g_states) {
            if (state->queue) {
                queues.push_back(state->queue);
            }
        }
    }
    for (auto& queue : queues) {
        try {
            queue.TryEnqueue([]() { RebuildForSettings(); });
        } catch (...) {
        }
    }
}

// Switched off from the window: every window is cleaned up from another thread, since
// the cleanup waits for this one to run its part.
void TurnColumnsOffEverywhere() {
    std::thread([]() { CloseEverything(); }).detach();
}

void ApplySettingChanged(const SettingInfo& info) {
    LoadSettings();
    Out(L"setting \"%s\" changed in the settings window, now \"%s\"", info.id, EffectiveText(&info).c_str());
    if (!g_enabled) {
        TurnColumnsOffEverywhere();
    } else if (info.rebuild) {
        RebuildEverywhere();
    }
}

void CommitSetting(std::wstring const& id, std::wstring const& text) {
    if (g_settingsBuilding || g_unloading) {
        return;
    }
    const SettingInfo* info = FindSetting(id.c_str());
    if (!info || EffectiveText(info) == text) {
        return;
    }
    WriteSettingOverride(info, text);
    ApplySettingChanged(*info);
}

void ShowSettingsPage(SettingsUi* ui, int page);

void ResetSettingsToWindhawk() {
    ClearAllOverrides();
    LoadSettings();
    Out(L"settings reset to Windhawk's page");
    if (!g_enabled) {
        TurnColumnsOffEverywhere();
    } else {
        RebuildEverywhere();
    }
    if (g_settingsUi) {
        ShowSettingsPage(g_settingsUi, g_settingsUi->page);
    }
}

// The control of a setting: it shows the value in use and writes the user's choice.
mux::UIElement MakeSettingControl(SettingInfo const& info) {
    switch (info.kind) {
        case SettingKind::Bool: {
            muxc::ToggleSwitch toggle;
            toggle.OnContent(winrt::box_value(L""));
            toggle.OffContent(winrt::box_value(L""));
            toggle.MinWidth(0);
            toggle.IsOn(SettingBool(info.id));
            std::wstring id = info.id;
            toggle.Toggled([id](Inspectable const& sender, mux::RoutedEventArgs const&) {
                if (auto changed = sender.try_as<muxc::ToggleSwitch>()) {
                    CommitSetting(id, changed.IsOn() ? L"1" : L"0");
                }
            });
            return toggle;
        }
        case SettingKind::Int: {
            muxc::NumberBox box;
            box.Minimum(info.minValue);
            box.Maximum(info.maxValue ? info.maxValue : 1000000);
            box.Value(SettingInt(info.id));
            box.Width(140);
            box.SpinButtonPlacementMode(muxc::NumberBoxSpinButtonPlacementMode::Compact);
            box.ValidationMode(muxc::NumberBoxValidationMode::InvalidInputOverwritten);
            std::wstring id = info.id;
            box.ValueChanged([id](muxc::NumberBox const&, muxc::NumberBoxValueChangedEventArgs const& args) {
                double value = args.NewValue();
                if (value == value) {  // Not a number while the box is cleared.
                    CommitSetting(id, std::to_wstring((int)value));
                }
            });
            return box;
        }
        case SettingKind::Enum: {
            muxc::ComboBox combo;
            std::wstring options = info.options;
            std::wstring current = SettingText(info.id);
            int selected = 0, index = 0;
            size_t at = 0;
            while (at <= options.size() && !options.empty()) {
                size_t end = options.find(L'|', at);
                std::wstring pair = options.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
                size_t equals = pair.find(L'=');
                if (equals != std::wstring::npos) {
                    combo.Items().Append(winrt::box_value(winrt::hstring{pair.substr(equals + 1)}));
                    if (pair.substr(0, equals) == current) {
                        selected = index;
                    }
                    index++;
                }
                if (end == std::wstring::npos) {
                    break;
                }
                at = end + 1;
            }
            combo.SelectedIndex(selected);
            combo.MinWidth(220);
            std::wstring id = info.id;
            std::wstring choices = info.options;
            combo.SelectionChanged([id, choices](Inspectable const& sender, muxc::SelectionChangedEventArgs const&) {
                auto box = sender.try_as<muxc::ComboBox>();
                int index = box ? box.SelectedIndex() : -1;
                if (index < 0) {
                    return;
                }
                size_t at = 0;
                for (int i = 0;; i++) {
                    size_t end = choices.find(L'|', at);
                    std::wstring pair = choices.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
                    if (i == index) {
                        size_t equals = pair.find(L'=');
                        if (equals != std::wstring::npos) {
                            CommitSetting(id, pair.substr(0, equals));
                        }
                        return;
                    }
                    if (end == std::wstring::npos) {
                        return;
                    }
                    at = end + 1;
                }
            });
            return combo;
        }
        default:
            break;
    }
    muxc::TextBlock text;
    text.Text(SettingText(info.id));
    return text;
}

muxc::StackPanel MakePageHeader(std::wstring const& title, std::wstring const& description) {
    muxc::StackPanel header;
    header.Spacing(4);
    header.Margin(mux::Thickness{0, 0, 0, 12});
    header.Children().Append(MakeText(title, 28, true, false));
    if (!description.empty()) {
        header.Children().Append(MakeText(description, 14, false, true));
    }
    return header;
}

std::wstring WindowsBuildText() {
    typedef LONG(WINAPI * RtlGetVersion_t)(OSVERSIONINFOW*);
    OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto get = (RtlGetVersion_t)GetProcAddress(ntdll, "RtlGetVersion")) {
            get(&info);
        }
    }
    return L"Windows build " + std::to_wstring(info.dwBuildNumber);
}

// ---- Quick actions ----

std::wstring DiagnosticsText() {
    std::wstring text = L"Explorer Extras ";
    text += WH_MOD_VERSION;
    text += L"\r\n" + WindowsBuildText() + L"\r\n";
    text += g_hooked ? L"Hooks: found\r\n" : L"Hooks: not found\r\n";
    {
        std::lock_guard<std::mutex> lock(g_statesMutex);
        text += L"Explorer windows known: " + std::to_wstring(g_states.size()) + L"\r\n";
    }
    text += L"Settings (an asterisk marks one set in the settings window):\r\n";
    for (size_t i = 0; i < kSettingCount; i++) {
        auto const& info = kSettings[i];
        text += L"  ";
        text += SettingIsOverridden(&info) ? L"* " : L"  ";
        text += info.id;
        text += L" = " + EffectiveText(&info) + L"\r\n";
    }
    text += g_outPath[0] ? std::wstring(L"Log: ") + g_outPath + L"\r\n" : L"Log: none (the log folder does not exist)\r\n";
    return text;
}

bool CopyTextToClipboard(HWND owner, std::wstring const& text) {
    if (!OpenClipboard(owner)) {
        return false;
    }
    bool done = false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* target = GlobalLock(memory)) {
            memcpy(target, text.c_str(), bytes);
            GlobalUnlock(memory);
            done = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        }
        if (!done) {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    return done;
}

// What's new and the tip.
bool TipDismissed() {
    return Wh_GetIntValue(L"ui.tipDismissed", 0) != 0;
}

mux::UIElement BuildHomePage() {
    muxc::StackPanel page;
    page.Spacing(8);
    page.Children().Append(MakePageHeader(L"Explorer Extras", L"Optional additions to File Explorer, each one with its own page and switch."));

    // A tip, until it is dismissed.
    if (!TipDismissed()) {
        auto card = MakeCard();
        muxc::Grid line;
        muxc::ColumnDefinition textColumn, buttonColumn;
        textColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        buttonColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
        line.ColumnDefinitions().Append(textColumn);
        line.ColumnDefinitions().Append(buttonColumn);
        line.ColumnSpacing(16);
        muxc::StackPanel texts;
        texts.Children().Append(MakeText(L"Tip", 14, true, false));
        auto body = MakeText(L"Choose Columns in the View menu of a File Explorer window. The Extras entry just below it opens this window.", 13, false, true);
        body.Margin(mux::Thickness{0, 2, 0, 0});
        texts.Children().Append(body);
        muxc::Grid::SetColumn(texts, 0);
        line.Children().Append(texts);
        muxc::Button gotIt;
        gotIt.Content(winrt::box_value(L"Got it"));
        gotIt.VerticalAlignment(mux::VerticalAlignment::Center);
        gotIt.Click([card](Inspectable const&, mux::RoutedEventArgs const&) {
            Wh_SetIntValue(L"ui.tipDismissed", 1);
            card.Visibility(mux::Visibility::Collapsed);
        });
        muxc::Grid::SetColumn(gotIt, 1);
        line.Children().Append(gotIt);
        card.Child(line);
        page.Children().Append(card);
    }

    // The features.
    {
        auto title = MakeText(L"Features", 14, true, false);
        title.Margin(mux::Thickness{0, 8, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        const SettingInfo* enabled = FindSetting(L"enabled");
        muxc::ToggleSwitch toggle;
        toggle.OnContent(winrt::box_value(L""));
        toggle.OffContent(winrt::box_value(L""));
        toggle.MinWidth(0);
        toggle.IsOn(SettingBool(L"enabled"));
        toggle.Toggled([](Inspectable const& sender, mux::RoutedEventArgs const&) {
            if (auto changed = sender.try_as<muxc::ToggleSwitch>()) {
                CommitSetting(L"enabled", changed.IsOn() ? L"1" : L"0");
            }
        });
        card.Child(MakeSettingRow(L"Columns", enabled ? enabled->description : L"", toggle));
        page.Children().Append(card);
    }
    // Status and version.
    {
        auto title = MakeText(L"Status", 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        rows.Spacing(2);
        rows.Children().Append(MakeSettingRow(L"Version", L"", MakeText(WH_MOD_VERSION, 14, false, true)));
        rows.Children().Append(MakeSettingRow(L"Windows", L"", MakeText(WindowsBuildText(), 14, false, true)));
        rows.Children().Append(MakeSettingRow(L"Hooks", L"The Explorer extension the mod hooks into.",
                                              MakeText(g_hooked ? L"Found" : L"Not found", 14, false, true)));
        card.Child(rows);
        page.Children().Append(card);
    }
    // Quick actions, to come.
    {
        auto title = MakeText(L"Quick actions", 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        for (auto const& label : {L"Open the log", L"Copy diagnostics", L"Reset to Windhawk's settings"}) {
            muxc::Button button;
            button.Content(winrt::box_value(label));
            button.MinWidth(180);
            bool isReset = wcsncmp(label, L"Reset", 5) == 0;
            bool isLog = wcsncmp(label, L"Open", 4) == 0;
            std::wstring detail = isReset ? L"Every setting goes back to what Windhawk's page for the mod says."
                                  : isLog ? L"The file the mod writes while it works, if its folder exists."
                                          : L"Version, settings and state, as text, to paste in a message.";
            if (isReset) {
                button.Click([](Inspectable const&, mux::RoutedEventArgs const&) { ResetSettingsToWindhawk(); });
            } else if (isLog) {
                button.Click([](Inspectable const& sender, mux::RoutedEventArgs const&) {
                    bool opened = g_outPath[0] && GetFileAttributesW(g_outPath) != INVALID_FILE_ATTRIBUTES &&
                                  (INT_PTR)ShellExecuteW(nullptr, L"open", g_outPath, nullptr, nullptr, SW_SHOWNORMAL) > 32;
                    if (auto clicked = sender.try_as<muxc::Button>()) {
                        clicked.Content(winrt::box_value(opened ? L"Opened" : L"No log file"));
                    }
                });
            } else {
                button.Click([](Inspectable const& sender, mux::RoutedEventArgs const&) {
                    bool copied = CopyTextToClipboard(g_settingsWindow, DiagnosticsText());
                    if (auto clicked = sender.try_as<muxc::Button>()) {
                        clicked.Content(winrt::box_value(copied ? L"Copied" : L"Could not copy"));
                    }
                });
            }
            rows.Children().Append(MakeSettingRow(label, detail, button));
        }
        card.Child(rows);
        page.Children().Append(card);
    }
    // What's new.
    if (kWhatsNewCount) {
        auto title = MakeText(L"What's new", 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel items;
        items.Spacing(8);
        for (size_t i = 0; i < kWhatsNewCount; i++) {
            items.Children().Append(MakeText(kWhatsNew[i], 13, false, i > 0 ? true : false));
        }
        card.Child(items);
        page.Children().Append(card);
    }
    return page;
}

mux::UIElement BuildColumnsPage() {
    muxc::StackPanel page;
    page.Spacing(8);
    page.Children().Append(MakePageHeader(
        L"Columns", L"A Columns layout in the View menu: Miller columns drawn with Explorer's own controls."));
    {
        auto note = MakeCard();
        note.Child(MakeText(
            L"Changes apply at once. The mod keeps them in front of Windhawk's page, and a later edit on that page for the same setting wins. Reset on Home goes back to Windhawk's values.",
            12, false, true));
        page.Children().Append(note);
    }
    for (size_t c = 0; c < kCardCount; c++) {
        bool any = false;
        for (size_t i = 0; i < kSettingCount; i++) {
            any = any || (!kSettings[i].windhawkOnly && wcscmp(kSettings[i].page, L"columns") == 0 &&
                          wcscmp(kSettings[i].card, kCards[c].id) == 0);
        }
        if (!any) {
            continue;
        }
        auto title = MakeText(kCards[c].name, 14, true, false);
        title.Margin(mux::Thickness{0, 12, 0, 0});
        page.Children().Append(title);
        auto card = MakeCard();
        muxc::StackPanel rows;
        rows.Spacing(2);
        for (size_t i = 0; i < kSettingCount; i++) {
            auto const& info = kSettings[i];
            if (info.windhawkOnly || wcscmp(info.page, L"columns") != 0 || wcscmp(info.card, kCards[c].id) != 0) {
                continue;
            }
            rows.Children().Append(MakeSettingRow(info.name, info.description, MakeSettingControl(info)));
        }
        card.Child(rows);
        page.Children().Append(card);
    }
    return page;
}

void ShowSettingsPage(SettingsUi* ui, int page) {
    ui->page = page;
    g_settingsBuilding = true;
    mux::UIElement content{nullptr};
    try {
        content = page == 0 ? BuildHomePage() : BuildColumnsPage();
    } catch (...) {
        g_settingsBuilding = false;
        throw;
    }
    ui->content.Content(content);
    g_settingsBuilding = false;
    ui->content.ChangeView(nullptr, 0.0, nullptr, true);
}

// The icon of the window and the taskbar: the ExploitProtection glyph of Segoe Fluent
// Icons, drawn into a bitmap in the colour the caption text has, with its shape as
// the alpha. Made again when the theme changes.
HICON g_settingsIconSmall = nullptr;
HICON g_settingsIconBig = nullptr;

HICON IconFromGlyph(wchar_t glyph, int size, COLORREF colour) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // Top-down.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    void* pixels = nullptr;
    HBITMAP colourBitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(nullptr, screen);
    HICON icon = nullptr;
    if (dc && colourBitmap && pixels) {
        HGDIOBJ oldBitmap = SelectObject(dc, colourBitmap);
        memset(pixels, 0, (size_t)size * size * 4);
        HFONT font = CreateFontW(-(size * 9 / 10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                                 L"Segoe Fluent Icons");
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT box = {0, 0, size, size};
        wchar_t text[2] = {glyph, 0};
        DrawTextW(dc, text, 1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        SelectObject(dc, oldFont);
        DeleteObject(font);
        // White on black: the brightness is the shape.
        uint8_t* data = (uint8_t*)pixels;
        for (int i = 0; i < size * size; i++) {
            uint8_t* px = data + i * 4;
            unsigned alpha = std::max(px[0], std::max(px[1], px[2]));
            px[0] = (uint8_t)(GetBValue(colour) * alpha / 255);
            px[1] = (uint8_t)(GetGValue(colour) * alpha / 255);
            px[2] = (uint8_t)(GetRValue(colour) * alpha / 255);
            px[3] = (uint8_t)alpha;
        }
        SelectObject(dc, oldBitmap);
        HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
        ICONINFO made = {TRUE, 0, 0, mask, colourBitmap};
        icon = CreateIconIndirect(&made);
        DeleteObject(mask);
    }
    if (colourBitmap) {
        DeleteObject(colourBitmap);
    }
    if (dc) {
        DeleteDC(dc);
    }
    return icon;
}

void UpdateSettingsWindowIcon(HWND window, bool dark) {
    UINT dpi = GetDpiForWindow(window);
    int small = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    int big = GetSystemMetricsForDpi(SM_CXICON, dpi);
    COLORREF colour = dark ? RGB(235, 235, 235) : RGB(32, 32, 32);
    HICON newSmall = IconFromGlyph(L'', small, colour);
    HICON newBig = IconFromGlyph(L'', big, colour);
    if (newSmall) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)newSmall);
    }
    if (newBig) {
        SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)newBig);
    }
    if (g_settingsIconSmall) {
        DestroyIcon(g_settingsIconSmall);
    }
    if (g_settingsIconBig) {
        DestroyIcon(g_settingsIconBig);
    }
    g_settingsIconSmall = newSmall;
    g_settingsIconBig = newBig;
}

// The window's backdrop and caption follow the theme of its content.
void ApplySettingsWindowTheme(HWND window, SettingsUi* ui) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) {
        return;
    }
    typedef HRESULT(WINAPI * Set_t)(HWND, DWORD, const void*, DWORD);
    struct FrameMargins {
        int left, right, top, bottom;
    };
    typedef HRESULT(WINAPI * Extend_t)(HWND, const FrameMargins*);
    auto set = (Set_t)GetProcAddress(dwm, "DwmSetWindowAttribute");
    auto extend = (Extend_t)GetProcAddress(dwm, "DwmExtendFrameIntoClientArea");
    if (set) {
        BOOL dark = ui && ui->root && ui->root.ActualTheme() == mux::ElementTheme::Dark;
        UpdateSettingsWindowIcon(window, dark != FALSE);
        set(window, 20, &dark, sizeof(dark));          // DWMWA_USE_IMMERSIVE_DARK_MODE
        int backdrop = 2;                              // DWMSBT_MAINWINDOW: Mica
        set(window, 38, &backdrop, sizeof(backdrop));  // DWMWA_SYSTEMBACKDROP_TYPE
    }
    if (extend) {
        FrameMargins margins = {-1, -1, -1, -1};
        extend(window, &margins);
    }
    FreeLibrary(dwm);
}

void RememberSettingsWindow(HWND window) {
    if (!g_settingsUi || IsIconic(window) || IsZoomed(window)) {
        return;
    }
    RECT box;
    if (GetWindowRect(window, &box)) {
        wchar_t text[96];
        swprintf(text, 96, L"%ld,%ld,%ld,%ld,%d", box.left, box.top, box.right - box.left, box.bottom - box.top,
                 g_settingsUi->page);
        Wh_SetStringValue(L"ui.window", text);
    }
}

// Ends the window. A window closed by the user is released properly; one whose
// owner is going (the Explorer window, or the mod) is let go without releasing,
// as the panel is, since releasing XAML objects there crashes Explorer.
void DestroySettingsUi(bool abandon) {
    SettingsUi* ui = g_settingsUi;
    g_settingsUi = nullptr;
    if (!ui) {
        return;
    }
    if (abandon) {
        (void)winrt::detach_abi(ui->source);
        (void)winrt::detach_abi(ui->bridge);
        (void)winrt::detach_abi(ui->root);
        (void)winrt::detach_abi(ui->nav);
        (void)winrt::detach_abi(ui->content);
        return;  // The struct itself is let go with them.
    }
    try {
        ui->nav = nullptr;
        ui->content = nullptr;
        if (ui->source) {
            ui->source.Content(nullptr);
            ui->source.Close();
        }
    } catch (...) {
        LogHresult(L"closing the settings window");
    }
    ui->source = nullptr;
    ui->bridge = nullptr;
    ui->root = nullptr;
    delete ui;
}

LRESULT CALLBACK SettingsWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (g_settingsUi && g_settingsUi->bridge && wparam != SIZE_MINIMIZED) {
                try {
                    RECT client;
                    GetClientRect(window, &client);
                    g_settingsUi->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
                        0, 0, client.right - client.left, client.bottom - client.top});
                } catch (...) {
                }
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* limits = (MINMAXINFO*)lparam;
            UINT dpi = GetDpiForWindow(window);
            limits->ptMinTrackSize.x = MulDiv(760, dpi, 96);
            limits->ptMinTrackSize.y = MulDiv(520, dpi, 96);
            return 0;
        }
        case WM_DPICHANGED: {
            auto* suggested = (RECT*)lparam;
            SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_EXITSIZEMOVE:
            RememberSettingsWindow(window);
            return 0;
        case WM_CLOSE:
            RememberSettingsWindow(window);
            if (!g_settingsAbandon) {
                // Closing only hides it: the XAML island lives on, so opening again is
                // instant and nothing is created or released while Explorer runs.
                ShowWindow(window, SW_HIDE);
                return 0;
            }
            DestroySettingsUi(true);
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (g_settingsWindow == window) {
                g_settingsWindow = nullptr;
            }
            if (g_settingsIconSmall) {
                DestroyIcon(g_settingsIconSmall);
                g_settingsIconSmall = nullptr;
            }
            if (g_settingsIconBig) {
                DestroyIcon(g_settingsIconBig);
                g_settingsIconBig = nullptr;
            }
            DestroySettingsUi(true);  // A window destroyed another way: nothing is released.
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// Opens the window, or brings it forward. Called on the UI thread of an Explorer window.
void OpenSettingsWindow(WindowState* state) {
    if (g_unloading) {
        return;
    }
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        // The XAML objects belong to the thread that made them: only that thread
        // reads the settings into the page again.
        if (g_settingsThread == GetCurrentThreadId() && g_settingsUi) {
            try {
                ShowSettingsPage(g_settingsUi, g_settingsUi->page);
            } catch (...) {
                LogHresult(L"refreshing the settings window");
            }
        }
        ShowWindow(g_settingsWindow, IsIconic(g_settingsWindow) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g_settingsWindow);
        RedrawWindow(g_settingsWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return;
    }
    try {
        if (!g_settingsClassMade) {
            WNDCLASSEXW cls = {};
            cls.cbSize = sizeof(cls);
            cls.lpfnWndProc = SettingsWindowProc;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.lpszClassName = kSettingsClass;
            if (!RegisterClassExW(&cls)) {
                Out(L"settings window: the class could not be registered, gle=%lu", GetLastError());
                return;
            }
            g_settingsClassMade = true;
        }
        // The last place and page, if they are still on a screen.
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        UINT dpi = GetDpiForSystem();
        int width = MulDiv(980, dpi, 96), height = MulDiv(700, dpi, 96);
        int page = 0;
        {
            wchar_t saved[96] = L"";
            Wh_GetStringValue(L"ui.window", saved, ARRAYSIZE(saved));
            long sx, sy, sw, sh;
            int sp;
            if (swscanf(saved, L"%ld,%ld,%ld,%ld,%d", &sx, &sy, &sw, &sh, &sp) == 5 && sw > 300 && sh > 300) {
                RECT box = {sx, sy, sx + sw, sy + sh};
                if (MonitorFromRect(&box, MONITOR_DEFAULTTONULL)) {
                    x = sx;
                    y = sy;
                    width = sw;
                    height = sh;
                }
                page = sp == 1 ? 1 : 0;
            }
        }
        HWND window = CreateWindowExW(0, kSettingsClass, L"Explorer Extras", WS_OVERLAPPEDWINDOW, x, y, width, height,
                                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) {
            Out(L"settings window: could not be created, gle=%lu", GetLastError());
            return;
        }
        g_settingsWindow = window;
        g_settingsThread = GetCurrentThreadId();
        auto* ui = new SettingsUi();
        g_settingsUi = ui;

        ui->source = mux::Hosting::DesktopWindowXamlSource();
        ui->source.Initialize(winrt::Microsoft::UI::WindowId{(uint64_t)window});
        ui->bridge = ui->source.SiteBridge();

        // The layout: a sidebar of pages, a page that scrolls.
        // The island paints its own opaque background: with a transparent root, a white
        // rectangle of the island's first size showed through over the Mica backdrop.
        auto root = LoadXaml(std::wstring(L"<Grid ") + kXamlNs +
                             L" Background='{ThemeResource SolidBackgroundFillColorBaseBrush}'/>")
                        .as<muxc::Grid>();
        muxc::ColumnDefinition sidebarColumn, pageColumn;
        sidebarColumn.Width(mux::GridLength{248, mux::GridUnitType::Pixel});
        pageColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        root.ColumnDefinitions().Append(sidebarColumn);
        root.ColumnDefinitions().Append(pageColumn);
        ui->root = root;

        muxc::ListView nav;
        nav.Margin(mux::Thickness{8, 16, 8, 8});
        nav.SelectionMode(muxc::ListViewSelectionMode::Single);
        for (int i = 0; i < 2; i++) {
            muxc::ListViewItem item;
            muxc::StackPanel line;
            line.Orientation(muxc::Orientation::Horizontal);
            line.Spacing(12);
            if (i == 0) {
                muxc::FontIcon icon;
                icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
                icon.Glyph(L"");
                icon.FontSize(16);
                line.Children().Append(icon);
            } else {
                auto icon = MakeColumnsIcon();
                icon.as<mux::FrameworkElement>().VerticalAlignment(mux::VerticalAlignment::Center);
                line.Children().Append(icon);
            }
            auto label = MakeText(i == 0 ? L"Home" : L"Columns", 14, false, false);
            label.VerticalAlignment(mux::VerticalAlignment::Center);
            line.Children().Append(label);
            item.Content(line);
            item.Height(40);
            nav.Items().Append(item);
        }
        muxc::Grid::SetColumn(nav, 0);
        root.Children().Append(nav);
        ui->nav = nav;

        muxc::ScrollViewer content;
        content.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
        content.Padding(mux::Thickness{32, 24, 32, 32});
        muxc::Grid::SetColumn(content, 1);
        root.Children().Append(content);
        ui->content = content;

        nav.SelectionChanged([](Inspectable const&, muxc::SelectionChangedEventArgs const&) {
            SettingsUi* now = g_settingsUi;
            if (now && now->nav) {
                int selected = now->nav.SelectedIndex();
                if (selected >= 0 && selected != now->page) {
                    ShowSettingsPage(now, selected);
                }
            }
        });
        root.ActualThemeChanged([window](mux::FrameworkElement const&, Inspectable const&) {
            ApplySettingsWindowTheme(window, g_settingsUi);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        });
        ui->source.Content(root);

        nav.SelectedIndex(page);
        ShowSettingsPage(ui, page);
        ApplySettingsWindowTheme(window, ui);
        RECT client;
        GetClientRect(window, &client);
        ui->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{0, 0, client.right, client.bottom});
        ui->bridge.Show();
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        Out(L"settings window opened, window=%p, dpi=%u, client %ldx%ld", window, GetDpiForWindow(window),
            client.right, client.bottom);
    } catch (...) {
        LogHresult(L"OpenSettingsWindow");
        if (g_settingsWindow) {
            DestroyWindow(g_settingsWindow);
        }
    }
}

// The Explorer window that owns the settings window is going: the window goes with it,
// its XAML objects let go.
void CloseSettingsWindowOfThread() {
    if (g_settingsWindow && g_settingsThread == GetCurrentThreadId() && IsWindow(g_settingsWindow)) {
        Out(L"the settings window goes with its Explorer window");
        g_settingsAbandon = true;
        DestroyWindow(g_settingsWindow);
        g_settingsAbandon = false;
    }
}

// The mod is unloading: ask the window to close, on its own thread, and let the class go.
void CloseSettingsWindowAtUnload() {
    g_settingsAbandon = true;
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(g_settingsWindow, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 3000, &result);
    }
    if (g_settingsClassMade) {
        UnregisterClassW(kSettingsClass, GetModuleHandleW(nullptr));
        g_settingsClassMade = false;
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
                // A folder that was left in another layout stays in it.
                IShellBrowser* openBrowser = BrowserOf(state->topLevel);
                Pidl openFolder = openBrowser ? CurrentFolder(openBrowser) : Pidl();
                if (openFolder && RememberedColumns(PathOf(openFolder.get())) == 0) {
                    Out(L"the folder this window opens on was left in its own layout, not restoring Columns");
                    StopEntry(state);
                    return;
                }
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
    if (g_unloading || !g_enabled) {
        return;
    }
    // The tab that "open in a new tab" asked for.
    {
        WindowState* waiting = StateForThisThread(false);
        if (waiting && waiting->openTabFolder && !waiting->newTab &&
            GetTickCount64() < waiting->openTabUntil && waiting->topLevel &&
            GetAncestor(tab, GA_PARENT) == waiting->topLevel) {
            waiting->newTab = tab;
        }
    }
    if (!g_restore) {
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
        state->syncSelAt = now + 70;
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
void CloseSettingsWindowAtUnload() {}
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
    g_enabled = SettingBool(L"enabled");
    g_restore = SettingBool(L"restoreOnOpen");
    g_sidebarRoot = SettingBool(L"sidebarRoot");
    g_focusRect = SettingBool(L"focusRect");
    g_compactScroll = SettingBool(L"compactScrollbars");
    g_doubleClickUp = SettingBool(L"doubleClickUp");
    g_headerKeyboard = SettingBool(L"headerKeyboard");
    g_archives = SettingBool(L"archives");
    g_dragOut = SettingBool(L"dragOut");
    g_keepStale = SettingBool(L"keepStale");
    g_harmonize = SettingBool(L"harmonizeWidths");
    g_springLoad = SettingBool(L"springLoad");
    g_rememberSort = SettingBool(L"rememberSort");
    g_rememberWidth = SettingBool(L"rememberWidth");
    g_rememberLayout = SettingBool(L"rememberLayout");
    g_remoteMarks = SettingBool(L"remoteMarks");
    g_desktopRealOnly = SettingBool(L"desktopRealOnly");
    {
        std::wstring mode = SettingText(L"background");
        g_backgroundMode = mode == L"theme" ? 1 : mode == L"transparent" ? 2 : 0;
    }
    g_styleVersion++;
    int minWidth = SettingInt(L"minWidth");
    int maxWidth = std::max(SettingInt(L"maxWidth"), minWidth);
    g_minWidth = minWidth;
    g_maxWidth = maxWidth;
    g_follow = SettingBool(L"followActiveColumn");
    g_maxItems = SettingInt(L"maxItems");
    g_debugLog = SettingBool(L"debugLog");
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
    Out(L"explorer-extras v0.27.5 start, compiled as %s, pid=%lu, columns=%d width=%d..%d follow=%d",
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
    CloseSettingsWindowAtUnload();
    UnregisterNotifyClass();
    Out(L"explorer-extras end");
}
