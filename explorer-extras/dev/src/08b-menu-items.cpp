// The shell's context menu for any set of items, in the columns' own flyout (08-menu-edit.cpp's ShowItemFlyout is the one for a
// column's rows): a quick row (Cut, Copy, Paste, Delete, Properties), Open, the caller's own rows, Open in a new tab, Tags, Copy as
// path, the format's main action, and the rest of the shell's entries behind "More options". Used by the sidebar's rows and by the list
// of a tag's files, which are not column rows. The menu is XAML (a native popup runs a modal loop inside a XAML handler, which crashed
// Explorer before) and may go past the edge of the island it is asked in (the sidebar is narrower than the menu).
//
// What it leaves out of the columns' menu, on purpose: Rename (a row of these lists cannot be edited), Compress to, New folder from
// selection and the archive cases, which need a column. The columns' menu is adjusted later on the same pieces.

struct MenuItemSet {
    Pidl folder;                      // The folder that holds the items (absolute).
    std::vector<Pidl> children;       // Their ids in it.
    std::vector<std::wstring> paths;  // Their paths on disk, an empty one for an item that has none.
    std::vector<bool> isFolder;
    struct Extra {
        std::wstring text;
        const wchar_t* glyph = L"";
        std::function<void()> action;
    };
    std::vector<Extra> extras;         // The caller's rows, after Open.
    std::vector<std::wstring> hideVerbs;  // Shell verbs the menu leaves out (the caller has its own).
    std::vector<std::wstring> promoteVerbs;  // Shell verbs shown as rows of their own after Open, not behind "More options" (Empty).
    std::function<void()> after;       // After a command of the shell ran (read a list again).
    mux::UIElement anchor{nullptr};    // The element the menu is placed on, and the point on it.
    winrt::Windows::Foundation::Point position{};
    std::optional<winrt::Windows::Foundation::Rect> rowBox;  // The row, so that the menu does not cover it.
    HWND hostWindow = nullptr;         // The island of the anchor (it is not the columns').
    double hostScale = 1.0;
};

