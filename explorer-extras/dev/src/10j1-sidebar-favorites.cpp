// Sidebar, Favorites section: the folders Explorer has pinned to Quick access, in the shell's order. A folder dropped on the
// section is pinned the same way Explorer's own "Pin to Quick access" does (the shell verb `pintohome`; `unpinfromhome` unpins).
// Facts from the probe (PLAN.md, "Sidebar probes"): pins are listed by enumerating shell:::{679f85cb-...}; the verbs are used by
// their ids, never their localized names; the store is %APPDATA%\Microsoft\Windows\Recent\AutomaticDestinations\
// f01b4d95cf55d32a.automaticDestinations-ms, whose write time changes on a pin, so a change notification on its folder tells of
// pins made elsewhere. There is no route to reorder pins, so the shell's order stands.
//
// Everything that asks the shell (listing, pinning) runs on a mod thread: a slow shell extension must not stall a window.
// Also here: the drop target of the sidebar island, which pins a dropped folder and hands a drop on a Locations folder to that
// folder's own drop target.

namespace {

std::mutex g_favMutex;
[[clang::no_destroy]] std::vector<FavoriteEntry> g_favoritesCache;
std::atomic<unsigned> g_favoritesVersion{0};
std::atomic<bool> g_favoritesDirty{true};
bool g_favoritesRunning = false;  // Under g_favMutex.

const wchar_t kFavoritesHome[] = L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}";

std::shared_ptr<ITEMIDLIST> FavoritesPidl(PIDLIST_ABSOLUTE pidl) {
    return std::shared_ptr<ITEMIDLIST>(pidl, [](ITEMIDLIST* p) {
        if (p) {
            CoTaskMemFree(p);
        }
    });
}

// Calls `each` with every folder pinned to Quick access, until it returns false.
template <typename Each>
void FavoritesEach(Each each) {
    PIDLIST_ABSOLUTE homePidl = nullptr;
    if (FAILED(SHParseDisplayName(kFavoritesHome, nullptr, &homePidl, 0, nullptr)) || !homePidl) {
        return;
    }
    IShellItem* home = nullptr;
    IEnumShellItems* list = nullptr;
    if (SUCCEEDED(SHCreateItemFromIDList(homePidl, IID_PPV_ARGS(&home))) && home &&
        SUCCEEDED(home->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&list))) && list) {
        IShellItem* child = nullptr;
        ULONG got = 0;
        while (list->Next(1, &child, &got) == S_OK && got == 1) {
            SFGAOF attributes = 0;
            child->GetAttributes(SFGAO_FOLDER, &attributes);
            bool goOn = true;
            if (attributes & SFGAO_FOLDER) {
                goOn = each(child);
            }
            child->Release();
            if (!goOn) {
                break;
            }
        }
    }
    if (list) {
        list->Release();
    }
    if (home) {
        home->Release();
    }
    CoTaskMemFree(homePidl);
}

std::wstring FavoritesPathOf(IShellItem* item) {
    std::wstring path;
    wchar_t* text = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) && text) {
        path = text;
        CoTaskMemFree(text);
    }
    return path;
}

// The shell verb of an item, by id, with no user interface.
bool FavoritesInvokeVerb(IShellItem* item, const char* verb) {
    IContextMenu* menu = nullptr;
    if (FAILED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&menu))) || !menu) {
        return false;
    }
    HMENU popup = CreatePopupMenu();
    menu->QueryContextMenu(popup, 0, 1, 0x7FFF, CMF_NORMAL);
    CMINVOKECOMMANDINFO command = {sizeof(command)};
    command.fMask = CMIC_MASK_FLAG_NO_UI;
    command.lpVerb = verb;
    command.nShow = SW_HIDE;
    HRESULT hr = menu->InvokeCommand(&command);
    DestroyMenu(popup);
    menu->Release();
    if (FAILED(hr)) {
        Wh_Log(L"favorites: the verb %S failed, hr=0x%08lX", verb, hr);
    }
    return SUCCEEDED(hr);
}

// The folders (on disk, not archives) among the items of a drag.
std::vector<std::wstring> FavoritesFoldersOf(IDataObject* data) {
    std::vector<std::wstring> folders;
    IShellItemArray* items = nullptr;
    if (!data || FAILED(SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(&items))) || !items) {
        return folders;
    }
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count && i < 64; i++) {
        IShellItem* item = nullptr;
        if (FAILED(items->GetItemAt(i, &item)) || !item) {
            continue;
        }
        SFGAOF attributes = 0;
        item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM, &attributes);
        if ((attributes & SFGAO_FOLDER) && (attributes & SFGAO_FILESYSTEM) && !(attributes & SFGAO_STREAM)) {
            std::wstring path = FavoritesPathOf(item);
            if (!path.empty()) {
                folders.push_back(path);
            }
        }
        item->Release();
    }
    items->Release();
    return folders;
}

}  // namespace

