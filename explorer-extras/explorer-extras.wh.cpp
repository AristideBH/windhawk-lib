// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Optional additions to File Explorer. First one: a Columns layout in the View menu, Miller columns drawn with Explorer's own WinUI 3 controls.
// @version         0.2
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid -ladvapi32
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

Not yet: real file icons, keyboard navigation between columns, right-click
menus, drag and drop, and memory of the choice across restarts.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- columns:
  - enabled: true
    $name: Enabled
    $description: Adds the Columns entry to the View menu. Off removes it.
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

#undef GetCurrentTime

#include <propkey.h>

#include <algorithm>
#include <atomic>
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
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
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
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\explorer-extras.log";
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

    if (!g_debugLog || !g_outPath[0]) {
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
};

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
    for (UINT i = 0; i < 80; i++) {
        SHCOLUMNID id{};
        if (FAILED(folder->MapColumnToSCID(i, &id))) {
            break;
        }
        if (IsEqualPropertyKey(id, key)) {
            return (int)i;
        }
    }
    return -1;
}

// Splits the sorted entries into the shell's own groups for `spec.groupKey`.
// Leaves the result ungrouped when the folder offers no categorizer.
void GroupEntries(IShellFolder* folder, const SortSpec& spec,
                  FolderResult* result) {
    size_t count = result->entries.size();
    if (!spec.grouped || count == 0) {
        return;
    }
    ICategoryProvider* provider = nullptr;
    HRESULT hr = folder->CreateViewObject(nullptr, IID_PPV_ARGS(&provider));
    if (FAILED(hr) || !provider) {
        Out(L"group: no category provider, hr=0x%08lX", hr);
        return;
    }
    GUID category{};
    ICategorizer* categorizer = nullptr;
    hr = provider->GetCategoryForSCID(&spec.groupKey, &category);
    if (SUCCEEDED(hr)) {
        hr = provider->CreateCategory(&category, IID_PPV_ARGS(&categorizer));
    }
    provider->Release();
    if (FAILED(hr) || !categorizer) {
        Out(L"group: no categorizer for this property, hr=0x%08lX", hr);
        return;
    }

    std::vector<PCUITEMID_CHILD> children(count);
    for (size_t i = 0; i < count; i++) {
        children[i] = result->entries[i].child.get();
    }
    std::vector<DWORD> ids(count, 0);
    hr = categorizer->GetCategory((UINT)count, children.data(), ids.data());
    if (FAILED(hr)) {
        Out(L"group: GetCategory failed, hr=0x%08lX", hr);
        categorizer->Release();
        return;
    }

    // Distinct group ids, in the shell's order.
    std::vector<DWORD> order;
    for (DWORD id : ids) {
        if (std::find(order.begin(), order.end(), id) == order.end()) {
            order.push_back(id);
        }
    }
    bool ascending = spec.groupAscending;
    std::stable_sort(order.begin(), order.end(),
                     [categorizer, ascending](DWORD a, DWORD b) {
                         HRESULT cmp = categorizer->CompareCategory(
                             CATSORT_DEFAULT, a, b);
                         short value = (short)HRESULT_CODE(cmp);
                         return ascending ? value < 0 : value > 0;
                     });

    // Entries regrouped, keeping their sorted order inside each group.
    std::vector<Entry> regrouped;
    regrouped.reserve(count);
    for (size_t g = 0; g < order.size(); g++) {
        CATEGORY_INFO info{};
        std::wstring name;
        if (SUCCEEDED(categorizer->GetCategoryInfo(order[g], &info))) {
            name = info.wszName;
        }
        result->groupNames.push_back(name);
        for (size_t i = 0; i < count; i++) {
            if (ids[i] == order[g]) {
                regrouped.push_back(std::move(result->entries[i]));
                result->groupOf.push_back((int)g);
            }
        }
    }
    result->entries = std::move(regrouped);
    categorizer->Release();
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
            Entry entry;
            entry.name = DisplayName(folder, child);
            entry.folder = IsFolderItem(folder, child);
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
            int column = ColumnForKey(folder2, sortColumn.propkey);
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
    Pidl selectedChild;                  // Restored after a reload.
    unsigned generation = 0;
    bool userSized = false;
    double charWidth = 7;
    muxc::StackPanel cell{nullptr};
    muxc::ListView list{nullptr};
    muxc::Border grip{nullptr};
    winrt::event_token selectionToken{};
    winrt::event_token doubleTapToken{};
    winrt::event_token gripPressedToken{};
    winrt::event_token gripMovedToken{};
    winrt::event_token gripReleasedToken{};
    winrt::event_token gripLostToken{};
    winrt::event_token gripDoubleToken{};
};

