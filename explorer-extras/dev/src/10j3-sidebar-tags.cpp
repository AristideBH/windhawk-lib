// Sidebar, Tags section: the most recent visible tags as rows (the label shape in the label colour), a "…" menu on the header to
// manage them (new tag, and for each tag rename, colour, show or hide, delete), and files dropped on a row, which get that tag.
// Rename, recolour and delete change the tag on every file that carries it: a pass over the files the tag index lists, on a mod
// thread, with a progress line under the header and a way to stop it. The list of tags is 03d, the index is 04d, the writing of
// a tag on a file is the labels' own (10g).
//
// The rows are made again whenever the tags that should show (names, colours, order) are not the ones that do: a tag added
// elsewhere, one that was used, one that was renamed. A tick checks it.

void TagsOpenInNewTab(WindowState* state, std::wstring const& name);
void TagsRename(WindowState* state, std::wstring const& oldName);
void TagsRecolour(WindowState* state, std::wstring const& name, int colour);
void TagsDelete(WindowState* state, std::wstring const& name);

namespace {

[[clang::no_destroy]] thread_local muxc::Flyout t_tagsFlyout{nullptr};  // The prompt or the confirmation that is open.

std::wstring TagsSignature(std::vector<TagDef> const& list) {
    std::wstring text;
    for (auto const& tag : list) {
        text += tag.name + L"\t" + std::to_wstring((int)tag.colour) + L"\n";
    }
    return text;
}

// The user's own order of the tags (the "My order" setting): their names, one per line, in a value of the mod's.
const wchar_t kTagsOrderValue[] = L"sidebar.tags.order";

std::vector<std::wstring> TagsOrderLoad() {
    std::vector<std::wstring> order;
    std::vector<wchar_t> buffer(32000);
    size_t length = Wh_GetStringValue(kTagsOrderValue, buffer.data(), buffer.size());
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

// Every tag that is not hidden, in the order the setting asks for.
std::vector<TagDef> TagsOrdered() {
    int mode = g_sidebarTagsSort;
    std::vector<TagDef> all = TagListRecent((int)kTagListMax);  // Visible tags, the most recently used first.
    if (mode == 2) {
        std::stable_sort(all.begin(), all.end(), [](TagDef const& a, TagDef const& b) {
            return CompareStringOrdinal(a.name.c_str(), (int)a.name.size(), b.name.c_str(), (int)b.name.size(), TRUE) == CSTR_LESS_THAN;
        });
    } else if (mode == 1) {
        auto order = TagsOrderLoad();
        std::vector<TagDef> sorted;
        std::vector<bool> taken(all.size(), false);
        for (auto const& name : order) {
            for (size_t i = 0; i < all.size(); i++) {
                if (!taken[i] && TagNamesEqual(all[i].name, name)) {
                    taken[i] = true;
                    sorted.push_back(all[i]);
                    break;
                }
            }
        }
        for (size_t i = 0; i < all.size(); i++) {
            if (!taken[i]) {
                sorted.push_back(all[i]);  // A tag the order does not know yet comes after the others, most recent first.
            }
        }
        all = std::move(sorted);
    }
    return all;
}

std::vector<TagDef> TagsShown() {
    int wanted = g_sidebarRecentTags;
    size_t most = wanted < 1 ? kTagListMax : (size_t)wanted;  // Below 1 (the setting's -1): every tag that is not hidden.
    auto list = TagsOrdered();
    if (list.size() > most) {
        list.resize(most);
    }
    return list;
}

// Puts a tag at `index` of the rows as shown (the order of "My order"), and saves the whole order.
void TagsMoveTo(std::wstring const& name, size_t index) {
    auto all = TagsOrdered();
    std::vector<std::wstring> names;
    size_t at = 0;
    for (size_t i = 0; i < all.size(); i++) {
        if (TagNamesEqual(all[i].name, name)) {
            continue;
        }
        if (i < index) {
            at++;
        }
        names.push_back(all[i].name);
    }
    names.insert(names.begin() + std::min(at, names.size()), name);
    std::wstring text;
    for (auto const& n : names) {
        text += n + L"\n";
    }
    Wh_SetStringValue(kTagsOrderValue, text.c_str());
}

// Between which two rows of Tags a point of the island falls, as for Favorites.
void TagsInsertPoint(SidebarPanel* panel, winrt::Windows::Foundation::Point point, size_t* index, float* lineY) {
    winrt::Windows::Foundation::Rect block{};
    float top = panel->tagsBlock && SidebarRectOf(panel->tagsBlock, &block) ? block.Y : 0;
    size_t n = 0;
    float lastBottom = -1;
    *index = 0;
    *lineY = -1;
    for (auto const& mark : panel->locationMarks) {
        if (!mark.button || mark.key.rfind(L"tag.", 0) != 0) {
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

void TagsShowInsertLine(SidebarPanel* panel, float lineY) {
    if (!panel->tagLine) {
        return;
    }
    if (lineY < 0) {
        panel->tagLine.Visibility(mux::Visibility::Collapsed);
        return;
    }
    panel->tagLine.Margin(mux::Thickness{10, lineY - 1, 10, 0});
    panel->tagLine.Visibility(mux::Visibility::Visible);
}

mux::FrameworkElement TagsAnchorFor(SidebarPanel* panel, std::wstring const& name) {
    for (auto const& mark : panel->locationMarks) {
        if (mark.key == L"tag." + name && mark.button) {
            return mark.button;
        }
    }
    return panel->host;
}

// ---- The passes over the files ----

TagRewriteResult TagsRunPass(TagRewrite const& job, std::vector<std::wstring> const* paths,
                             std::function<void(size_t, size_t)> const& progress, std::atomic<bool> const& cancel) {
    if (paths) {
        return TagIndexRewriteFiles(*paths, job, progress, cancel, nullptr);
    }
    return TagIndexRewrite(job, progress, cancel);
}

// Starts a pass on a mod thread (its progress shows at the next tick); one at a time. `removeTagAfter` takes the tag out of the
// list when the pass went through on every file (a delete).
bool TagsStartJob(WindowState* state, TagRewrite job, bool removeTagAfter) {
    SidebarPanel* panel = state ? state->sidebar : nullptr;
    if (!panel || (panel->tagJob && !panel->tagJob->finished)) {
        return false;
    }
    auto running = std::make_shared<TagsJob>();
    panel->tagJob = running;
    bool started = StartModThread([running, job, removeTagAfter]() {
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        TagRewriteResult result = TagsRunPass(
            job, nullptr,
            [running](size_t done, size_t total) {
                running->done = done;
                running->total = total;
            },
            running->cancel);
        if (removeTagAfter && !result.cancelled && result.failed == 0) {
            TagListRemove(job.oldName);
        }
        if (com) {
            CoUninitialize();
        }
        running->finished = true;
    });
    if (!started) {
        panel->tagJob.reset();
    }
    return started;
}

// ---- Prompts ----

void TagsClosePrompt() {
    if (t_tagsFlyout) {
        try {
            t_tagsFlyout.Hide();
        } catch (...) {
        }
        t_tagsFlyout = nullptr;
    }
}

muxc::Button TagsFlyoutButton(const wchar_t* text, bool accent) {
    auto button = LoadXaml(std::wstring(L"<Button ") + kXamlNs + (accent ? L" Style='{StaticResource AccentButtonStyle}'/>" : L"/>")).as<muxc::Button>();
    button.Content(winrt::box_value(winrt::hstring{text}));
    return button;
}

void TagsShowFlyout(mux::FrameworkElement const& anchor, muxc::Flyout const& flyout) {
    TagsClosePrompt();
    t_tagsFlyout = flyout;
    Keep(flyout, flyout.Closed(winrt::auto_revoke, [](Inspectable const&, Inspectable const&) { t_tagsFlyout = nullptr; }));
    flyout.ShowAt(anchor);
}

std::wstring TagsProblemOf(std::wstring const& name, std::wstring const& except) {
    if (!TagNameValid(name)) {
        return Str(L"sidebar.tags.invalid");
    }
    for (auto const& tag : TagListSnapshot()) {
        if (TagNamesEqual(tag.name, name) && !TagNamesEqual(tag.name, except)) {
            return Str(L"sidebar.tags.exists");
        }
    }
    return std::wstring();
}

void TagsRefillLater(WindowState* state) {
    if (state && state->queue) {
        state->queue.TryEnqueue([state]() {
            if (state->sidebar && !g_unloading) {
                SidebarFillTags(state, state->sidebar);
            }
        });
    }
}

// A row with a name box, where the name of a tag is typed: at the top of the section for a new tag, in place of the row of the tag
// that is renamed. Enter keeps the name (a name that is refused stays in the box, with the reason as its tooltip), Escape drops
// it, and leaving the box keeps a good name and drops a bad one. `accept` gets the trimmed name and returns what is wrong with it.
void TagsStartEdit(WindowState* state, std::wstring const& initial, int colour, std::wstring const& renaming,
                   std::function<std::wstring(std::wstring const&)> accept) {
    SidebarPanel* panel = state ? state->sidebar : nullptr;
    if (!panel || panel->tagEditing || g_unloading) {
        return;
    }
    SidebarPanel::Section* section = nullptr;
    for (auto& s : panel->sections) {
        if (s.key == L"tags") {
            section = &s;
        }
    }
    if (!section || !section->body) {
        return;
    }
    if (section->collapsed) {
        SidebarToggleSection(state, L"tags");
    }
    auto body = section->body;
    uint32_t at = 0;
    if (!renaming.empty()) {
        for (auto const& mark : panel->locationMarks) {
            uint32_t found = 0;
            if (mark.key == L"tag." + renaming && mark.button && body.Children().IndexOf(mark.button, found)) {
                at = found;
                mark.button.Visibility(mux::Visibility::Collapsed);
            }
        }
    }
    muxc::Grid row;
    row.Height(32);
    row.Margin(mux::Thickness{12, 0, 8, 0});
    row.ColumnDefinitions().Append(muxc::ColumnDefinition());
    row.ColumnDefinitions().GetAt(0).Width(mux::GridLength{0, mux::GridUnitType::Auto});
    row.ColumnDefinitions().Append(muxc::ColumnDefinition());
    auto icon = SidebarTagIcon(colour, panel->dark);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    icon.Margin(mux::Thickness{3, 0, 8, 0});
    row.Children().Append(icon);
    auto box = LoadXaml(std::wstring(L"<TextBox ") + kXamlNs + L" MaxLength='" + std::to_wstring(kLabelsMaxName) +
                        L"' MinHeight='0' Height='28' Padding='6,0' VerticalContentAlignment='Center' "
                        L"AutomationProperties.AutomationId='SidebarTagEdit'/>").as<muxc::TextBox>();
    box.Text(initial);
    muxc::Grid::SetColumn(box, 1);
    row.Children().Append(box);
    body.Children().InsertAt(at, row);
    panel->tagEditing = true;
    g_renaming = true;

    auto finished = std::make_shared<bool>(false);
    winrt::weak_ref<muxc::TextBox> weakBox = box;
    // mode 0: Enter. 1: the box lost the keyboard. 2: Escape.
    auto finish = [state, finished, weakBox, accept, row](int mode) {
        if (*finished) {
            return;
        }
        SidebarPanel* panel = state->sidebar;
        auto box = weakBox.get();
        if (!panel || !box) {
            return;
        }
        std::wstring name = LabelsTrim(std::wstring(box.Text()));
        if (mode != 2 && !name.empty()) {
            std::wstring wrong = accept(name);
            if (!wrong.empty()) {
                if (mode == 0) {
                    box.BorderBrush(mux::Application::Current().Resources().Lookup(winrt::box_value(L"SystemFillColorCriticalBrush")).as<mux::Media::Brush>());
                    muxc::ToolTipService::SetToolTip(box, winrt::box_value(winrt::hstring{wrong}));
                    return;
                }
            }
        }
        *finished = true;
        panel->tagEditing = false;
        g_renaming = false;
        SidebarFillTags(state, panel);  // The row goes, and the rows of the tags are made again.
    };
    Keep(box, box.KeyDown(winrt::auto_revoke, [finish](Inspectable const&, mux::Input::KeyRoutedEventArgs const& args) {
        if (args.Key() == winrt::Windows::System::VirtualKey::Enter) {
            args.Handled(true);
            finish(0);
        } else if (args.Key() == winrt::Windows::System::VirtualKey::Escape) {
            args.Handled(true);
            finish(2);
        }
    }));
    Keep(box, box.LostFocus(winrt::auto_revoke, [finish](Inspectable const&, mux::RoutedEventArgs const&) { finish(1); }));
    box.Focus(mux::FocusState::Programmatic);
    box.SelectAll();
}

// ---- The menu of a tag ----

template <typename Items>
void TagsMenuItemsFor(WindowState* state, Items items, TagDef const& tag, bool withOpen = false) {
    std::wstring name = tag.name;
    if (withOpen) {
        // The row's own menu starts with where it leads: Open, Open in new tab, then a line.
        muxc::MenuFlyoutItem open;
        open.Text(Str(L"sidebar.tags.open"));
        Keep(open, open.Click(winrt::auto_revoke, [state, name](Inspectable const&, mux::RoutedEventArgs const&) {
            TagResultsShow(state, name);
        }));
        items.Append(open);
        muxc::MenuFlyoutItem newTab;
        newTab.Text(Str(L"sidebar.newtab"));
        Keep(newTab, newTab.Click(winrt::auto_revoke, [state, name](Inspectable const&, mux::RoutedEventArgs const&) {
            TagsOpenInNewTab(state, name);
        }));
        items.Append(newTab);
        items.Append(muxc::MenuFlyoutSeparator());
    }
    muxc::MenuFlyoutItem rename;
    rename.Text(Str(L"sidebar.tags.rename"));
    Keep(rename, rename.Click(winrt::auto_revoke, [state, name](Inspectable const&, mux::RoutedEventArgs const&) { TagsRename(state, name); }));
    items.Append(rename);

    muxc::MenuFlyoutSubItem colours;
    colours.Text(Str(L"sidebar.tags.colour"));
    bool dark = LabelsDarkTheme();
    for (int colour = 0; colour < 8; colour++) {
        muxc::ToggleMenuFlyoutItem item;
        std::wstring colourName = LabelsColourName(colour);
        item.Text(colourName.empty() ? std::to_wstring(colour + 1) : colourName);
        item.IsChecked(tag.colour == colour);
        muxc::FontIcon dot;
        dot.FontFamily(mux::Media::FontFamily(L"Segoe UI"));
        dot.Glyph(L"●");
        COLORREF fill = LabelsDotRgb(colour, dark);
        dot.Foreground(mux::Media::SolidColorBrush(
            winrt::Windows::UI::ColorHelper::FromArgb(255, GetRValue(fill), GetGValue(fill), GetBValue(fill))));
        item.Icon(dot);
        Keep(item, item.Click(winrt::auto_revoke, [state, name, colour](Inspectable const&, mux::RoutedEventArgs const&) {
            TagsRecolour(state, name, colour);
        }));
        colours.Items().Append(item);
    }
    items.Append(colours);

    muxc::MenuFlyoutItem hide;
    hide.Text(Str(tag.hidden ? L"sidebar.tags.show" : L"sidebar.tags.hide"));
    bool hidden = tag.hidden;
    Keep(hide, hide.Click(winrt::auto_revoke, [state, name, hidden](Inspectable const&, mux::RoutedEventArgs const&) {
        TagListSetHidden(name, !hidden);
        TagsRefillLater(state);
    }));
    items.Append(hide);

    muxc::MenuFlyoutItem remove;
    remove.Text(Str(L"sidebar.tags.delete"));
    Keep(remove, remove.Click(winrt::auto_revoke, [state, name](Inspectable const&, mux::RoutedEventArgs const&) { TagsDelete(state, name); }));
    items.Append(remove);
}

// The "…" menu: a new tag, then every tag (hidden ones too) with its own menu.
void TagsFillManageMenu(WindowState* state, muxc::MenuFlyout const& menu, mux::FrameworkElement const& anchor) {
    menu.Items().Clear();
    muxc::MenuFlyoutItem add;
    add.Text(Str(L"sidebar.tags.new"));
    winrt::weak_ref<mux::FrameworkElement> weakAnchor = anchor;
    Keep(add, add.Click(winrt::auto_revoke, [state, weakAnchor](Inspectable const&, mux::RoutedEventArgs const&) {
        auto target = weakAnchor.get();
        if (!target) {
            return;
        }
        TagsStartEdit(state, std::wstring(), 0, std::wstring(), [state](std::wstring const& name) {
            std::wstring wrong = TagsProblemOf(name, std::wstring());
            if (wrong.empty()) {
                TagListAdd(name, 0);
                TagListTouch(name);
            }
            return wrong;
        });
    }));
    menu.Items().Append(add);
    auto all = TagListSnapshot();
    if (!all.empty()) {
        menu.Items().Append(muxc::MenuFlyoutSeparator());
    }
    for (auto const& tag : all) {
        muxc::MenuFlyoutSubItem sub;
        sub.Text(tag.hidden ? tag.name + L" (" + Str(L"sidebar.tags.hiddennote") + L")" : tag.name);
        TagsMenuItemsFor(state, sub.Items(), tag);
        menu.Items().Append(sub);
    }
}

TagDef TagsDefOf(std::wstring const& name) {
    for (auto const& tag : TagListSnapshot()) {
        if (TagNamesEqual(tag.name, name)) {
            return tag;
        }
    }
    TagDef none;
    none.name = name;
    return none;
}

}  // namespace

// ---- What the tags do ----

// Adds the tag to every item that lacks it, and marks the tag as the latest used. Writes the files: not for a window's thread.
void TagsAddToItems(std::vector<std::wstring> const& paths, std::wstring const& tag) {
    TagDef def = TagsDefOf(tag);
    bool known = false;
    for (auto const& listed : TagListSnapshot()) {
        known = known || TagNamesEqual(listed.name, tag);
    }
    if (!known) {
        return;
    }
    LabelsApplyToSelection(paths, LabelTag{def.colour, def.name}, true);
    TagListTouch(def.name);
}

// Takes the tag off every file in `paths` (the files the index lists when `paths` is null) and, when that went through on all of
// them, out of the list. Returns what the pass did.
TagRewriteResult TagsDeletePass(std::wstring const& name, std::vector<std::wstring> const* paths,
                                std::function<void(size_t, size_t)> const& progress, std::atomic<bool> const& cancel) {
    TagRewrite job;
    job.oldName = name;
    TagRewriteResult result = TagsRunPass(job, paths, progress, cancel);
    if (!result.cancelled && result.failed == 0) {
        TagListRemove(name);
    }
    return result;
}

void TagsRename(WindowState* state, std::wstring const& oldName) {
    SidebarPanel* panel = state ? state->sidebar : nullptr;
    if (!panel) {
        return;
    }
    TagsStartEdit(state, oldName, TagsDefOf(oldName).colour, oldName, [state, oldName](std::wstring const& name) {
        std::wstring wrong = TagsProblemOf(name, oldName);
        if (!wrong.empty()) {
            return wrong;
        }
        if (name == oldName) {
            return std::wstring();
        }
        if (!TagListRename(oldName, name)) {
            return std::wstring(Str(L"sidebar.tags.invalid"));
        }
        TagRewrite job;
        job.oldName = oldName;
        job.newName = name;
        TagsStartJob(state, job, false);
        return std::wstring();
    });
}

void TagsRecolour(WindowState* state, std::wstring const& name, int colour) {
    TagListRecolor(name, colour);
    TagRewrite job;
    job.oldName = name;
    job.newName = name;
    job.newColour = colour;
    TagsStartJob(state, job, false);
    TagsRefillLater(state);
}

// Asks first (with the number of files that have the tag), then runs the pass.
void TagsDelete(WindowState* state, std::wstring const& name) {
    SidebarPanel* panel = state ? state->sidebar : nullptr;
    if (!panel) {
        return;
    }
    size_t count = TagIndexCount(name);
    muxc::Flyout flyout;
    muxc::StackPanel content;
    content.Spacing(10);
    content.MaxWidth(190);
    wchar_t text[400];
    swprintf_s(text, Str(L"sidebar.tags.confirm"), name.c_str(), (unsigned)count);
    auto message = MakeText(text, 13, false, false);
    message.TextWrapping(mux::TextWrapping::Wrap);
    content.Children().Append(message);
    muxc::StackPanel buttons;
    buttons.Orientation(muxc::Orientation::Horizontal);
    buttons.Spacing(8);
    buttons.HorizontalAlignment(mux::HorizontalAlignment::Right);
    auto yes = TagsFlyoutButton(Str(L"sidebar.tags.delete"), true);
    auto no = TagsFlyoutButton(Str(L"sidebar.tags.cancel"), false);
    buttons.Children().Append(yes);
    buttons.Children().Append(no);
    content.Children().Append(buttons);
    flyout.Content(content);
    Keep(yes, yes.Click(winrt::auto_revoke, [state, name](Inspectable const&, mux::RoutedEventArgs const&) {
        TagsClosePrompt();
        TagRewrite job;
        job.oldName = name;
        TagsStartJob(state, job, true);
    }));
    Keep(no, no.Click(winrt::auto_revoke, [](Inspectable const&, mux::RoutedEventArgs const&) { TagsClosePrompt(); }));
    TagsShowFlyout(TagsAnchorFor(panel, name), flyout);
}

// Opens a new tab (on Home) and shows the tag's list in it once the tab has settled on its folder: a list opened earlier would see the
// tab still navigating and close.
void TagsOpenInNewTab(WindowState* state, std::wstring const& name) {
    if (!state || !state->queue) {
        return;
    }
    HWND oldTab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
    PIDLIST_ABSOLUTE home = nullptr;
    if (FAILED(SHParseDisplayName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}", nullptr, &home, 0, nullptr)) || !home) {
        return;
    }
    auto homePidl = std::shared_ptr<ITEMIDLIST>(home, [](ITEMIDLIST* p) { CoTaskMemFree(p); });
    OpenFolderInNewTab(state, homePidl.get());
    // Settled: the new tab's folder has been the same for half a second. (Not compared with the Home we asked for: the tab can show
    // another id for Home, and the compare never came true.)
    auto settled = std::make_shared<int>(0);
    auto last = std::make_shared<Pidl>();
    ULONGLONG started = GetTickCount64();
    try {
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(100));
        // A timer nobody holds is dropped and never ticks: the tick's own capture holds it, and lets go when it stops.
        auto holder = std::make_shared<mud::DispatcherQueueTimer>(timer);
        Keep(timer, timer.Tick(winrt::auto_revoke, [holder, oldTab, homePidl, settled, last, started, name](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
            WindowState* now = StateForThisThread(false);
            if (!now || g_unloading || GetTickCount64() - started > 8000) {
                LogVerbose(L"TAGS new tab: gave up (window %d)", now ? 1 : 0);
                sender.Stop();
                *holder = nullptr;
                return;
            }
            HWND tab = FindVisibleChild(now->topLevel, L"ShellTabWindowClass");
            IShellBrowser* browser = BrowserOf(now->topLevel);
            if (!tab || tab == oldTab || !browser) {
                return;
            }
            Pidl folder = CurrentFolder(browser);
            LogVerbose(L"TAGS new tab: tab=%p old=%p settled=%d folder=\"%s\"", (void*)tab, (void*)oldTab, *settled, folder ? PathOf(folder.get()).c_str() : L"(none)");
            if (folder && *last && ILIsEqual(folder.get(), last->get())) {
                (*settled)++;
            } else {
                *settled = 0;
                *last = folder ? Pidl(ILClone(folder.get())) : Pidl();
            }
            if (*settled >= 5) {
                sender.Stop();
                *holder = nullptr;
                TagResultsShow(now, name);
            }
        }));
        timer.Start();
    } catch (...) {
        LogHresult(L"opening a tag in a new tab");
    }
}

// The files (and folders) of a drag, as paths on disk.
std::vector<std::wstring> TagsItemsOf(IDataObject* data) {
    std::vector<std::wstring> paths;
    IShellItemArray* items = nullptr;
    if (!data || FAILED(SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(&items))) || !items) {
        return paths;
    }
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count && i < 500; i++) {
        IShellItem* item = nullptr;
        if (FAILED(items->GetItemAt(i, &item)) || !item) {
            continue;
        }
        wchar_t* text = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) && text) {
            paths.push_back(text);
            CoTaskMemFree(text);
        }
        item->Release();
    }
    items->Release();
    return paths;
}