// The pinned folders, with their names and whether they still exist. Asks the shell: not for a window's thread.
std::vector<FavoriteEntry> FavoritesLoad() {
    std::vector<FavoriteEntry> list;
    FavoritesEach([&](IShellItem* child) {
        FavoriteEntry entry;
        entry.path = FavoritesPathOf(child);
        if (entry.path.empty()) {
            return true;
        }
        wchar_t* name = nullptr;
        if (SUCCEEDED(child->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) {
            entry.name = name;
            CoTaskMemFree(name);
        }
        if (entry.name.empty()) {
            entry.name = entry.path;
        }
        entry.exists = GetFileAttributesW(entry.path.c_str()) != INVALID_FILE_ATTRIBUTES;
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (entry.exists && SUCCEEDED(SHParseDisplayName(entry.path.c_str(), nullptr, &pidl, 0, nullptr))) {
            entry.pidl = FavoritesPidl(pidl);
        }
        list.push_back(std::move(entry));
        return true;
    });
    return list;
}

std::vector<std::wstring> FavoritesList() {
    std::vector<std::wstring> paths;
    for (auto const& entry : FavoritesLoad()) {
        paths.push_back(entry.path);
    }
    return paths;
}

bool FavoritesPin(std::wstring const& folderPath) {
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(folderPath.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item) {
        return false;
    }
    bool ok = FavoritesInvokeVerb(item, "pintohome");
    item->Release();
    g_favoritesDirty = true;
    return ok;
}

bool FavoritesUnpin(std::wstring const& folderPath) {
    bool ok = false;
    FavoritesEach([&](IShellItem* child) {
        if (_wcsicmp(FavoritesPathOf(child).c_str(), folderPath.c_str()) == 0) {
            ok = FavoritesInvokeVerb(child, "unpinfromhome");
            return false;
        }
        return true;
    });
    g_favoritesDirty = true;
    return ok;
}

namespace {

// Reads the list again on a mod thread when something said it changed.
void FavoritesRefreshIfDirty() {
    if (!g_favoritesDirty) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_favMutex);
        if (g_favoritesRunning) {
            return;
        }
        g_favoritesRunning = true;
    }
    g_favoritesDirty = false;
    bool started = StartModThread([]() {
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        auto list = FavoritesLoad();
        if (com) {
            CoUninitialize();
        }
        std::lock_guard<std::mutex> lock(g_favMutex);
        g_favoritesCache = std::move(list);
        g_favoritesRunning = false;
        g_favoritesVersion++;
    });
    if (!started) {
        std::lock_guard<std::mutex> lock(g_favMutex);
        g_favoritesRunning = false;
        g_favoritesDirty = true;
    }
}

// Pins and unpins run on a mod thread, then the list is read again.
void FavoritesPinLater(std::vector<std::wstring> paths, bool pin) {
    StartModThread([paths, pin]() {
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        for (auto const& path : paths) {
            if (pin) {
                FavoritesPin(path);
            } else {
                FavoritesUnpin(path);
            }
        }
        if (com) {
            CoUninitialize();
        }
        g_favoritesDirty = true;
    });
}

}  // namespace

// One thread per process watches the store of the pins, and marks the list stale when its write time changes.
void FavoritesWatchStart() {
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    StartModThread([]() {
        wchar_t* appData = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) || !appData) {
            return;
        }
        std::wstring folder = std::wstring(appData) + L"\\Microsoft\\Windows\\Recent\\AutomaticDestinations";
        CoTaskMemFree(appData);
        std::wstring store = folder + L"\\f01b4d95cf55d32a.automaticDestinations-ms";
        auto writeTime = [&]() {
            WIN32_FILE_ATTRIBUTE_DATA data = {};
            GetFileAttributesExW(store.c_str(), GetFileExInfoStandard, &data);
            return data.ftLastWriteTime;
        };
        HANDLE watch = FindFirstChangeNotificationW(folder.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
        if (watch == INVALID_HANDLE_VALUE) {
            return;
        }
        FILETIME last = writeTime();
        HANDLE waits[2] = {g_stopEvent, watch};
        while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            FILETIME now = writeTime();
            if (CompareFileTime(&now, &last) != 0) {
                last = now;
                g_favoritesDirty = true;
            }
            if (!FindNextChangeNotification(watch)) {
                break;
            }
        }
        FindCloseChangeNotification(watch);
    });
}

