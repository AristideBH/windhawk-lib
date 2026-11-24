// Sidebar, Locations section: a fixed set of places (Home, the user's folder, Gallery, the drives and devices, Network, the
// Recycle Bin and the cloud folders), each of which the user can show or hide. Drives are listed flat up to two of them and
// grouped under "Drives and devices" from three. A click opens the place in the window; the entry that contains the folder
// the window shows is the selected one.
//
// The pure parts (grouping, the entry that contains a folder, the saved visibility) are tested by 10j2a. The rest builds rows
// with the shell of 10j. Dropping on an entry comes with the drop target of Task 6, which Favorites needs as well.

bool LocationsGroupedFor(size_t driveEntryCount, bool flatOnly) {
    return !flatOnly && driveEntryCount >= 3;
}

bool LocationsGrouped(size_t driveEntryCount) {
    return LocationsGroupedFor(driveEntryCount, g_sidebarDrivesFlat);
}

namespace {

std::wstring LocationValueName(std::wstring const& key) {
    return L"sidebar.loc." + key;
}

std::wstring LocationTrimmed(std::wstring path) {
    while (!path.empty() && path.back() == L'\\') {
        path.pop_back();
    }
    return path;
}

std::shared_ptr<ITEMIDLIST> LocationPidl(PIDLIST_ABSOLUTE pidl) {
    return std::shared_ptr<ITEMIDLIST>(pidl, [](ITEMIDLIST* p) {
        if (p) {
            CoTaskMemFree(p);
        }
    });
}

std::wstring LocationDisplayName(PCIDLIST_ABSOLUTE pidl) {
    std::wstring text;
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&item))) && item) {
        wchar_t* name = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) {
            text = name;
            CoTaskMemFree(name);
        }
        item->Release();
    }
    return text;
}

PIDLIST_ABSOLUTE LocationParse(const wchar_t* parsingName) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(parsingName, nullptr, &pidl, 0, nullptr))) {
        return nullptr;
    }
    return pidl;
}

void LocationAdd(std::vector<LocationEntry>& list, std::wstring key, PIDLIST_ABSOLUTE pidl, std::wstring path, bool drive) {
    if (!pidl) {
        return;
    }
    LocationEntry entry;
    entry.key = std::move(key);
    entry.pidl = LocationPidl(pidl);
    entry.name = LocationDisplayName(pidl);
    if (entry.name.empty()) {
        entry.name = entry.key;
    }
    entry.path = std::move(path);
    entry.drive = drive;
    entry.visible = LocationVisible(entry.key);
    list.push_back(std::move(entry));
}

void LocationAddKnown(std::vector<LocationEntry>& list, const wchar_t* key, REFKNOWNFOLDERID id, bool withPath) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetKnownFolderIDList(id, 0, nullptr, &pidl)) || !pidl) {
        return;
    }
    std::wstring path;
    if (withPath) {
        wchar_t* text = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &text)) && text) {
            path = text;
            CoTaskMemFree(text);
        }
    }
    LocationAdd(list, key, pidl, path, false);
}

// The cloud folders the sync clients registered (OneDrive and the like): HKCU\...\SyncRootManager\<id>\UserSyncRoots holds
// one value per user, whose data is the folder.
void LocationAddCloudRoots(std::vector<LocationEntry>& list) {
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SyncRootManager", 0,
                      KEY_READ, &root) != ERROR_SUCCESS) {
        return;
    }
    wchar_t id[256];
    for (DWORD i = 0;; i++) {
        DWORD length = ARRAYSIZE(id);
        if (RegEnumKeyExW(root, i, id, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        std::wstring sub = std::wstring(id) + L"\\UserSyncRoots";
        HKEY roots = nullptr;
        if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &roots) != ERROR_SUCCESS) {
            continue;
        }
        wchar_t valueName[128];
        wchar_t data[MAX_PATH * 2];
        DWORD nameLength = ARRAYSIZE(valueName);
        DWORD dataBytes = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegEnumValueW(roots, 0, valueName, &nameLength, nullptr, &type, (BYTE*)data, &dataBytes) == ERROR_SUCCESS &&
            type == REG_SZ) {
            data[dataBytes / sizeof(wchar_t)] = 0;
            std::wstring path = data;
            bool known = false;
            for (auto const& entry : list) {
                if (!entry.path.empty() && _wcsicmp(entry.path.c_str(), path.c_str()) == 0) {
                    known = true;
                }
            }
            if (!known && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                LocationAdd(list, L"cloud." + std::wstring(id), LocationParse(path.c_str()), path, false);
            }
        }
        RegCloseKey(roots);
    }
    RegCloseKey(root);
}

