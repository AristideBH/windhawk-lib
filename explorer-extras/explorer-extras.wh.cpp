// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Optional additions to File Explorer. First one: a Columns layout in the View menu, Miller columns drawn with Explorer's own WinUI 3 controls.
// @version         0.3.3
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

In 0.3 the Columns entry is in the View menu as soon as a window opens, with
no click on the command bar needed. When Columns was the last layout you
chose, new windows open in it, also after Explorer restarts. Turn this off
with "Open new windows in Columns".

Not yet: real file icons, right-click menus, and drag and drop.
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
#include <winrt/Windows.System.h>
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
            *due = GetTickCount64() + 300;
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
    Pidl selectedChild;                  // Restored after a reload.
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
    muxc::Border pill{nullptr};
    ULONGLONG hoverSince = 0;
    ULONG notifyId = 0;
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

    // Entry at window creation, and restoring the last layout.
    mux::XamlRoot xamlRoot{nullptr};
    mud::DispatcherQueueTimer entryTimer{nullptr};
    winrt::event_token entryToken{};
    bool entryStopped = false;
    int entryTries = 0;
    int restoreSettle = 0;

    // Panel.
    bool on = false;
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token tickToken{};
    muxc::ScrollViewer scroller{nullptr};
    muxc::Grid rootGrid{nullptr};
    Inspectable wheelHandler{nullptr};  // The boxed handler, for removal.
    double wheelTarget = 0;             // Where the wheel is scrolling to.
    ULONGLONG wheelAt = 0;              // Tick of the last wheel event.
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
void SetActiveColumn(WindowState* state, int index) {
    state->active = index;
    for (size_t i = 0; i < state->columns.size(); i++) {
        try {
            state->columns[i]->list.Background(
                (int)i == index ? Brush(22, 128, 128, 128)
                                : Brush(0, 0, 0, 0));
        } catch (...) {
        }
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
            // The highlight stays here. It moves to the new column when the
            // user enters it.
            SetActiveColumn(state, columnIndex);
        } else {
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

// Right or Enter on a folder enters its column, Left goes back to the parent
// column, Enter on a file opens it.
void OnColumnKey(muxc::ListView const& list,
                 mux::Input::KeyRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
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

        if (key == VirtualKey::Left) {
            if (index > 0) {
                SetActiveColumn(state, index - 1);
                state->columns[index - 1]->list.Focus(mux::FocusState::Keyboard);
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
    (void)winrt::detach_abi(state->strip);
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
    Out(L"Explorer navigated elsewhere, rebuilding");
    RebuildFromExplorer(state);
    SetActiveColumn(state, 0);
}

// What Explorer's own view shows changed.
void OnExplorerFolderChanged(WindowState* state, Pidl const& current) {
    state->lastSeen.reset(ILClone(current.get()));
    if (state->expected && ILIsEqual(current.get(), state->expected.get())) {
        state->expected.reset();
        return;
    }
    state->expected.reset();
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
        if (state->reloadDue && now >= state->reloadDue &&
            now - state->lastReload >= 2000) {
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
        scroller.Content(strip);

        muxc::Grid root;
        bool dark = IsDarkMode();
        root.Background(dark ? Brush(255, 25, 25, 25)
                             : Brush(255, 255, 255, 255));
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
        {{LR"(public: struct winrt::fire_and_forget __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::OnDpiChanged(struct winrt::Microsoft::UI::Xaml::XamlRoot,struct winrt::Microsoft::UI::Xaml::XamlRootChangedEventArgs))"},
         &DpiChanged_Original,
         DpiChanged_Hook,
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
    Out(L"explorer-extras v0.3.3 start, compiled as %s, pid=%lu, columns=%d width=%d..%d follow=%d",
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