namespace {
void SidebarNavigate(WindowState* state, PCIDLIST_ABSOLUTE pidl);  // 10j2.
void FavoritesInsertPoint(SidebarPanel* panel, winrt::Windows::Foundation::Point point, size_t* index, float* lineY);
void SidebarShowInsertLine(SidebarPanel* panel, float lineY);

// The order of the list is the mod's own: the shell keeps pins in the order they were made and has no way to move one, so the
// order the user gave in the sidebar is kept as a setting (paths, one per line). A pin that is not in it comes after the others,
// in the shell's order.
const wchar_t kFavoritesOrderValue[] = L"sidebar.favorites.order";

std::vector<std::wstring> FavoritesOrderLoad() {
    std::vector<std::wstring> order;
    std::vector<wchar_t> buffer(32000);
    size_t length = Wh_GetStringValue(kFavoritesOrderValue, buffer.data(), buffer.size());
    if (!length || length >= buffer.size()) {
        return order;
    }
    std::wstring all(buffer.data(), length);
    size_t start = 0;
    while (start <= all.size()) {
        size_t end = all.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = all.size();
        }
        if (end > start) {
            order.push_back(all.substr(start, end - start));
        }
        start = end + 1;
    }
    return order;
}

void FavoritesApplyOrder(std::vector<FavoriteEntry>& list) {
    auto order = FavoritesOrderLoad();
    if (order.empty()) {
        return;
    }
    std::vector<FavoriteEntry> sorted;
    std::vector<bool> taken(list.size(), false);
    for (auto const& path : order) {
        for (size_t i = 0; i < list.size(); i++) {
            if (!taken[i] && _wcsicmp(list[i].path.c_str(), path.c_str()) == 0) {
                taken[i] = true;
                sorted.push_back(list[i]);
                break;
            }
        }
    }
    for (size_t i = 0; i < list.size(); i++) {
        if (!taken[i]) {
            sorted.push_back(list[i]);
        }
    }
    list = std::move(sorted);
}

// Puts `moving` at `index` of the list as it is shown (a folder already in the list moves there), and saves that order.
void FavoritesPlace(SidebarPanel* panel, std::vector<std::wstring> const& moving, size_t index) {
    auto isMoving = [&](std::wstring const& path) {
        for (auto const& m : moving) {
            if (_wcsicmp(m.c_str(), path.c_str()) == 0) {
                return true;
            }
        }
        return false;
    };
    std::vector<std::wstring> rest;
    size_t at = 0;
    for (size_t i = 0; i < panel->favorites.size(); i++) {
        if (isMoving(panel->favorites[i].path)) {
            continue;
        }
        if (i < index) {
            at++;
        }
        rest.push_back(panel->favorites[i].path);
    }
    std::vector<std::wstring> order(rest.begin(), rest.begin() + at);
    for (auto const& m : moving) {
        bool twice = false;
        for (auto const& o : order) {
            twice = twice || _wcsicmp(o.c_str(), m.c_str()) == 0;
        }
        if (!twice) {
            order.push_back(m);
        }
    }
    order.insert(order.end(), rest.begin() + at, rest.end());
    std::wstring text;
    for (auto const& path : order) {
        text += path + L"\n";
    }
    Wh_SetStringValue(kFavoritesOrderValue, text.c_str());
}
}

