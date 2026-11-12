// ==WindhawkMod==
// @id              explorer-columns
// @name            Explorer Columns View
// @description     Adds a Columns layout to File Explorer's View menu: Miller columns drawn with Explorer's own WinUI 3 controls.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid -ladvapi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Explorer Columns View

Adds a **Columns** entry to File Explorer's View menu, next to Details, List
and the icon sizes. Selecting it shows the folder as Miller columns: click a
folder and its content opens in the next column.

The columns are WinUI 3 controls hosted in the Explorer window, so they use
Explorer's own styles. The mod needs no symbol file from Microsoft for
System32 modules, so it keeps working right after a Windows update.

## Status: first version, 0.1

Works:

- Columns entry in the View menu. It appears the first time you open the menu.
- One column per folder level, with a chevron on folders.
- The first column follows the sort order chosen in the Sort menu.
- Compact view is respected.
- Double-click opens a file.
- Explorer's own file list follows the active column, so the address bar, Back
  and the status bar match what you see.

Not yet:

- Real file icons, grouping, a preview column, keyboard navigation between
  columns, right-click menus and drag and drop.
- The choice is kept per window, not per folder, and not across restarts.
- Folders are read on the UI thread. A slow network folder blocks the window
  while it loads.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- columnWidth: 240
  $name: Column width
  $description: In pixels at 100% scale.
- followActiveColumn: true
  $name: Explorer follows the active column
  $description: Explorer's own file list navigates to the folder of the column you are in, so the address bar, Back and the status bar stay in step.
- maxItems: 1500
  $name: Maximum items per column
- debugLog: true
  $name: Write a debug log
  $description: Writes to Code\windhawk-lib\probes\logs\explorer-columns.log in your user folder, if that folder exists.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

#undef GetCurrentTime

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef MOD_NO_WINRT
#include <windhawk_utils.h>

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
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
std::atomic<int> g_columnWidth{240};
std::atomic<int> g_maxItems{1500};
thread_local ULONGLONG g_lastWalk = 0;

// Every log line also goes to a file in the repo, so the log can be read from
// disk: %USERPROFILE%\Code\windhawk-lib\probes\logs\explorer-columns.log
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
    std::wstring path = dir + L"\\explorer-columns.log";
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

// The items of Explorer's own file list, in the order it shows them. That
// order is the one chosen in the Sort menu.
bool ReadViewItems(IShellBrowser* browser, std::vector<Entry>* entries,
                   bool* truncated) {
    IFolderView* folderView = FolderViewOf(browser);
    if (!folderView) {
        return false;
    }
    bool ok = false;
    IShellFolder* folder = nullptr;
    if (SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&folder))) && folder) {
        int count = 0;
        folderView->ItemCount(SVGIO_ALLVIEW, &count);
        int limit = g_maxItems;
        *truncated = count > limit;
        for (int i = 0; i < count && i < limit; i++) {
            PITEMID_CHILD child = nullptr;
            if (FAILED(folderView->Item(i, &child)) || !child) {
                continue;
            }
            Entry entry;
            entry.name = DisplayName(folder, child);
            entry.folder = IsFolderItem(folder, child);
            entry.child.reset(child);
            entries->push_back(std::move(entry));
        }
        folder->Release();
        ok = true;
    }
    folderView->Release();
    return ok;
}

// The items of any folder, folders first, then by name as the shell compares
// names.
bool EnumFolder(PCIDLIST_ABSOLUTE folderPidl, std::vector<Entry>* entries,
                bool* truncated) {
    IShellFolder* folder = nullptr;
    HRESULT hr;
    if (ILIsEmpty(folderPidl)) {
        hr = SHGetDesktopFolder(&folder);
    } else {
        hr = SHBindToObject(nullptr, folderPidl, nullptr,
                            IID_PPV_ARGS(&folder));
    }
    if (FAILED(hr) || !folder) {
        Out(L"EnumFolder: can't bind, hr=0x%08lX", hr);
        return false;
    }
    IEnumIDList* list = nullptr;
    hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS,
                             &list);
    if (hr == S_OK && list) {
        int limit = g_maxItems;
        PITEMID_CHILD child = nullptr;
        while (list->Next(1, &child, nullptr) == S_OK && child) {
            if ((int)entries->size() >= limit) {
                *truncated = true;
                CoTaskMemFree(child);
                break;
            }
            Entry entry;
            entry.name = DisplayName(folder, child);
            entry.folder = IsFolderItem(folder, child);
            entry.child.reset(child);
            entries->push_back(std::move(entry));
            child = nullptr;
        }
        list->Release();
        std::stable_sort(entries->begin(), entries->end(),
                         [folder](const Entry& a, const Entry& b) {
                             if (a.folder != b.folder) {
                                 return a.folder;
                             }
                             HRESULT order = folder->CompareIDs(
                                 0, a.child.get(), b.child.get());
                             return SUCCEEDED(order) &&
                                    (short)HRESULT_CODE(order) < 0;
                         });
    }
    folder->Release();
    return SUCCEEDED(hr);
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
    muxc::ListView list{nullptr};
    winrt::event_token selectionToken{};
    winrt::event_token doubleTapToken{};
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

    // Folder tracking.
    Pidl lastSeen;   // Explorer's own folder at the last tick.
    Pidl expected;   // A folder this mod asked Explorer to open.
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