void LocationAddFolder(std::vector<LocationEntry>& list, std::wstring key, std::wstring const& path) {
    for (auto const& entry : list) {
        if (!entry.path.empty() && _wcsicmp(entry.path.c_str(), path.c_str()) == 0) {
            return;
        }
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        LocationAdd(list, std::move(key), LocationParse(path.c_str()), path, true);  // Under "Drives and devices", with the drives.
    }
}

// OneDrive, from its own accounts (HKCU\Software\Microsoft\OneDrive\Accounts\<name>\UserFolder): SyncRootManager can be empty
// while OneDrive runs. And the phones Phone Link shows as folders under %USERPROFILE%\CrossDevice, one folder per phone.
void LocationAddOneDriveAndPhones(std::vector<LocationEntry>& list) {
    HKEY accounts = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\OneDrive\\Accounts", 0, KEY_READ, &accounts) == ERROR_SUCCESS) {
        wchar_t name[256];
        for (DWORD i = 0;; i++) {
            DWORD length = ARRAYSIZE(name);
            if (RegEnumKeyExW(accounts, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                break;
            }
            wchar_t folder[MAX_PATH * 2];
            DWORD bytes = sizeof(folder) - sizeof(wchar_t);
            DWORD type = 0;
            if (RegGetValueW(accounts, name, L"UserFolder", RRF_RT_REG_SZ, &type, folder, &bytes) == ERROR_SUCCESS) {
                LocationAddFolder(list, L"onedrive." + std::wstring(name), folder);
            }
        }
        RegCloseKey(accounts);
    }
    wchar_t* profile = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile)) && profile) {
        std::wstring cross = std::wstring(profile) + L"\\CrossDevice";
        CoTaskMemFree(profile);
        WIN32_FIND_DATAW found;
        HANDLE find = FindFirstFileExW((cross + L"\\*").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, 0);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(found.cFileName, L".") != 0 &&
                    wcscmp(found.cFileName, L"..") != 0) {
                    LocationAddFolder(list, L"phone." + std::wstring(found.cFileName), cross + L"\\" + found.cFileName);
                }
            } while (FindNextFileW(find, &found));
            FindClose(find);
        }
    }
}

// Network drives (mapped letters). The shell can take seconds to name one that is disconnected, so they are named on a mod
// thread and join the list when they are ready (the version changes and the sidebar fills the section again).
std::mutex g_remoteMutex;
[[clang::no_destroy]] std::vector<LocationEntry> g_remoteEntries;
DWORD g_remoteMask = 0;
bool g_remoteRunning = false;
std::atomic<unsigned> g_remoteVersion{0};

DWORD LocationRemoteMask() {
    DWORD mask = GetLogicalDrives();
    DWORD remote = 0;
    for (int i = 0; i < 26; i++) {
        if (mask & (1u << i)) {
            wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
            if (GetDriveTypeW(root) == DRIVE_REMOTE) {
                remote |= 1u << i;
            }
        }
    }
    return remote;
}

void LocationRefreshRemote() {
    DWORD remote = LocationRemoteMask();
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        if (remote == g_remoteMask || g_remoteRunning) {
            return;
        }
        g_remoteRunning = true;
    }
    bool started = StartModThread([remote]() {
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        std::vector<LocationEntry> found;
        for (int i = 0; i < 26; i++) {
            if (!(remote & (1u << i))) {
                continue;
            }
            wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
            LocationAdd(found, std::wstring(L"drive.") + (wchar_t)(L'A' + i), LocationParse(root), root, true);
        }
        if (com) {
            CoUninitialize();
        }
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        g_remoteEntries = std::move(found);
        g_remoteMask = remote;
        g_remoteRunning = false;
        g_remoteVersion++;
    });
    if (!started) {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        g_remoteRunning = false;
    }
}

}  // namespace

bool LocationVisible(std::wstring const& key) {
    return Wh_GetIntValue(LocationValueName(key).c_str(), 1) != 0;
}

void LocationSetVisible(std::wstring const& key, bool visible) {
    Wh_SetIntValue(LocationValueName(key).c_str(), visible ? 1 : 0);
}

