// Sidebar: the list of the files that carry a tag, and the list of all tags. Opened from a tag row ("View all" for the second). It
// is the mod's own flat list in an island placed over the file area of the window (so it works with Columns off), fed by the
// tag index (04d): Name, Location, Date modified, Size, sortable by a click on a title. The folder Explorer shows is not changed;
// the list goes when the user navigates anywhere (a click in the sidebar, the address bar, Back, a Location click), on the
// arrow of its header, on Back / Backspace / Alt+Left / Escape.
//
// The rows are drawn by hand and only those in view exist (a canvas as tall as the list, rows put where the scroll offset says), so a
// tag on thousands of files costs a screenful. A shell context menu comes on a right click (a window of its own takes its
// messages), a double click opens the item. Not here: dragging the rows out, renaming in the list (both are in the folder view).
//
// The pure parts (rows from the index, their sort) are tested by 10j4a.

struct TagResultRow {
    std::wstring path;
    std::wstring name;
    std::wstring folder;  // The folder that holds it: the Location column.
    FILETIME modified{};
    ULONGLONG size = 0;
    bool isFolder = false;
};

enum class TagResultSort { Name, Location, Modified, Size };

std::vector<TagResultRow> TagResultsRows(std::vector<IndexedFile> const& files) {
    std::vector<TagResultRow> rows;
    for (auto const& file : files) {
        TagResultRow row;
        row.path = file.path;
        const wchar_t* base = PathFindFileNameW(file.path.c_str());
        row.name = base;
        row.folder = file.path.substr(0, (size_t)(base - file.path.c_str()));
        if (row.folder.size() > 3 && row.folder.back() == L'\\') {
            row.folder.pop_back();  // "C:\" keeps its slash, "C:\Users\" does not.
        }
        row.modified = file.modified;
        row.size = file.isFolder ? 0 : file.size;
        row.isFolder = file.isFolder;
        rows.push_back(std::move(row));
    }
    return rows;
}

namespace {

int TagResultsText(std::wstring const& a, std::wstring const& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) - CSTR_EQUAL;
}

}  // namespace

void TagResultsSort(std::vector<TagResultRow>& rows, TagResultSort by, bool ascending) {
    std::stable_sort(rows.begin(), rows.end(), [by, ascending](TagResultRow const& a, TagResultRow const& b) {
        int c = 0;
        switch (by) {
            case TagResultSort::Name: c = TagResultsText(a.name, b.name); break;
            case TagResultSort::Location: c = TagResultsText(a.folder, b.folder); break;
            case TagResultSort::Modified: c = CompareFileTime(&a.modified, &b.modified); break;
            case TagResultSort::Size: c = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0); break;
        }
        if (c == 0) {
            c = TagResultsText(a.path, b.path);
        }
        return ascending ? c < 0 : c > 0;
    });
}

// ---- The panel ----

struct TagViewPanel {
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    muxc::Grid root{nullptr};
    muxc::Grid iconHost{nullptr};
    muxc::TextBlock title{nullptr};
    muxc::TextBlock count{nullptr};
    muxc::Grid columnHeader{nullptr};
    muxc::ScrollViewer scroller{nullptr};
    muxc::Canvas canvas{nullptr};
    muxc::TextBlock message{nullptr};
    muxc::StackPanel messageHost{nullptr};  // The spinner and the message, at the top of the list.
    muxc::ProgressRing ring{nullptr};
    bool loadedOnce = false;                // The index has answered (or the cache did) since the list opened.
    bool active = false;
    bool shown = false;
    RECT rect{};
    bool tagsMode = false;  // The list of all tags, not the files of one.
    std::wstring tag;
    int colour = 0;
    std::vector<TagResultRow> rows;
    struct TagLine {
        std::wstring name;
        int colour = 0;
        size_t count = 0;
    };
    std::vector<TagLine> tagLines;
    bool ready = false;      // The index has had its first walk.
    size_t progress = 0;     // Items it has seen, while it has not.
    TagResultSort sort = TagResultSort::Name;
    bool ascending = true;
    std::set<size_t> selected;
    size_t anchor = 0;
    double widths[3] = {260, 150, 90};  // Location, Date modified, Size, in pixels (Name takes the rest). The user drags them.
    struct Realized {
        muxc::Grid grid{nullptr};
        muxc::Grid cells{nullptr};
        muxc::Border back{nullptr};
    };
    std::map<size_t, Realized> realized;
    std::shared_ptr<ITEMIDLIST> folderAtOpen;  // The folder Explorer showed when the list opened: a change of it closes the list.
    unsigned generation = 0;
    bool loading = false;
    unsigned ticks = 0;
    // The title of the tab the list is in: Explorer's tab item (TabViewItem) holds a StackPanel header [spinner, TextBlock] that its
    // own code writes, and an IconSource. While the list shows, the text is the tag's name and count and the icon is the tag's.
    winrt::weak_ref<muxc::TabViewItem> tabItem;
    bool titled = false;
    std::wstring savedText, ourText;
    muxc::IconSource savedIcon{nullptr};
    muxc::IconSource ourIcon{nullptr};
    int ourColour = -1;
    HWND tab = nullptr;          // The tab (ShellTabWindowClass) the list belongs to: another tab shows its own folder.
    bool tabHidden = false;      // The list is out of sight because another tab is showing.
    std::wstring hiddenKey;      // The sidebar's pill key while it is.
};

void TagViewLeave(WindowState* state);
void TagResultsShow(WindowState* state, std::wstring const& tag);

namespace {

void TagViewLoadSoon(WindowState* state);
void TagViewLoadNow(WindowState* state);

// Runs `work` after the handler that asked, on the window's thread: a handler of a row must not remake the rows (its own too).
void TagViewLater(WindowState* state, std::function<void()> work) {
    if (!state->queue) {
        return;
    }
    state->queue.TryEnqueue([work]() {
        if (!g_unloading) {
            try {
                work();
            } catch (...) {
                LogHresult(L"a tag list action");
            }
        }
    });
}

// What the lists showed last, by tag (and the list of tags), so that opening one shows it at once and the index is read again
// behind it. Under the mutex: every window of the process uses it.
std::mutex g_tagViewCacheMutex;
std::map<std::wstring, std::vector<TagResultRow>> g_tagViewRowsCache;
std::vector<TagViewPanel::TagLine> g_tagViewLinesCache;
bool g_tagViewLinesCached = false;

std::wstring TagViewCacheKey(std::wstring tag) {
    CharLowerBuffW(tag.data(), (DWORD)tag.size());
    return tag;
}

constexpr double kTagViewRow = 32;

thread_local HWND g_tagViewWindowLocal = nullptr;

size_t TagViewCount(TagViewPanel* view) {
    return view->tagsMode ? view->tagLines.size() : view->rows.size();
}

muxc::TabViewItem TagViewSelectedTab(mux::DependencyObject const& node, int depth, int* visited) {
    if (!node || depth > 40 || ++*visited > 6000) {
        return nullptr;
    }
    if (auto item = node.try_as<muxc::TabViewItem>()) {
        if (item.IsSelected()) {
            return item;
        }
    }
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        if (auto found = TagViewSelectedTab(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1, visited)) {
            return found;
        }
    }
    return nullptr;
}

