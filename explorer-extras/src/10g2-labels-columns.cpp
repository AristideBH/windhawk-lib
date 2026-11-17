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
    if (!g_labels || entry >= column->labelHosts.size() || entry >= column->entries.size()) {
        return;
    }
    auto host = column->labelHosts[entry];
    if (!host) {
        return;
    }
    host.Children().Clear();
    host.Width(std::numeric_limits<double>::quiet_NaN());
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
    if (!state || !g_labels) {
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
muxc::MenuFlyout LabelsTagsSubmenu(std::vector<std::wstring> const& pathList, muxc::ListView const& list) {
    muxc::MenuFlyout menu;
    auto paths = std::make_shared<std::vector<std::wstring>>(pathList);
    std::vector<std::vector<LabelTag>> perItem;
    bool unreadable = false;
    for (auto const& path : *paths) {
        std::vector<LabelTag> tags;
        if (!LabelsReadFile(path, &tags)) {
            unreadable = true;
            tags.clear();
        }
        perItem.push_back(std::move(tags));
    }
    bool dark = LabelsDarkTheme();
    for (int colour = 0; colour < 8; colour++) {
        LabelTag tag{(uint8_t)colour, L""};
        bool everywhere = !perItem.empty();
        for (auto const& item : perItem) {
            bool has = false;
            for (auto const& existing : item) {
                has = has || (existing.colour == colour && existing.name.empty());
            }
            everywhere = everywhere && has;
        }
        muxc::ToggleMenuFlyoutItem item;
        std::wstring name = LabelsColourName(colour);
        item.Text(name.empty() ? std::wstring(L"…") : name);
        item.IsChecked(everywhere);
        item.IsEnabled(!unreadable);
        muxc::FontIcon dot;
        dot.FontFamily(mux::Media::FontFamily(L"Segoe UI"));
        dot.Glyph(L"●");
        COLORREF fill = LabelsDotRgb(colour, dark);
        dot.Foreground(mux::Media::SolidColorBrush(
            winrt::Windows::UI::ColorHelper::FromArgb(255, GetRValue(fill), GetGValue(fill), GetBValue(fill))));
        item.Icon(dot);
        Keep(item, item.Click(winrt::auto_revoke, [paths, tag, everywhere](Inspectable const&, mux::RoutedEventArgs const&) {
            if (!g_unloading) {
                LabelsApplyToSelection(*paths, tag, !everywhere);
            }
        }));
        menu.Items().Append(item);
    }
    menu.Items().Append(muxc::MenuFlyoutSeparator());
    muxc::MenuFlyoutItem add;
    add.Text(Str(L"labels.addtag"));
    add.IsEnabled(!unreadable);
    Keep(add, add.Click(winrt::auto_revoke, [paths, list](Inspectable const&, mux::RoutedEventArgs const&) {
        if (g_unloading) {
            return;
        }
        winrt::weak_ref<muxc::StackPanel> none;
        t_labelsMenuFlyout = LabelsMakeFlyout(none, paths);
        Keep(t_labelsMenuFlyout, t_labelsMenuFlyout.Closed(winrt::auto_revoke, [](Inspectable const&, Inspectable const&) {
            t_labelsMenuFlyout = nullptr;
        }));
        t_labelsMenuFlyout.ShowAt(list);
    }));
    menu.Items().Append(add);
    muxc::MenuFlyoutItem clear;
    clear.Text(Str(L"labels.clear"));
    clear.IsEnabled(!unreadable);
    Keep(clear, clear.Click(winrt::auto_revoke, [paths](Inspectable const&, mux::RoutedEventArgs const&) {
        if (!g_unloading) {
            LabelsClearSelection(*paths);
        }
    }));
    menu.Items().Append(clear);
    return menu;
}