// A drop on a tag row: the files get the tag, written on a mod thread.
void TagsDropLater(std::wstring const& tag, std::vector<std::wstring> paths) {
    StartModThread([tag, paths]() {
        bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        TagsAddToItems(paths, tag);
        if (com) {
            CoUninitialize();
        }
    });
}

// ---- The section ----

void SidebarFillTags(WindowState* state, SidebarPanel* panel) {
    SidebarPanel::Section* section = nullptr;
    for (auto& s : panel->sections) {
        if (s.key == L"tags") {
            section = &s;
        }
    }
    if (!section || !section->body) {
        return;
    }
    auto body = section->body;
    body.Children().Clear();
    panel->locationMarks.erase(std::remove_if(panel->locationMarks.begin(), panel->locationMarks.end(),
                                              [](SidebarPanel::Mark const& m) { return m.key.rfind(L"tag.", 0) == 0; }),
                               panel->locationMarks.end());
    panel->tagRows.clear();
    auto shown = TagsShown();
    panel->tagSig = TagsSignature(shown);
    for (auto const& tag : shown) {
        std::wstring key = L"tag." + tag.name;
        auto row = SidebarMakeRow(key, tag.name, false);
        row.iconHost.Children().Append(SidebarTagIcon(tag.colour, panel->dark));
        SidebarPanel::TagRow remembered;
        remembered.icon = row.iconHost;
        remembered.colour = tag.colour;
        remembered.name = tag.name;
        if (g_sidebarTagCounts) {
            // The number of files with the tag, muted at the right of the row (kept up to date by the tick).
            auto count = MakeText(L"", 12, false, true);
            count.HorizontalAlignment(mux::HorizontalAlignment::Right);
            count.VerticalAlignment(mux::VerticalAlignment::Center);
            count.Margin(mux::Thickness{0, 0, 12, 0});
            count.IsHitTestVisible(false);
            row.content.Children().Append(count);
            remembered.count = count;
            count.Text(std::to_wstring(TagIndexCount(tag.name)));
        }
        panel->tagRows.push_back(remembered);
        SidebarPanel::Mark mark;
        mark.key = key;
        mark.selected = row.selected;
        mark.pill = row.pill;
        mark.button = row.button;
        panel->locationMarks.push_back(std::move(mark));
        muxc::MenuFlyout flyout;
        TagsMenuItemsFor(state, flyout.Items(), tag, true);
        row.button.ContextFlyout(flyout);
        std::wstring tagName = tag.name;
        Keep(row.button, row.button.Click(winrt::auto_revoke, [state, tagName](Inspectable const&, mux::RoutedEventArgs const&) {
            if (state->sidebar && state->sidebar->suppressClick) {
                state->sidebar->suppressClick = false;  // The release of a drag that moved the row.
                return;
            }
            TagResultsShow(state, tagName);
        }));
        if (g_sidebarTagsSort == 1) {
            // "My order": a row can be dragged to another place.
            SidebarAttachReorder(
                state, row.button,
                [state](winrt::Windows::Foundation::Point point, size_t* index, float* line) {
                    if (state->sidebar) {
                        TagsInsertPoint(state->sidebar, point, index, line);
                    }
                },
                [state](float line) {
                    if (state->sidebar) {
                        TagsShowInsertLine(state->sidebar, line);
                    }
                },
                [state, tagName](size_t index) {
                    TagsMoveTo(tagName, index);
                    if (state->sidebar) {
                        SidebarFillTags(state, state->sidebar);
                    }
                });
        }
        body.Children().Append(row.button);
    }
    // "View all": the list of every tag, with how many files each has. Not shown when the rows already are every tag.
    if (TagListSnapshot().size() > shown.size()) {
        auto all = SidebarNewButton(L"SidebarRow.tags.viewall", 28, L"0");
        auto allText = MakeText(Str(L"sidebar.tags.viewall"), 12, false, true);
        allText.Margin(mux::Thickness{41, 0, 11, 0});
        allText.VerticalAlignment(mux::VerticalAlignment::Center);
        all.Content(allText);
        Keep(all, all.Click(winrt::auto_revoke, [state](Inspectable const&, mux::RoutedEventArgs const&) { TagListShow(state); }));
        body.Children().Append(all);
    }
    if (shown.empty()) {
        auto hint = MakeText(Str(L"sidebar.tags.empty"), 12, false, true);
        hint.TextWrapping(mux::TextWrapping::Wrap);
        hint.Margin(mux::Thickness{15, 4, 11, 4});
        body.Children().Append(hint);
    }
}