muxc::TextBlock TagViewTabText(muxc::TabViewItem const& item) {
    if (auto header = item.Header().try_as<muxc::Panel>()) {
        for (auto child : header.Children()) {
            if (auto block = child.try_as<muxc::TextBlock>()) {
                return block;
            }
        }
    }
    return nullptr;
}

// Puts the tag's name and count, and its icon, on the tab. Every tick: Explorer writes the folder's name there when it likes.
void TagViewTitle(WindowState* state, TagViewPanel* view) {
    if (!view->active || view->tabHidden) {
        return;
    }
    try {
        auto item = view->tabItem.get();
        if (!item) {
            if (!state->xamlRoot || view->titled) {
                return;
            }
            int visited = 0;
            item = TagViewSelectedTab(state->xamlRoot.Content().try_as<mux::DependencyObject>(), 0, &visited);
            if (!item) {
                return;
            }
            view->tabItem = item;
        }
        auto block = TagViewTabText(item);
        if (!block) {
            return;
        }
        std::wstring name = view->tagsMode ? std::wstring(Str(L"sidebar.tags")) : view->tag;
        size_t count = TagViewCount(view);
        std::wstring wanted = view->loadedOnce ? name + L" (" + std::to_wstring(count) + L")" : name;
        if (!view->titled) {
            view->savedText = block.Text();
            view->savedIcon = item.IconSource();
            view->titled = true;
        }
        if (std::wstring(block.Text()) != wanted) {
            block.Text(wanted);
        }
        view->ourText = wanted;
        int colour = view->tagsMode ? 0 : view->colour;
        if (!view->ourIcon || view->ourColour != colour) {
            // The tag shape of the sidebar and of the list's header (the same path), in the tag's colour.
            auto icon = LoadXaml(std::wstring(L"<PathIconSource ") + kXamlNs +
                                 L" Data='F0 M 2.5,2.5 L 8.2,2.5 C 8.6,2.5 8.9,2.65 9.2,2.9 "
                                 L"L 13.1,6.8 C 13.7,7.4 13.7,8.4 13.1,9 L 9,13.1 C 8.4,13.7 7.4,13.7 6.8,13.1 L 2.9,9.2 C 2.65,8.9 2.5,8.6 2.5,8.2 Z "
                                 L"M 4.6,3.9 A 0.8,0.8 0 1 0 4.6,5.5 A 0.8,0.8 0 1 0 4.6,3.9 Z'/>")
                            .as<muxc::PathIconSource>();
            COLORREF rgb = LabelsDotRgb(colour, LabelsDarkTheme());
            icon.Foreground(mux::Media::SolidColorBrush(winrt::Windows::UI::Color{255, GetRValue(rgb), GetGValue(rgb), GetBValue(rgb)}));
            view->ourIcon = icon;
            view->ourColour = colour;
        }
        if (item.IconSource() != view->ourIcon) {
            item.IconSource(view->ourIcon);
        }
    } catch (...) {
        LogHresult(L"the title of the tag list's tab");
    }
}

// Gives the tab its own title and icon back, unless Explorer has already written a new title there (it navigated).
void TagViewRestoreTitle(TagViewPanel* view) {
    if (!view->titled) {
        return;
    }
    view->titled = false;
    try {
        if (auto item = view->tabItem.get()) {
            if (auto block = TagViewTabText(item)) {
                if (std::wstring(block.Text()) == view->ourText) {
                    block.Text(view->savedText);
                }
            }
            if (view->ourIcon && item.IconSource() == view->ourIcon) {
                item.IconSource(view->savedIcon);
            }
        }
    } catch (...) {
        LogHresult(L"giving the tab its title back");
    }
    view->ourIcon = nullptr;
    view->savedIcon = nullptr;
    view->ourColour = -1;
    view->tabItem = winrt::weak_ref<muxc::TabViewItem>();
}

std::wstring TagViewDate(FILETIME const& time) {
    if (time.dwHighDateTime == 0 && time.dwLowDateTime == 0) {
        return std::wstring();
    }
    FILETIME local;
    SYSTEMTIME at;
    if (!FileTimeToLocalFileTime(&time, &local) || !FileTimeToSystemTime(&local, &at)) {
        return std::wstring();
    }
    wchar_t date[64] = L"";
    wchar_t clock[32] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &at, nullptr, date, ARRAYSIZE(date), nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &at, nullptr, clock, ARRAYSIZE(clock));
    return std::wstring(date) + L" " + clock;
}

std::wstring TagViewFormat(const wchar_t* pattern, unsigned long long number) {
    wchar_t text[300];
    swprintf_s(text, pattern, (unsigned)number);
    return text;
}

void TagViewRealize(WindowState* state, TagViewPanel* view);
void TagViewApply(WindowState* state, TagViewPanel* view);

void TagViewSetSelectedLook(TagViewPanel* view, size_t index) {
    auto found = view->realized.find(index);
    if (found != view->realized.end() && found->second.back) {
        found->second.back.Opacity(view->selected.count(index) ? 1.0 : 0.0);
    }
}

void TagViewSelect(TagViewPanel* view, size_t index, bool ctrl, bool shift) {
    std::set<size_t> before = view->selected;
    if (shift && !view->selected.empty()) {
        size_t from = std::min(view->anchor, index), to = std::max(view->anchor, index);
        view->selected.clear();
        for (size_t i = from; i <= to; i++) {
            view->selected.insert(i);
        }
    } else if (ctrl) {
        if (!view->selected.erase(index)) {
            view->selected.insert(index);
        }
        view->anchor = index;
    } else {
        view->selected.clear();
        view->selected.insert(index);
        view->anchor = index;
    }
    for (size_t i : before) {
        TagViewSetSelectedLook(view, i);
    }
    for (size_t i : view->selected) {
        TagViewSetSelectedLook(view, i);
    }
}

std::vector<std::wstring> TagViewSelectedPaths(TagViewPanel* view) {
    std::vector<std::wstring> paths;
    if (view->tagsMode) {
        return paths;
    }
    for (size_t i : view->selected) {
        if (i < view->rows.size()) {
            paths.push_back(view->rows[i].path);
        }
    }
    return paths;
}

// ---- Opening things ----

