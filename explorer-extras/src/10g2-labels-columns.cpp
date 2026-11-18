// Labels in the Columns view: the dots at the end of a row, and the "Tags" submenu of the Columns context menu.
// The tags come from the same folder cache as Explorer's own list (04b), looked up by the row's displayed name.

namespace {

// The dot of a Columns row as XAML: the colour, and a thin ring in the page colour, so that stacked dots are cut free
// of each other.
mux::FrameworkElement LabelsRowDot(int colour, int side, int left, bool dark) {
    COLORREF fill = LabelsDotRgb(colour, dark);
    wchar_t markup[500];
    swprintf_s(markup,
               L"<Ellipse xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Width='%d' Height='%d' "
               L"Fill='#FF%02X%02X%02X' Stroke='%s' StrokeThickness='1.5' HorizontalAlignment='Left' "
               L"VerticalAlignment='Center' Margin='%d,0,0,0'/>",
               side, side, GetRValue(fill), GetGValue(fill), GetBValue(fill), dark ? L"#FF191919" : L"#FFFFFFFF", left);
    return LoadXaml(markup).as<mux::FrameworkElement>();
}

std::wstring LabelsColumnPath(Column* column) {
    if (!column->labelPathKnown) {
        column->labelPath = column->folder ? PathOf(column->folder.get()) : std::wstring();
        column->labelPathKnown = true;
    }
    return column->labelPath;
}

}  // namespace

// Fills the dots of one row from the cache. Nothing is shown until the folder has been read.
void LabelsFillRowDots(Column* column, size_t entry) {
    if (entry >= column->labelHosts.size() || entry >= column->entries.size()) {
        return;
    }
    auto host = column->labelHosts[entry];
    if (!host) {
        return;
    }
    host.Children().Clear();
    host.Width(std::numeric_limits<double>::quiet_NaN());
    if (!g_labels) {
        return;  // Switched off: the dots go.
    }
    std::wstring path = LabelsColumnPath(column);
    if (path.empty()) {
        return;
    }
    auto data = LabelsFolderFor(path);
    if (!data->complete || !data->streamsSupported) {
        return;
    }
    std::wstring const& name = column->entries[entry].name;
    if (data->ambiguous.count(name)) {
        return;
    }
    auto found = data->byName.find(name);
    if (found == data->byName.end()) {
        return;
    }
    bool more = false;
    std::vector<int> colours = LabelsDotColours(found->second, &more);
    if (colours.empty()) {
        return;
    }
    const int side = 9;
    int step = LabelsDotStep(side);
    bool dark = LabelsDarkTheme();
    int count = (int)colours.size();
    for (int i = count - 1; i >= 0; i--) {
        host.Children().Append(LabelsRowDot(colours[i], side, i * step, dark));
    }
    int width = side + (count - 1) * step;
    if (more) {
        auto plus = MakeText(L"+", 11, false, true);
        plus.HorizontalAlignment(mux::HorizontalAlignment::Left);
        plus.VerticalAlignment(mux::VerticalAlignment::Center);
        plus.Margin(mux::Thickness{(double)width + 4, 0, 0, 0});
        host.Children().Append(plus);
        width += 4 + 8;
    }
    host.Width(width);
}

// Every built row of this window's columns gets its dots again (a folder was read, or tags changed).
void LabelsRefreshColumns() {
    if (g_unloading) {
        return;
    }
    WindowState* state = StateForThisThread(false);
    if (!state) {
        return;
    }
    try {
        int rows = 0, dots = 0;
        for (auto& column : state->columns) {
            if (!column->list) {
                continue;
            }
            for (size_t i = 0; i < column->entries.size() && i < column->built.size(); i++) {
                if (column->built[i]) {
                    LabelsFillRowDots(column.get(), i);
                    rows++;
                    dots += (i < column->labelHosts.size() && column->labelHosts[i] && column->labelHosts[i].Children().Size() > 0) ? 1 : 0;
                }
            }
        }
        LogVerbose(L"LABELS COLUMNS refreshed rows=%d withDots=%d", rows, dots);
    } catch (...) {
    }
}

namespace {

[[clang::no_destroy]] thread_local muxc::Flyout t_labelsMenuFlyout{nullptr};  // The "Add tag" flyout opened from the menu.

}  // namespace