struct SiblingHook {
    muxc::MenuFlyoutItem item{nullptr};
    winrt::event_token token{};
};

struct WindowState {
    DWORD thread = 0;
    HWND topLevel = nullptr;
    mud::DispatcherQueue queue{nullptr};

    // View menu entry.
    ItemsVector menuItems{nullptr};
    muxc::RadioMenuFlyoutItem menuItem{nullptr};
    winrt::event_token menuClickToken{};
    std::vector<SiblingHook> siblings;

    // Panel.
    bool on = false;
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token tickToken{};
    muxc::ScrollViewer scroller{nullptr};
    muxc::StackPanel strip{nullptr};
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
    int selectTries = 0;

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
            try {
                job->queue.TryEnqueue([loaded]() { ApplyLoaded(loaded); });
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
            }
            if (column->grip) {
                column->grip.PointerPressed(column->gripPressedToken);
                column->grip.PointerMoved(column->gripMovedToken);
                column->grip.PointerReleased(column->gripReleasedToken);
                column->grip.PointerCaptureLost(column->gripLostToken);
                column->grip.DoubleTapped(column->gripDoubleToken);
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
}

double MeasureText(const std::wstring& text) {
    muxc::TextBlock block;
    block.Text(text);
    block.Measure(winrt::Windows::Foundation::Size{10000, 10000});
    return block.DesiredSize().Width;
}

// Space a row uses besides the name: icon, chevron, padding and the list's
// own margins.
const double kRowChrome = 24 + 16 + 18 + 22;

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

muxc::ListViewItem MakeRow(const Entry& entry, double rowHeight,
                           size_t capacity, muxc::TextBlock* textOut) {
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
    icon.Glyph(entry.folder ? L"" : L"");
    icon.FontSize(14);
    icon.HorizontalAlignment(mux::HorizontalAlignment::Left);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid::SetColumn(icon, 0);
    grid.Children().Append(icon);

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
    }

    muxc::ListViewItem row;
    row.Content(grid);
    row.MinHeight(rowHeight);
    row.Height(rowHeight);
    row.Padding(mux::Thickness{10, 0, 8, 0});
    row.HorizontalContentAlignment(mux::HorizontalAlignment::Stretch);
    // The full name, for names that were cut.
    muxc::ToolTipService::SetToolTip(row, winrt::box_value(winrt::hstring{entry.name}));
    return row;
}

muxc::ListViewItem MakeHeaderRow(const std::wstring& title, double rowHeight) {
    muxc::TextBlock text;
    text.Text(title);
    text.FontSize(12);
    text.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    text.Opacity(0.75);
    text.VerticalAlignment(mux::VerticalAlignment::Bottom);
    muxc::ListViewItem row;
    row.Content(text);
    row.MinHeight(rowHeight);
    row.Height(rowHeight + 4);
    row.Padding(mux::Thickness{10, 0, 8, 2});
    row.IsHitTestVisible(false);
    row.IsTabStop(false);
    return row;
}

// Fills a column's list from freshly loaded data.
void FillColumn(WindowState* state, Column* column, FolderResult&& data) {
    column->entries = std::move(data.entries);
    column->rowToEntry.clear();
    column->texts.clear();
    column->texts.resize(column->entries.size(), muxc::TextBlock{nullptr});

    FitWidth(column);
    size_t capacity = NameCapacity(column);
    double rowHeight = IsCompactMode() ? 24 : 32;
    bool grouped = data.groupOf.size() == column->entries.size() &&
                   !data.groupNames.empty();

    auto items = column->list.Items();
    items.Clear();
    int selectRow = -1;
    for (size_t i = 0; i < column->entries.size(); i++) {
        if (grouped && (i == 0 || data.groupOf[i] != data.groupOf[i - 1])) {
            int group = data.groupOf[i];
            std::wstring title =
                group >= 0 && group < (int)data.groupNames.size()
                    ? data.groupNames[group]
                    : L"";
            items.Append(MakeHeaderRow(title, rowHeight));
            column->rowToEntry.push_back(-1);
        }
        if (column->selectedChild &&
            ILIsEqual(column->entries[i].child.get(),
                      column->selectedChild.get())) {
            selectRow = (int)column->rowToEntry.size();
        }
        items.Append(MakeRow(column->entries[i], rowHeight, capacity,
                             &column->texts[i]));
        column->rowToEntry.push_back((int)i);
    }
    if (data.truncated) {
        muxc::ListViewItem more;
        more.Content(winrt::box_value(L"More items not shown"));
        more.IsEnabled(false);
        items.Append(more);
        column->rowToEntry.push_back(-1);
    }
    if (selectRow >= 0) {
        column->list.SelectedIndex(selectRow);
    }
    Out(L"column \"%s\": %d items, %d groups%s, width %d",
        PathOf(column->folder.get()).c_str(), (int)column->entries.size(),
        grouped ? (int)data.groupNames.size() : 0,
        data.truncated ? L", truncated" : L"", (int)column->list.Width());
}

