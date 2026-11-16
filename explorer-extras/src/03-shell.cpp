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

bool PaneHandleAltP(HWND hwnd, LPMSG msg);  // 10d: Alt+P shows or hides the details pane.

using TranslateAcceleratorW_t = int(WINAPI*)(HWND, HACCEL, LPMSG);
TranslateAcceleratorW_t TranslateAcceleratorW_Original;
int WINAPI TranslateAcceleratorW_Hook(HWND hwnd, HACCEL accel, LPMSG msg) {
    if (PaneHandleAltP(hwnd, msg)) {
        return 1;  // Taken: Explorer's Preview pane never opens.
    }
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
    if (!g_sidebarRoot || !browser || tried.exchange(true)) {
        return;
    }
    void** vtable = *(void***)browser;
    // IUnknown 0-2, IOleWindow 3-4, then InsertMenusSB, SetMenuSB,
    // RemoveMenusSB, SetStatusTextSB, EnableModelessSB, TranslateAcceleratorSB,
    // and BrowseObject at slot 11.
    void* target = vtable[11];
    BOOL ok = WindhawkUtils::SetFunctionHook((BrowseObject_t)target, BrowseObject_Hook, &BrowseObject_Original);
    if (ok) {
        Wh_ApplyHookOperations();
    }
    Wh_Log(L"BrowseObject trace installed: %d, target=%s", ok ? 1 : 0,
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
                         (found.dwReserved0 & 0xFFFF0FFF) == 0x9000001A;  // IO_REPARSE_TAG_CLOUD and its _1 to _F
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
        LogVerbose(L"group fallback: no IShellFolder2");
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
    LogVerbose(L"group fallback: key pid=%lu, first value type=%d", key.pid, firstType);
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
        LogVerbose(L"group fallback: the key gives no values here, not grouping");
        return;
    }
    ApplyGroups(result, ids, order, names);
    LogVerbose(L"group fallback: %d groups, date=%d size=%d name=%d column=%d",
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
            LogVerbose(L"group: key pid=%lu, CanCategorizeOnSCID=0x%08lX GetCategoryForSCID=0x%08lX CreateCategory=0x%08lX",
                spec.groupKey.pid, can, forScid, create);
        }
        provider->Release();
    } else {
        Wh_Log(L"group: no category provider, hr=0x%08lX", hr);
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
        Wh_Log(L"group: GetCategory failed, hr=0x%08lX", hr);
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
        Wh_Log(L"load: can't bind, hr=0x%08lX", hr);
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
            if (g_unloading) {
                CoTaskMemFree(child);
                break;  // The mod is unloading: the folder is not read to its end.
            }
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
            Wh_Log(L"load: \"%s\" read %d items in %d ms, enumeration hr=0x%08lX", key.c_str(),
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
            Wh_Log(L"marks in \"%s\": %d cloud, network or device, %d syncing", key.c_str(), marked, syncing);
        }
        if (last.size() > 500) {
            last.clear();  // A log helper: it must not grow with every folder ever read.
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
                LogVerbose(L"sort: key pid=%lu maps to column %d, direction %d",
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
                         if (g_foldersFirst) {
                             bool aFolder = a.folder && !a.archive;
                             bool bFolder = b.folder && !b.archive;
                             if (aFolder != bFolder) {
                                 return aFolder;
                             }
                         }
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

// Nothing selected in Explorer's own view.
std::vector<Pidl> ExplorerSelection(IShellBrowser* browser);

void ClearExplorerSelection(IShellBrowser* browser) {
    // Each selected item is deselected by name: a null item with "deselect others" did not
    // empty the selection.
    std::vector<Pidl> selected = ExplorerSelection(browser);
    IShellView* view = nullptr;
    if (!selected.empty() && SUCCEEDED(browser->QueryActiveShellView(&view)) && view) {
        for (auto const& child : selected) {
            view->SelectItem(child.get(), SVSI_DESELECT | SVSI_NOTAKEFOCUS);
        }
        view->Release();
    }
    Wh_Log(L"cleared Explorer's selection, %d items were selected", (int)selected.size());
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