// The keys of the sidebar, for the row that has the keyboard (arrows and Enter are the controls' own, with the focus moves of XAML):
// Delete unpins a favorite, F2 renames a tag, Ctrl+Up and Ctrl+Down move a favorite (or a tag, in "My order") one place.
void SidebarKeyDown(WindowState* state, mux::Input::KeyRoutedEventArgs const& args) {
    using winrt::Windows::System::VirtualKey;
    SidebarPanel* panel = state->sidebar;
    if (!panel || !panel->host || panel->tagEditing || !state->queue) {
        return;
    }
    auto focused = mux::Input::FocusManager::GetFocusedElement(panel->host.XamlRoot()).try_as<muxc::Button>();
    if (!focused) {
        return;
    }
    std::wstring key;
    for (auto const& mark : panel->locationMarks) {
        if (mark.button && mark.button == focused) {
            key = mark.key;
        }
    }
    if (key.empty()) {
        return;
    }
    VirtualKey pressed = args.Key();
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool move = ctrl && (pressed == VirtualKey::Up || pressed == VirtualKey::Down);
    bool up = pressed == VirtualKey::Up;
    // After the rows are made again the row that moved has the keyboard again.
    auto refill = [state, key](bool favorites) {
        state->queue.TryEnqueue([state, key, favorites]() {
            SidebarPanel* now = state->sidebar;
            if (!now || g_unloading) {
                return;
            }
            if (favorites) {
                SidebarFillFavorites(state, now);
            } else {
                SidebarFillTags(state, now);
            }
            for (auto const& mark : now->locationMarks) {
                if (mark.key == key && mark.button) {
                    mark.button.Focus(mux::FocusState::Keyboard);
                }
            }
        });
    };
    if (key.rfind(L"fav.", 0) == 0) {
        std::wstring path = key.substr(4);
        if (pressed == VirtualKey::Delete) {
            FavoritesPinLater({path}, false);
            args.Handled(true);
        } else if (move) {
            size_t at = 0;
            for (size_t i = 0; i < panel->favorites.size(); i++) {
                if (_wcsicmp(panel->favorites[i].path.c_str(), path.c_str()) == 0) {
                    at = i;
                }
            }
            FavoritesPlace(panel, {path}, up ? (at > 0 ? at - 1 : 0) : at + 2);
            refill(true);
            args.Handled(true);
        }
    } else if (key.rfind(L"tag.", 0) == 0) {
        std::wstring name = key.substr(4);
        if (pressed == VirtualKey::F2) {
            TagsRename(state, name);
            args.Handled(true);
        } else if (move && g_sidebarTagsSort == 1) {
            size_t at = 0;
            for (size_t i = 0; i < panel->tagRows.size(); i++) {
                if (TagNamesEqual(panel->tagRows[i].name, name)) {
                    at = i;
                }
            }
            TagsMoveTo(name, up ? (at > 0 ? at - 1 : 0) : at + 2);
            refill(false);
            args.Handled(true);
        }
    }
}