// The rows of the Favorites body, made again from the cached list.
void SidebarFillFavorites(WindowState* state, SidebarPanel* panel) {
    SidebarPanel::Section* section = nullptr;
    for (auto& s : panel->sections) {
        if (s.key == L"favorites") {
            section = &s;
        }
    }
    if (!section || !section->body) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_favMutex);
        panel->favorites = g_favoritesCache;
        panel->favoritesVersion = g_favoritesVersion;
    }
    FavoritesApplyOrder(panel->favorites);
    auto body = section->body;
    body.Children().Clear();
    panel->locationMarks.erase(std::remove_if(panel->locationMarks.begin(), panel->locationMarks.end(),
                                              [](SidebarPanel::Mark const& m) { return m.key.rfind(L"fav.", 0) == 0; }),
                               panel->locationMarks.end());
    for (auto const& entry : panel->favorites) {
        std::wstring key = L"fav." + entry.path;
        auto row = SidebarMakeRow(key, entry.name, false);
        if (entry.pidl) {
            SidebarRowIconFromShell(state, row, entry.pidl.get());
        }
        SidebarPanel::Mark mark;
        mark.key = key;
        mark.selected = row.selected;
        mark.pill = row.pill;
        mark.button = row.button;
        panel->locationMarks.push_back(std::move(mark));

        std::shared_ptr<ITEMIDLIST> pidl = entry.pidl;
        std::wstring path = entry.path;
        if (!entry.exists) {
            row.button.Opacity(0.5);
            muxc::ToolTipService::SetToolTip(row.button, winrt::box_value(winrt::hstring(Str(L"sidebar.missing"))));
        }
        Keep(row.button, row.button.Click(winrt::auto_revoke, [state, pidl](Inspectable const&, mux::RoutedEventArgs const&) {
            if (state->sidebar && state->sidebar->suppressClick) {
                state->sidebar->suppressClick = false;  // The release of a drag that moved the row.
                return;
            }
            if (pidl) {
                SidebarNavigate(state, pidl.get());
            }
        }));
        // Dragging a row to another place in the list: the order is the sidebar's own (the shell cannot reorder pins).
        SidebarAttachReorder(
            state, row.button,
            [state](winrt::Windows::Foundation::Point point, size_t* index, float* line) {
                if (state->sidebar) {
                    FavoritesInsertPoint(state->sidebar, point, index, line);
                }
            },
            [state](float line) {
                if (state->sidebar) {
                    SidebarShowInsertLine(state->sidebar, line);
                }
            },
            [state, path](size_t index) {
                SidebarPanel* now = state->sidebar;
                if (!now) {
                    return;
                }
                FavoritesPlace(now, {path}, index);
                SidebarFillFavorites(state, now);
            });

        // The shell's menu of the folder, with "Unpin from sidebar" (the shell's own unpin verbs are left out). A folder that is gone
        // has only that.
        if (pidl) {
            Keep(row.button, row.button.RightTapped(winrt::auto_revoke, [state, panel, button = row.button, pidl, path](Inspectable const&, mux::Input::RightTappedRoutedEventArgs const& args) {
                std::vector<MenuItemSet::Extra> extras;
                MenuItemSet::Extra unpin;
                unpin.text = Str(L"sidebar.unpin");
                unpin.glyph = L"\uE77A";
                unpin.action = [path]() { FavoritesPinLater({path}, false); };
                extras.push_back(std::move(unpin));
                SidebarShowItemMenu(state, state->sidebar, button, pidl.get(), path, std::move(extras), {L"pintohome", L"unpinfromhome"}, {}, args);
            }));
        } else {
            muxc::MenuFlyout flyout;
            muxc::MenuFlyoutItem unpin;
            unpin.Text(Str(L"sidebar.unpin"));
            Keep(unpin, unpin.Click(winrt::auto_revoke, [path](Inspectable const&, mux::RoutedEventArgs const&) {
                FavoritesPinLater({path}, false);
            }));
            flyout.Items().Append(unpin);
            row.button.ContextFlyout(flyout);
        }
        body.Children().Append(row.button);
    }
    SidebarMarkSelected(panel, panel->selectedKey);
    panel->lastFolder.reset();
}

void FavoritesTick(WindowState* state, SidebarPanel* panel) {
    FavoritesRefreshIfDirty();
    if (g_favoritesVersion != panel->favoritesVersion) {
        SidebarFillFavorites(state, panel);
    }
}

// ---- Drops on the sidebar ----