// The key of the entry whose folder holds `folder`, the closest one when several do; empty when none does.
std::wstring SidebarEntryIn(std::vector<LocationPath> const& entries, std::wstring const& folder) {
    std::wstring best;
    size_t bestLength = 0;
    for (auto const& entry : entries) {
        std::wstring path = LocationTrimmed(entry.path);
        if (path.empty() || folder.size() < path.size()) {
            continue;
        }
        if (_wcsnicmp(folder.c_str(), path.c_str(), path.size()) != 0) {
            continue;
        }
        if (folder.size() != path.size() && folder[path.size()] != L'\\') {
            continue;
        }
        if (best.empty() || path.size() > bestLength) {
            best = entry.key;
            bestLength = path.size();
        }
    }
    return best;
}

// Every place the section can list, in the order it lists them, hidden ones included (`visible` says which are shown).
// Drives that are not ready or remote are named by the shell, so a slow network drive is left out: it would stall the window.
std::vector<LocationEntry> LocationsList() {
    std::vector<LocationEntry> list;
    LocationAdd(list, L"home", LocationParse(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}"), L"", false);
    LocationAddKnown(list, L"user", FOLDERID_Profile, true);
    LocationAdd(list, L"gallery", LocationParse(L"shell:::{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}"), L"", false);
    LocationAddKnown(list, L"recent", FOLDERID_Recent, true);  // Windows' Recent: the files and folders opened lately.
    if (!list.empty() && list.back().key == L"recent") {
        list.back().name = Str(L"sidebar.recent");  // The shell's own name for it is a long one ("Éléments récents").
    }
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) {
            continue;
        }
        wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
        UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_CDROM) {  // Network drives: LocationRefreshRemote.
            continue;
        }
        LocationAdd(list, std::wstring(L"drive.") + (wchar_t)(L'A' + i), LocationParse(root), root, true);
    }
    LocationRefreshRemote();
    {
        std::lock_guard<std::mutex> lock(g_remoteMutex);
        for (auto const& entry : g_remoteEntries) {
            list.push_back(entry);
            list.back().visible = LocationVisible(entry.key);
        }
    }
    LocationAddKnown(list, L"network", FOLDERID_NetworkFolder, false);
    LocationAddKnown(list, L"recyclebin", FOLDERID_RecycleBinFolder, false);
    LocationAddCloudRoots(list);
    LocationAddOneDriveAndPhones(list);
    return list;
}