// Runs on the window's UI thread when the worker has read a folder.
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
                FillColumn(state, column.get(), std::move(loaded->data));
                state->scroller.UpdateLayout();
                state->scroller.ChangeView(state->scroller.ScrollableWidth(),
                                           nullptr, nullptr);
            } catch (...) {
                LogHresult(L"filling a column");
            }
            state->building = wasBuilding;
            return;
        }
    }
    // No column waits for this result any more. Drop it.
}

void OnRowSelected(muxc::ListView const& list);
void OnRowDoubleTapped(muxc::ListView const& list);

// Adds an empty column for `folder` at the right end and asks the worker to
// read it. Takes ownership of `folder`.
void AddColumn(WindowState* state, Pidl folder) {
    auto column = std::make_unique<Column>();
    column->folder = std::move(folder);

    muxc::ListView list;
    list.Width(g_minWidth);
    list.SelectionMode(muxc::ListViewSelectionMode::Single);
    list.Padding(mux::Thickness{0, 4, 0, 4});
    column->selectionToken = list.SelectionChanged(
        [](Inspectable const& sender,
           muxc::SelectionChangedEventArgs const&) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnRowSelected(list);
            }
        });
    column->doubleTapToken = list.DoubleTapped(
        [](Inspectable const& sender,
           mux::Input::DoubleTappedRoutedEventArgs const&) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnRowDoubleTapped(list);
            }
        });

    // The column's right edge: a separator line that can be dragged.
    muxc::Border line;
    line.Width(1);
    line.Background(Brush(48, 128, 128, 128));
    line.HorizontalAlignment(mux::HorizontalAlignment::Center);
    muxc::Border grip;
    grip.Width(7);
    grip.Background(Brush(0, 0, 0, 0));
    grip.Child(line);
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
                if (width < 80) {
                    width = 80;
                }
                if (width > 1200) {
                    width = 1200;
                }
                Column* column = state->columns[index].get();
                column->userSized = true;
                column->list.Width(width);
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
    state->strip.Children().Append(cell);
    Column* raw = column.get();
    state->columns.push_back(std::move(column));
    RequestLoad(state, raw);
}

// Reads every column again, after the sort or group settings changed.
void ReloadAll(WindowState* state) {
    for (auto& column : state->columns) {
        RequestLoad(state, column.get());
    }
}