namespace {

struct SidebarDropZone {
    enum Kind { None, Pin, Folder, Tag } kind = None;
    std::wstring key;
    std::shared_ptr<ITEMIDLIST> pidl;
    std::wstring name;    // Folder, Tag: what the row is called (for the label of a drag).
    size_t index = 0;     // Pin: where in the list a dropped folder goes.
    float lineY = -1;     // Pin: where the line that shows it is, from the top of the block (-1: no line).
};

// The accent line between two rows of Favorites (lineY from the top of the block; below zero hides it).
void SidebarShowInsertLine(SidebarPanel* panel, float lineY) {
    if (!panel->favLine) {
        return;
    }
    if (lineY < 0) {
        // Collapsed, not only clear: its margin counts in the size of the block and would leave a blank after a row goes.
        panel->favLine.Visibility(mux::Visibility::Collapsed);
        return;
    }
    panel->favLine.Margin(mux::Thickness{10, lineY - 1, 10, 0});
    panel->favLine.Visibility(mux::Visibility::Visible);
    panel->favLine.Opacity(1);
}

bool SidebarRectOf(mux::UIElement const& element, winrt::Windows::Foundation::Rect* rect) {
    auto frame = element.try_as<mux::FrameworkElement>();
    if (!frame || frame.ActualWidth() <= 0 || frame.ActualHeight() <= 0) {
        return false;
    }
    auto corner = element.TransformToVisual(nullptr).TransformPoint(winrt::Windows::Foundation::Point{0, 0});
    *rect = winrt::Windows::Foundation::Rect{corner.X, corner.Y, (float)frame.ActualWidth(), (float)frame.ActualHeight()};
    return true;
}

bool SidebarRectHolds(winrt::Windows::Foundation::Rect const& r, winrt::Windows::Foundation::Point const& p) {
    return p.X >= r.X && p.X < r.X + r.Width && p.Y >= r.Y && p.Y < r.Y + r.Height;
}

// Between which two rows of Favorites a point (of the island) falls: before the first row whose middle is below it, else at the end.
// `lineY` is where the line that shows it goes, from the top of the block (-1: none).
void FavoritesInsertPoint(SidebarPanel* panel, winrt::Windows::Foundation::Point point, size_t* index, float* lineY) {
    winrt::Windows::Foundation::Rect block{};
    float top = panel->favBlock && SidebarRectOf(panel->favBlock, &block) ? block.Y : 0;
    size_t n = 0;
    float lastBottom = -1;
    *index = 0;
    *lineY = -1;
    for (auto const& mark : panel->locationMarks) {
        if (!mark.button || mark.key.rfind(L"fav.", 0) != 0) {
            continue;
        }
        winrt::Windows::Foundation::Rect row;
        if (SidebarRectOf(mark.button, &row)) {
            if (point.Y < row.Y + row.Height / 2) {
                *index = n;
                *lineY = row.Y - top;
                return;
            }
            lastBottom = row.Y + row.Height;
        }
        n++;
    }
    *index = n;
    *lineY = lastBottom >= 0 ? lastBottom - top : -1;
}

SidebarDropZone SidebarZoneAt(SidebarPanel* panel, POINTL screen) {
    SidebarDropZone zone;
    HWND window = (HWND)panel->bridge.WindowId().Value;
    POINT client = {screen.x, screen.y};
    ScreenToClient(window, &client);
    double scale = 1.0;
    if (auto root = panel->host.XamlRoot()) {
        scale = root.RasterizationScale();
    }
    if (scale <= 0) {
        scale = 1.0;
    }
    winrt::Windows::Foundation::Point point{(float)(client.x / scale), (float)(client.y / scale)};
    winrt::Windows::Foundation::Rect rect;
    for (auto const& mark : panel->locationMarks) {
        if (!mark.button || mark.key.rfind(L"fav.", 0) == 0 || mark.key == L"drives") {
            continue;
        }
        if (mark.key.rfind(L"tag.", 0) == 0) {
            if (SidebarRectOf(mark.button, &rect) && SidebarRectHolds(rect, point)) {
                zone.kind = SidebarDropZone::Tag;  // A row of Tags: what is dropped on it gets the tag.
                zone.key = mark.key;
                zone.name = mark.key.substr(4);
                return zone;
            }
            continue;
        }
        if (SidebarRectOf(mark.button, &rect) && SidebarRectHolds(rect, point)) {
            for (auto const& entry : panel->locations) {
                if (entry.key == mark.key && entry.pidl) {
                    zone.kind = SidebarDropZone::Folder;
                    zone.key = mark.key;
                    zone.name = entry.name;
                    zone.pidl = entry.pidl;
                    return zone;
                }
            }
            return zone;  // A row that takes no drop (Home, for one).
        }
    }
    if (panel->favBlock && SidebarRectOf(panel->favBlock, &rect) && SidebarRectHolds(rect, point)) {
        zone.kind = SidebarDropZone::Pin;
        zone.key = L"favorites";
        FavoritesInsertPoint(panel, point, &zone.index, &zone.lineY);
    }
    return zone;
}

struct SidebarDropTarget : IDropTarget {
    LONG refs = 1;
    IDataObject* data = nullptr;
    std::vector<std::wstring> folders;  // The folders of the drag, for pinning.
    std::vector<std::wstring> items;    // Everything on disk in the drag, for tagging.
    IDropTarget* folder = nullptr;      // The folder's own target, between its DragEnter and DragLeave.
    std::wstring folderKey;
    IDropTargetHelper* helper = nullptr;  // The shell's: draws the drag image and the label ("Pin to Favorites") while over us.
    bool helperActive = false;
    std::wstring described;               // What the label says now (kind and key), to write it again only when it changes.