void TagViewOpenItem(WindowState* state, std::wstring const& path, bool isFolder) {
    if (isFolder) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
            TagViewLeave(state);
            SidebarNavigate(state, pidl);
            CoTaskMemFree(pidl);
        }
        return;
    }
    SHELLEXECUTEINFOW info = {sizeof(info)};
    info.fMask = SEE_MASK_NOASYNC;
    info.hwnd = state->topLevel;
    info.lpFile = path.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        Wh_Log(L"tag list: opening a file failed, error %lu", GetLastError());
    }
}

void TagViewOpenFolder(WindowState* state, std::wstring const& folder) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (!folder.empty() && SUCCEEDED(SHParseDisplayName(folder.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
        TagViewLeave(state);
        SidebarNavigate(state, pidl);
        CoTaskMemFree(pidl);
    }
}

// ---- Rows ----

mux::UIElement TagViewCell(std::wstring const& text, int column, bool muted, bool right = false) {
    auto block = MakeText(text, 13, false, muted);
    block.TextWrapping(mux::TextWrapping::NoWrap);
    block.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    block.VerticalAlignment(mux::VerticalAlignment::Center);
    block.Margin(mux::Thickness{8, 0, 8, 0});
    if (right) {
        block.HorizontalAlignment(mux::HorizontalAlignment::Right);
    }
    muxc::Grid::SetColumn(block, column);
    return block;
}

void TagViewColumns(muxc::Grid const& grid, bool files, TagViewPanel* view) {
    auto add = [&grid](double value, mux::GridUnitType kind) {
        muxc::ColumnDefinition column;
        column.Width(mux::GridLength{value, kind});
        grid.ColumnDefinitions().Append(column);
    };
    if (files) {
        add(1, mux::GridUnitType::Star);
        for (double width : view->widths) {
            add(width, mux::GridUnitType::Pixel);
        }
    } else {
        add(1, mux::GridUnitType::Star);
        add(110, mux::GridUnitType::Pixel);
    }
}

void TagViewMakeRow(WindowState* state, TagViewPanel* view, size_t index) {
    muxc::Grid grid;
    grid.Height(kTagViewRow);
    grid.Background(mux::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
    auto back = SidebarThemedBorder(L"SubtleFillColorSecondaryBrush");
    back.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
    back.Margin(mux::Thickness{4, 0, 4, 0});
    back.Opacity(view->selected.count(index) ? 1.0 : 0.0);
    back.OpacityTransition(mux::ScalarTransition());
    back.IsHitTestVisible(false);
    grid.Children().Append(back);
    muxc::Grid cells;
    TagViewColumns(cells, !view->tagsMode, view);
    cells.Margin(mux::Thickness{8, 0, 8, 0});
    grid.Children().Append(cells);

    // The first cell: the icon, then the name.
    muxc::StackPanel first;
    first.Orientation(muxc::Orientation::Horizontal);
    first.VerticalAlignment(mux::VerticalAlignment::Center);
    first.Margin(mux::Thickness{8, 0, 8, 0});
    muxc::Grid iconHost;
    iconHost.Width(16);
    iconHost.Height(16);
    first.Children().Append(iconHost);
    std::wstring name;
    if (view->tagsMode) {
        auto const& line = view->tagLines[index];
        iconHost.Children().Append(SidebarTagIcon(line.colour, LabelsDarkTheme()));
        name = line.name;
    } else {
        auto const& row = view->rows[index];
        name = row.name;
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(row.path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
            winrt::weak_ref<muxc::Grid> weak = iconHost;
            SidebarLoadIcon(state, pidl, [weak](mux::Media::ImageSource const& source) {
                if (auto host = weak.get()) {
                    muxc::Image image;
                    image.Source(source);
                    image.Width(16);
                    image.Height(16);
                    host.Children().Append(image);
                }
            });
            CoTaskMemFree(pidl);
        }
    }
    auto label = MakeText(name, 13, false, false);
    label.TextWrapping(mux::TextWrapping::NoWrap);
    label.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    label.VerticalAlignment(mux::VerticalAlignment::Center);
    label.Margin(mux::Thickness{10, 0, 0, 0});
    first.Children().Append(label);
    muxc::Grid::SetColumn(first, 0);
    cells.Children().Append(first);

    if (view->tagsMode) {
        cells.Children().Append(TagViewCell(TagViewFormat(Str(L"tagview.files"), view->tagLines[index].count), 1, true, true));
        Keep(grid, grid.PointerPressed(winrt::auto_revoke, [state, index](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
            TagViewPanel* v = state->tagView;
            if (v && index < v->tagLines.size()) {
                // Not from inside this row's own handler: opening a tag makes the rows again, this one included.
                std::wstring name = v->tagLines[index].name;
                TagViewLater(state, [state, name]() { TagResultsShow(state, name); });
            }
        }));
    } else {
        auto const& row = view->rows[index];
        auto location = TagViewCell(row.folder, 1, true);
        std::wstring folder = row.folder;
        Keep(location, location.PointerPressed(winrt::auto_revoke, [state, folder](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
            args.Handled(true);
            TagViewLater(state, [state, folder]() { TagViewOpenFolder(state, folder); });
        }));
        muxc::ToolTipService::SetToolTip(location, winrt::box_value(winrt::hstring{row.folder}));
        cells.Children().Append(location);
        cells.Children().Append(TagViewCell(TagViewDate(row.modified), 2, true));
        wchar_t size[40] = L"";
        if (!row.isFolder) {
            StrFormatByteSizeW((LONGLONG)row.size, size, ARRAYSIZE(size));
        }
        cells.Children().Append(TagViewCell(size, 3, true, true));

        Keep(grid, grid.PointerPressed(winrt::auto_revoke, [state, index](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
            TagViewPanel* v = state->tagView;
            if (!v || index >= v->rows.size()) {
                return;
            }
            v->root.Focus(mux::FocusState::Pointer);
            auto props = args.GetCurrentPoint(v->root).Properties();
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (props.IsRightButtonPressed()) {
                return;  // The menu is the RightTapped handler's.
            } else {
                TagViewSelect(v, index, ctrl, shift);
            }
        }));
        Keep(grid, grid.RightTapped(winrt::auto_revoke, [state, index](Inspectable const&, mux::Input::RightTappedRoutedEventArgs const& args) {
            TagViewPanel* v = state->tagView;
            if (!v || index >= v->rows.size()) {
                return;
            }
            args.Handled(true);
            if (!v->selected.count(index)) {
                TagViewSelect(v, index, false, false);
            }
            // The items of the clicked one's folder (the shell's menu is for items of one folder).
            std::wstring folder = v->rows[index].folder;
            MenuItemSet set;
            for (size_t i : v->selected) {
                if (i >= v->rows.size() || v->rows[i].folder != folder) {
                    continue;
                }
                PIDLIST_ABSOLUTE absolute = nullptr;
                if (SUCCEEDED(SHParseDisplayName(v->rows[i].path.c_str(), nullptr, &absolute, 0, nullptr)) && absolute) {
                    MenuItemSetAdd(set, absolute, v->rows[i].path, v->rows[i].isFolder);
                    CoTaskMemFree(absolute);
                }
            }
            MenuItemSet::Extra location;
            location.text = Str(L"tagview.openlocation");
            location.glyph = L"\uE838";
            location.action = [state, folder]() { TagViewLater(state, [state, folder]() { TagViewOpenFolder(state, folder); }); };
            set.extras.push_back(std::move(location));
            set.after = [state]() {
                // A delete, a move: the list is read again.
                TagViewLater(state, [state]() {
                    if (state->tagView) {
                        state->tagView->loading = false;
                        TagViewLoadNow(state);
                    }
                });
            };
            auto grid = args.OriginalSource().try_as<mux::FrameworkElement>();
            auto anchor = v->realized.count(index) ? v->realized[index].grid : muxc::Grid{nullptr};
            if (!anchor) {
                return;
            }
            set.anchor = anchor;
            set.position = args.GetPosition(anchor);
            set.rowBox = winrt::Windows::Foundation::Rect{0, 0, (float)anchor.ActualWidth(), (float)anchor.ActualHeight()};
            set.hostWindow = (HWND)v->bridge.WindowId().Value;
            set.hostScale = anchor.XamlRoot() ? anchor.XamlRoot().RasterizationScale() : 1.0;
            ShowItemFlyoutFor(state, std::move(set));
        }));
        Keep(grid, grid.DoubleTapped(winrt::auto_revoke, [state, index](Inspectable const&, mux::Input::DoubleTappedRoutedEventArgs const&) {
            TagViewPanel* v = state->tagView;
            if (v && index < v->rows.size()) {
                std::wstring path = v->rows[index].path;
                bool isFolder = v->rows[index].isFolder;
                TagViewLater(state, [state, path, isFolder]() { TagViewOpenItem(state, path, isFolder); });
            }
        }));
    }
    Keep(grid, grid.PointerEntered(winrt::auto_revoke, [state, index](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
        TagViewPanel* v = state->tagView;
        if (!v || v->selected.count(index)) {
            return;
        }
        auto found = v->realized.find(index);
        if (found != v->realized.end()) {
            found->second.back.Opacity(0.55);
        }
    }));
    Keep(grid, grid.PointerExited(winrt::auto_revoke, [state, index](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
        TagViewPanel* v = state->tagView;
        if (v) {
            TagViewSetSelectedLook(v, index);
        }
    }));
    muxc::Canvas::SetTop(grid, index * kTagViewRow);
    view->canvas.Children().Append(grid);
    TagViewPanel::Realized made;
    made.grid = grid;
    made.back = back;
    made.cells = cells;
    view->realized[index] = made;
}

// Makes the rows in view (and a few around them), and lets go of the others.
void TagViewRealize(WindowState* state, TagViewPanel* view) {
    if (!view->canvas || !view->scroller) {
        return;
    }
    size_t count = TagViewCount(view);
    double offset = view->scroller.VerticalOffset();
    double viewport = view->scroller.ViewportHeight();
    if (viewport <= 0) {
        viewport = 600;
    }
    size_t first = (size_t)std::max(0.0, std::floor(offset / kTagViewRow) - 3);
    size_t last = std::min(count, (size_t)(std::ceil((offset + viewport) / kTagViewRow) + 4));
    double width = view->scroller.ViewportWidth();
    for (auto it = view->realized.begin(); it != view->realized.end();) {
        if (it->first < first || it->first >= last) {
            uint32_t at = 0;
            if (view->canvas.Children().IndexOf(it->second.grid, at)) {
                view->canvas.Children().RemoveAt(at);
            }
            it = view->realized.erase(it);
        } else {
            ++it;
        }
    }
    for (size_t i = first; i < last; i++) {
        if (!view->realized.count(i)) {
            TagViewMakeRow(state, view, i);
        }
    }
    if (width > 0) {
        for (auto& entry : view->realized) {
            entry.second.grid.Width(width);
        }
    }
}

// The list as the data says: the rows are made again, the title, the count and the message are set.
void TagViewApply(WindowState* state, TagViewPanel* view) {
    view->canvas.Children().Clear();
    view->realized.clear();
    size_t count = TagViewCount(view);
    view->canvas.Height(count * kTagViewRow);
    std::set<size_t> kept;
    for (size_t i : view->selected) {
        if (i < count) {
            kept.insert(i);
        }
    }
    view->selected = kept;
    view->columnHeader.Visibility(view->tagsMode ? mux::Visibility::Collapsed : mux::Visibility::Visible);
    view->count.Text(view->ready || view->tagsMode ? TagViewFormat(Str(L"tagview.count"), count) : std::wstring());
    std::wstring message;
    bool spin = false;
    if (!view->ready) {
        message = TagViewFormat(Str(L"tagview.indexing"), view->progress);
        spin = true;
    } else if (!view->loadedOnce) {
        spin = true;  // Nothing to show yet (not in the cache): the spinner alone.
    } else if (count == 0) {
        message = Str(view->tagsMode ? L"tagview.notags" : L"tagview.empty");
    }
    view->message.Text(message);
    view->message.Visibility(message.empty() ? mux::Visibility::Collapsed : mux::Visibility::Visible);
    view->ring.IsActive(spin);
    view->ring.Visibility(spin ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    view->messageHost.Visibility(spin || !message.empty() ? mux::Visibility::Visible : mux::Visibility::Collapsed);
    TagViewRealize(state, view);
    TagViewTitle(state, view);
}

// Reads the index on a mod thread and applies it on the window's.
void TagViewLoadNow(WindowState* state) {
    TagViewPanel* view = state->tagView;
    if (!view || !view->active || view->loading || !state->queue) {
        return;
    }
    view->loading = true;
    unsigned generation = ++view->generation;
    bool tagsMode = view->tagsMode;
    std::wstring tag = view->tag;
    mud::DispatcherQueue queue = state->queue;
    bool started = StartModThread([state, view, queue, generation, tagsMode, tag]() {
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        auto result = std::make_shared<std::tuple<bool, size_t, std::vector<IndexedFile>, std::vector<TagViewPanel::TagLine>>>();
        bool ready = TagIndexReady();
        std::get<0>(*result) = ready;
        std::get<1>(*result) = TagIndexProgress();
        if (ready) {
            if (tagsMode) {
                for (auto const& def : TagListSnapshot()) {
                    TagViewPanel::TagLine line;
                    line.name = def.name;
                    line.colour = def.colour;
                    line.count = TagIndexCount(def.name);
                    std::get<3>(*result).push_back(std::move(line));
                }
            } else {
                std::get<2>(*result) = TagIndexFiles(tag);
            }
        }
        if (com) {
            CoUninitialize();
        }
        if (g_unloading) {
            return;
        }
        try {
            queue.TryEnqueue([state, view, generation, result]() {
                if (g_unloading || state->tagView != view || view->generation != generation) {
                    return;
                }
                view->loading = false;
                view->ready = std::get<0>(*result);
                view->progress = std::get<1>(*result);
                view->loadedOnce = true;
                if (view->ready) {
                    std::lock_guard<std::mutex> lock(g_tagViewCacheMutex);
                    if (view->tagsMode) {
                        view->tagLines = std::get<3>(*result);
                        g_tagViewLinesCache = view->tagLines;
                        g_tagViewLinesCached = true;
                    } else {
                        view->rows = TagResultsRows(std::get<2>(*result));
                        g_tagViewRowsCache[TagViewCacheKey(view->tag)] = view->rows;
                        TagResultsSort(view->rows, view->sort, view->ascending);
                    }
                }
                TagViewApply(state, view);
            });
        } catch (...) {
        }
    });
    if (!started) {
        view->loading = false;
    }
}

void TagViewLoadSoon(WindowState* state) {
    TagViewLoadNow(state);
}

void TagViewUpdateSortGlyphs(TagViewPanel* view) {
    // The arrow sits on the title of the column the list is sorted by.
    int at = 0;
    for (auto child : view->columnHeader.Children()) {
        auto button = child.try_as<muxc::Button>();
        if (!button) {
            continue;
        }
        auto face = button.Content().try_as<muxc::StackPanel>();
        if (face && face.Children().Size() > 1) {
            auto arrow = face.Children().GetAt(1).try_as<muxc::FontIcon>();
            if (arrow) {
                bool mine = (int)view->sort == at;
                arrow.Opacity(mine ? 1.0 : 0.0);
                arrow.Glyph(view->ascending ? L"\uE70E" : L"\uE70D");
            }
        }
        at++;
    }
}

void TagViewSortBy(WindowState* state, TagViewPanel* view, TagResultSort by) {
    if (view->sort == by) {
        view->ascending = !view->ascending;
    } else {
        view->sort = by;
        view->ascending = true;
    }
    view->selected.clear();
    TagResultsSort(view->rows, view->sort, view->ascending);
    TagViewUpdateSortGlyphs(view);
    TagViewApply(state, view);
}

void TagViewKey(WindowState* state, TagViewPanel* view, mux::Input::KeyRoutedEventArgs const& args) {
    using winrt::Windows::System::VirtualKey;
    VirtualKey key = args.Key();
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    size_t count = TagViewCount(view);
    if (key == VirtualKey::Back || key == VirtualKey::Escape || (alt && key == VirtualKey::Left)) {
        args.Handled(true);
        TagViewLeave(state);
        return;
    }
    if (view->tagsMode || count == 0) {
        return;
    }
    if (key == VirtualKey::Down || key == VirtualKey::Up) {
        size_t at = view->selected.empty() ? (key == VirtualKey::Down ? SIZE_MAX : count) : (key == VirtualKey::Down ? *view->selected.rbegin() : *view->selected.begin());
        size_t next = key == VirtualKey::Down ? (at == SIZE_MAX ? 0 : std::min(count - 1, at + 1)) : (at == 0 ? 0 : std::min(count - 1, at - 1));
        TagViewSelect(view, next, false, shift);
        double top = next * kTagViewRow;
        double offset = view->scroller.VerticalOffset(), viewport = view->scroller.ViewportHeight();
        if (top < offset) {
            view->scroller.ChangeView(nullptr, top, nullptr, true);
        } else if (top + kTagViewRow > offset + viewport) {
            view->scroller.ChangeView(nullptr, top + kTagViewRow - viewport, nullptr, true);
        }
        args.Handled(true);
    } else if (key == VirtualKey::Enter && !view->selected.empty()) {
        size_t first = *view->selected.begin();
        if (first < view->rows.size()) {
            TagViewOpenItem(state, view->rows[first].path, view->rows[first].isFolder);
        }
        args.Handled(true);
    } else if (ctrl && key == VirtualKey::A) {
        view->selected.clear();
        for (size_t i = 0; i < count; i++) {
            view->selected.insert(i);
        }
        for (auto& entry : view->realized) {
            TagViewSetSelectedLook(view, entry.first);
        }
        args.Handled(true);
    }
}

void TagViewBuild(WindowState* state, TagViewPanel* view) {
    muxc::Grid root = LoadXaml(std::wstring(L"<Grid ") + kXamlNs +
                               L" Background='{ThemeResource SolidBackgroundFillColorBaseBrush}' IsTabStop='True' "
                               L"AutomationProperties.AutomationId='TagView'/>").as<muxc::Grid>();
    for (double height : {56.0, 32.0}) {
        muxc::RowDefinition row;
        row.Height(mux::GridLength{height, mux::GridUnitType::Pixel});
        root.RowDefinitions().Append(row);
    }
    muxc::RowDefinition rest;
    rest.Height(mux::GridLength{1, mux::GridUnitType::Star});
    root.RowDefinitions().Append(rest);

    // The title bar: the arrow back, the tag's icon, its name and how many files.
    muxc::Grid bar;
    bar.Margin(mux::Thickness{12, 0, 12, 0});
    auto back = SidebarNewButtonWith(L"TagViewBack", 32, L"0", kSidebarSmallTemplate);
    back.Width(32);
    back.HorizontalAlignment(mux::HorizontalAlignment::Left);
    back.VerticalAlignment(mux::VerticalAlignment::Center);
    back.Content(SidebarGlyph(L"\uE72B", 14));
    mux::Automation::AutomationProperties::SetName(back, Str(L"tagview.back"));
    bar.Children().Append(back);
    muxc::StackPanel titles;
    titles.Orientation(muxc::Orientation::Horizontal);
    titles.VerticalAlignment(mux::VerticalAlignment::Center);
    titles.Margin(mux::Thickness{44, 0, 0, 0});
    muxc::Grid iconHost;
    iconHost.Width(16);
    iconHost.Height(16);
    iconHost.Margin(mux::Thickness{0, 0, 10, 0});
    titles.Children().Append(iconHost);
    auto title = MakeText(L"", 20, true, false);
    title.VerticalAlignment(mux::VerticalAlignment::Center);
    titles.Children().Append(title);
    auto count = MakeText(L"", 13, false, true);
    count.VerticalAlignment(mux::VerticalAlignment::Center);
    count.Margin(mux::Thickness{12, 4, 0, 0});
    titles.Children().Append(count);
    bar.Children().Append(titles);
    root.Children().Append(bar);

    // The titles of the columns, each a button that sorts.
    muxc::Grid header;
    header.Margin(mux::Thickness{8, 0, 8, 0});  // As the rows' cells, so that the columns line up.
    muxc::Grid::SetRow(header, 1);
    for (int k = 0; k < 3; k++) {
        double saved = (double)Wh_GetIntValue((L"sidebar.tagview.w" + std::to_wstring(k)).c_str(), 0);
        if (saved >= 40 && saved <= 1000) {
            view->widths[k] = saved;
        }
    }
    TagViewColumns(header, true, view);
    const wchar_t* ids[4] = {L"tagview.name", L"tagview.location", L"tagview.modified", L"tagview.size"};
    TagResultSort sorts[4] = {TagResultSort::Name, TagResultSort::Location, TagResultSort::Modified, TagResultSort::Size};
    for (int i = 0; i < 4; i++) {
        auto button = SidebarNewButton((std::wstring(L"TagViewSort.") + std::to_wstring(i)).c_str(), 32, L"0");
        muxc::StackPanel face;  // [title, arrow]: the arrow is the second child (TagViewUpdateSortGlyphs).
        face.Orientation(muxc::Orientation::Horizontal);
        face.Spacing(6);
        face.HorizontalAlignment(i == 3 ? mux::HorizontalAlignment::Right : mux::HorizontalAlignment::Left);
        face.VerticalAlignment(mux::VerticalAlignment::Center);
        face.Margin(mux::Thickness{i == 0 ? 34.0 : 8.0, 0, 8, 0});  // The name's title starts where the names do (after the icon).
        face.Children().Append(MakeText(Str(ids[i]), 12, false, true));
        auto arrow = SidebarGlyph(L"\uE70E", 10);
        arrow.Opacity(i == 0 ? 1.0 : 0.0);
        arrow.VerticalAlignment(mux::VerticalAlignment::Center);
        face.Children().Append(arrow);
        button.Content(face);
        muxc::Grid::SetColumn(button, i);
        header.Children().Append(button);
        TagResultSort by = sorts[i];
        Keep(button, button.Click(winrt::auto_revoke, [state, by](Inspectable const&, mux::RoutedEventArgs const&) {
            if (state->tagView) {
                TagViewSortBy(state, state->tagView, by);
            }
        }));
    }
    // The grips: a handle on the right edge of the first three titles. Dragging the one after Name takes from (or gives to) Location,
    // the others change their own column; a double click puts the widths back.
    auto applyWidths = [state]() {
        TagViewPanel* v = state->tagView;
        if (!v) {
            return;
        }
        for (int k = 0; k < 3; k++) {
            auto width = mux::GridLength{v->widths[k], mux::GridUnitType::Pixel};
            v->columnHeader.ColumnDefinitions().GetAt(k + 1).Width(width);
            for (auto& entry : v->realized) {
                if (entry.second.cells && entry.second.cells.ColumnDefinitions().Size() == 4) {
                    entry.second.cells.ColumnDefinitions().GetAt(k + 1).Width(width);
                }
            }
        }
    };
    for (int i = 0; i < 3; i++) {
        muxc::Border grip;
        grip.Width(12);
        grip.HorizontalAlignment(mux::HorizontalAlignment::Right);
        grip.Margin(mux::Thickness{0, 0, -6, 0});
        grip.Background(Brush(0, 0, 0, 0));
        muxc::Border line = SidebarThemedBorder(L"DividerStrokeColorDefaultBrush");
        line.Width(1);
        line.Margin(mux::Thickness{0, 8, 0, 8});
        grip.Child(line);
        muxc::Grid::SetColumn(grip, i);
        try {
            grip.as<mux::IUIElementProtected>().ProtectedCursor(winrt::Microsoft::UI::Input::InputSystemCursor::Create(
                winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast));
        } catch (...) {
            LogHresult(L"setting the resize cursor of a column");
        }
        // [dragging, pointer x at the press, widths at the press]
        auto drag = std::make_shared<std::array<double, 5>>();
        (*drag)[0] = 0;
        winrt::weak_ref<muxc::Grid> weakHeader = header;
        Keep(grip, grip.PointerPressed(winrt::auto_revoke, [drag, weakHeader, state](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
            auto h = weakHeader.get();
            if (!h || !state->tagView) {
                return;
            }
            (*drag)[0] = 1;
            (*drag)[1] = args.GetCurrentPoint(h).Position().X;
            for (int k = 0; k < 3; k++) {
                (*drag)[2 + k] = state->tagView->widths[k];
            }
            sender.as<mux::UIElement>().CapturePointer(args.Pointer());
            args.Handled(true);
        }));
        Keep(grip, grip.PointerMoved(winrt::auto_revoke, [drag, weakHeader, state, i, applyWidths](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
            auto h = weakHeader.get();
            if (!(*drag)[0] || !h || !state->tagView) {
                return;
            }
            double delta = args.GetCurrentPoint(h).Position().X - (*drag)[1];
            int k = i == 0 ? 0 : i - 1;  // Which of the three widths it changes.
            double next = (*drag)[2 + k] + (i == 0 ? -delta : delta);
            state->tagView->widths[k] = std::clamp(next, 60.0, 900.0);
            applyWidths();
        }));
        auto finish = [drag, state](bool) {
            if (!(*drag)[0]) {
                return;
            }
            (*drag)[0] = 0;
            if (state->tagView) {
                for (int k = 0; k < 3; k++) {
                    Wh_SetIntValue((L"sidebar.tagview.w" + std::to_wstring(k)).c_str(), (int)std::lround(state->tagView->widths[k]));
                }
            }
        };
        Keep(grip, grip.PointerReleased(winrt::auto_revoke, [finish](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
            sender.as<mux::UIElement>().ReleasePointerCapture(args.Pointer());
            finish(true);
        }));
        Keep(grip, grip.PointerCanceled(winrt::auto_revoke, [finish](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) { finish(true); }));
        Keep(grip, grip.DoubleTapped(winrt::auto_revoke, [state, applyWidths](Inspectable const&, mux::Input::DoubleTappedRoutedEventArgs const& args) {
            if (state->tagView) {
                const double defaults[3] = {260, 150, 90};
                for (int k = 0; k < 3; k++) {
                    state->tagView->widths[k] = defaults[k];
                    Wh_SetIntValue((L"sidebar.tagview.w" + std::to_wstring(k)).c_str(), 0);
                }
                applyWidths();
            }
            args.Handled(true);
        }));
        header.Children().Append(grip);
    }
    root.Children().Append(header);

    muxc::ScrollViewer scroller;
    scroller.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
    scroller.HorizontalScrollBarVisibility(muxc::ScrollBarVisibility::Disabled);
    muxc::Grid::SetRow(scroller, 2);
    muxc::Canvas canvas;
    canvas.VerticalAlignment(mux::VerticalAlignment::Top);  // With a height of its own it would sit in the middle of the viewer.
    canvas.HorizontalAlignment(mux::HorizontalAlignment::Stretch);
    scroller.Content(canvas);
    root.Children().Append(scroller);
    auto message = MakeText(L"", 14, false, true);
    message.HorizontalAlignment(mux::HorizontalAlignment::Center);
    message.Visibility(mux::Visibility::Collapsed);
    muxc::ProgressRing ring;
    ring.Width(28);
    ring.Height(28);
    ring.HorizontalAlignment(mux::HorizontalAlignment::Center);
    ring.Visibility(mux::Visibility::Collapsed);
    muxc::StackPanel messageHost;
    messageHost.Spacing(12);
    messageHost.HorizontalAlignment(mux::HorizontalAlignment::Center);
    messageHost.VerticalAlignment(mux::VerticalAlignment::Top);
    messageHost.Margin(mux::Thickness{0, 40, 0, 0});
    messageHost.IsHitTestVisible(false);
    messageHost.Visibility(mux::Visibility::Collapsed);
    messageHost.Children().Append(ring);
    messageHost.Children().Append(message);
    muxc::Grid::SetRow(messageHost, 2);
    root.Children().Append(messageHost);
    view->messageHost = messageHost;
    view->ring = ring;

    view->root = root;
    view->iconHost = iconHost;
    view->title = title;
    view->count = count;
    view->columnHeader = header;
    view->scroller = scroller;
    view->canvas = canvas;
    view->message = message;
    Keep(back, back.Click(winrt::auto_revoke, [state](Inspectable const&, mux::RoutedEventArgs const&) { TagViewLeave(state); }));
    Keep(scroller, scroller.ViewChanged(winrt::auto_revoke, [state](Inspectable const&, muxc::ScrollViewerViewChangedEventArgs const&) {
        if (state->tagView && state->tagView->active) {
            TagViewRealize(state, state->tagView);
        }
    }));
    Keep(scroller, scroller.SizeChanged(winrt::auto_revoke, [state](Inspectable const&, mux::SizeChangedEventArgs const&) {
        if (state->tagView && state->tagView->active) {
            TagViewRealize(state, state->tagView);
        }
    }));
    Keep(root, root.KeyDown(winrt::auto_revoke, [state](Inspectable const&, mux::Input::KeyRoutedEventArgs const& args) {
        if (state->tagView && state->tagView->active) {
            TagViewKey(state, state->tagView, args);
        }
    }));

    mux::Hosting::DesktopWindowXamlSource source;
    source.Initialize(winrt::Microsoft::UI::WindowId{(uint64_t)state->topLevel});
    source.Content(root);
    view->source = source;
    view->bridge = source.SiteBridge();
}

bool TagViewViewRect(WindowState* state, RECT* rect) {
    HWND view = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
    if (!view || !GetWindowRect(view, rect)) {
        return false;
    }
    MapWindowPoints(nullptr, state->topLevel, (POINT*)rect, 2);
    return rect->right > rect->left && rect->bottom > rect->top;
}


// Opens the panel on the file area for a tag (or for the list of tags).
void TagViewOpen(WindowState* state, bool tagsMode, std::wstring const& tag) {
    LogVerbose(L"TAGVIEW open tag=%s tags=%d sidebar=%p queue=%d", tag.c_str(), (int)tagsMode, (void*)state->sidebar, state->queue ? 1 : 0);
    SidebarPanel* panel = state->sidebar;
    if (!panel || !state->queue) {
        return;
    }
    if (!state->tagView) {
        state->tagView = new TagViewPanel();
    }
    TagViewPanel* view = state->tagView;
    try {
        // One list per window: opening one in another tab than the open list's closes that list (its tab gets its title back).
        if (view->active && view->tab && FindVisibleChild(state->topLevel, L"ShellTabWindowClass") != view->tab) {
            TagViewLeave(state);
        }
        if (!view->source) {
            TagViewBuild(state, view);
        }
        view->tab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
        view->tabHidden = false;
        if (!view->active) {
            IShellBrowser* browser = BrowserOf(state->topLevel);
            Pidl folder = browser ? CurrentFolder(browser) : Pidl();
            view->folderAtOpen = folder ? LocationPidl(ILClone(folder.get())) : nullptr;
        }
        view->tagsMode = tagsMode;
        view->tag = tag;
        view->colour = 0;
        for (auto const& def : TagListSnapshot()) {
            if (!tagsMode && TagNamesEqual(def.name, tag)) {
                view->colour = def.colour;
            }
        }
        view->rows.clear();
        view->tagLines.clear();
        view->selected.clear();
        view->ready = true;
        view->progress = 0;
        view->sort = TagResultSort::Name;
        view->ascending = true;
        view->loadedOnce = false;
        {
            // What the list showed last time is shown at once; the index is read again behind it.
            std::lock_guard<std::mutex> lock(g_tagViewCacheMutex);
            if (tagsMode && g_tagViewLinesCached) {
                view->tagLines = g_tagViewLinesCache;
                view->loadedOnce = true;
            } else if (!tagsMode) {
                auto cached = g_tagViewRowsCache.find(TagViewCacheKey(tag));
                if (cached != g_tagViewRowsCache.end()) {
                    view->rows = cached->second;
                    TagResultsSort(view->rows, view->sort, view->ascending);
                    view->loadedOnce = true;
                }
            }
        }
        TagViewUpdateSortGlyphs(view);
        view->iconHost.Children().Clear();
        if (!tagsMode) {
            view->iconHost.Children().Append(SidebarTagIcon(view->colour, LabelsDarkTheme()));
            view->iconHost.Visibility(mux::Visibility::Visible);
            view->title.Text(tag);
        } else {
            view->iconHost.Visibility(mux::Visibility::Collapsed);
            view->title.Text(Str(L"sidebar.tags"));
        }
        view->active = true;
        view->ticks = 0;
        view->generation++;
        view->loading = false;
        panel->viewKey = tagsMode ? std::wstring(L"tags") : L"tag." + tag;
        panel->lastFolder.reset();
        RECT rect{};
        if (TagViewViewRect(state, &rect)) {
            view->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top});
            view->bridge.Show();
            // The island's picture follows the window order only when that changes: a window that is already first in it (but
            // whose picture is under the columns') is put last and first again.
            HWND window = (HWND)view->bridge.WindowId().Value;
            SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            view->rect = rect;
            view->shown = true;
        }
        g_tagViewWindowLocal = (HWND)view->bridge.WindowId().Value;
        LogVerbose(L"TAGVIEW shown=%d rect=%d,%d,%d,%d", view->shown ? 1 : 0, (int)view->rect.left, (int)view->rect.top, (int)view->rect.right, (int)view->rect.bottom);
        TagViewApply(state, view);
        TagViewLoadNow(state);
        view->root.Focus(mux::FocusState::Programmatic);
    } catch (...) {
        LogHresult(L"opening the tag list");
    }
}

}  // namespace