// Starts over with Explorer's current folder as the only column.
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
    state->building = true;
    RemoveColumnsFrom(state, 0);
    state->building = false;
    state->lastSeen.reset(ILClone(current.get()));
    state->expected.reset();
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    AddColumn(state, std::move(current));
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
    HRESULT hr = browser->BrowseObject(folder, SBSP_ABSOLUTE | SBSP_SAMEBROWSER);
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
    state->pendingNav.reset(ILClone(folder));
    state->navDue = GetTickCount64() + 350;
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
        int row = list.SelectedIndex();
        int entryIndex = row >= 0 && row < (int)column->rowToEntry.size()
                             ? column->rowToEntry[row]
                             : -1;
        state->building = true;
        RemoveColumnsFrom(state, columnIndex + 1);
        state->building = false;
        state->selectFolder.reset();
        state->selectChild.reset();
        if (entryIndex < 0) {
            column->selectedChild.reset();
            return;
        }
        const Entry& entry = column->entries[entryIndex];
        column->selectedChild.reset(ILClone(entry.child.get()));
        Pidl absolute(ILCombine(column->folder.get(), entry.child.get()));
        if (!absolute) {
            return;
        }
        if (entry.folder) {
            ScheduleNavigation(state, absolute.get());
            AddColumn(state, std::move(absolute));
        } else {
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

void OnRowDoubleTapped(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
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

void HidePanel(WindowState* state, const wchar_t* why) {
    if (!state->source) {
        state->on = false;
        return;
    }
    Out(L"panel off: %s", why);
    state->on = false;
    try {
        if (state->timer) {
            state->timer.Stop();
            state->timer.Tick(state->tickToken);
            state->timer = nullptr;
        }
        state->building = true;
        RemoveColumnsFrom(state, 0);
        state->building = false;
        state->source.Content(nullptr);
        state->source.Close();
    } catch (...) {
        LogHresult(L"HidePanel");
    }
    state->source = nullptr;
    state->bridge = nullptr;
    state->scroller = nullptr;
    state->strip = nullptr;
    state->lastSeen.reset();
    state->expected.reset();
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    state->lastFileList = nullptr;
    state->specCheckAt = 0;
    state->dragColumn = -1;
    state->lastRect = {};
}

// What Explorer's own view shows changed. Decide whether this mod asked for
// it, the user went back into a folder that is a column, or went elsewhere.
void OnExplorerFolderChanged(WindowState* state, Pidl const& current) {
    state->lastSeen.reset(ILClone(current.get()));
    if (state->expected && ILIsEqual(current.get(), state->expected.get())) {
        state->expected.reset();
        return;
    }
    state->expected.reset();
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (ILIsEqual(current.get(), state->columns[i]->folder.get())) {
            if (i + 1 < state->columns.size() ||
                state->columns[i]->selectedChild) {
                Out(L"Explorer moved to column %d's folder", (int)i + 1);
                state->building = true;
                RemoveColumnsFrom(state, i + 1);
                state->columns[i]->selectedChild.reset();
                state->columns[i]->list.SelectedIndex(-1);
                state->building = false;
                state->pendingNav.reset();
                state->selectFolder.reset();
                state->selectChild.reset();
            }
            return;
        }
    }
    Out(L"Explorer navigated elsewhere, rebuilding");
    RebuildFromExplorer(state);
}

void OnTick() {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || !state->bridge) {
        return;
    }
    if (g_unloading || !g_enabled) {
        HidePanel(state, L"mod unloading or feature off");
        return;
    }
    try {
        if (!IsWindow(state->topLevel)) {
            HidePanel(state, L"window closed");
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

        // The Sort menu was used. Take its new settings if they changed.
        if (state->specCheckAt && now >= state->specCheckAt) {
            state->specCheckAt = 0;
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
        state->lastFileList = fileList;
        if (!state->lastSeen ||
            !ILIsEqual(current.get(), state->lastSeen.get())) {
            OnExplorerFolderChanged(state, current);
            if (!state->on) {
                return;
            }
        }

        // Mirror the selected file into Explorer's own view. The view fills
        // asynchronously, so this retries for a few seconds.
        if (state->selectChild && state->selectFolder && !state->pendingNav &&
            !state->expected &&
            ILIsEqual(current.get(), state->selectFolder.get())) {
            if (SelectInExplorer(browser, state->selectChild.get())) {
                state->selectFolder.reset();
                state->selectChild.reset();
            } else if (++state->selectTries > 40) {
                Out(L"selection could not be mirrored into Explorer's view");
                state->selectFolder.reset();
                state->selectChild.reset();
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
        scroller.Content(strip);

        muxc::Grid root;
        bool dark = IsDarkMode();
        root.Background(dark ? Brush(255, 25, 25, 25)
                             : Brush(255, 255, 255, 255));
        root.RequestedTheme(dark ? mux::ElementTheme::Dark
                                 : mux::ElementTheme::Light);
        root.Children().Append(scroller);

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
        state->specCheckAt = GetTickCount64() + 300;
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
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarManager::InvokeMenuItem(struct winrt::WindowsUdk::UI::MenuItem const &))"},
         &InvokeMenuItem_Original,
         InvokeMenuItem_Hook,
         true},
    };
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Out(L"HookSymbols returned %d, PointerPressedHandler=%p, Wave1=%p, InvokeMenuItem=%p",
        ok ? 1 : 0, (void*)PointerPressed_Original,
        (void*)PointerPressedWave1_Original, (void*)InvokeMenuItem_Original);
    g_hooked = PointerPressed_Original || PointerPressedWave1_Original;
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
                    HidePanel(state, L"mod unloading");
                    RemoveMenuEntry(state);
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

void LoadSettings() {
    g_enabled = Wh_GetIntSetting(L"columns.enabled") != 0;
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
    Out(L"explorer-extras v0.2 start, compiled as %s, pid=%lu, columns=%d width=%d..%d follow=%d",
        arch, GetCurrentProcessId(), g_enabled ? 1 : 0, g_minWidth.load(),
        g_maxWidth.load(), g_follow ? 1 : 0);

    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        Out(L"FileExplorerExtensions.dll isn't loaded yet. Open a File Explorer window, then disable and enable this mod.");
        return TRUE;
    }
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
    Out(L"explorer-extras end");
}