void RemoveColumnsFrom(WindowState* state, size_t index) {
    while (state->columns.size() > index) {
        auto& column = state->columns.back();
        try {
            if (column->list) {
                column->list.SelectionChanged(column->selectionToken);
                column->list.DoubleTapped(column->doubleTapToken);
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
}

void OnRowSelected(muxc::ListView const& list);
void OnRowDoubleTapped(muxc::ListView const& list);

muxc::ListViewItem MakeRow(const Entry& entry, double rowHeight) {
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
    name.Text(entry.name);
    name.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    name.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid::SetColumn(name, 1);
    grid.Children().Append(name);

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
    return row;
}

// Adds a column for `folder` at the right end. Takes ownership of both
// arguments.
void AddColumn(WindowState* state, Pidl folder, std::vector<Entry> entries,
               bool truncated) {
    auto column = std::make_unique<Column>();
    column->folder = std::move(folder);
    column->entries = std::move(entries);

    double rowHeight = IsCompactMode() ? 24 : 32;
    muxc::ListView list;
    list.Width(g_columnWidth);
    list.SelectionMode(muxc::ListViewSelectionMode::Single);
    list.BorderBrush(Brush(48, 128, 128, 128));
    list.BorderThickness(mux::Thickness{0, 0, 1, 0});
    list.Padding(mux::Thickness{0, 4, 0, 4});
    for (auto& entry : column->entries) {
        list.Items().Append(MakeRow(entry, rowHeight));
    }
    if (truncated) {
        muxc::ListViewItem more;
        more.Content(winrt::box_value(L"More items not shown"));
        more.IsEnabled(false);
        list.Items().Append(more);
    }
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
    column->list = list;
    state->strip.Children().Append(list);
    Out(L"column %d: \"%s\", %d items%s", (int)state->columns.size() + 1,
        PathOf(column->folder.get()).c_str(), (int)column->entries.size(),
        truncated ? L", truncated" : L"");
    state->columns.push_back(std::move(column));

    try {
        state->scroller.UpdateLayout();
        state->scroller.ChangeView(state->scroller.ScrollableWidth(), nullptr,
                                   nullptr);
    } catch (...) {
        LogHresult(L"scrolling to the new column");
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
    std::vector<Entry> entries;
    bool truncated = false;
    ReadViewItems(browser, &entries, &truncated);
    state->lastSeen.reset(ILClone(current.get()));
    state->expected.reset();
    AddColumn(state, std::move(current), std::move(entries), truncated);
    state->building = false;
}

int ColumnIndexOf(WindowState* state, muxc::ListView const& list) {
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (state->columns[i]->list == list) {
            return (int)i;
        }
    }
    return -1;
}

// Asks Explorer's own file list to show `folder`, and remembers the request
// so the tick doesn't take it for a navigation by the user.
void NavigateExplorer(WindowState* state, PCIDLIST_ABSOLUTE folder) {
    if (!g_follow) {
        return;
    }
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

// The same, after the current event handler has returned.
void NavigateExplorerLater(WindowState* state, PCIDLIST_ABSOLUTE folder) {
    if (!g_follow || !state->queue) {
        return;
    }
    auto copy = std::make_shared<Pidl>(ILClone(folder));
    state->queue.TryEnqueue([copy]() {
        WindowState* state = StateForThisThread(false);
        if (state && state->on && !g_unloading && copy->get()) {
            try {
                NavigateExplorer(state, copy->get());
            } catch (...) {
                LogHresult(L"NavigateExplorer");
            }
        }
    });
}

void OnRowSelected(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || state->building || g_unloading) {
        return;
    }
    try {
        int columnIndex = ColumnIndexOf(state, list);
        if (columnIndex < 0) {
            return;
        }
        int row = list.SelectedIndex();
        Column* column = state->columns[columnIndex].get();
        state->building = true;
        RemoveColumnsFrom(state, columnIndex + 1);
        state->building = false;
        if (row < 0 || row >= (int)column->entries.size()) {
            return;
        }
        const Entry& entry = column->entries[row];
        Pidl absolute(ILCombine(column->folder.get(), entry.child.get()));
        if (!absolute) {
            return;
        }
        if (entry.folder) {
            std::vector<Entry> entries;
            bool truncated = false;
            EnumFolder(absolute.get(), &entries, &truncated);
            Pidl forColumn(ILClone(absolute.get()));
            state->building = true;
            AddColumn(state, std::move(forColumn), std::move(entries),
                      truncated);
            state->building = false;
            NavigateExplorerLater(state, absolute.get());
        } else {
            // A file: Explorer should show the file's own folder.
            NavigateExplorerLater(state, column->folder.get());
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
        int columnIndex = ColumnIndexOf(state, list);
        int row = list.SelectedIndex();
        if (columnIndex < 0 || row < 0) {
            return;
        }
        Column* column = state->columns[columnIndex].get();
        if (row >= (int)column->entries.size() ||
            column->entries[row].folder) {
            return;
        }
        Pidl absolute(ILCombine(column->folder.get(),
                                column->entries[row].child.get()));
        if (absolute) {
            Out(L"open \"%s\"", column->entries[row].name.c_str());
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
    state->lastRect = {};
}

void OnTick() {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || !state->bridge) {
        return;
    }
    if (g_unloading) {
        HidePanel(state, L"mod unloading");
        return;
    }
    try {
        if (!IsWindow(state->topLevel)) {
            HidePanel(state, L"window closed");
            return;
        }
        // Keep the panel on the file list.
        HWND fileList = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
        RECT rect;
        if (fileList && FileListRect(state->topLevel, fileList, &rect) &&
            !EqualRect(&rect, &state->lastRect)) {
            state->lastRect = rect;
            state->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
                rect.left, rect.top, rect.right - rect.left,
                rect.bottom - rect.top});
        }

        // Follow navigation done outside the panel: address bar, Back, the
        // side pane.
        IShellBrowser* browser = BrowserOf(state->topLevel);
        if (!browser) {
            return;
        }
        Pidl current = CurrentFolder(browser);
        if (!current) {
            return;
        }
        if (state->lastSeen && ILIsEqual(current.get(), state->lastSeen.get())) {
            return;
        }
        state->lastSeen.reset(ILClone(current.get()));
        if (state->expected &&
            ILIsEqual(current.get(), state->expected.get())) {
            state->expected.reset();
            return;
        }
        state->expected.reset();
        // A folder that is already a column: keep the columns up to it.
        for (size_t i = 0; i < state->columns.size(); i++) {
            if (ILIsEqual(current.get(), state->columns[i]->folder.get())) {
                Out(L"Explorer moved to column %d's folder", (int)i + 1);
                state->building = true;
                RemoveColumnsFrom(state, i + 1);
                state->columns[i]->list.SelectedIndex(-1);
                state->building = false;
                return;
            }
        }
        Out(L"Explorer navigated elsewhere, rebuilding");
        RebuildFromExplorer(state);
    } catch (...) {
        state->building = false;
        LogHresult(L"OnTick");
    }
}

void ShowPanel(WindowState* state) {
    if (state->source || g_unloading) {
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
        state->scroller = scroller;
        state->strip = strip;
        state->on = true;

        step = L"reading the folder";
        RebuildFromExplorer(state);

        step = L"starting the timer";
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(250));
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
    };
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Out(L"HookSymbols returned %d, PointerPressedHandler=%p, Wave1=%p",
        ok ? 1 : 0, (void*)PointerPressed_Original,
        (void*)PointerPressedWave1_Original);
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

#endif

void LoadSettings() {
    int width = Wh_GetIntSetting(L"columnWidth");
    g_columnWidth = width < 120 ? 120 : (width > 600 ? 600 : width);
    g_follow = Wh_GetIntSetting(L"followActiveColumn") != 0;
    int maxItems = Wh_GetIntSetting(L"maxItems");
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
    Out(L"explorer-columns v0.1 start, compiled as %s, pid=%lu, width=%d follow=%d",
        arch, GetCurrentProcessId(), g_columnWidth.load(), g_follow ? 1 : 0);

    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        Out(L"FileExplorerExtensions.dll isn't loaded yet. Open a File Explorer window, then disable and enable this mod.");
        return TRUE;
    }
    HookExtensions(module);
    if (g_hooked) {
        Out(L"ready. Open the View menu of a File Explorer window.");
    } else {
        Out(L"no hook could be installed. The mod does nothing.");
    }
    return TRUE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    Out(L"settings changed: width=%d follow=%d maxItems=%d",
        g_columnWidth.load(), g_follow ? 1 : 0, g_maxItems.load());
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    CloseEverything();
}

void Wh_ModUninit() {
    Out(L"explorer-columns end");
}