    // The label of the drag: a DROPDESCRIPTION put in the data object, which the shell's helper draws next to the image.
    void Describe(std::wstring const& which, int imageType, const wchar_t* message, const wchar_t* insert) {
        if (!data || described == which) {
            return;
        }
        described = which;
        static UINT format = RegisterClipboardFormatW(CFSTR_DROPDESCRIPTION);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPDESCRIPTION));
        if (!memory) {
            return;
        }
        auto* description = (DROPDESCRIPTION*)GlobalLock(memory);
        description->type = (DROPIMAGETYPE)imageType;
        wcsncpy_s(description->szMessage, message, _TRUNCATE);
        wcsncpy_s(description->szInsert, insert, _TRUNCATE);
        GlobalUnlock(memory);
        FORMATETC wanted = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium = {};
        medium.tymed = TYMED_HGLOBAL;
        medium.hGlobal = memory;
        if (FAILED(data->SetData(&wanted, &medium, TRUE))) {
            GlobalFree(memory);
        }
    }

    void HelperOver(POINTL pt, DWORD effect, bool enter) {
        WindowState* state = StateForThisThread(false);
        SidebarPanel* panel = state ? state->sidebar : nullptr;
        if (!panel || !panel->dropWindow || !data || state->outDragActive) {
            return;  // A drag of the columns has its own label (GhostOver).
        }
        if (!helper) {
            CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper));
        }
        if (!helper) {
            return;
        }
        POINT p = {pt.x, pt.y};
        if (enter) {
            helperActive = SUCCEEDED(helper->DragEnter(panel->dropWindow, data, &p, effect));
        } else if (helperActive) {
            helper->DragOver(&p, effect);
        }
    }

    void HelperLeave() {
        if (helper && helperActive) {
            helper->DragLeave();
        }
        helperActive = false;
        described.clear();
    }

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

    void LeaveFolder(bool drop) {
        if (folder) {
            if (!drop) {
                folder->DragLeave();
            }
            folder->Release();
            folder = nullptr;
        }
        folderKey.clear();
    }

    // The row or section under the drag shows it takes the drop; the one it left goes back to normal.
    void Show(SidebarPanel* panel, std::wstring const& zoneKey) {
        if (panel->dropZone == zoneKey) {
            return;
        }
        panel->dropZone = zoneKey;
        if (panel->favHighlight) {
            panel->favHighlight.Opacity(zoneKey == L"favorites" ? 1 : 0);
        }
        SidebarMarkSelected(panel, panel->selectedKey);  // Back to the pill's row.
        if (!zoneKey.empty() && zoneKey != L"favorites") {
            for (auto& mark : panel->locationMarks) {
                if (mark.key == zoneKey) {
                    mark.selected.Visibility(mux::Visibility::Visible);
                }
            }
        }
    }

    SidebarDropZone lastZone;  // The zone the last move was over, for the label.

    // The label of a drag that began in the columns (the shell's helper draws it for the others): the same one the columns show,
    // in the sidebar's own overlay, with a line that says what a drop here does.
    void GhostGone() {
        WindowState* state = StateForThisThread(false);
        if (!state || !state->drag.ghost) {
            return;
        }
        SidebarPanel* panel = state->sidebar;
        if (!panel || !panel->ghostPopup) {
            return;  // Not the sidebar's label.
        }
        try {
            panel->ghostPopup.IsOpen(false);
            panel->ghostPopup.Child(nullptr);
        } catch (...) {
        }
        panel->ghostPopup = nullptr;
        auto& d = state->drag;
        d.ghost = nullptr;
        d.ghostText = nullptr;
        d.ghostAction = nullptr;
        d.ghostIcon = nullptr;
        d.ghostRow = nullptr;
    }

    void GhostOver(POINTL pt, DWORD keys, DWORD effect) {
        WindowState* state = StateForThisThread(false);
        SidebarPanel* panel = state ? state->sidebar : nullptr;
        if (!panel || !panel->overlay || !panel->bridge || !state->outDragActive) {
            return;
        }
        auto saved = state->overlay;
        state->overlay = panel->overlay;
        try {
            if (!state->drag.ghost) {
                ShowGhost(state, state->outSpec);
                // The label is made in the overlay, then moved to a popup that is not bound to the island: the sidebar is narrow
                // and would cut it.
                auto ghost = state->drag.ghost;
                uint32_t index = 0;
                if (panel->overlay.Children().IndexOf(ghost, index)) {
                    panel->overlay.Children().RemoveAt(index);
                }
                muxc::Primitives::Popup popup;
                popup.ShouldConstrainToRootBounds(false);
                popup.XamlRoot(panel->host.XamlRoot());
                popup.IsHitTestVisible(false);
                popup.Child(ghost);
                popup.IsOpen(true);
                panel->ghostPopup = popup;
            }
            HWND window = (HWND)panel->bridge.WindowId().Value;
            POINT client = {pt.x, pt.y};
            ScreenToClient(window, &client);
            double scale = 1.0;
            if (auto root = panel->host.XamlRoot()) {
                scale = root.RasterizationScale();
            }
            if (scale <= 0) {
                scale = 1.0;
            }
            panel->ghostPopup.HorizontalOffset(client.x / scale + 16);
            panel->ghostPopup.VerticalOffset(client.y / scale + 16);
            auto& d = state->drag;
            d.ghostText.Text(Shortened(d.labelName, 40).c_str());
            std::wstring line;
            const wchar_t* glyph = L"";
            auto fill = [](const wchar_t* pattern, std::wstring const& insert) {
                std::wstring text = pattern;
                size_t at = text.find(L"%1");
                if (at != std::wstring::npos) {
                    text.replace(at, 2, insert);
                }
                return text;
            };
            if (lastZone.kind == SidebarDropZone::Pin && effect != DROPEFFECT_NONE) {
                line = fill(Str(L"sidebar.drop.pin"), Str(L"sidebar.favorites"));
                glyph = L"";
            } else if (lastZone.kind == SidebarDropZone::Tag && effect != DROPEFFECT_NONE) {
                line = fill(Str(L"sidebar.drop.tag"), Shortened(lastZone.name, 28));
                glyph = L"";
            } else if (lastZone.kind == SidebarDropZone::Folder) {
                if (effect == DROPEFFECT_NONE) {
                    line = L"Can’t drop here";
                    glyph = L"";
                } else {
                    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                    const wchar_t* verb = L"Drop in ";
                    if (state->outFromArchive) {
                        verb = L"Extract to ";
                        glyph = L"";
                    } else if (ctrl && !shift) {
                        verb = L"Copy to ";
                        glyph = L"";
                    } else if (alt) {
                        verb = L"Link in ";
                        glyph = L"";
                    } else if (shift) {
                        verb = L"Move to ";
                    }
                    line = std::wstring(verb) + Shortened(lastZone.name, 28);
                }
            }
            if (line.empty()) {
                d.ghostRow.Visibility(mux::Visibility::Collapsed);
            } else {
                d.ghostIcon.Glyph(glyph);
                d.ghostAction.Text(line.c_str());
                d.ghostRow.Visibility(mux::Visibility::Visible);
            }
        } catch (...) {
            LogHresult(L"the label of a drag over the sidebar");
        }
        state->overlay = saved;
    }

    void Over(DWORD keys, POINTL pt, DWORD* effect) {
        lastZone = SidebarDropZone();
        OverCore(keys, pt, effect);
        GhostOver(pt, keys, *effect);
    }

    void OverCore(DWORD keys, POINTL pt, DWORD* effect) {
        DWORD wanted = *effect;
        *effect = DROPEFFECT_NONE;
        WindowState* state = StateForThisThread(false);
        SidebarPanel* panel = state ? state->sidebar : nullptr;
        if (!panel || !panel->bridge || !data) {
            LeaveFolder(false);
            return;
        }
        SidebarDropZone zone;
        try {
            zone = SidebarZoneAt(panel, pt);
        } catch (...) {
            LogHresult(L"finding the sidebar's drop zone");
        }
        lastZone = zone;
        SidebarShowInsertLine(panel, zone.kind == SidebarDropZone::Pin && !folders.empty() ? zone.lineY : -1);
        if (zone.kind == SidebarDropZone::Pin) {
            LeaveFolder(false);
            if (!folders.empty()) {
                *effect = DROPEFFECT_LINK;
                Describe(L"pin", DROPIMAGE_LINK, Str(L"sidebar.drop.pin"), Str(L"sidebar.favorites"));
                Show(panel, zone.key);
            } else {
                Describe(L"none", DROPIMAGE_NONE, L"", L"");
                Show(panel, L"");
            }
            return;
        }
        if (zone.kind == SidebarDropZone::Tag) {
            LeaveFolder(false);
            if (!items.empty()) {
                *effect = DROPEFFECT_LINK;
                Describe(zone.key, DROPIMAGE_LABEL, Str(L"sidebar.drop.tag"), zone.key.substr(4).c_str());
                Show(panel, zone.key);
            } else {
                Describe(L"none", DROPIMAGE_NONE, L"", L"");
                Show(panel, L"");
            }
            return;
        }
        if (zone.kind != SidebarDropZone::Folder) {
            Describe(L"none", DROPIMAGE_NONE, L"", L"");
        } else {
            described.clear();  // The folder's own target writes the label.
        }
        if (zone.kind == SidebarDropZone::Folder) {
            if (!folder || folderKey != zone.key) {
                LeaveFolder(false);
                IShellFolder* shellFolder = nullptr;
                IDropTarget* created = nullptr;
                if (SUCCEEDED(SHBindToObject(nullptr, zone.pidl.get(), nullptr, IID_PPV_ARGS(&shellFolder))) && shellFolder) {
                    shellFolder->CreateViewObject(state->topLevel, IID_PPV_ARGS(&created));
                    shellFolder->Release();
                }
                if (!created) {
                    Show(panel, L"");
                    return;
                }
                DWORD offered = wanted;
                if (FAILED(created->DragEnter(data, keys, pt, &offered))) {
                    created->Release();
                    Show(panel, L"");
                    return;
                }
                folder = created;
                folderKey = zone.key;
                *effect = offered;
            } else {
                DWORD offered = wanted;
                if (SUCCEEDED(folder->DragOver(keys, pt, &offered))) {
                    *effect = offered;
                }
            }
            Show(panel, *effect != DROPEFFECT_NONE ? zone.key : L"");
            return;
        }
        LeaveFolder(false);
        Show(panel, L"");
    }

    void Finish() {
        WindowState* state = StateForThisThread(false);
        if (state && state->sidebar) {
            SidebarShowInsertLine(state->sidebar, -1);
            Show(state->sidebar, L"");
        }
        if (data) {
            data->Release();
            data = nullptr;
        }
        folders.clear();
        items.clear();
    }

    STDMETHODIMP DragEnter(IDataObject* object, DWORD keys, POINTL pt, DWORD* effect) override {
        if (data) {
            data->Release();
        }
        data = object;
        if (data) {
            data->AddRef();
        }
        folders = FavoritesFoldersOf(data);
        items = TagsItemsOf(data);
        described.clear();
        Over(keys, pt, effect);
        HelperOver(pt, *effect, true);
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD keys, POINTL pt, DWORD* effect) override {
        Over(keys, pt, effect);
        HelperOver(pt, *effect, false);
        return S_OK;
    }
    STDMETHODIMP DragLeave() override {
        GhostGone();
        HelperLeave();
        LeaveFolder(false);
        Finish();
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* object, DWORD keys, POINTL pt, DWORD* effect) override {
        DWORD wanted = *effect;
        *effect = DROPEFFECT_NONE;
        WindowState* state = StateForThisThread(false);
        SidebarPanel* panel = state ? state->sidebar : nullptr;
        SidebarDropZone zone;
        if (panel && panel->bridge) {
            try {
                zone = SidebarZoneAt(panel, pt);
            } catch (...) {
                LogHresult(L"finding the sidebar's drop zone");
            }
        }
        if (zone.kind == SidebarDropZone::Pin && !folders.empty()) {
            std::vector<std::wstring> pins = folders;
            LeaveFolder(false);
            FavoritesPlace(panel, pins, zone.index);
            std::vector<std::wstring> fresh;  // Those not pinned yet: a folder already in the list only moves.
            for (auto const& path : pins) {
                bool listed = false;
                for (auto const& entry : panel->favorites) {
                    listed = listed || _wcsicmp(entry.path.c_str(), path.c_str()) == 0;
                }
                if (!listed) {
                    fresh.push_back(path);
                }
            }
            if (!fresh.empty()) {
                FavoritesPinLater(fresh, true);
            }
            SidebarFillFavorites(state, panel);  // The new order shows at once.
            *effect = DROPEFFECT_LINK;
        } else if (zone.kind == SidebarDropZone::Tag && !items.empty()) {
            LeaveFolder(false);
            TagsDropLater(zone.key.substr(4), items);
            *effect = DROPEFFECT_LINK;
        } else if (zone.kind == SidebarDropZone::Folder && folder && folderKey == zone.key) {
            DWORD offered = wanted;
            if (SUCCEEDED(folder->Drop(object, keys, pt, &offered))) {
                *effect = offered;
            }
            LeaveFolder(true);
        } else {
            LeaveFolder(false);
        }
        if (helper && helperActive) {
            POINT p = {pt.x, pt.y};
            helper->Drop(object, &p, *effect);
            helperActive = false;
        }
        described.clear();
        GhostGone();
        Finish();
        return S_OK;
    }
};

}  // namespace