HWND TagViewWindow() {
    return g_tagViewWindowLocal;
}

void TagResultsShow(WindowState* state, std::wstring const& tag) {
    TagViewOpen(state, false, tag);
}

void TagListShow(WindowState* state) {
    TagViewOpen(state, true, std::wstring());
}

bool TagViewActive(WindowState* state) {
    return state && state->tagView && state->tagView->active;
}

// Closes the list: the panel goes, the pill goes back to the folder's entry. Explorer's own folder was never changed.
void TagViewLeave(WindowState* state) {
    TagViewPanel* view = state ? state->tagView : nullptr;
    if (!view || !view->active) {
        return;
    }
    LogVerbose(L"TAGVIEW leave");
    TagViewRestoreTitle(view);
    view->active = false;
    view->generation++;
    view->loading = false;
    g_tagViewWindowLocal = nullptr;
    try {
        if (view->bridge && view->shown) {
            view->bridge.Hide();
        }
        view->shown = false;
        view->canvas.Children().Clear();
        view->realized.clear();
        view->rows.clear();
        view->tagLines.clear();
    } catch (...) {
        LogHresult(L"closing the tag list");
    }
    if (state->sidebar) {
        state->sidebar->viewKey.clear();
        state->sidebar->lastFolder.reset();  // The entry of the folder is worked out again at the next tick.
    }
}