// Every few ticks: the rows when the tags changed, and the progress of a running change.
void TagsTick(WindowState* state, SidebarPanel* panel) {
#ifdef EXTRAS_VERBOSE
    // Debug build: a file named open-tag-new-tab.txt in %LOCALAPPDATA%\ExplorerExtras, holding a tag's name, opens that tag in a new
    // tab of the window that is in front (a way to try "Open in new tab" without the pointer).
    if (panel->ticks % 5 == 0) {
        wchar_t local[MAX_PATH];
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        HWND front = GetForegroundWindow();
        if (length && length < MAX_PATH && front && GetAncestor(front, GA_ROOT) == state->topLevel) {
            std::wstring flag = std::wstring(local) + L"\\ExplorerExtras\\open-tag-new-tab.txt";
            HANDLE file = CreateFileW(flag.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                char text[256] = {};
                DWORD read = 0;
                ReadFile(file, text, sizeof(text) - 1, &read, nullptr);
                CloseHandle(file);
                DeleteFileW(flag.c_str());
                std::wstring name(read + 1, L'\0');
                name.resize((size_t)MultiByteToWideChar(CP_UTF8, 0, text, (int)read, name.data(), (int)name.size()));
                while (!name.empty() && (name.back() == L'\r' || name.back() == L'\n' || name.back() == L' ')) {
                    name.pop_back();
                }
                Wh_Log(L"TAGS debug: open \"%s\" in a new tab", name.c_str());
                TagsOpenInNewTab(state, name);
            }
        }
    }
#endif
    if (!panel->tagEditing && TagsSignature(TagsShown()) != panel->tagSig) {
        SidebarFillTags(state, panel);
    }
    if (g_sidebarTagCounts && panel->ticks % 50 == 0) {
        // About every three seconds: the counts of the rows (a tag was added to files, a file went).
        for (auto const& row : panel->tagRows) {
            if (auto count = row.count.get()) {
                std::wstring text = std::to_wstring(TagIndexCount(row.name));
                if (std::wstring(count.Text()) != text) {
                    count.Text(text);
                }
            }
        }
    }
    if (!panel->tagProgress) {
        return;
    }
    auto job = panel->tagJob;
    if (!job) {
        panel->tagProgress.Visibility(mux::Visibility::Collapsed);
        return;
    }
    if (job->finished) {
        panel->tagJob.reset();
        panel->tagProgress.Visibility(mux::Visibility::Collapsed);
        if (!panel->tagEditing) {
            SidebarFillTags(state, panel);
        }
        return;
    }
    panel->tagProgress.Visibility(mux::Visibility::Visible);
    size_t total = job->total;
    if (panel->tagBar) {
        panel->tagBar.IsIndeterminate(total == 0);
        if (total) {
            panel->tagBar.Value(100.0 * (double)job->done / (double)total);
        }
    }
}