namespace {

void SidebarNavigate(WindowState* state, PCIDLIST_ABSOLUTE pidl) {
    TagViewLeaveHere(state);  // Any navigation closes the list of a tag's files (of this tab).
    IShellBrowser* browser = state ? BrowserOf(state->topLevel) : nullptr;
    if (!browser || !pidl) {
        return;
    }
    // The flags of the native pane's own click (0x80001): the BrowseObject hook sees a pane click, as Columns expects, and
    // Explorer's file list does not take the keyboard.
    HRESULT hr = browser->BrowseObject(pidl, SBSP_SAMEBROWSER | SBSP_ABSOLUTE | SBSP_ACTIVATE_NOFOCUS);
    if (FAILED(hr)) {
        Wh_Log(L"sidebar: opening a location failed, hr=0x%08lX", hr);
    }
    if (!g_sidebarKeyboard && state->queue) {
        // The click gave the sidebar the keyboard: it goes back to the file area (the columns' panel, or Explorer's own view) so that
        // the arrow keys go on moving through the folder, as they did before the click.
        state->queue.TryEnqueue([state]() {
            if (g_unloading || StateForThisThread(false) != state) {
                return;
            }
            HWND target = state->on && state->bridge ? (HWND)state->bridge.WindowId().Value
                                                     : FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
            if (target && IsWindow(target)) {
                SetFocus(target);
            }
        });
    }
}

// The pill and the backing go to one row: the one of `key`, or the Drives group when that holds the key and is closed.
void SidebarPlacePill(SidebarPanel* panel) {
    if (!panel->pill || !panel->pillHost) {
        return;
    }
    for (auto const& mark : panel->locationMarks) {
        if (mark.key != panel->shownKey || !mark.button) {
            continue;
        }
        // In a section that is collapsed the pill goes to the section's header, so that it still says where the window is.
        muxc::Button target = mark.button;
        const wchar_t* sectionKey = mark.key.rfind(L"fav.", 0) == 0 ? L"favorites" : mark.key.rfind(L"tag.", 0) == 0 || mark.key == L"tags" ? L"tags" : L"locations";
        for (auto const& section : panel->sections) {
            if (section.key == sectionKey && section.collapsed && section.headerButton) {
                target = section.headerButton;
            }
        }
        double height = target.ActualHeight();
        if (height <= 0 || target.ActualWidth() <= 0) {
            break;  // Not laid out: no pill.
        }
        // On a header it is a dot as wide as the pill, not a bar.
        double pillHeight = target == mark.button ? 16 : 3;
        if (panel->pill.Height() != pillHeight) {
            panel->pill.Height(pillHeight);
        }
        double y = target.TransformToVisual(panel->pillHost).TransformPoint(winrt::Windows::Foundation::Point{0, 0}).Y + (height - pillHeight) / 2;
        if (!panel->pillPlaced) {
            // The first place is taken at once, with no slide from the corner.
            panel->pill.TranslationTransition(nullptr);
            panel->pill.Translation(winrt::Windows::Foundation::Numerics::float3{0, (float)y, 0});
            {
                mux::Vector3Transition slide;
                slide.Duration(std::chrono::milliseconds(140));
                panel->pill.TranslationTransition(slide);
            }
            panel->pillPlaced = true;
        } else {
            auto now = panel->pill.Translation();
            if (fabsf(now.y - (float)y) > 0.5f) {
                panel->pill.Translation(winrt::Windows::Foundation::Numerics::float3{0, (float)y, 0});
            }
        }
        panel->pill.Opacity(1);
        return;
    }
    panel->pill.Opacity(0);
    panel->pillPlaced = false;
}

void SidebarMarkSelected(SidebarPanel* panel, std::wstring const& key) {
    panel->selectedKey = key;
    std::wstring shown = key;
    if (key.rfind(L"drive.", 0) == 0 && panel->driveChildren && panel->driveChildren.Visibility() != mux::Visibility::Visible) {
        shown = L"drives";
    }
    for (auto& mark : panel->locationMarks) {
        mark.selected.Visibility(mark.key == shown ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        mark.pill.Visibility(mux::Visibility::Collapsed);  // The row's own pill is not used: one pill slides (SidebarPlacePill).
    }
    panel->shownKey = shown;
    SidebarPlacePill(panel);
}

void SidebarAddMark(SidebarPanel* panel, std::wstring const& key, SidebarRow const& row) {
    SidebarPanel::Mark mark;
    mark.key = key;
    mark.selected = row.selected;
    mark.pill = row.pill;
    mark.button = row.button;
    panel->locationMarks.push_back(std::move(mark));
}

// The free-space bar of a drive's row: a 3 px line along the bottom of the row's face (it takes no room), as wide as the text. Its
// two columns are given the used and the free space when the drive has been asked, on a mod thread.
void SidebarAddSpaceBar(SidebarPanel* panel, SidebarRow const& row, LocationEntry const& entry, double indent) {
    muxc::Grid bar;
    bar.Height(2);
    bar.VerticalAlignment(mux::VerticalAlignment::Bottom);
    bar.Margin(mux::Thickness{15 + indent + 26, 0, 12, 2});
    bar.IsHitTestVisible(false);
    bar.Visibility(mux::Visibility::Collapsed);
    mux::Automation::AutomationProperties::SetAutomationId(bar, L"SidebarSpace." + entry.key);
    muxc::ColumnDefinition used, free;
    used.Width(mux::GridLength{0, mux::GridUnitType::Star});
    free.Width(mux::GridLength{1, mux::GridUnitType::Star});
    bar.ColumnDefinitions().Append(used);
    bar.ColumnDefinitions().Append(free);
    auto track = SidebarThemedBorder(L"ControlFillColorTertiaryBrush");
    track.CornerRadius(mux::CornerRadius{1, 1, 1, 1});
    muxc::Grid::SetColumnSpan(track, 2);
    bar.Children().Append(track);
    auto fill = SidebarThemedBorder(L"AccentFillColorDefaultBrush");
    fill.CornerRadius(mux::CornerRadius{1, 1, 1, 1});
    muxc::Grid::SetColumn(fill, 0);
    bar.Children().Append(fill);
    row.content.Children().Append(bar);
    SidebarPanel::DriveBar remembered;
    remembered.path = entry.path;
    remembered.bar = bar;
    panel->driveBars.push_back(std::move(remembered));
}

// Asks the drives of the bars how full they are (on a mod thread: a card reader without a card can take seconds) and sets the bars.
void SidebarRefreshDriveSpace(WindowState* state, SidebarPanel* panel) {
    if (panel->driveBars.empty() || !state->queue) {
        return;
    }
    auto bars = panel->driveBars;
    mud::DispatcherQueue queue = state->queue;
    StartModThread([bars, queue]() {
        auto result = std::make_shared<std::vector<std::pair<double, double>>>();  // used, total (bytes)
        for (auto const& entry : bars) {
            ULARGE_INTEGER freeBytes{}, total{}, totalFree{};
            if (GetDiskFreeSpaceExW(entry.path.c_str(), &freeBytes, &total, &totalFree) && total.QuadPart > 0) {
                result->push_back({(double)(total.QuadPart - freeBytes.QuadPart), (double)total.QuadPart});
            } else {
                result->push_back({0, 0});
            }
        }
        if (g_unloading) {
            return;
        }
        try {
            queue.TryEnqueue([bars, result]() {
                if (g_unloading) {
                    return;
                }
                for (size_t i = 0; i < bars.size() && i < result->size(); i++) {
                    auto bar = bars[i].bar.get();
                    // The fill is the second child of the bar (a weak reference to a Border made by the XAML reader does not resolve).
                    muxc::Border fill = bar && bar.Children().Size() > 1 ? bar.Children().GetAt(1).try_as<muxc::Border>() : muxc::Border{nullptr};
                    auto [used, total] = (*result)[i];
                    if (!bar || !fill || total <= 0) {
                        continue;
                    }
                    // Shares, not bytes: star weights of hundreds of gigabytes are asking for trouble.
                    bar.ColumnDefinitions().GetAt(0).Width(mux::GridLength{used / total, mux::GridUnitType::Star});
                    bar.ColumnDefinitions().GetAt(1).Width(mux::GridLength{1 - used / total, mux::GridUnitType::Star});
                    // Nearly full: the colour of a warning.
                    try {
                        fill.Background(mux::Application::Current().Resources().Lookup(
                            winrt::box_value(used / total > 0.9 ? L"SystemFillColorCriticalBrush" : L"AccentFillColorDefaultBrush")).as<mux::Media::Brush>());
                    } catch (...) {
                    }
                    bar.Visibility(mux::Visibility::Visible);
                }
            });
        } catch (...) {
        }
    });
}

// The eject button of a removable drive's row: at the right, shown while the pointer is over the row. It asks the shell's own
// "Eject" verb of the drive, on a mod thread.
void SidebarAddEject(SidebarRow const& row, LocationEntry const& entry) {
    auto button = SidebarNewButtonWith((L"SidebarEject." + entry.key).c_str(), 24, L"0", kSidebarSmallTemplate);
    button.Width(24);
    button.HorizontalAlignment(mux::HorizontalAlignment::Right);
    button.VerticalAlignment(mux::VerticalAlignment::Center);
    button.Margin(mux::Thickness{0, 0, 8, 0});
    // The eject symbol: a triangle over a bar, drawn as a shape (no font has it under a code I could check).
    button.Content(LoadXaml(std::wstring(L"<Path ") + kXamlNs +
                            L" Data='M 8,3 L 13,9 L 3,9 Z M 3,11 L 13,11 L 13,13 L 3,13 Z' Width='16' Height='16' Stretch='None' "
                            L"Fill='{ThemeResource TextFillColorSecondaryBrush}'/>"));
    button.Opacity(0);
    button.OpacityTransition(mux::ScalarTransition());
    mux::Automation::AutomationProperties::SetName(button, Str(L"sidebar.eject"));
    row.content.Children().Append(button);
    winrt::weak_ref<muxc::Button> weak = button;
    Keep(row.button, row.button.PointerEntered(winrt::auto_revoke, [weak](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
        if (auto b = weak.get()) {
            b.Opacity(1);
        }
    }));
    Keep(row.button, row.button.PointerExited(winrt::auto_revoke, [weak](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
        if (auto b = weak.get()) {
            b.Opacity(0);
        }
    }));
    std::shared_ptr<ITEMIDLIST> pidl = entry.pidl;
    Keep(button, button.Click(winrt::auto_revoke, [pidl](Inspectable const&, mux::RoutedEventArgs const&) {
        StartModThread([pidl]() {
            bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
            IShellFolder* parent = nullptr;
            PCUITEMID_CHILD child = nullptr;
            if (SUCCEEDED(SHBindToParent(pidl.get(), IID_PPV_ARGS(&parent), &child)) && parent) {
                IContextMenu* menu = nullptr;
                if (SUCCEEDED(parent->GetUIObjectOf(nullptr, 1, &child, IID_IContextMenu, nullptr, (void**)&menu)) && menu) {
                    HMENU popup = CreatePopupMenu();
                    menu->QueryContextMenu(popup, 0, 1, 0x7FFF, CMF_NORMAL);
                    CMINVOKECOMMANDINFO invoke = {sizeof(invoke)};
                    invoke.fMask = CMIC_MASK_FLAG_NO_UI;
                    invoke.lpVerb = "eject";
                    invoke.nShow = SW_HIDE;
                    HRESULT hr = menu->InvokeCommand(&invoke);
                    Wh_Log(L"sidebar: eject, hr=0x%08lX", hr);
                    DestroyMenu(popup);
                    menu->Release();
                }
                parent->Release();
            }
            if (com) {
                CoUninitialize();
            }
        });
    }));
}

// One place as a row that opens it, with "Hide from sidebar" in its context menu.
SidebarRow SidebarLocationRow(WindowState* state, SidebarPanel* panel, LocationEntry const& entry, double indent) {
    auto row = SidebarMakeRow(entry.key, entry.name, false, indent);
    SidebarRowIconFromShell(state, row, entry.pidl.get());
    SidebarAddMark(panel, entry.key, row);
    if (entry.key.rfind(L"drive.", 0) == 0 && entry.path.size() >= 2) {
        UINT type = GetDriveTypeW((entry.path.substr(0, 2) + L"\\").c_str());  // Asking the type does not wake the drive.
        if (g_sidebarDriveSpace && (type == DRIVE_FIXED || type == DRIVE_REMOVABLE)) {
            SidebarAddSpaceBar(panel, row, entry, indent);
        }
        if (g_sidebarDriveEject && (type == DRIVE_REMOVABLE || type == DRIVE_CDROM) && entry.pidl) {
            SidebarAddEject(row, entry);
        }
    }

    std::shared_ptr<ITEMIDLIST> pidl = entry.pidl;
    Keep(row.button, row.button.Click(winrt::auto_revoke, [state, pidl](Inspectable const&, mux::RoutedEventArgs const&) {
        SidebarNavigate(state, pidl.get());
    }));

    // The shell's menu of the place, with "Hide from sidebar".
    std::wstring key = entry.key;
    std::wstring path = entry.path;
    Keep(row.button, row.button.RightTapped(winrt::auto_revoke, [state, panel, button = row.button, pidl, key, path](Inspectable const&, mux::Input::RightTappedRoutedEventArgs const& args) {
        std::vector<MenuItemSet::Extra> extras;
        MenuItemSet::Extra hide;
        hide.text = Str(L"sidebar.hide");
        hide.glyph = L"\uED1A";
        hide.action = [state, key]() {
            LocationSetVisible(key, false);
            if (state->queue) {
                state->queue.TryEnqueue([state]() {
                    if (state->sidebar) {
                        SidebarFillLocations(state, state->sidebar);
                    }
                });
            }
        };
        extras.push_back(std::move(hide));
        // The Recycle Bin's "Empty Recycle Bin" and a drive's Eject and Format are rows of their own, not behind "More options".
        std::vector<std::wstring> promote;
        if (key == L"recyclebin") {
            promote = {L"empty"};
        } else if (key.rfind(L"drive.", 0) == 0) {
            promote = {L"eject", L"format"};
        }
        SidebarShowItemMenu(state, panel, button, pidl.get(), path, std::move(extras), {}, std::move(promote), args);
    }));
    return row;
}

// The "Drives and devices" group: a row, and the drives under it, each hung from the row by an elbow. The group closes and opens
// only when the user chose that (the "collapsible" setting); otherwise it is always open and a click on its row opens This PC.
void SidebarAddDriveGroup(WindowState* state, SidebarPanel* panel, muxc::StackPanel const& body, std::vector<LocationEntry> const& drives) {
    PIDLIST_ABSOLUTE computer = nullptr;
    SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &computer);
    // Named as the shell names This PC (in the user's language), since a click on the row opens it.
    std::wstring groupName = computer ? LocationDisplayName(computer) : std::wstring();
    auto groupRow = SidebarMakeRow(L"drives", groupName.empty() ? std::wstring(Str(L"sidebar.drives")) : groupName, false);
    std::shared_ptr<ITEMIDLIST> computerPidl;
    if (computer) {
        SidebarRowIconFromShell(state, groupRow, computer);
        computerPidl = LocationPidl(computer);
    }
    SidebarAddMark(panel, L"drives", groupRow);
    if (computerPidl) {
        // This PC's own shell menu (Open, Manage, Map network drive, Properties), with "Map network drive" as a row of its own (the
        // shell's id of that verb is not written anywhere I could read: the likely ones are listed, the debug log names the rest).
        Keep(groupRow.button, groupRow.button.RightTapped(winrt::auto_revoke, [state, panel, button = groupRow.button, computerPidl](Inspectable const&, mux::Input::RightTappedRoutedEventArgs const& args) {
            SidebarShowItemMenu(state, panel, button, computerPidl.get(), std::wstring(), {}, {},
                                {L"connectnetworkdrive", L"mapnetdrive", L"mapnetworkdrive", L"mapdrive", L"connect"}, args);
        }));
    }
    bool collapsible = g_sidebarDrivesCollapsible;
    muxc::FontIcon chevron{nullptr};
    if (collapsible) {
        chevron = SidebarGlyph(L"\uE70E", 12);
        chevron.HorizontalAlignment(mux::HorizontalAlignment::Right);
        chevron.VerticalAlignment(mux::VerticalAlignment::Center);
        chevron.Margin(mux::Thickness{0, 0, 12, 0});
        groupRow.button.Content().as<muxc::Grid>().Children().Append(chevron);
    }
    body.Children().Append(groupRow.button);

    muxc::StackPanel list;
    list.Orientation(muxc::Orientation::Vertical);
    list.Spacing(2);
    for (auto const& drive : drives) {
        // The icon starts where the group's text does (15 + 16 + 10), and the elbow comes down from under the group's icon.
        auto row = SidebarLocationRow(state, panel, drive, 26);
        auto elbow = LoadXaml(std::wstring(L"<Path ") + kXamlNs +
                              L" Data='M 0.5,7 V 12 Q 0.5,16.5 5,16.5 H 10' Stroke='{ThemeResource TextFillColorDisabledBrush}' "
                              L"StrokeThickness='1' Opacity='0.55' HorizontalAlignment='Left' VerticalAlignment='Top' Margin='22,0,0,0' "
                              L"IsHitTestVisible='False'/>");  // Softer than a control's stroke: a hint of the tree, not a line.
        row.button.Content().as<muxc::Grid>().Children().Append(elbow.as<mux::UIElement>());
        list.Children().Append(row.button);
    }
    bool open = !collapsible || Wh_GetIntValue(L"sidebar.collapsed.drives", 0) == 0;
    list.Visibility(open ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    if (chevron) {
        chevron.Glyph(open ? L"\uE70E" : L"\uE70D");
    }
    muxc::Grid children;
    children.Children().Append(list);
    body.Children().Append(children);
    panel->driveChildren = children;

    winrt::weak_ref<muxc::FontIcon> weakChevron = chevron;
    Keep(groupRow.button, groupRow.button.Click(winrt::auto_revoke, [state, weakChevron, collapsible, computerPidl](Inspectable const&, mux::RoutedEventArgs const&) {
        SidebarPanel* p = state->sidebar;
        if (!collapsible) {
            if (computerPidl) {
                SidebarNavigate(state, computerPidl.get());
            }
            return;
        }
        if (!p || !p->driveChildren) {
            return;
        }
        bool nowOpen = p->driveChildren.Visibility() != mux::Visibility::Visible;
        p->driveChildren.Visibility(nowOpen ? mux::Visibility::Visible : mux::Visibility::Collapsed);
        Wh_SetIntValue(L"sidebar.collapsed.drives", nowOpen ? 0 : 1);
        if (auto glyph = weakChevron.get()) {
            glyph.Glyph(nowOpen ? L"\uE70E" : L"\uE70D");
        }
        SidebarMarkSelected(p, p->selectedKey);
    }));
}

// The "…" menu of the section header: a check for each place, to show or hide it.
void SidebarFillLocationsMenu(WindowState* state, SidebarPanel* panel, std::vector<LocationEntry> const& all) {
    if (!panel->locationsMenu) {
        return;
    }
    panel->locationsMenu.Items().Clear();
    for (auto const& entry : all) {
        muxc::ToggleMenuFlyoutItem item;
        item.Text(entry.name);
        item.IsChecked(entry.visible);
        std::wstring key = entry.key;
        Keep(item, item.Click(winrt::auto_revoke, [state, key, item](Inspectable const&, mux::RoutedEventArgs const&) {
            LocationSetVisible(key, item.IsChecked());
            if (state->queue) {
                state->queue.TryEnqueue([state]() {
                    if (state->sidebar) {
                        SidebarFillLocations(state, state->sidebar);
                    }
                });
            }
        }));
        panel->locationsMenu.Items().Append(item);
    }
}

}  // namespace

// The rows of the Locations body, made again from the list: at the start, after a change of what is shown and when a drive
// comes or goes.
void SidebarFillLocations(WindowState* state, SidebarPanel* panel) {
    SidebarPanel::Section* section = nullptr;
    for (auto& s : panel->sections) {
        if (s.key == L"locations") {
            section = &s;
        }
    }
    if (!section || !section->body) {
        return;
    }
    auto body = section->body;
    body.Children().Clear();
    panel->locationMarks.erase(std::remove_if(panel->locationMarks.begin(), panel->locationMarks.end(),
                                              [](SidebarPanel::Mark const& m) {
                                                  return m.key.rfind(L"fav.", 0) != 0 && m.key.rfind(L"tag.", 0) != 0;
                                              }),
                               panel->locationMarks.end());
    panel->driveChildren = nullptr;
    panel->driveBars.clear();
    panel->driveMask = GetLogicalDrives();
    panel->remoteVersion = g_remoteVersion;
    auto all = LocationsList();
    panel->locations.clear();
    std::vector<LocationEntry> drives;
    for (auto const& entry : all) {
        if (entry.visible) {
            panel->locations.push_back(entry);
            if (entry.drive) {
                drives.push_back(entry);
            }
        }
    }
    bool grouped = LocationsGrouped(drives.size());
    bool groupDone = false;
    for (auto const& entry : panel->locations) {
        if (entry.drive) {
            if (!grouped) {
                body.Children().Append(SidebarLocationRow(state, panel, entry, 0).button);
            } else if (!groupDone) {
                groupDone = true;
                SidebarAddDriveGroup(state, panel, body, drives);
            }
            continue;
        }
        body.Children().Append(SidebarLocationRow(state, panel, entry, 0).button);
    }
    SidebarFillLocationsMenu(state, panel, all);
    SidebarRefreshDriveSpace(state, panel);
    SidebarMarkSelected(panel, panel->selectedKey);
    panel->lastFolder.reset();  // The selection is worked out again at the next tick.
}

// The "…" button of the header, with its menu of places to show or hide (the header row of 10j shows it on hover).
muxc::Button SidebarLocationsMore(WindowState* state, SidebarPanel* panel) {
    auto more = SidebarNewButtonWith(L"SidebarMenu.locations", 26, L"0", kSidebarSmallTemplate);
    more.Width(26);
    more.Content(SidebarGlyph(L"\uE712", 14));
    mux::Automation::AutomationProperties::SetName(more, Str(L"sidebar.locations.edit"));
    panel->locationsMenu = muxc::MenuFlyout();
    more.Flyout(panel->locationsMenu);
    return more;
}

// Selects the entry that holds the folder the window shows now (checked every few ticks), and rebuilds the list when a drive
// has come or gone.
void SidebarFollowFolder(WindowState* state, SidebarPanel* panel) {
    if (!panel->viewKey.empty()) {
        SidebarMarkSelected(panel, panel->viewKey);  // A tag's list is open: that tag has the pill.
        SidebarPlacePill(panel);
        return;
    }
    if (GetLogicalDrives() != panel->driveMask || g_remoteVersion != panel->remoteVersion) {
        SidebarFillLocations(state, panel);
    }
    SidebarPlacePill(panel);  // Rows move when a section opens or closes.
    if (panel->ticks % 1000 < 3) {
        SidebarRefreshDriveSpace(state, panel);  // About once a minute.
    }
    IShellBrowser* browser = BrowserOf(state->topLevel);
    if (!browser) {
        return;
    }
    Pidl folder = CurrentFolder(browser);
    if (!folder) {
        return;
    }
    if (panel->lastFolder && ILIsEqual(panel->lastFolder.get(), folder.get())) {
        return;
    }
    panel->lastFolder = LocationPidl(ILClone(folder.get()));
    // Favorites come first, so a folder that is both a favorite and a location is selected as the favorite.
    std::wstring key;
    for (auto const& entry : panel->favorites) {
        if (entry.pidl && ILIsEqual(entry.pidl.get(), folder.get())) {
            key = L"fav." + entry.path;
            break;
        }
    }
    for (auto const& entry : panel->locations) {
        if (key.empty() && entry.pidl && ILIsEqual(entry.pidl.get(), folder.get())) {
            key = entry.key;
        }
    }
    if (key.empty()) {
        std::vector<LocationPath> paths;
        for (auto const& entry : panel->favorites) {
            paths.push_back({L"fav." + entry.path, entry.path});
        }
        for (auto const& entry : panel->locations) {
            paths.push_back({entry.key, entry.path});
        }
        key = SidebarEntryIn(paths, PathOf(folder.get()));
    }
    SidebarMarkSelected(panel, key);
}