// The paths of the rows selected in the open list (for the details pane); false when no list shows or nothing is selected.
bool TagViewSelectedPaths(WindowState* state, std::vector<std::wstring>* paths) {
    TagViewPanel* view = state ? state->tagView : nullptr;
    if (!view || !view->active || view->tabHidden || view->tagsMode || !view->shown) {
        return false;
    }
    for (size_t i : view->selected) {
        if (i < view->rows.size()) {
            paths->push_back(view->rows[i].path);
        }
    }
    return !paths->empty();
}

// A navigation from the sidebar closes the list of the tab it is made in; another tab's list stays.
void TagViewLeaveHere(WindowState* state) {
    TagViewPanel* view = state ? state->tagView : nullptr;
    if (view && view->active && (!view->tab || FindVisibleChild(state->topLevel, L"ShellTabWindowClass") == view->tab)) {
        TagViewLeave(state);
    }
}

// Every tick of the sidebar: closes the list when Explorer went elsewhere, keeps it on the file area, reads the index again now
// and then (and often while it is still being made).
void TagViewTick(WindowState* state) {
    TagViewPanel* view = state->tagView;
    if (!view || !view->active) {
        return;
    }
    view->ticks++;
    // The list belongs to one tab: it goes out of sight while another tab shows, and the sidebar follows that tab's folder.
    if (view->tab && !IsWindow(view->tab)) {
        TagViewLeave(state);
        return;
    }
    HWND shownTab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
    if (view->tab && shownTab != view->tab) {
        if (view->shown) {
            view->bridge.Hide();
            view->shown = false;
        }
        if (!view->tabHidden) {
            view->tabHidden = true;
            g_tagViewWindowLocal = nullptr;
            if (state->sidebar) {
                view->hiddenKey = state->sidebar->viewKey;
                state->sidebar->viewKey.clear();
                state->sidebar->lastFolder.reset();
            }
        }
        return;
    }
    if (view->tabHidden) {
        view->tabHidden = false;
        g_tagViewWindowLocal = (HWND)view->bridge.WindowId().Value;
        if (state->sidebar) {
            state->sidebar->viewKey = view->hiddenKey;
        }
        view->rect = RECT{};  // Placed again below.
    }
    IShellBrowser* browser = BrowserOf(state->topLevel);
    if (browser && view->folderAtOpen) {
        Pidl folder = CurrentFolder(browser);
        if (folder && !ILIsEqual(folder.get(), view->folderAtOpen.get())) {
            TagViewLeave(state);  // Before the title: Explorer has written the new folder's name on the tab, which stays.
            return;
        }
    }
    TagViewTitle(state, view);
    try {
        RECT rect{};
        if (!TagViewViewRect(state, &rect)) {
            if (view->shown) {
                view->bridge.Hide();
                view->shown = false;
            }
            return;
        }
        if (!view->shown || memcmp(&rect, &view->rect, sizeof(rect)) != 0) {
            view->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top});
            view->bridge.Show();
            SetWindowPos((HWND)view->bridge.WindowId().Value, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            view->rect = rect;
            view->shown = true;
        }
        HWND window = (HWND)view->bridge.WindowId().Value;
        if (window && GetWindow(window, GW_HWNDPREV)) {
            SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);  // A tab made later covers it.
        }
        unsigned every = view->ready ? 66 : 16;  // About every 4 seconds, and every second while the index is being made.
        if (view->ticks % every == 0) {
            TagViewLoadNow(state);
        }
    } catch (...) {
        LogHresult(L"placing the tag list");
    }
}