// The "..." button of the header, with its menu (the header row of 10j shows it on hover).
muxc::Button SidebarTagsMore(WindowState* state, SidebarPanel*) {
    auto more = SidebarNewButtonWith(L"SidebarMenu.tags", 26, L"0", kSidebarSmallTemplate);
    more.Width(26);
    more.Content(SidebarGlyph(L"\uE712", 14));
    mux::Automation::AutomationProperties::SetName(more, Str(L"sidebar.tags.manage"));
    muxc::MenuFlyout menu;
    more.Flyout(menu);
    winrt::weak_ref<muxc::Button> weak = more;
    winrt::weak_ref<muxc::MenuFlyout> weakMenu = menu;
    Keep(menu, menu.Opening(winrt::auto_revoke, [state, weakMenu, weak](Inspectable const&, Inspectable const&) {
        auto shownMenu = weakMenu.get();
        auto anchor = weak.get();
        if (shownMenu && anchor) {
            TagsFillManageMenu(state, shownMenu, anchor);
        }
    }));
    return more;
}

// The progress line under the header: a thin bar and a button that stops the change. Shown while a change runs.
muxc::Grid SidebarTagsProgress(WindowState* state, SidebarPanel* panel) {
    muxc::Grid progress;
    progress.Margin(mux::Thickness{15, 2, 11, 6});
    progress.Visibility(mux::Visibility::Collapsed);
    progress.ColumnDefinitions().Append(muxc::ColumnDefinition());
    muxc::ColumnDefinition stopColumn;
    stopColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
    progress.ColumnDefinitions().Append(stopColumn);
    muxc::ProgressBar bar;
    bar.Maximum(100);
    bar.VerticalAlignment(mux::VerticalAlignment::Center);
    progress.Children().Append(bar);
    auto stop = SidebarNewButtonWith(L"SidebarTagsStop", 24, L"0", kSidebarSmallTemplate);
    stop.Width(24);
    stop.Margin(mux::Thickness{6, 0, 0, 0});
    stop.Content(SidebarGlyph(L"\uE711", 11));
    mux::Automation::AutomationProperties::SetName(stop, Str(L"sidebar.tags.stop"));
    muxc::Grid::SetColumn(stop, 1);
    Keep(stop, stop.Click(winrt::auto_revoke, [state](Inspectable const&, mux::RoutedEventArgs const&) {
        if (state->sidebar && state->sidebar->tagJob) {
            state->sidebar->tagJob->cancel = true;
        }
    }));
    progress.Children().Append(stop);
    panel->tagProgress = progress;
    panel->tagBar = bar;
    return progress;
}

void TagsReleaseThreadObjects() {
    t_tagsFlyout = nullptr;
    g_renaming = false;
}