void ShowItemFlyoutFor(WindowState* state, MenuItemSet set) {
    try {
        CloseContextMenu(state);
        if (set.children.empty() || !set.anchor) {
            return;
        }
        IShellFolder* parent = nullptr;
        HRESULT hr = ILIsEmpty(set.folder.get()) ? SHGetDesktopFolder(&parent)
                                                 : SHBindToObject(nullptr, set.folder.get(), nullptr, IID_PPV_ARGS(&parent));
        if (FAILED(hr) || !parent) {
            Wh_Log(L"item menu: can't bind the folder, hr=0x%08lX", hr);
            return;
        }
        std::vector<PCUITEMID_CHILD> ids;
        for (auto const& child : set.children) {
            ids.push_back(child.get());
        }
        IContextMenu* menu = nullptr;
        hr = parent->GetUIObjectOf(state->topLevel, (UINT)ids.size(), ids.data(), IID_IContextMenu, nullptr, (void**)&menu);
        parent->Release();
        if (FAILED(hr) || !menu) {
            Wh_Log(L"item menu: no IContextMenu, hr=0x%08lX", hr);
            return;
        }
        HMENU hmenu = CreatePopupMenu();
        hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE);
        if (FAILED(hr)) {
            DestroyMenu(hmenu);
            menu->Release();
            Wh_Log(L"item menu: QueryContextMenu hr=0x%08lX", hr);
            return;
        }
        state->context = std::make_unique<ContextMenuState>();
        state->context->menu = menu;
        state->context->hmenu = hmenu;
        state->context->owner = state->topLevel;
        state->context->generic = true;
        state->context->genericFolder = Pidl(ILClone(set.folder.get()));
        for (auto const& child : set.children) {
            state->context->genericChildren.push_back(Pidl(ILCloneChild(child.get())));
        }
        state->context->afterCommand = set.after;

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
            menu->GetCommandString(info.wID - state->context->idFirst, GCS_VERBW, nullptr, (char*)verb, ARRAYSIZE(verb));
            row.verb = verb;
            LogVerbose(L"item menu entry %d: id=%u verb=\"%s\" text=\"%s\"%s", i, row.id, verb, text.c_str(), row.sub ? L" (submenu)" : L"");
            rows.push_back(std::move(row));
        }

        muxc::Flyout flyout;
        MenuBuilder builder(state, 300, true);
        builder.hostWindow = set.hostWindow;
        builder.hostScale = set.hostScale;
        builder.unconstrained = set.hostWindow != nullptr;
        std::set<int> used;  // Positions shown above "More options".
        for (auto const& row : rows) {
            for (auto const& hidden : set.hideVerbs) {
                if (_wcsicmp(row.verb.c_str(), hidden.c_str()) == 0) {
                    used.insert(row.pos);
                }
            }
        }
        bool single = set.children.size() == 1;
        bool onDisk = true;
        for (auto const& path : set.paths) {
            onDisk = onDisk && !path.empty();
        }
        onDisk = onDisk && set.paths.size() == set.children.size();

        auto findRow = [&](const wchar_t* verb) -> MenuRow* {
            for (auto& row : rows) {
                if (!used.count(row.pos) && _wcsicmp(row.verb.c_str(), verb) == 0) {
                    return &row;
                }
            }
            return nullptr;
        };

        // The quick row: Cut, Copy, Paste (a folder takes it), Delete, and the properties pinned at the right end.
        {
            std::vector<MenuQuick> header;
            auto addQuick = [&](MenuRow* row, bool pinned) {
                used.insert(row->pos);
                UINT id = row->id;
                MenuQuick item;
                item.label = row->text;
                item.tip = row->text;
                item.glyph = MockupGlyphForVerb(row->verb);
                item.icon = OfficialIconName(row->verb);
                item.glyphOnly = pinned;
                item.enabled = row->enabled;
                item.pinned = pinned;
                item.action = [id]() { RunContextCommand(id); };
                header.push_back(std::move(item));
            };
            for (const wchar_t* verb : {L"cut", L"copy", L"paste", L"delete"}) {
                if (MenuRow* row = findRow(verb)) {
                    addQuick(row, false);
                }
            }
            if (MenuRow* row = findRow(L"properties")) {
                addQuick(row, true);
            }
            builder.QuickRow(header);
        }

        auto addRow = [&](std::wstring const& text, const wchar_t* glyph, bool enabled, std::function<void()> action,
                          muxc::MenuFlyout const& submenu, const wchar_t* iconFile) {
            MenuRowSpec spec;
            spec.text = text;
            spec.glyph = glyph ? glyph : L"";
            spec.icon = iconFile ? iconFile : L"";
            spec.enabled = enabled;
            spec.action = std::move(action);
            spec.submenu = submenu;
            builder.Row(spec);
        };
        auto addShellRow = [&](MenuRow* row, const wchar_t* glyphOverride, mux::UIElement custom = nullptr) {
            used.insert(row->pos);
            UINT id = row->id;
            muxc::MenuFlyout subMenu{nullptr};
            if (row->sub) {
                subMenu = muxc::MenuFlyout();
                InitSubmenu(state, row->sub, row->pos);
                FillFlyoutItems(state, row->sub, subMenu.Items(), 1, nullptr);
            }
            const wchar_t* icon = OfficialIconName(row->verb);
            if (!icon[0]) {
                icon = FormatActionIcon(row->verb);
            }
            MenuRowSpec spec;
            spec.text = row->text;
            spec.glyph = glyphOverride ? glyphOverride : MockupGlyphForVerb(row->verb);
            spec.icon = icon;
            spec.shortcut = ShortcutForVerb(row->verb);
            spec.enabled = row->enabled;
            spec.custom = custom;
            spec.action = row->sub ? std::function<void()>() : [id]() { RunContextCommand(id); };
            spec.submenu = subMenu;
            builder.Row(spec);
        };

        // First group: Open, the caller's rows, Open in new tab (a folder), Open with.
        if (MenuRow* row = findRow(L"open")) {
            mux::UIElement appIcon{nullptr};
            if (single && !set.isFolder[0] && onDisk) {
                appIcon = AppIconOf(set.paths[0]);
            }
            addShellRow(row, nullptr, appIcon);
        }
        for (auto const& extra : set.extras) {
            auto action = extra.action;
            addRow(extra.text, extra.glyph, true, [action]() {
                WindowState* now = StateForThisThread(false);
                if (now && !g_unloading) {
                    action();
                }
            }, muxc::MenuFlyout{nullptr}, L"");
        }
        for (auto const& verb : set.promoteVerbs) {
            if (MenuRow* row = findRow(verb.c_str())) {
                mux::UIElement custom{nullptr};
                if (_wcsicmp(verb.c_str(), L"eject") == 0) {
                    // The eject symbol (a triangle over a bar) drawn as a shape: the glyph fonts have no code for it that I could check.
                    custom = mux::Markup::XamlReader::Load(std::wstring(L"<Path xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'") +
                                      L" Data='M 8,3 L 13,9 L 3,9 Z M 3,11 L 13,11 L 13,13 L 3,13 Z' Width='16' Height='16' "
                                      L"Stretch='None' Fill='{ThemeResource TextFillColorPrimaryBrush}'/>").as<mux::UIElement>();
                }
                addShellRow(row, nullptr, custom);
            }
        }
        if (single && set.isFolder[0]) {
            Pidl target(ILCombine(set.folder.get(), set.children[0].get()));
            auto shared = std::make_shared<Pidl>(std::move(target));
            addRow(L"Open in new tab", L"", true, [shared]() { OpenFolderInNewTab(StateForThisThread(false), shared->get()); },
                   muxc::MenuFlyout{nullptr}, L"windows.opennewtab");
        }
        if (MenuRow* row = findRow(L"openas")) {
            addShellRow(row, nullptr);
        }

        // Second group: Tags (every item needs a path), Copy as path, the format's main action.
        builder.separatorWanted = true;
        if (onDisk && g_labels) {
            std::vector<std::wstring> labelPaths = set.paths;
            bool keeps = true;
            {
                std::wstring folderPath = labelPaths[0];
                PathRemoveFileSpecW(folderPath.data());
                folderPath.resize(wcslen(folderPath.c_str()));
                auto cached = LabelsFolderFor(folderPath);
                if (cached->complete) {
                    keeps = cached->streamsSupported;
                }
            }
            winrt::weak_ref<mux::FrameworkElement> weakAnchor = set.anchor.as<mux::FrameworkElement>();
            addRow(Str(keeps ? L"labels.menu" : L"labels.menu.ntfs"), L"", keeps, nullptr,
                   keeps ? LabelsTagsSubmenu(labelPaths, [weakAnchor]() -> mux::FrameworkElement { return weakAnchor.get(); })
                         : muxc::MenuFlyout{nullptr},
                   L"");
        }
        if (MenuRow* row = findRow(L"copyaspath")) {
            addShellRow(row, nullptr);
        }
        const wchar_t* formatVerbs[] = {L"setdesktopwallpaper", L"Windows.PowerShell.Run", L"runas", L"edit",
                                        L"print",               L"play",                   L"mount", L"install"};
        for (auto verb : formatVerbs) {
            if (MenuRow* row = findRow(verb)) {
                if (!row->sub) {
                    addShellRow(row, L"");
                    break;
                }
            }
        }

        // Third group: everything else the shell offers.
        muxc::MenuFlyout more;
        FillFlyoutItems(state, hmenu, more.Items(), 0, &used);
        if (more.Items().Size() > 0) {
            builder.separatorWanted = true;
            addRow(L"More options", L"", true, nullptr, more, L"");
        }
        builder.Show(flyout, set.anchor, set.position, set.rowBox);
        Wh_Log(L"item menu shown for %d item(s): %u parts", (int)set.children.size(), builder.root.Children().Size());
    } catch (...) {
        LogHresult(L"ShowItemFlyoutFor");
        CloseContextMenu(state);
    }
}

// The set for one path on disk or one shell item: the folder that holds it and its id in it. `path` may be empty when only the
// absolute id is known.
bool MenuItemSetAdd(MenuItemSet& set, PCIDLIST_ABSOLUTE absolute, std::wstring const& path, bool folder) {
    if (!absolute) {
        return false;
    }
    Pidl parent(ILClone(absolute));
    if (!ILRemoveLastID(parent.get())) {
        return false;
    }
    if (set.children.empty()) {
        set.folder = std::move(parent);
    } else if (!ILIsEqual(set.folder.get(), parent.get())) {
        return false;  // The shell's menu is for items of one folder.
    }
    set.children.push_back(Pidl(ILCloneChild(ILFindLastID(absolute))));
    set.paths.push_back(path);
    set.isFolder.push_back(folder);
    return true;
}