// The "Tags" submenu of the Columns context menu: the colours as checkable items, "Add tag...", "Clear tags".
muxc::MenuFlyout LabelsTagsSubmenu(std::vector<std::wstring> const& pathList, std::function<mux::FrameworkElement()> anchor) {
    muxc::MenuFlyout menu;
    auto paths = std::make_shared<std::vector<std::wstring>>(pathList);
    // The items are there at once, disabled; the first 30 items are read on a mod thread (a big selection is not read when
    // the menu is built) and tell which colours are on all of them. Until then no click can act on a wrong state.
    auto everywhere = std::make_shared<std::array<bool, 8>>();
    everywhere->fill(false);
    std::vector<winrt::weak_ref<muxc::ToggleMenuFlyoutItem>> toggles;
    bool dark = LabelsDarkTheme();
    for (int colour = 0; colour < 8; colour++) {
        LabelTag tag{(uint8_t)colour, L""};
        muxc::ToggleMenuFlyoutItem item;
        std::wstring name = LabelsColourName(colour);
        item.Text(name.empty() ? std::wstring(L"…") : name);
        item.IsEnabled(false);
        muxc::FontIcon dot;
        dot.FontFamily(mux::Media::FontFamily(L"Segoe UI"));
        dot.Glyph(L"●");
        COLORREF fill = LabelsDotRgb(colour, dark);
        dot.Foreground(mux::Media::SolidColorBrush(
            winrt::Windows::UI::ColorHelper::FromArgb(255, GetRValue(fill), GetGValue(fill), GetBValue(fill))));
        item.Icon(dot);
        Keep(item, item.Click(winrt::auto_revoke, [paths, tag, everywhere, colour, anchor](Inspectable const&, mux::RoutedEventArgs const&) {
            LabelsFromMenu(paths, tag, !(*everywhere)[colour], false, anchor);
        }));
        toggles.push_back(winrt::make_weak(item));
        menu.Items().Append(item);
    }
    menu.Items().Append(muxc::MenuFlyoutSeparator());
    muxc::MenuFlyoutItem add;
    add.Text(Str(L"labels.addtag"));
    add.IsEnabled(false);
    Keep(add, add.Click(winrt::auto_revoke, [paths, anchor](Inspectable const&, mux::RoutedEventArgs const&) {
        mux::FrameworkElement target = g_unloading ? mux::FrameworkElement{nullptr} : anchor();
        if (!target) {
            return;
        }
        winrt::weak_ref<muxc::StackPanel> none;
        t_labelsMenuFlyout = LabelsMakeFlyout(none, paths);
        Keep(t_labelsMenuFlyout, t_labelsMenuFlyout.Closed(winrt::auto_revoke, [](Inspectable const&, Inspectable const&) {
            t_labelsMenuFlyout = nullptr;
        }));
        t_labelsMenuFlyout.ShowAt(target);
    }));
    menu.Items().Append(add);
    muxc::MenuFlyoutItem clear;
    clear.Text(Str(L"labels.clear"));
    clear.IsEnabled(false);
    Keep(clear, clear.Click(winrt::auto_revoke, [paths, anchor](Inspectable const&, mux::RoutedEventArgs const&) {
        LabelsFromMenu(paths, LabelTag{}, false, true, anchor);
    }));
    menu.Items().Append(clear);
    struct Read {
        std::vector<std::vector<LabelTag>> perItem;
        bool unreadable = false;
    };
    auto weakAdd = winrt::make_weak(add);
    auto weakClear = winrt::make_weak(clear);
    LabelsReadAsync<Read>(
        [paths]() {
            Read out;
            for (size_t i = 0; i < paths->size() && i < 30; i++) {
                std::vector<LabelTag> tags;
                if (!LabelsReadFile((*paths)[i], &tags)) {
                    out.unreadable = true;
                    tags.clear();
                }
                out.perItem.push_back(std::move(tags));
            }
            return out;
        },
        [toggles, everywhere, weakAdd, weakClear](Read const& read) {
            for (int colour = 0; colour < 8; colour++) {
                bool all = !read.perItem.empty();
                for (auto const& item : read.perItem) {
                    bool has = false;
                    for (auto const& existing : item) {
                        has = has || (existing.colour == colour && existing.name.empty());
                    }
                    all = all && has;
                }
                (*everywhere)[colour] = all;
                if (auto item = toggles[colour].get()) {
                    item.IsChecked(all);
                    item.IsEnabled(!read.unreadable);
                }
            }
            if (auto add = weakAdd.get()) {
                add.IsEnabled(!read.unreadable);
            }
            if (auto clear = weakClear.get()) {
                clear.IsEnabled(!read.unreadable);
            }
        });
    return menu;
}

// Called on each UI thread that holds label objects, at unload: its flyouts, its notice and its fonts.
void LabelsRemoveMenuEntries();  // 10h

void LabelsReleaseThreadObjects() {
    LabelsRemoveMenuEntries();
    t_labelsMenuFlyout = nullptr;
    t_labelsNotice = nullptr;
    t_labelsSection = LabelsSectionRef();
    LabelsReleaseFonts();
}