// The window goes (or the sidebar is switched off): the panel is let go.
void ReleaseTagView(WindowState* state) {
    TagViewPanel* view = state->tagView;
    if (!view) {
        return;
    }
    state->tagView = nullptr;
    g_tagViewWindowLocal = nullptr;
    view->generation++;
    TagViewRestoreTitle(view);
    try {
        if (view->bridge) {
            view->bridge.Hide();
        }
        view->canvas = nullptr;
        view->realized.clear();
        if (view->source) {
            view->source.Content(nullptr);
            view->source.Close();
        }
    } catch (...) {
        LogHresult(L"releasing the tag list");
    }
    delete view;
}

// The window closed: nothing is released (the objects belong to a window that is gone).
void AbandonTagView(WindowState* state) {
    TagViewPanel* view = state->tagView;
    if (!view) {
        return;
    }
    state->tagView = nullptr;
    g_tagViewWindowLocal = nullptr;
    view->generation++;
    (void)winrt::detach_abi(view->source);
    (void)winrt::detach_abi(view->bridge);
    (void)winrt::detach_abi(view->root);
    (void)winrt::detach_abi(view->iconHost);
    (void)winrt::detach_abi(view->title);
    (void)winrt::detach_abi(view->count);
    (void)winrt::detach_abi(view->columnHeader);
    (void)winrt::detach_abi(view->scroller);
    (void)winrt::detach_abi(view->canvas);
    (void)winrt::detach_abi(view->message);
    (void)winrt::detach_abi(view->messageHost);
    (void)winrt::detach_abi(view->savedIcon);
    (void)winrt::detach_abi(view->ourIcon);
    (void)winrt::detach_abi(view->ring);
    for (auto& entry : view->realized) {
        (void)winrt::detach_abi(entry.second.grid);
        (void)winrt::detach_abi(entry.second.back);
    }
    (void)view;  // Leaked on purpose, like the sidebar's.
}