void SidebarRegisterDrop(WindowState* state, SidebarPanel* panel) {
    if (panel->drop || !panel->bridge) {
        return;
    }
    HWND window = (HWND)panel->bridge.WindowId().Value;
    auto* target = new SidebarDropTarget();
    HRESULT hr = RegisterDragDrop(window, target);
    if (hr == DRAGDROP_E_ALREADYREGISTERED) {
        RevokeDragDrop(window);  // The XAML island's own, which nothing here uses.
        hr = RegisterDragDrop(window, target);
    }
    if (FAILED(hr)) {
        Wh_Log(L"the sidebar could not take drops, hr=0x%08lX", hr);
        target->Release();
        return;
    }
    panel->drop = target;
    panel->dropWindow = window;
}

void SidebarRevokeDrop(SidebarPanel* panel) {
    if (!panel->drop) {
        return;
    }
    auto* target = static_cast<SidebarDropTarget*>(panel->drop);
    panel->drop = nullptr;
    if (IsWindow(panel->dropWindow)) {
        RevokeDragDrop(panel->dropWindow);
    }
    panel->dropWindow = nullptr;
    target->LeaveFolder(false);
    target->GhostGone();
    target->HelperLeave();
    if (target->helper) {
        target->helper->Release();
        target->helper = nullptr;
    }
    if (target->data) {
        target->data->Release();
        target->data = nullptr;
    }
    target->Release();
}
