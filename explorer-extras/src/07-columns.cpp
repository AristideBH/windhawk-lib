void LogRowTree(mux::DependencyObject const& node, int depth);
void RestoreViewButtonIcon(WindowState* state);
void HoverOn(WindowState* state);
void HoverOff(WindowState* state);
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
                column->list.RightTapped(column->rightTapToken);
                column->list.Tapped(column->tapToken);
                column->list.PointerMoved(column->moveToken);
                column->list.PointerExited(column->exitToken);
                column->list.PointerPressed(column->pressToken);
                if (column->stopScrollHandler) {
                    column->list.RemoveHandler(mux::UIElement::PointerPressedEvent(),
                                               winrt::box_value(column->stopScrollHandler));
                    column->stopScrollHandler = nullptr;
                }
                column->list.PointerReleased(column->releaseToken);
                column->list.PointerCaptureLost(column->captureLostToken);
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
            if (column->cancel) {
                *column->cancel = true;
            }
            if (column->viewer) {
                column->viewer.ViewChanged(column->viewToken);
                column->viewer = nullptr;
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
// Moves the pill to the active column. A change of column springs: the pill
// stretches while it travels and overshoots a little, like the indicator of a
// WinUI NavigationView. A column resized or scrolled sideways only repositions
// it, with no animation.
void UpdatePill(WindowState* state, bool animate) {
    // During a rebuild the new first column is hidden and not yet fitted:
    // leave the pill where it is until the swap, which moves it once.
    if (!state->pill || !state->strip || state->swapOld) {
        return;
    }
    try {
        int index = state->active;
        if (index < 0 || index >= (int)state->columns.size() ||
            !state->columns[index]->list) {
            state->pill.Opacity(0);
            return;
        }
        auto list = state->columns[index]->list;
        double left = state->columns[index]->cell.TransformToVisual(state->strip)
                          .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                          .X;
        double target = left + list.Width() / 2 - 8;
        ULONGLONG now = GetTickCount64();
        if (state->pill.Opacity() < 1) {
            state->pill.Opacity(1);
            animate = false;
        }
        if (std::abs(target - state->pillLeft) < 0.5) {
            return;
        }
        if (!animate || state->pillLeft < 0) {
            // While a move is animating, wait: the next tick tries again.
            // Remembering the target now would leave the pill where it was.
            if (now < state->pillUntil) {
                return;
            }
            muxc::Canvas::SetLeft(state->pill, target);
            state->pillLeft = target;
            return;
        }
        namespace anim = mux::Media::Animation;
        double from = state->pillLeft;
        if (now < state->pillUntil) {
            // A move is still going: the new one starts from where the pill is
            // drawn now, not from where the old one was heading.
            try {
                from = state->pill.TransformToVisual(state->overlay)
                           .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                           .X;
            } catch (...) {
            }
        }
        muxc::Canvas::SetLeft(state->pill, target);  // Where it stays.
        state->pillLeft = target;
        state->pillUntil = now + 430;

        // A short glide that overshoots only a little.
        anim::BackEase spring;
        spring.EasingMode(anim::EasingMode::EaseOut);
        spring.Amplitude(0.15);
        anim::DoubleAnimation travel;
        travel.From(from);
        travel.To(target);
        travel.Duration(mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(380)));
        travel.EasingFunction(spring);
        travel.EnableDependentAnimation(true);
        travel.FillBehavior(anim::FillBehavior::Stop);
        anim::Storyboard::SetTarget(travel, state->pill);
        anim::Storyboard::SetTargetProperty(travel, L"(Canvas.Left)");

        // The stretch: wider while it moves, back to 16 at the end.
        double distance = std::abs(target - from);
        anim::CubicEase ease;
        ease.EasingMode(anim::EasingMode::EaseOut);
        anim::DoubleAnimation stretch;
        stretch.From(16.0);
        stretch.To(16.0 + std::min(distance / 8, 10.0));
        stretch.Duration(mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(110)));
        stretch.AutoReverse(true);
        stretch.EasingFunction(ease);
        stretch.EnableDependentAnimation(true);
        stretch.FillBehavior(anim::FillBehavior::Stop);
        anim::Storyboard::SetTarget(stretch, state->pill);
        anim::Storyboard::SetTargetProperty(stretch, L"Width");

        anim::Storyboard board;
        board.Children().Append(travel);
        board.Children().Append(stretch);
        board.Begin();
    } catch (...) {
        LogHresult(L"UpdatePill");
    }
}

// Scrolls the strip, smoothly, so that column `index` is fully in view. If the
// column is wider than the view, its left edge wins: the icon and the start of
// the names must be visible.
void EnsureColumnVisible(WindowState* state, int index) {
    if (!state->scroller || !state->strip || index < 0 ||
        index >= (int)state->columns.size() || !state->columns[index]->cell) {
        return;
    }
    try {
        auto cell = state->columns[index]->cell;
        double left = cell.TransformToVisual(state->strip)
                          .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                          .X;
        double width = cell.ActualWidth();
        double offset = state->scroller.HorizontalOffset();
        double view = state->scroller.ViewportWidth();
        double target = offset;
        if (left + width > offset + view) {
            target = left + width - view;
        }
        if (left < target) {
            target = left;  // Also when the column is wider than the view.
        }
        if (target < 0) {
            target = 0;
        }
        if (target != offset) {
            state->scroller.ChangeView(target, nullptr, nullptr, false);
            state->scrollQuietUntil = GetTickCount64() + 600;
        }
    } catch (...) {
    }
}

// Scrolls the strip, smoothly, so that column `index` is in the middle of the
// view, as far as the ends of the strip allow.
void CenterColumn(WindowState* state, int index) {
    if (!state->scroller || !state->strip || index < 0 ||
        index >= (int)state->columns.size() || !state->columns[index]->cell) {
        return;
    }
    try {
        auto cell = state->columns[index]->cell;
        double left = cell.TransformToVisual(state->strip)
                          .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                          .X;
        double width = cell.ActualWidth();
        double view = state->scroller.ViewportWidth();
        double target = left + width / 2 - view / 2;
        double most = state->scroller.ScrollableWidth();
        target = std::max(0.0, std::min(target, most));
        if (std::abs(target - state->scroller.HorizontalOffset()) > 1) {
            state->scroller.ChangeView(target, nullptr, nullptr, false);
            state->scrollQuietUntil = GetTickCount64() + 700;
        }
    } catch (...) {
    }
}

void SetActiveColumn(WindowState* state, int index, bool ensureVisible = true) {
    state->active = index;
    if (ensureVisible) {
        EnsureColumnVisible(state, index);
    }
    // The active column is shown by the pill, not by its background. The
    // transparent brush keeps the empty space clickable.
    for (size_t i = 0; i < state->columns.size(); i++) {
        try {
            state->columns[i]->list.Background(Brush(0, 0, 0, 0));
        } catch (...) {
        }
    }
    UpdatePill(state, true);
}

double MeasureText(const std::wstring& text) {
    muxc::TextBlock block;
    block.Text(text);
    block.Measure(winrt::Windows::Foundation::Size{10000, 10000});
    return block.DesiredSize().Width;
}

// Space a row uses besides the name: icon, chevron, padding and the list's
// own margins.
const double kRowChrome = 24 + 16 + 18 + 22 + 10;  // The last 10 is the gutter.

// The width a column's longest names ask for, inside the limits.
double FittedWidth(Column* column) {
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
    return width;
}

// Sets the column's width from its longest names, unless the user sized it.
void FitWidth(Column* column) {
    if (column->userSized) {
        return;
    }
    column->list.Width(FittedWidth(column));
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

// Compact scrollbars: every scrollbar inside `root` is drawn half as wide (or
// as high), whatever state it is in. The transform moves the hit area with the
// drawing, so a compact bar is still clicked and dragged where it is seen.
int StyleScrollBars(mux::DependencyObject const& root, bool compact, int depth = 0) {
    if (!root || depth > 10) {
        return 0;
    }
    if (auto bar = root.try_as<muxc::Primitives::ScrollBar>()) {
        bool vertical = bar.Orientation() == muxc::Orientation::Vertical;
        // The little arrow buttons at the ends would be squeezed by the
        // transform. A compact bar has none: the thumb and the track stay.
        std::function<void(mux::DependencyObject const&, int)> arrows =
            [&](mux::DependencyObject const& node, int level) {
                if (!node || level > 12) {
                    return;
                }
                if (auto element = node.try_as<mux::FrameworkElement>()) {
                    std::wstring name = element.Name().c_str();
                    if (name.find(L"SmallDecrease") != std::wstring::npos ||
                        name.find(L"SmallIncrease") != std::wstring::npos) {
                        if (compact) {
                            element.Width(0);
                            element.Height(0);
                            element.MinWidth(0);
                            element.MinHeight(0);
                            element.Opacity(0);
                            element.IsHitTestVisible(false);
                        } else {
                            element.ClearValue(mux::FrameworkElement::WidthProperty());
                            element.ClearValue(mux::FrameworkElement::HeightProperty());
                            element.ClearValue(mux::FrameworkElement::MinWidthProperty());
                            element.ClearValue(mux::FrameworkElement::MinHeightProperty());
                            element.Opacity(1);
                            element.IsHitTestVisible(true);
                        }
                    }
                }
                int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
                for (int i = 0; i < count; i++) {
                    arrows(mux::Media::VisualTreeHelper::GetChild(node, i), level + 1);
                }
            };
        arrows(bar, 0);
        // A narrower bar, not a squeezed one: the thumb keeps its round corners.
        if (compact) {
            bar.RenderTransform(nullptr);
            if (vertical) {
                bar.Width(7);
                bar.HorizontalAlignment(mux::HorizontalAlignment::Right);
                // A negative left margin takes the bar's width out of the layout,
                // so it floats over the right edge instead of narrowing the rows.
                bar.Margin(mux::Thickness{-7, 0, 0, 0});
            } else {
                bar.Height(7);
                bar.VerticalAlignment(mux::VerticalAlignment::Bottom);
            }
        } else {
            bar.RenderTransform(nullptr);
            bar.ClearValue(mux::FrameworkElement::WidthProperty());
            bar.ClearValue(mux::FrameworkElement::HeightProperty());
            bar.ClearValue(mux::FrameworkElement::MarginProperty());
            bar.HorizontalAlignment(vertical ? mux::HorizontalAlignment::Right
                                             : mux::HorizontalAlignment::Stretch);
            bar.VerticalAlignment(vertical ? mux::VerticalAlignment::Stretch
                                           : mux::VerticalAlignment::Bottom);
        }
        return 1;
    }
    int found = 0;
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; i++) {
        found += StyleScrollBars(mux::Media::VisualTreeHelper::GetChild(root, i), compact, depth + 1);
    }
    return found;
}

// The focus outline around the item under the keyboard cursor, shown or not
// as the setting says.
void ApplyFocusStyle(muxc::ListViewItem const& row) {
    bool show = g_focusRect;
    row.UseSystemFocusVisuals(show);
    row.FocusVisualPrimaryThickness(mux::Thickness{show ? 2.0 : 0.0, show ? 2.0 : 0.0,
                                                    show ? 2.0 : 0.0, show ? 2.0 : 0.0});
    row.FocusVisualSecondaryThickness(mux::Thickness{show ? 1.0 : 0.0, show ? 1.0 : 0.0,
                                                      show ? 1.0 : 0.0, show ? 1.0 : 0.0});
}

// A row without content: only the item, sized. Its content is built when the
// row comes near the screen, so a long folder opens at once.
muxc::ListViewItem MakeRowShell(double rowHeight) {
    muxc::ListViewItem row;
    row.MinHeight(rowHeight);
    row.Height(rowHeight);
    row.Padding(mux::Thickness{10, 0, 8, 0});
    ApplyFocusStyle(row);
    row.HorizontalContentAlignment(mux::HorizontalAlignment::Stretch);
    return row;
}

void ApplyIconBits(Column* column, IconBits const& icon);

// Builds the content of one row.
void BuildRowContent(Column* column, size_t entryIndex, muxc::ListViewItem const& row) {
    const Entry& entry = column->entries[entryIndex];
    size_t capacity = NameCapacity(column);
    muxc::Grid grid;
    muxc::ColumnDefinition iconColumn, nameColumn, markColumn, chevronColumn;
    iconColumn.Width(mux::GridLength{24, mux::GridUnitType::Pixel});
    nameColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
    chevronColumn.Width(mux::GridLength{16, mux::GridUnitType::Pixel});
    grid.ColumnDefinitions().Append(iconColumn);
    markColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
    grid.ColumnDefinitions().Append(nameColumn);
    grid.ColumnDefinitions().Append(markColumn);
    grid.ColumnDefinitions().Append(chevronColumn);

    // Stand-in glyphs until real file icons are drawn.
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(entry.launcher ? L"\uE713" : entry.folder ? L"" : L"");
    icon.FontSize(14);
    icon.HorizontalAlignment(mux::HorizontalAlignment::Left);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid iconHost;
    iconHost.Children().Append(icon);
    muxc::Grid::SetColumn(iconHost, 0);
    grid.Children().Append(iconHost);
    column->iconHosts[entryIndex] = iconHost;  // The worker swaps in the real icon later.

    muxc::TextBlock name;
    std::wstring shown = MiddleEllipsis(entry.name, capacity);
    name.Text(shown);
    name.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    name.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid::SetColumn(name, 1);
    grid.Children().Append(name);
    column->texts[entryIndex] = name;

    if (entry.remote) {
        muxc::FontIcon mark;
        mark.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        // Cloud, network, device, always kept (green check), downloaded (check).
        mark.Glyph(entry.remote == 1   ? L"\uE753"
                   : entry.remote == 2 ? L"\uE839"
                   : entry.remote == 3 ? L"\uE8EA"
                   : entry.remote == 6 ? L"\uE895"
                                       : L"\uE73E");
        if (entry.remote == 6) {  // Syncing now: the blue of OneDrive.
            mark.Foreground(mux::Media::SolidColorBrush(
                winrt::Windows::UI::ColorHelper::FromArgb(255, 0, 120, 212)));
        }
        if (entry.remote == 4) {
            mark.Foreground(mux::Media::SolidColorBrush(
                winrt::Windows::UI::ColorHelper::FromArgb(255, 16, 124, 16)));
            mark.Opacity(1.0);
        }
        mark.FontSize(11);
        mark.Opacity(0.6);
        mark.Margin(mux::Thickness{6, 0, 4, 0});
        mark.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(mark, 2);
        grid.Children().Append(mark);
    }
    if (entry.folder) {
        muxc::FontIcon chevron;
        chevron.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
        chevron.Glyph(L"");
        chevron.FontSize(10);
        chevron.Opacity(0.6);
        chevron.HorizontalAlignment(mux::HorizontalAlignment::Right);
        chevron.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(chevron, 3);
        grid.Children().Append(chevron);
        column->chevrons[entryIndex] = chevron;
    }

    row.Content(grid);
    // The full name, for names that were cut. The detail tooltip replaces it
    // the first time the pointer is on the row.
    muxc::ToolTipService::SetToolTip(row, winrt::box_value(winrt::hstring{entry.name}));
    column->built[entryIndex] = 1;
    // Beyond the first batch the worker reads icons for the rows that get built.
    if (entryIndex >= kMaxIcons && entryIndex < column->iconAsked.size() &&
        !column->iconAsked[entryIndex]) {
        column->iconAsked[entryIndex] = 1;
        column->iconWanted.push_back(entryIndex);
    }
    auto pending = column->pendingIcons.find(entryIndex);
    if (pending != column->pendingIcons.end()) {
        ApplyIconBits(column, *pending->second);
        column->pendingIcons.erase(pending);
    }
}

// Builds the rows between two row numbers that don't have their content yet.
void BuildRowRange(Column* column, int firstRow, int lastRow) {
    auto items = column->list.Items();
    int count = (int)std::min<size_t>(items.Size(), column->rowToEntry.size());
    firstRow = std::max(0, firstRow);
    lastRow = std::min(count - 1, lastRow);
    int made = 0;
    for (int r = firstRow; r <= lastRow && made < 200; r++) {
        int entry = column->rowToEntry[r];
        if (entry < 0 || (size_t)entry >= column->built.size() || column->built[entry]) {
            continue;
        }
        if (auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
            BuildRowContent(column, (size_t)entry, item);
            made++;
        }
    }
}

// The rows on screen, and a few more each side, get their content.
void BuildVisibleRows(Column* column) {
    auto panel = column->list.ItemsPanelRoot().try_as<muxc::ItemsStackPanel>();
    if (!panel) {
        return;
    }
    int first = panel.FirstCacheIndex(), last = panel.LastCacheIndex();
    if (first < 0 || last < 0) {
        return;
    }
    BuildRowRange(column, first - 10, last + 10);
}

void EnsureRowBuilt(Column* column, size_t entryIndex) {
    if (entryIndex < column->entryToRow.size() && column->entryToRow[entryIndex] >= 0) {
        int row = column->entryToRow[entryIndex];
        BuildRowRange(column, row, row);
    }
}

// The chevron of a group header: down when open, right when collapsed.
// One right-pointing chevron, turned to point down when the group is open.
const wchar_t* HeaderGlyph() {
    return L"\uE76C";
}

muxc::ListViewItem MakeHeaderRow(const std::wstring& title, double rowHeight,
                                 bool collapsed, muxc::FontIcon* iconOut) {
    muxc::TextBlock text;
    text.Text(title);
    text.FontSize(12);
    text.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    text.Opacity(0.75);
    text.VerticalAlignment(mux::VerticalAlignment::Bottom);
    muxc::FontIcon chevron;
    chevron.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    chevron.Glyph(HeaderGlyph());
    chevron.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
    mux::Media::RotateTransform turn;
    turn.Angle(collapsed ? 0.0 : 90.0);
    chevron.RenderTransform(turn);
    chevron.FontSize(9);
    chevron.Opacity(0.7);
    chevron.Margin(mux::Thickness{0, 0, 6, 2});
    chevron.VerticalAlignment(mux::VerticalAlignment::Bottom);
    *iconOut = chevron;
    muxc::StackPanel line;
    line.Orientation(muxc::Orientation::Horizontal);
    line.Children().Append(chevron);
    line.Children().Append(text);
    muxc::ListViewItem row;
    row.Content(line);
    row.MinHeight(rowHeight);
    row.Height(rowHeight + 4);
    row.Padding(mux::Thickness{10, 0, 8, 2});
    row.IsHitTestVisible(false);
    row.IsTabStop(false);
    row.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
    return row;
}

// Fills a column's list from freshly loaded data.
void CancelRename(WindowState* state);
void BeginRename(WindowState* state, Column* column, size_t entryIndex);
muxc::ScrollViewer FindListScroller(mux::DependencyObject const& node, int depth = 0);
void OnRowSelected(muxc::ListView const& list);

// Explorer's own in-place rename box: a visible Edit control inside its file
// view.
BOOL CALLBACK FindEditProc(HWND hwnd, LPARAM lParam) {
    wchar_t name[32];
    if (GetClassNameW(hwnd, name, ARRAYSIZE(name)) && wcscmp(name, L"Edit") == 0 &&
        IsWindowVisible(hwnd)) {
        for (HWND up = GetParent(hwnd); up; up = GetParent(up)) {
            if (GetClassNameW(up, name, ARRAYSIZE(name)) &&
                wcscmp(name, L"SHELLDLL_DefView") == 0) {
                *(HWND*)lParam = hwnd;
                return FALSE;
            }
        }
    }
    return TRUE;
}

HWND FindNativeRenameBox(HWND topLevel) {
    HWND found = nullptr;
    EnumChildWindows(topLevel, FindEditProc, (LPARAM)&found);
    return found;
}

// The rows that are cut follow the clipboard: a Cut from anywhere shows the
// items dimmed, as long as they are all in one folder.
void ApplyCutState(WindowState* state);
void SyncCutFromClipboard(WindowState* state) {
    state->cutFolder.reset();
    state->cutFolderPath.clear();
    state->cutPaths.clear();
    IDataObject* data = nullptr;
    if (SUCCEEDED(OleGetClipboard(&data)) && data) {
        static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        bool move = false;
        FORMATETC wanted = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1,
                            TYMED_HGLOBAL};
        STGMEDIUM medium = {};
        if (data->GetData(&wanted, &medium) == S_OK) {
            if (medium.hGlobal) {
                if (DWORD* value = (DWORD*)GlobalLock(medium.hGlobal)) {
                    move = (*value & DROPEFFECT_MOVE) != 0;
                    GlobalUnlock(medium.hGlobal);
                }
            }
            ReleaseStgMedium(&medium);
        }
        FORMATETC files = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        if (move && data->GetData(&files, &medium) == S_OK) {
            HDROP drop = (HDROP)medium.hGlobal;
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::wstring parentPath;
            for (UINT i = 0; i < count && i < 500; i++) {
                wchar_t path[MAX_PATH] = L"";
                if (!DragQueryFileW(drop, i, path, MAX_PATH)) {
                    continue;
                }
                wchar_t folder[MAX_PATH];
                lstrcpynW(folder, path, MAX_PATH);
                PathRemoveFileSpecW(folder);
                if (parentPath.empty()) {
                    parentPath = folder;
                    state->cutFolderPath = folder;
                }
                if (_wcsicmp(parentPath.c_str(), folder) == 0) {
                    state->cutPaths.push_back(path);
                }
            }
            Out(L"cut from the clipboard: %d item(s) in \"%s\"",
                (int)state->cutPaths.size(), parentPath.c_str());
            ReleaseStgMedium(&medium);
        }
        data->Release();
    }
    ApplyCutState(state);
}

// Dims the rows that were cut, in every column that shows their folder.
void ApplyCutState(WindowState* state) {
    for (auto& column : state->columns) {
        bool same = false;
        if (!state->cutFolderPath.empty() && !state->cutPaths.empty()) {
            wchar_t shown[MAX_PATH] = L"";
            same = SHGetPathFromIDListW(column->folder.get(), shown) &&
                   _wcsicmp(shown, state->cutFolderPath.c_str()) == 0;
        }
        IShellFolder* folder = nullptr;
        if (same) {
            HRESULT hr = ILIsEmpty(column->folder.get())
                             ? SHGetDesktopFolder(&folder)
                             : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                              IID_PPV_ARGS(&folder));
            if (FAILED(hr)) {
                folder = nullptr;
            }
        }
        auto items = column->list.Items();
        for (size_t r = 0; r < column->rowToEntry.size() && r < items.Size();
             r++) {
            int entry = column->rowToEntry[r];
            if (entry < 0) {
                continue;
            }
            bool cut = false;
            if (folder) {
                std::wstring path = ParsingName(folder, column->entries[entry].child.get());
                for (auto& cutPath : state->cutPaths) {
                    if (_wcsicmp(cutPath.c_str(), path.c_str()) == 0) {
                        cut = true;
                        break;
                    }
                }
            }
            if (auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
                item.Opacity(cut ? 0.45 : 1.0);
            }
        }
        if (folder) {
            folder->Release();
        }
    }
}

// What follows a fill or a patch: the cut look, a rename or a selection that was waiting for
// the new rows, and a scroll position to put back.
void FinishFill(WindowState* state, Column* column) {
    column->filled = true;
    column->list.IsTabStop(column->entries.empty());
    if (!state->cutPaths.empty()) {
        ApplyCutState(state);
    }
    if (!state->renameName.empty() && state->renameColumn == column) {
        std::wstring wanted = state->renameName;
        state->renameName.clear();
        state->renameColumn = nullptr;
        for (size_t i = 0; i < column->entries.size(); i++) {
            if (column->entries[i].name == wanted && state->queue) {
                // After the column has been laid out.
                state->queue.TryEnqueue([column, i]() {
                    WindowState* state = StateForThisThread(false);
                    if (state && state->on && !g_unloading) {
                        try {
                            BeginRename(state, column, i);
                        } catch (...) {
                            LogHresult(L"rename of the new folder");
                        }
                    }
                });
                break;
            }
        }
    }
    if (!state->selectNames.empty() && state->selectNamesColumn == column) {
        // Select what was just extracted, and tell the rest (Explorer's view,
        // the columns) after the column has been laid out.
        int first = -1;
        bool wasBuilding = state->building;
        state->building = true;
        for (size_t i = 0; i < column->entries.size(); i++) {
            for (auto& wanted : state->selectNames) {
                if (_wcsicmp(column->entries[i].name.c_str(), wanted.c_str()) == 0 &&
                    i < column->entryToRow.size() && column->entryToRow[i] >= 0) {
                    int row = column->entryToRow[i];
                    if (first < 0) {
                        first = row;
                        column->list.SelectedIndex(row);
                    } else {
                        column->list.SelectRange(
                            winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(row, 1));
                    }
                    break;
                }
            }
        }
        state->building = wasBuilding;
        state->selectNames.clear();
        state->selectNamesColumn = nullptr;
        if (first >= 0 && state->queue) {
            state->queue.TryEnqueue([column, first]() {
                WindowState* state = StateForThisThread(false);
                if (!state || !state->on || g_unloading) {
                    return;
                }
                try {
                    bool known = false;
                    for (auto& c : state->columns) {
                        known = known || c.get() == column;
                    }
                    if (known) {
                        column->list.ScrollIntoView(column->list.Items().GetAt((uint32_t)first));
                        // Then centre the first extracted row in the column.
                        column->list.UpdateLayout();
                        auto scroller = FindListScroller(column->list, 0);
                        auto container = column->list.ContainerFromIndex(first)
                                             .try_as<mux::FrameworkElement>();
                        if (scroller && container) {
                            double top = container.TransformToVisual(scroller)
                                             .TransformPoint({0, 0}).Y;
                            double offset = scroller.VerticalOffset() + top -
                                            (scroller.ViewportHeight() - container.ActualHeight()) / 2;
                            scroller.ChangeView(nullptr, std::max(0.0, offset), nullptr, false);
                        }
                        OnRowSelected(column->list);
                    }
                } catch (...) {
                    LogHresult(L"selecting the extracted items");
                }
            });
        }
    }
    if (column->restoreScroll >= 0 && state->queue) {
        // A tab shown again: the column goes back to where it was scrolled.
        double wanted = column->restoreScroll;
        column->restoreScroll = -1;
        state->queue.TryEnqueue([column, wanted]() {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->on || g_unloading) {
                return;
            }
            try {
                bool known = false;
                for (auto& c : state->columns) {
                    known = known || c.get() == column;
                }
                if (!known) {
                    return;
                }
                column->list.UpdateLayout();
                auto scroller = FindListScroller(column->list, 0);
                if (scroller) {
                    scroller.ChangeView(nullptr, std::min(wanted, scroller.ScrollableHeight()),
                                        nullptr, true);
                }
            } catch (...) {
                LogHresult(L"restoring a column's scroll");
            }
        });
    }
}

void FillColumn(WindowState* state, Column* column, FolderResult&& data) {
    if (state->rename && state->rename->column == column) {
        CancelRename(state);
    }
    column->entries = std::move(data.entries);
    column->rowToEntry.clear();
    column->texts.clear();
    column->texts.resize(column->entries.size(), muxc::TextBlock{nullptr});
    column->iconHosts.clear();
    column->iconHosts.resize(column->entries.size(), muxc::Grid{nullptr});
    column->entryToRow.assign(column->entries.size(), -1);
    column->iconAsked.assign(column->entries.size(), 0);
    column->iconWanted.clear();
    column->built.assign(column->entries.size(), 0);
    column->pendingIcons.clear();
    column->chevrons.clear();
    column->chevrons.resize(column->entries.size(), muxc::FontIcon{nullptr});
    column->hintEntry = -1;
    column->focusHeader = -1;
    column->tipBuilt.assign(column->entries.size(), 0);
    column->animGroup = -1;
    column->animDoneAt = 0;
    column->rowGroup.clear();
    column->headerIcons.clear();
    column->groupTitles = data.groupNames;

    FitWidth(column);
    size_t capacity = NameCapacity(column);
    double rowHeight = IsCompactMode() ? 24 : 32;
    column->rowHeight = rowHeight;
    bool grouped = data.groupOf.size() == column->entries.size() &&
                   !data.groupNames.empty();

    auto items = column->list.Items();
    items.Clear();
    std::vector<winrt::Windows::Foundation::IInspectable> made;  // Set in one go at the end.
    made.reserve(column->entries.size() + 16);
    auto isCollapsed = [&](int group) {
        return grouped && group >= 0 && group < (int)data.groupNames.size() &&
               std::find(column->collapsed.begin(), column->collapsed.end(),
                         data.groupNames[group]) != column->collapsed.end();
    };
    // The selected item's group is opened: a selection can't be hidden.
    if (grouped && column->selectedChild) {
        for (size_t i = 0; i < column->entries.size(); i++) {
            if (ILIsEqual(column->entries[i].child.get(),
                          column->selectedChild.get()) &&
                isCollapsed(data.groupOf[i])) {
                auto at = std::find(column->collapsed.begin(),
                                    column->collapsed.end(),
                                    data.groupNames[data.groupOf[i]]);
                column->collapsed.erase(at);
                break;
            }
        }
    }
    int selectRow = -1;
    for (size_t i = 0; i < column->entries.size(); i++) {
        int group = grouped ? data.groupOf[i] : -1;
        if (grouped && (i == 0 || data.groupOf[i] != data.groupOf[i - 1])) {
            std::wstring title =
                group >= 0 && group < (int)data.groupNames.size()
                    ? data.groupNames[group]
                    : L"";
            muxc::FontIcon icon{nullptr};
            made.push_back(MakeHeaderRow(title, rowHeight, isCollapsed(group), &icon));
            column->rowToEntry.push_back(-1);
            column->rowGroup.push_back(group);
            column->headerIcons.push_back(icon);
        }
        if (column->selectedChild &&
            ILIsEqual(column->entries[i].child.get(),
                      column->selectedChild.get())) {
            selectRow = (int)column->rowToEntry.size();
        }
        auto row = MakeRowShell(rowHeight);
        if (isCollapsed(group)) {
            row.Visibility(mux::Visibility::Collapsed);
        }
        made.push_back(row);
        column->entryToRow[i] = (int)column->rowToEntry.size();
        column->rowToEntry.push_back((int)i);
        column->rowGroup.push_back(group);
        column->headerIcons.push_back(muxc::FontIcon{nullptr});
    }
    if (data.truncated) {
        muxc::ListViewItem more;
        size_t hidden = data.total > column->entries.size()
                            ? data.total - column->entries.size()
                            : 0;
        std::wstring text = hidden > 0
                                ? std::to_wstring(hidden) + (data.total >= (size_t)kReadCap ? L"+" : L"") +
                                      L" more items not shown"
                                : std::wstring(L"More items not shown");
        more.Content(winrt::box_value(winrt::hstring{text}));
        more.IsEnabled(false);
        made.push_back(more);
        column->rowToEntry.push_back(-1);
        column->rowGroup.push_back(-1);
        column->headerIcons.push_back(muxc::FontIcon{nullptr});
    }
    items.ReplaceAll(made);
    // The first rows, and the ones around the selection, have their content
    // now. The rest is built as the list scrolls.
    BuildRowRange(column, 0, 60);
    if (selectRow >= 0) {
        BuildRowRange(column, selectRow - 30, selectRow + 30);
        column->list.SelectedIndex(selectRow);
    }
    FinishFill(state, column);
    Out(L"column \"%s\": %d items, %d groups%s, width %d",
        PathOf(column->folder.get()).c_str(), (int)column->entries.size(),
        grouped ? (int)data.groupNames.size() : 0,
        data.truncated ? L", truncated" : L"", (int)column->list.Width());
}

// Runs on the window's UI thread when the worker has read a folder.
// Is a freshly read folder the same as what the column shows? Then the rows
// stay as they are: no flicker, no lost scroll position.
bool SameListing(const FolderResult& data, const Column& column) {
    if (!column.filled || data.entries.size() != column.entries.size()) {
        return false;
    }
    for (size_t i = 0; i < data.entries.size(); i++) {
        if (data.entries[i].name != column.entries[i].name ||
            data.entries[i].folder != column.entries[i].folder) {
            return false;
        }
        // The same item read again has the same bytes. ILIsEqual asks the shell,
        // which is slow for thousands of items on the UI thread.
        PCUITEMID_CHILD a = data.entries[i].child.get();
        PCUITEMID_CHILD b = column.entries[i].child.get();
        UINT sizeA = ILGetSize(a), sizeB = ILGetSize(b);
        if (!(sizeA == sizeB && memcmp(a, b, sizeA) == 0) && !ILIsEqual(a, b)) {
            return false;
        }
    }
    // Groups: the same header count and the same group of each entry.
    size_t headers = 0;
    for (int entry : column.rowToEntry) {
        headers += entry < 0 ? 1 : 0;
    }
    size_t expected = data.groupNames.empty() ? 0 : data.groupNames.size();
    bool more = !column.rowToEntry.empty() && column.rowToEntry.back() < 0 &&
                data.truncated;
    if (more) {
        headers -= 1;
    }
    if (headers != expected) {
        return false;
    }
    return data.truncated == more;
}


void ShapeSelectionRuns(Column* column, muxc::ListView const& list);
void UnshapeRows(Column* column);

// Brings a column's rows to a freshly read listing without building them all again: the rows
// of items that are still there stay as they are (with their icons, selection and place in the
// scroll), the rows of items that went are removed, and rows are made only for new items. A
// rename, a delete, a paste, a new folder or an extraction then touches a few rows. The
// listings are in the same order, so the items that stayed form a run in both; a longest such
// run is kept and the others (an item whose sort key changed too) go out and come back.
// False, with nothing changed, when the column is grouped or cut short, was or becomes empty,
// or when most of it changed: a full fill is cheaper then.
bool PatchColumn(WindowState* state, Column* column, FolderResult& data) {
    if (!column->filled || data.truncated || !data.groupNames.empty()) {
        return false;
    }
    auto items = column->list.Items();
    size_t oldCount = column->entries.size(), newCount = data.entries.size();
    if (oldCount == 0 || newCount == 0 || items.Size() != oldCount || column->rowToEntry.size() != oldCount ||
        column->built.size() != oldCount) {
        return false;
    }
    for (size_t i = 0; i < oldCount; i++) {
        if (column->rowToEntry[i] != (int)i) {
            return false;  // Group headers or a "more items" row are in the list.
        }
    }
    auto keyOf = [](Entry const& entry) {
        return std::string((const char*)entry.child.get(), ILGetSize(entry.child.get()));
    };
    std::map<std::string, size_t> oldIndex;
    for (size_t i = 0; i < oldCount; i++) {
        oldIndex.emplace(keyOf(column->entries[i]), i);
    }
    std::vector<int> match(newCount, -1);  // For each new entry, the old one it is, or -1.
    for (size_t j = 0; j < newCount; j++) {
        auto found = oldIndex.find(keyOf(data.entries[j]));
        if (found == oldIndex.end()) {
            continue;
        }
        Entry const& before = column->entries[found->second];
        Entry const& now = data.entries[j];
        if (before.name == now.name && before.folder == now.folder && before.archive == now.archive &&
            before.launcher == now.launcher && before.remote == now.remote) {
            match[j] = (int)found->second;
        }
    }
    // The longest run of items that keep their order.
    std::vector<int> tails, previous(newCount, -1);
    for (size_t j = 0; j < newCount; j++) {
        if (match[j] < 0) {
            continue;
        }
        size_t low = 0, high = tails.size();
        while (low < high) {
            size_t mid = (low + high) / 2;
            if (match[tails[mid]] < match[j]) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        previous[j] = low > 0 ? tails[low - 1] : -1;
        if (low == tails.size()) {
            tails.push_back((int)j);
        } else {
            tails[low] = (int)j;
        }
    }
    std::vector<char> keep(newCount, 0);
    size_t kept = 0;
    for (int j = tails.empty() ? -1 : tails.back(); j >= 0; j = previous[j]) {
        keep[j] = 1;
        kept++;
    }
    size_t removed = oldCount - kept, inserted = newCount - kept;
    if (removed + inserted > std::max<size_t>(40, oldCount / 2)) {
        return false;
    }
    if (state->rename && state->rename->column == column) {
        CancelRename(state);
    }
    UnshapeRows(column);

    std::vector<char> keepOld(oldCount, 0);
    std::vector<int> newOf(oldCount, -1);
    for (size_t j = 0; j < newCount; j++) {
        if (keep[j]) {
            keepOld[match[j]] = 1;
            newOf[match[j]] = (int)j;
        }
    }
    for (size_t i = oldCount; i-- > 0;) {
        if (!keepOld[i]) {
            items.RemoveAt((uint32_t)i);
        }
    }
    for (size_t j = 0; j < newCount; j++) {
        if (!keep[j]) {
            items.InsertAt((uint32_t)j, MakeRowShell(column->rowHeight));
        }
    }

    // The per-entry lists follow the new order.
    auto follow = [&](auto& list, auto blank) {
        std::decay_t<decltype(list)> out(newCount, blank);
        for (size_t j = 0; j < newCount; j++) {
            if (keep[j]) {
                out[j] = list[match[j]];
            }
        }
        list = std::move(out);
    };
    follow(column->texts, muxc::TextBlock{nullptr});
    follow(column->iconHosts, muxc::Grid{nullptr});
    follow(column->chevrons, muxc::FontIcon{nullptr});
    follow(column->built, (char)0);
    follow(column->iconAsked, (char)0);
    follow(column->tipBuilt, (char)0);
    column->entryToRow.assign(newCount, 0);
    column->rowToEntry.assign(newCount, 0);
    for (size_t j = 0; j < newCount; j++) {
        column->entryToRow[j] = (int)j;
        column->rowToEntry[j] = (int)j;
    }
    column->rowGroup.assign(newCount, -1);
    column->headerIcons.assign(newCount, muxc::FontIcon{nullptr});
    column->groupTitles.clear();
    {
        std::vector<size_t> wanted;
        for (size_t old : column->iconWanted) {
            if (old < oldCount && newOf[old] >= 0) {
                wanted.push_back((size_t)newOf[old]);
            }
        }
        column->iconWanted = std::move(wanted);
        std::map<size_t, std::shared_ptr<IconBits>> pending;
        for (auto& waiting : column->pendingIcons) {
            if (waiting.first < oldCount && newOf[waiting.first] >= 0) {
                pending.emplace((size_t)newOf[waiting.first], waiting.second);
            }
        }
        column->pendingIcons = std::move(pending);
        column->hintEntry = column->hintEntry >= 0 && (size_t)column->hintEntry < oldCount ? newOf[column->hintEntry] : -1;
    }
    column->entries = std::move(data.entries);

    FitWidth(column);
    for (size_t j = 0; j < newCount; j++) {
        if (!keep[j]) {
            BuildRowRange(column, (int)j, (int)j);  // The new rows have their content at once.
        }
    }
    FinishFill(state, column);
    ShapeSelectionRuns(column, column->list);
    Out(L"column \"%s\": patched, %d removed, %d new, %d kept", PathOf(column->folder.get()).c_str(), (int)removed,
        (int)inserted, (int)kept);
    return true;
}

void LiftStale(WindowState* state, int columnIndex);
void ScheduleNavigation(WindowState* state, PCIDLIST_ABSOLUTE folder, ULONGLONG delay = 350);
void SetActiveColumn(WindowState* state, int index, bool ensureVisible);

// A folder on disk that is no longer there: deleted, renamed or moved.
// Folders that aren't on disk (This PC, Control Panel) are never "gone" here.
bool FolderGone(PCIDLIST_ABSOLUTE folder) {
    wchar_t path[MAX_PATH] = L"";
    if (!folder || ILIsEmpty(folder) || !SHGetPathFromIDListW(folder, path) || !path[0]) {
        return false;
    }
    return !PathFileExistsW(path);
}

void ApplyLoaded(std::shared_ptr<Loaded> const& loaded) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    for (size_t position = 0; position < state->columns.size(); position++) {
        auto& column = state->columns[position];
        if (column->generation == loaded->generation) {
            // The column's folder is gone: the column and those after it are
            // removed, and the one before it shows its folder again, without
            // the row that led here.
            if (position > 0 && FolderGone(column->folder.get())) {
                Out(L"a column's folder is gone, its columns are removed");
                try {
                    bool wasBuilding = state->building;
                    state->building = true;
                    RemoveColumnsFrom(state, position);
                    Column* previous = state->columns[position - 1].get();
                    previous->selectedChild.reset();
                    previous->list.SelectedItems().Clear();
                    state->building = wasBuilding;
                    LiftStale(state, (int)position - 1);
                    SetActiveColumn(state, (int)position - 1);
                    ScheduleNavigation(state, previous->folder.get());
                    RequestLoad(state, previous);
                } catch (...) {
                    state->building = false;
                    LogHresult(L"pruning a column");
                }
                return;
            }
            bool wasBuilding = state->building;
            state->building = true;
            try {
                // Only a column that shows its first listing scrolls itself into
                // view. A reload of one that is already there must not pull the
                // strip back to the last column, which undid the centring of
                // the column Explorer's Up had just gone to.
                bool firstListing = !column->filled;
                bool changed = !SameListing(loaded->data, *column);
                if (changed) {
                    ULONGLONG fillStarted = GetTickCount64();
                    size_t fillCount = loaded->data.entries.size();
                    if (!firstListing && PatchColumn(state, column.get(), loaded->data)) {
                        fillCount = 0;
                    } else {
                        FillColumn(state, column.get(), std::move(loaded->data));
                    }
                    ULONGLONG fillTook = GetTickCount64() - fillStarted;
                    if (fillTook >= 40 && fillCount > 0) {
                        Out(L"fill: %d rows set up in %d ms on the UI thread", (int)fillCount, (int)fillTook);
                    }
                } else if (column->cancel) {
                    // The rows and their icons are already there: the icon pass
                    // that follows this load is not needed.
                    *column->cancel = true;
                    column->cancel = std::make_shared<std::atomic<bool>>(false);
                }
                // A synchronous layout pass, so only when something changed:
                // the reloads after a toolbar command came in the middle of a
                // scroll and made it stutter.
                if (changed || firstListing) {
                    state->scroller.UpdateLayout();
                }
                if (state->swapOld && column->generation == state->swapGeneration) {
                    CompleteSwap(state);
                } else if (firstListing && state->columns.back().get() == column.get()) {
                    EnsureColumnVisible(state, (int)state->columns.size() - 1);
                }
            } catch (...) {
                LogHresult(L"filling a column");
            }
            state->building = wasBuilding;
            return;
        }
    }
    // No column waits for this result any more. Drop it.
}

// The pixel buffer of a WriteableBitmap, as a byte pointer.
struct IBufferByteAccessLocal : ::IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Buffer(uint8_t** value) = 0;
};
const GUID kIidBufferByteAccess = {
    0x905a0fef, 0xbc53, 0x11df,
    {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};

// Runs on the window's UI thread when the worker has read some icons.
// Puts an icon into its row's icon cell.
void ApplyIconBits(Column* column, IconBits const& icon) {
    try {
        mux::Media::Imaging::WriteableBitmap bitmap(icon.width, icon.height);
        auto buffer = bitmap.PixelBuffer();
        winrt::com_ptr<IBufferByteAccessLocal> access;
        winrt::check_hresult(
            ((::IUnknown*)winrt::get_abi(buffer))
                ->QueryInterface(kIidBufferByteAccess, access.put_void()));
        uint8_t* pixels = nullptr;
        winrt::check_hresult(access->Buffer(&pixels));
        if (!pixels || buffer.Capacity() < icon.bgra.size()) {
            return;
        }
        memcpy(pixels, icon.bgra.data(), icon.bgra.size());
        buffer.Length((uint32_t)icon.bgra.size());
        bitmap.Invalidate();

        muxc::Image image;
        image.Source(bitmap);
        image.Width(20);
        image.Height(20);
        image.Stretch(mux::Media::Stretch::Uniform);
        image.HorizontalAlignment(mux::HorizontalAlignment::Left);
        image.VerticalAlignment(mux::VerticalAlignment::Center);
        auto host = column->iconHosts[icon.index];
        if (host.Children().Size() > 0 && host.Children().GetAt(0).try_as<muxc::Image>()) {
            return;  // Already has its icon.
        }
        host.Children().Clear();
        host.Children().Append(image);
    } catch (...) {
        static int logged = 0;
        if (logged++ < 3) {
            LogHresult(L"applying an icon");
        }
    }
}

// Runs on the window's UI thread when the worker has read some icons. An icon
// whose row has no content yet waits for it.
void ApplyIcons(std::shared_ptr<IconBatch> const& batch) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    for (auto& column : state->columns) {
        if (column->generation != batch->generation) {
            continue;
        }
        for (auto& icon : batch->icons) {
            if (icon.index >= column->iconHosts.size()) {
                continue;
            }
            if (!column->iconHosts[icon.index]) {
                column->pendingIcons[icon.index] = std::make_shared<IconBits>(icon);
                continue;
            }
            ApplyIconBits(column.get(), icon);
        }
        return;
    }
}

void OnRowSelected(muxc::ListView const& list);
void SetHeaderFocus(Column* column, muxc::ListView const& list, int row);
void OnRowDoubleTapped(muxc::ListView const& list);
void OnEmptyDoubleTapped(muxc::ListView const& list,
                         mux::Input::DoubleTappedRoutedEventArgs const& args);
void OnRowRightTapped(muxc::ListView const& list,
                      mux::Input::RightTappedRoutedEventArgs const& args);
void OnColumnBackgroundTapped(muxc::ListView const& list,
                              mux::Input::TappedRoutedEventArgs const& args);
void OnColumnPointerMoved(muxc::ListView const& list,
                          mux::Input::PointerRoutedEventArgs const& args);
void OnColumnPointerExited(muxc::ListView const& list);
void OnBandPressed(muxc::ListView const& list, mux::Input::PointerRoutedEventArgs const& args);
void OnBandReleased(muxc::ListView const& list);
void OnColumnKey(muxc::ListView const& list,
                 mux::Input::KeyRoutedEventArgs const& args);

// Adds an empty column for `folder` at the right end and asks the worker to
// read it. Takes ownership of `folder`.
// ---- The handle on a column's border: it grows where the pointer is, follows it
// up and down the border, and shrinks away when the pointer leaves.

void PillGoTo(muxc::Border const& pill, double scaleTo, double offsetTo, int milliseconds, bool hideAtEnd) {
    try {
        auto motion = pill.RenderTransform().try_as<mux::Media::CompositeTransform>();
        if (!motion) {
            return;
        }
        namespace anim = mux::Media::Animation;
        anim::CubicEase ease;
        ease.EasingMode(anim::EasingMode::EaseOut);
        auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(milliseconds));
        anim::Storyboard board;
        auto add = [&](const wchar_t* property, double from, double to) {
            anim::DoubleAnimation step;
            step.From(from);
            step.To(to);
            step.Duration(length);
            step.EasingFunction(ease);
            step.EnableDependentAnimation(true);
            step.FillBehavior(anim::FillBehavior::HoldEnd);
            anim::Storyboard::SetTarget(step, pill);
            anim::Storyboard::SetTargetProperty(step, property);
            board.Children().Append(step);
        };
        add(L"(UIElement.RenderTransform).(CompositeTransform.ScaleY)", motion.ScaleY(), scaleTo);
        add(L"(UIElement.RenderTransform).(CompositeTransform.TranslateY)", motion.TranslateY(), offsetTo);
        if (hideAtEnd) {
            board.Completed([pill](Inspectable const&, Inspectable const&) {
                if (!winrt::unbox_value_or<bool>(pill.Tag(), false)) {
                    pill.Opacity(0);
                }
            });
        }
        board.Begin();
    } catch (...) {
        LogHresult(L"animating the border handle");
    }
}

// Where the handle sits for a pointer at `y` in the grip: its middle follows
// the pointer, kept inside the grip.
double PillOffset(Column* column, double y) {
    double height = column->grip.ActualHeight();
    double half = column->pill.Height() / 2;
    double centre = y < half ? half : (y > height - half ? height - half : y);
    return centre - height / 2;
}

void PillShow(Column* column, double y) {
    if (!column->pill || !column->grip) {
        return;
    }
    try {
        auto motion = column->pill.RenderTransform().try_as<mux::Media::CompositeTransform>();
        if (!motion) {
            return;
        }
        bool wasShown = winrt::unbox_value_or<bool>(column->pill.Tag(), false) && column->pill.Opacity() > 0;
        double offset = PillOffset(column, y);
        column->pill.Tag(winrt::box_value(true));
        column->pill.Opacity(1);
        if (!wasShown) {
            motion.TranslateY(offset);  // It appears at the pointer, then grows.
        }
        PillGoTo(column->pill, 1.0, offset, wasShown ? 80 : 150, false);
    } catch (...) {
        LogHresult(L"showing the border handle");
    }
}

void PillFollow(Column* column, double y) {
    if (!column->pill || !column->grip || !winrt::unbox_value_or<bool>(column->pill.Tag(), false)) {
        return;
    }
    PillGoTo(column->pill, 1.0, PillOffset(column, y), 35, false);
}

void PillHide(Column* column) {
    if (!column->pill || !winrt::unbox_value_or<bool>(column->pill.Tag(), false)) {
        return;
    }
    column->pill.Tag(winrt::box_value(false));
    auto motion = column->pill.RenderTransform().try_as<mux::Media::CompositeTransform>();
    PillGoTo(column->pill, 0.0, motion ? motion.TranslateY() : 0.0, 110, true);
}

// Shift and double-click on a border: every column takes the average of the
// widths their names would fit, gliding there, and keeps it like a width
// dragged by hand.
void HarmonizeWidths(WindowState* state) {
    double total = 0;
    int counted = 0;
    for (auto& column : state->columns) {
        if (column->list && column->filled && !column->entries.empty()) {
            total += FittedWidth(column.get());
            counted++;
        }
    }
    if (counted == 0) {
        return;
    }
    double target = std::round(total / counted);
    target = std::clamp(target, (double)g_minWidth, (double)g_maxWidth);
    namespace anim = mux::Media::Animation;
    anim::CubicEase ease;
    ease.EasingMode(anim::EasingMode::EaseInOut);
    auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(260));
    anim::Storyboard board;
    for (auto& column : state->columns) {
        if (!column->list) {
            continue;
        }
        double from = column->list.ActualWidth();
        column->userSized = true;
        column->list.Width(target);
        if (std::abs(from - target) < 1) {
            continue;
        }
        anim::DoubleAnimation glide;
        glide.From(from);
        glide.To(target);
        glide.Duration(length);
        glide.EasingFunction(ease);
        glide.EnableDependentAnimation(true);
        glide.FillBehavior(anim::FillBehavior::Stop);
        anim::Storyboard::SetTarget(glide, column->list);
        anim::Storyboard::SetTargetProperty(glide, L"Width");
        board.Children().Append(glide);
    }
    board.Begin();
    // The active marker follows the widths while they change; the names are cut
    // again for the new width at the end.
    auto timer = state->queue.CreateTimer();
    timer.Interval(std::chrono::milliseconds(16));
    auto frames = std::make_shared<int>(0);
    timer.Tick([frames](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
        WindowState* state = StateForThisThread(false);
        if (!state || !state->on) {
            sender.Stop();
            return;
        }
        UpdatePill(state, false);
        if (++*frames >= 20) {
            sender.Stop();
            for (auto& column : state->columns) {
                if (column->list) {
                    RefreshNames(column.get());
                    if (column->folder) {
                        RememberFolderWidth(PathOf(column->folder.get()), column->list.Width());
                    }
                }
            }
            UpdatePill(state, false);
        }
    });
    timer.Start();
    Out(L"widths harmonized to %d across %d columns", (int)target, (int)state->columns.size());
}

void AddColumn(WindowState* state, Pidl folder, bool atFront = false, bool archive = false) {
    auto column = std::make_unique<Column>();
    column->folder = std::move(folder);
    column->notifyId = WatchFolder(state->notifyWindow, column->folder.get());

    muxc::ListView list;
    list.Width(g_minWidth);
    {
        FolderMemory memory = GetFolderMemory(PathOf(column->folder.get()));
        if (memory.hasSpec) {
            column->spec = memory.spec;
            column->hasSpec = true;
        }
        if (memory.width >= g_minWidth && memory.width <= g_maxWidth) {
            list.Width(memory.width);
            column->userSized = true;
        }
    }
    if (archive) {
        // The shell reads the archive on the worker thread, which can take a while.
        // A spinner shows meanwhile; the listing replaces it.
        muxc::ProgressRing spinner;
        spinner.IsActive(true);
        spinner.Width(28);
        spinner.Height(28);
        spinner.HorizontalAlignment(mux::HorizontalAlignment::Center);
        spinner.Margin(mux::Thickness{0, 24, 0, 0});
        muxc::ListViewItem reading;
        reading.Content(spinner);
        reading.IsHitTestVisible(false);
        reading.IsTabStop(false);
        reading.HorizontalContentAlignment(mux::HorizontalAlignment::Center);
        list.Items().Append(reading);
    }
    // A list with rows hands the keyboard to its rows. An empty one has none,
    // so it takes the keyboard itself, without a focus outline.
    list.IsTabStop(true);
    list.UseSystemFocusVisuals(false);
    list.FocusVisualPrimaryThickness(mux::Thickness{0, 0, 0, 0});
    list.FocusVisualSecondaryThickness(mux::Thickness{0, 0, 0, 0});
    list.SelectionMode(muxc::ListViewSelectionMode::Extended);
    list.Padding(mux::Thickness{5, 12, 10, 4});
    column->selectionToken = list.SelectionChanged(
        [](Inspectable const& sender,
           muxc::SelectionChangedEventArgs const&) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnRowSelected(list);
            }
        });
    column->doubleTapToken = list.DoubleTapped(
        [](Inspectable const& sender,
           mux::Input::DoubleTappedRoutedEventArgs const& args) {
            // Only a double-click on a row opens it, not one on a header or
            // on the empty space.
            mux::DependencyObject node =
                args.OriginalSource().try_as<mux::DependencyObject>();
            bool onRow = false;
            for (int i = 0; node && i < 20; i++) {
                if (node.try_as<muxc::ListViewItem>()) {
                    onRow = true;
                    break;
                }
                node = mux::Media::VisualTreeHelper::GetParent(node);
            }
            if (!onRow) {
                if (auto list = sender.try_as<muxc::ListView>()) {
                    OnEmptyDoubleTapped(list, args);
                }
                return;
            }
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnRowDoubleTapped(list);
            }
        });
    // The empty space is hit-testable, so a click on it can pick the column.
    list.Background(Brush(0, 0, 0, 0));
    column->tapToken = list.Tapped(
        [](Inspectable const& sender, mux::Input::TappedRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnColumnBackgroundTapped(list, args);
            }
        });
    column->moveToken = list.PointerMoved(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnColumnPointerMoved(list, args);
            }
        });
    column->pressToken = list.PointerPressed(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnBandPressed(list, args);
            }
        });
    // A click stops a scroll that is still gliding. A row marks its own press as
    // handled, so this is added to see handled presses too.
    column->stopScrollHandler = mux::Input::PointerEventHandler(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& pressArgs) {
            try {
                if (auto list = sender.try_as<muxc::ListView>()) {
                    auto scroller = FindListScroller(list, 0);
                    WindowState* pressState = StateForThisThread(false);
                    // A row marks its own press as handled, so the list's ordinary
                    // press handler never sees it. A drag of rows starts here.
                    if (pressState && pressState->on && !g_unloading && !pressState->rename) {
                        mux::DependencyObject node =
                            pressArgs.OriginalSource().try_as<mux::DependencyObject>();
                        for (int i = 0; node && i < 20; i++) {
                            if (auto row = node.try_as<muxc::ListViewItem>()) {
                                OnDragPressed(pressState, list, row, pressArgs);
                                break;
                            }
                            node = mux::Media::VisualTreeHelper::GetParent(node);
                        }
                    }
                    if (pressState) {
                        int at = ColumnIndexOfList(pressState, list);
                        if (at >= 0) {
                            Column* c = pressState->columns[at].get();
                            // Turns that keep coming right after the press are a glide.
                            c->wheelSuppress = GetTickCount64() - c->lastWheelAt < 600;
                            c->wheelTarget = scroller ? scroller.VerticalOffset() : 0;
                        }
                    }
                    if (scroller) {
                        double offset = scroller.VerticalOffset();
                        // Move by half a pixel and back, without animation: a change to
                        // the same offset is ignored while the list glides.
                        double nudge = offset + 0.5 <= scroller.ScrollableHeight() ? 0.5 : -0.5;
                        scroller.ChangeView(nullptr, offset + nudge, nullptr, true);
                        scroller.ChangeView(nullptr, offset, nullptr, true);
                        bool inertia = scroller.IsScrollInertiaEnabled();
                        scroller.IsScrollInertiaEnabled(false);
                        scroller.IsScrollInertiaEnabled(inertia);
                        // A ChangeView to where the list already is can be ignored
                        // while it glides. Turning vertical scrolling off and on
                        // again ends the glide.
                        auto mode = scroller.VerticalScrollMode();
                        scroller.VerticalScrollMode(muxc::ScrollMode::Disabled);
                        scroller.VerticalScrollMode(mode);
                        static int logged = 0;
                        if (logged++ < 6) {
                            Out(L"a press stops the scroll of a list, offset %.1f", offset);
                        }
                    }
                }
            } catch (...) {
            }
        });
    list.AddHandler(mux::UIElement::PointerPressedEvent(),
                    winrt::box_value(column->stopScrollHandler), true);
    column->releaseToken = list.PointerReleased(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& relArgs) {
            if (relArgs.Pointer().PointerDeviceType() != winrt::Microsoft::UI::Input::PointerDeviceType::Mouse) {
                Out(L"touch: pointer released, device=%d id=%u", (int)relArgs.Pointer().PointerDeviceType(),
                    relArgs.Pointer().PointerId());
                if (WindowState* releaseState = StateForThisThread(false)) {
                    TouchTrack(releaseState, relArgs, true);
                }
            }
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnBandReleased(list);
            }
        });
    column->captureLostToken = list.PointerCaptureLost(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const& lostArgs) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                // A finger that still touches, in a drag that is armed or going, did not let go: the
                // system took the capture (a pan). Take it back, a few times at most.
                WindowState* lostState = StateForThisThread(false);
                if (lostState && lostState->on && lostArgs.Pointer().PointerDeviceType() ==
                                                      winrt::Microsoft::UI::Input::PointerDeviceType::Touch) {
                    auto& drag = lostState->drag;
                    {
                        auto& band = lostState->band;
                        if (band.touch && (band.pending || band.active)) {
                            bool contact = false;
                            try {
                                contact = lostArgs.GetCurrentPoint(list).IsInContact();
                            } catch (...) {
                            }
                            Out(L"touch: capture lost in a rectangle, xaml contact=%d", contact ? 1 : 0);
                            if (contact && band.recaptures++ < 5) {
                                try {
                                    list.CapturePointer(lostArgs.Pointer());
                                } catch (...) {
                                }
                                return;
                            }
                        }
                    }
                    bool xamlContact = false;
                    try {
                        xamlContact = lostArgs.GetCurrentPoint(list).IsInContact();
                    } catch (...) {
                    }
                    POINTER_INFO win32Info{};
                    bool win32Ok = GetPointerInfo(drag.pointerId, &win32Info) != 0;
                    if (drag.active || drag.armed) {
                        Out(L"touch: capture lost in a drag, xaml contact=%d, GetPointerInfo=%d (err %lu, flags 0x%lX)",
                            xamlContact ? 1 : 0, win32Ok ? 1 : 0, win32Ok ? 0ul : GetLastError(),
                            (unsigned long)win32Info.pointerFlags);
                    }
                    if ((drag.active || (drag.pending && drag.armed)) && drag.pointerId == lostArgs.Pointer().PointerId() &&
                        xamlContact) {
                        if (drag.recaptures++ < 5) {
                            try {
                                list.CapturePointer(lostArgs.Pointer());
                            } catch (...) {
                            }
                        }
                        return;
                    }
                }
                OnBandReleased(list);
            }
        });
    column->exitToken = list.PointerExited(
        [](Inspectable const& sender, mux::Input::PointerRoutedEventArgs const&) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnColumnPointerExited(list);
            }
        });
    // Touch, for the log: a hold, and a pointer that the system takes away (a pan).
    list.Holding([](Inspectable const& sender, mux::Input::HoldingRoutedEventArgs const& args) {
        Out(L"touch: holding state=%d device=%d", (int)args.HoldingState(), (int)args.PointerDeviceType());
        if ((int)args.HoldingState() == 0 && (int)args.PointerDeviceType() == 0) {
            WindowState* holdState = StateForThisThread(false);
            if (auto list = sender.try_as<muxc::ListView>(); list && holdState && holdState->on && !g_unloading) {
                OnDragHold(holdState, list);
            }
        }
    });
    list.PointerCanceled([](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
        Out(L"touch: the pointer was cancelled, device=%d id=%u", (int)args.Pointer().PointerDeviceType(),
            args.Pointer().PointerId());
    });
    list.PointerCaptureLost([](Inspectable const&, mux::Input::PointerRoutedEventArgs const& args) {
        if (args.Pointer().PointerDeviceType() != winrt::Microsoft::UI::Input::PointerDeviceType::Mouse) {
            Out(L"touch: the pointer capture was lost, device=%d id=%u", (int)args.Pointer().PointerDeviceType(),
                args.Pointer().PointerId());
        }
    });
    column->rightTapToken = list.RightTapped(
        [](Inspectable const& sender,
           mux::Input::RightTappedRoutedEventArgs const& args) {
            if (auto list = sender.try_as<muxc::ListView>()) {
                OnRowRightTapped(list, args);
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
                            if (!element) {
                                return;
                            }
                            auto props = args.GetCurrentPoint(element).Properties();
                            if (props.IsHorizontalMouseWheel()) {
                                args.Handled(true);
                                return;
                            }
                            // A vertical turn: scrolled here, not by the list, so that a
                            // press can stop it. A touchpad keeps sending turns while it
                            // glides, and a press ends that by dropping the turns until
                            // there is a pause.
                            WindowState* state = StateForThisThread(false);
                            if (!state || !state->on || g_unloading) {
                                return;
                            }
                            for (auto& c : state->columns) {
                                if (c->panel != element) {
                                    continue;
                                }
                                auto scroller = FindListScroller(c->list, 0);
                                if (!scroller) {
                                    return;
                                }
                                ULONGLONG now = GetTickCount64();
                                if (c->wheelSuppress) {
                                    if (now - c->lastWheelAt > 120) {
                                        c->wheelSuppress = false;
                                    } else {
                                        c->lastWheelAt = now;
                                        args.Handled(true);
                                        return;
                                    }
                                }
                                int delta = props.MouseWheelDelta();
                                bool notch = delta % 120 == 0;
                                double from = (notch && now - c->lastWheelAt < 300)
                                                  ? c->wheelTarget
                                                  : scroller.VerticalOffset();
                                double target = from - delta * 0.8;
                                target = std::max(0.0, std::min(target, scroller.ScrollableHeight()));
                                c->wheelTarget = target;
                                c->lastWheelAt = now;
                                scroller.ChangeView(nullptr, target, nullptr, !notch);
                                args.Handled(true);
                                return;
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
    {
        mux::Media::CompositeTransform motion;
        motion.ScaleY(0);
        pill.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
        pill.RenderTransform(motion);
        pill.Tag(winrt::box_value(false));
    }
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
           mux::Input::PointerRoutedEventArgs const& args) {
            WindowState* state = StateForThisThread(false);
            int index = state ? ColumnIndexOfGrip(state, sender) : -1;
            if (index >= 0) {
                Column* column = state->columns[index].get();
                column->hoverSince = GetTickCount64();
                if (state->dragColumn < 0) {
                    PillShow(column, args.GetCurrentPoint(column->grip).Position().Y);
                }
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
                    PillHide(state->columns[index].get());
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
            if (!state || !state->on) {
                return;
            }
            try {
                int index = ColumnIndexOfGrip(state, sender);
                if (index >= 0) {
                    Column* column = state->columns[index].get();
                    PillFollow(column, args.GetCurrentPoint(column->grip).Position().Y);
                }
                if (state->dragColumn < 0 || index != state->dragColumn) {
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
                UpdatePill(state, false);  // Follows the drag, frame by frame.
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
                    PillHide(state->columns[index].get());
                }
                RefreshNames(state->columns[index].get());
                RememberFolderWidth(PathOf(state->columns[index]->folder.get()),
                                    state->columns[index]->list.Width());
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
                if (g_harmonize && (GetKeyState(VK_SHIFT) & 0x8000)) {
                    HarmonizeWidths(state);
                    return;
                }
                Column* column = state->columns[index].get();
                column->userSized = false;
                FitWidth(column);
                RefreshNames(column);
                UpdatePill(state, false);
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

// Finishes a rebuild: the new first column is shown, the old ones are removed.
void CompleteSwap(WindowState* state) {
    if (!state->swapOld) {
        return;
    }
    state->swapOld = 0;
    try {
        bool wasBuilding = state->building;
        state->building = true;
        RemoveColumnsFrom(state, 1);
        state->building = wasBuilding;
        if (!state->columns.empty() && state->columns[0]->cell) {
            state->columns[0]->cell.Visibility(mux::Visibility::Visible);
        }
        // The pill glides from where it was to the new first column.
        SetActiveColumn(state, 0);
    } catch (...) {
        LogHresult(L"CompleteSwap");
    }
}

// Starts over with Explorer's current folder as the only column.
// Is this folder one of the navigation pane's flat entries: Home, Gallery,
// OneDrive, or a pinned or frequent folder? The expandable tree (This PC,
// drives, Network) is not.
bool IsFlatSidebarTarget(PCIDLIST_ABSOLUTE folder) {
    PWSTR parsing = nullptr;
    if (FAILED(SHGetNameFromIDList(folder, SIGDN_DESKTOPABSOLUTEPARSING,
                                   &parsing)) ||
        !parsing) {
        return false;
    }
    std::wstring target = parsing;
    CoTaskMemFree(parsing);
    if (_wcsicmp(target.c_str(), L"::{F874310E-B6B7-47DC-BC84-B9E6B38F5903}") == 0 ||
        _wcsicmp(target.c_str(), L"::{E88865EA-0E1C-4E20-9AA6-EDCD0212C87C}") == 0) {
        return true;  // Home, Gallery.
    }
    // OneDrive and the user's known folders.
    const KNOWNFOLDERID* known[] = {&FOLDERID_Desktop,   &FOLDERID_Downloads,
                                    &FOLDERID_Documents, &FOLDERID_Pictures,
                                    &FOLDERID_Music,     &FOLDERID_Videos};
    for (auto id : known) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, 0, nullptr, &path)) && path) {
            bool same = _wcsicmp(path, target.c_str()) == 0;
            CoTaskMemFree(path);
            if (same) {
                return true;
            }
        }
    }
    wchar_t oneDrive[MAX_PATH] = L"";
    if (GetEnvironmentVariableW(L"OneDrive", oneDrive, MAX_PATH) &&
        _wcsicmp(oneDrive, target.c_str()) == 0) {
        return true;
    }
    // Pinned and frequent folders, from Quick access.
    bool found = false;
    PIDLIST_ABSOLUTE quick = nullptr;
    if (SUCCEEDED(SHParseDisplayName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}",
                                     nullptr, &quick, 0, nullptr)) &&
        quick) {
        IShellFolder* folder = nullptr;
        if (SUCCEEDED(SHBindToObject(nullptr, quick, nullptr,
                                     IID_PPV_ARGS(&folder))) &&
            folder) {
            IEnumIDList* list = nullptr;
            if (folder->EnumObjects(nullptr, SHCONTF_FOLDERS, &list) == S_OK &&
                list) {
                PITEMID_CHILD child = nullptr;
                int count = 0;
                while (!found && count++ < 100 &&
                       list->Next(1, &child, nullptr) == S_OK && child) {
                    STRRET text = {};
                    wchar_t path[MAX_PATH] = L"";
                    if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_FORPARSING,
                                                           &text)) &&
                        SUCCEEDED(StrRetToBufW(&text, child, path, MAX_PATH))) {
                        found = _wcsicmp(path, target.c_str()) == 0;
                    }
                    CoTaskMemFree(child);
                    child = nullptr;
                }
                list->Release();
            }
            folder->Release();
        }
        CoTaskMemFree(quick);
    }
    return found;
}

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
    state->lastSeen.reset(ILClone(current.get()));
    state->expected.reset();
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    // The new first folder brings its own sort and grouping. Without this, a
    // group key of the old folder (Home groups by its own property) put every
    // item of the new one in "Unspecified".
    {
        SortSpec fresh;
        if (ReadSortSpec(browser, &fresh)) {
            state->spec = fresh;
            RememberFolderSpec(PathOf(current.get()), fresh);
            Out(L"rebuild: spec %d sort columns, grouped=%d", (int)fresh.columns.size(),
                fresh.grouped ? 1 : 0);
        }
    }
    if (state->columns.empty()) {
        AddColumn(state, std::move(current));
        return;
    }
    // Columns are showing: add the new first column hidden, in front, and let
    // ApplyLoaded swap the old ones out once it has its rows.
    if (state->swapOld) {
        CompleteSwap(state);
    }
    size_t old = state->columns.size();
    AddColumn(state, std::move(current), true);
    Column* fresh = state->columns[0].get();
    if (fresh->cell) {
        fresh->cell.Visibility(mux::Visibility::Collapsed);
    }
    state->swapOld = old;
    state->swapGeneration = fresh->generation;
    state->swapDeadline = GetTickCount64() + 3000;
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
    // Explorer's view takes focus when it navigates. If the keyboard was in
    // a column, the tick gives it back.
    if (state->bridge && state->active >= 0 &&
        state->active < (int)state->columns.size()) {
        HWND panel = (HWND)state->bridge.WindowId().Value;
        HWND focused = GetFocus();
        if (panel && focused && (focused == panel || IsChild(panel, focused))) {
            state->focusColumn = state->columns[state->active].get();
            state->focusUntil = GetTickCount64() + 2500;
        }
    }
    if (FolderGone(folder)) {
        Out(L"not navigating Explorer to a folder that is gone");
        return;
    }
    state->cursorFixUntil = GetTickCount64() + 2500;
    g_ourBrowse = true;
    // Without taking the keyboard: Explorer's view used to take it, and giving
    // it back later flashed the selection and scrolled the strip.
    HRESULT hr = browser->BrowseObject(
        folder, SBSP_ABSOLUTE | SBSP_SAMEBROWSER | SBSP_ACTIVATE_NOFOCUS);
    g_ourBrowse = false;
    if (FAILED(hr)) {
        Out(L"BrowseObject failed, hr=0x%08lX", hr);
        state->expected.reset();
    } else {
        state->specSyncDue = GetTickCount64() + 700;
    }
}

// Explorer follows after a short pause, so a run of quick selections costs
// one navigation. A selection in the folder Explorer already shows needs no
// navigation: it is mirrored at once, so a previewer that asks Explorer for the
// selected item (QuickLook on Space) finds it there.
void ScheduleNavigation(WindowState* state, PCIDLIST_ABSOLUTE folder, ULONGLONG delay) {
    if (!g_follow) {
        return;
    }
    if (IsInControlPanel(folder)) {
        // Explorer's view stays where it is. The columns still show it.
        state->pendingNav.reset();
        state->selectFolder.reset();
        state->selectChild.reset();
        return;
    }
    if (state->lastSeen && !state->expected && ILIsEqual(state->lastSeen.get(), folder)) {
        state->pendingNav.reset();
        if (state->selectChild && state->selectFolder && ILIsEqual(state->selectFolder.get(), folder)) {
            if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
                if (SelectInExplorer(browser, state->selectChild.get())) {
                    if (!state->selectExtra.empty()) {
                        AddToSelectionInExplorer(browser, state->selectExtra);
                        state->selectExtra.clear();
                    }
                    state->selectFolder.reset();
                    state->selectChild.reset();
                }
            }
        }
        return;
    }
    state->pendingNav.reset(ILClone(folder));
    state->navDue = GetTickCount64() + delay;
}

// While the columns to the right of `columnIndex` are stale, the chevron of the
// folder that leads to them sits on a small rounded badge, a quiet thread of the
// hierarchy.
void SetStaleHint(WindowState* state, int columnIndex, bool on) {
    if (columnIndex < 0 || columnIndex >= (int)state->columns.size()) {
        return;
    }
    Column* column = state->columns[columnIndex].get();
    try {
        if (column->hintBadge) {
            if (auto parent = mux::Media::VisualTreeHelper::GetParent(column->hintBadge).try_as<muxc::Panel>()) {
                uint32_t index = 0;
                if (parent.Children().IndexOf(column->hintBadge, index)) {
                    parent.Children().RemoveAt(index);
                }
            }
            column->hintBadge = nullptr;
        }
        if (column->hintEntry >= 0 &&
            column->hintEntry < (int)column->chevrons.size() &&
            column->chevrons[column->hintEntry]) {
            column->chevrons[column->hintEntry].Opacity(0.6);
        }
        column->hintEntry = -1;
        if (!on || columnIndex + 1 >= (int)state->columns.size()) {
            return;
        }
        Pidl leading(ILClone(ILFindLastID(state->columns[columnIndex + 1]->folder.get())));
        if (!leading) {
            return;
        }
        for (size_t i = 0; i < column->entries.size() && i < column->chevrons.size(); i++) {
            if (ILIsEqual(column->entries[i].child.get(), leading.get())) {
                EnsureRowBuilt(column, i);
                auto chevron = column->chevrons[i];
                if (!chevron) {
                    break;
                }
                if (auto grid = mux::Media::VisualTreeHelper::GetParent(chevron).try_as<muxc::Panel>()) {
                    uint32_t at = 0;
                    if (grid.Children().IndexOf(chevron, at)) {
                        bool dark = chevron.ActualTheme() == mux::ElementTheme::Dark;
                        muxc::Border badge;
                        // A circle whose middle is the chevron's middle.
                        double glyph = chevron.ActualWidth() > 1 ? chevron.ActualWidth() : 10;
                        badge.Width(15);
                        badge.Height(15);
                        badge.CornerRadius(mux::CornerRadius{7.5, 7.5, 7.5, 7.5});
                        badge.Margin(mux::Thickness{0, 0, glyph / 2 - 7.5, 0});
                        badge.Background(dark ? Brush(13, 255, 255, 255) : Brush(10, 0, 0, 0));
                        badge.HorizontalAlignment(mux::HorizontalAlignment::Right);
                        badge.VerticalAlignment(mux::VerticalAlignment::Center);
                        badge.IsHitTestVisible(false);
                        muxc::Grid::SetColumn(badge, 3);
                        grid.Children().InsertAt(at, badge);  // Under the chevron.
                        column->hintBadge = badge;
                    }
                }
                chevron.Opacity(0.9);
                column->hintEntry = (int)i;
                break;
            }
        }
    } catch (...) {
        LogHresult(L"SetStaleHint");
    }
}

// Working in a column, by choosing an item in it or clicking its empty space,
// lifts the stale look of it and of the columns before it, and puts the
// selection of the earlier columns back on the folders that lead here. The
// columns after it stay lit only while the selection here still leads to them.
void LiftStale(WindowState* state, int columnIndex) {
    for (int j = 0; j <= columnIndex; j++) {
        if (state->columns[j]->cell) {
            state->columns[j]->cell.Opacity(1.0);
        }
        if (j < columnIndex) {
            SetStaleHint(state, j, false);
        }
    }
    for (int j = columnIndex; j > 0; j--) {
        Column* previous = state->columns[j - 1].get();
        Column* here = state->columns[j].get();
        if (!ILIsParent(previous->folder.get(), here->folder.get(), TRUE)) {
            break;
        }
        Pidl leading(ILClone(ILFindLastID(here->folder.get())));
        if (leading &&
            (!previous->selectedChild ||
             !ILIsEqual(previous->selectedChild.get(), leading.get()))) {
            SelectChildInColumn(state, previous, leading.get());
        }
    }
    // The columns after this one: lit while the chain holds.
    bool chain = true;
    for (size_t k = columnIndex + 1; k < state->columns.size(); k++) {
        Column* previous = state->columns[k - 1].get();
        Column* here = state->columns[k].get();
        Pidl leading(ILClone(ILFindLastID(here->folder.get())));
        chain = chain && previous->selectedChild && leading &&
                ILIsParent(previous->folder.get(), here->folder.get(), TRUE) &&
                ILIsEqual(previous->selectedChild.get(), leading.get());
        if (here->cell) {
            here->cell.Opacity(chain ? 1.0 : 0.5);
        }
    }
    SetStaleHint(state, columnIndex, false);
}

// The entries of the rows selected in a column, in row order. Group headers
// are left out.
std::vector<size_t> SelectedEntries(Column* column,
                                    muxc::ListView const& list) {
    std::vector<size_t> result;
    auto items = list.Items();
    uint32_t count = items.Size();
    for (uint32_t r = 0; r < count && r < column->rowToEntry.size(); r++) {
        int entry = column->rowToEntry[r];
        if (entry < 0) {
            continue;
        }
        auto item = items.GetAt(r).try_as<muxc::ListViewItem>();
        if (item && item.IsSelected() &&
            item.Visibility() == mux::Visibility::Visible) {
            result.push_back((size_t)entry);
        }
    }
    return result;
}

// The background of a selected row is a border inside its presenter, inset by
// 4 px at the sides and 2.3 px above and below. Rows that are selected next to
// each other share one background, as in Finder: the border loses the inset
// where it meets the next one, and its corners there are square. Only the rows
// touched by the last pass are put back, so a long column costs nothing when
// the selection does not change.
muxc::Border SelectionBorderOf(muxc::ListViewItem const& item) {
    try {
        if (mux::Media::VisualTreeHelper::GetChildrenCount(item) > 0) {
            auto presenter = mux::Media::VisualTreeHelper::GetChild(item, 0);
            if (presenter && mux::Media::VisualTreeHelper::GetChildrenCount(presenter) > 0) {
                return mux::Media::VisualTreeHelper::GetChild(presenter, 0).try_as<muxc::Border>();
            }
        }
    } catch (...) {
    }
    return nullptr;
}

// Puts the rows that were shaped for a run of selected rows back to their own look.
void UnshapeRows(Column* column) {
    try {
        auto items = column->list.Items();
        for (uint32_t r : column->shapedRows) {
            if (r < items.Size()) {
                if (auto item = items.GetAt(r).try_as<muxc::ListViewItem>()) {
                    if (auto border = SelectionBorderOf(item)) {
                        border.Margin(mux::Thickness{4, 2.3, 4, 2.3});
                        border.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
                    }
                }
            }
        }
        column->shapedRows.clear();
    } catch (...) {
    }
}

void ShapeSelectionRuns(Column* column, muxc::ListView const& list) {
    static bool known = false;
    static mux::Thickness defaultMargin{4, 2.3, 4, 2.3};
    static mux::CornerRadius defaultCorner{4, 4, 4, 4};
    try {
        auto items = list.Items();
        uint32_t count = items.Size();
        auto rowAt = [&](uint32_t r) -> muxc::ListViewItem {
            return r < count ? items.GetAt(r).try_as<muxc::ListViewItem>() : muxc::ListViewItem{nullptr};
        };
        auto selectedAt = [&](uint32_t r) {
            if (r >= count || r >= column->rowToEntry.size() || column->rowToEntry[r] < 0) {
                return false;
            }
            auto item = rowAt(r);
            return item && item.IsSelected() && item.Visibility() == mux::Visibility::Visible;
        };
        for (uint32_t r : column->shapedRows) {
            if (auto item = rowAt(r)) {
                if (auto border = SelectionBorderOf(item)) {
                    border.Margin(defaultMargin);
                    border.CornerRadius(defaultCorner);
                }
            }
        }
        column->shapedRows.clear();
        auto selected = list.SelectedItems();
        for (uint32_t i = 0; i < selected.Size(); i++) {
            auto item = selected.GetAt(i).try_as<muxc::ListViewItem>();
            if (!item) {
                continue;
            }
            int index = list.IndexFromContainer(item);
            if (index < 0) {
                continue;
            }
            auto border = SelectionBorderOf(item);
            if (!border) {
                continue;
            }
            if (!known) {
                known = true;
                defaultMargin = border.Margin();
                defaultCorner = border.CornerRadius();
            }
            uint32_t r = (uint32_t)index;
            bool up = r > 0 && selectedAt(r - 1);
            bool down = selectedAt(r + 1);
            if (!up && !down) {
                continue;
            }
            border.Margin(mux::Thickness{defaultMargin.Left, up ? 0.0 : defaultMargin.Top, defaultMargin.Right,
                                         down ? 0.0 : defaultMargin.Bottom});
            double top = up ? 0 : defaultCorner.TopLeft, bottom = down ? 0 : defaultCorner.BottomLeft;
            border.CornerRadius(mux::CornerRadius{top, top, bottom, bottom});
            column->shapedRows.push_back(r);
        }
    } catch (...) {
        LogHresult(L"shaping runs of selected rows");
    }
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
        ShapeSelectionRuns(column, list);
        if (column->focusHeader >= 0) {
            SetHeaderFocus(column, list, -1);  // A row was chosen: the keyboard leaves the header.
        }
        column->selectionChangedAt = GetTickCount64();
        state->slowRenameAt = 0;

        LiftStale(state, columnIndex);

        // Group headers and the "more items" row can't be selected, as when
        // Shift-click selects a range across them.
        {
            auto items = list.Items();
            auto selected = list.SelectedItems();
            state->building = true;
            for (uint32_t r = 0; r < items.Size() && r < column->rowToEntry.size();
                 r++) {
                auto object = items.GetAt(r);
                bool hidden = false;
                if (column->rowToEntry[r] >= 0) {
                    auto item = object.try_as<muxc::ListViewItem>();
                    hidden = item && item.Visibility() == mux::Visibility::Collapsed;
                    if (!hidden) {
                        continue;
                    }
                }
                uint32_t at = 0;
                if (selected.IndexOf(object, at)) {
                    selected.RemoveAt(at);
                }
            }
            state->building = false;
        }

        int row = list.SelectedIndex();
        int entryIndex = row >= 0 && row < (int)column->rowToEntry.size()
                             ? column->rowToEntry[row]
                             : -1;
        state->selectFolder.reset();
        state->selectChild.reset();
        state->selectExtra.clear();

        // The columns to the right stay while the selection is a file, several
        // items or nothing, dimmed, until another folder is chosen here.
        auto dimAfter = [&](bool dim) {
            if (dim && !g_keepStale) {
                // The setting asks for them to go instead of waiting, dimmed.
                state->building = true;
                RemoveColumnsFrom(state, columnIndex + 1);
                state->building = false;
                SetStaleHint(state, columnIndex, false);
                return;
            }
            for (size_t j = columnIndex + 1; j < state->columns.size(); j++) {
                if (state->columns[j]->cell) {
                    state->columns[j]->cell.Opacity(dim ? 0.5 : 1.0);
                }
            }
            SetStaleHint(state, columnIndex, dim);
        };

        // Several items selected: no column opens. Explorer's own view gets
        // the same selection, so the preview pane, status bar and command
        // bar act on all of them.
        std::vector<size_t> picked = SelectedEntries(column, list);
        if (picked.size() > 1) {
            column->selectedChild.reset(
                ILClone(column->entries[picked[0]].child.get()));
            SetActiveColumn(state, columnIndex);
            state->selectFolder.reset(ILClone(column->folder.get()));
            state->selectChild.reset(
                ILClone(column->entries[picked[0]].child.get()));
            for (size_t i = 1; i < picked.size(); i++) {
                state->selectExtra.emplace_back(
                    ILClone(column->entries[picked[i]].child.get()));
            }
            state->selectTries = 0;
            ScheduleNavigation(state, column->folder.get(), 120);
            dimAfter(true);
            Out(L"%d items selected in column %d", (int)picked.size(),
                columnIndex + 1);
            return;
        }
        if (entryIndex < 0 && picked.size() == 1) {
            entryIndex = (int)picked[0];
        }
        if (entryIndex < 0) {
            column->selectedChild.reset();
            dimAfter(true);
            return;
        }
        const Entry& entry = column->entries[entryIndex];
        column->selectedChild.reset(ILClone(entry.child.get()));
        Pidl absolute(entry.linkTarget ? ILClone(entry.linkTarget.get())
                                       : ILCombine(column->folder.get(), entry.child.get()));
        if (!absolute) {
            return;
        }
        if (entry.launcher) {
            // Nothing to show in Explorer's view or in a column. Enter or a
            // double-click runs it.
            dimAfter(true);
            SetActiveColumn(state, columnIndex);
            return;
        }
        if (entry.folder && FolderGone(absolute.get())) {
            // The row is stale: the folder was deleted, renamed or moved. Read
            // the column again instead of opening a column on nothing.
            Out(L"\"%s\" is gone, the column is read again", entry.name.c_str());
            RequestLoad(state, column);
            return;
        }
        if (entry.folder) {
            // Explorer's view shows this column's folder with the item
            // selected, so the toolbar and the panes act on the folder.
            state->selectFolder.reset(ILClone(column->folder.get()));
            state->selectChild.reset(ILClone(entry.child.get()));
            state->selectTries = 0;
            ScheduleNavigation(state, column->folder.get());
            if (columnIndex + 1 < (int)state->columns.size() &&
                ILIsEqual(state->columns[columnIndex + 1]->folder.get(),
                          absolute.get())) {
                // Back on the folder whose columns are still there.
                dimAfter(false);
            } else {
                state->building = true;
                RemoveColumnsFrom(state, columnIndex + 1);
                state->building = false;
                SetStaleHint(state, columnIndex, false);
                AddColumn(state, std::move(absolute), false, entry.archive);
            }
            // The highlight stays here. It moves to the new column when the
            // user enters it.
            SetActiveColumn(state, columnIndex);
        } else {
            dimAfter(true);
            SetActiveColumn(state, columnIndex);
            // A file: Explorer shows the file's folder with the file selected.
            state->selectFolder.reset(ILClone(column->folder.get()));
            state->selectChild.reset(ILClone(entry.child.get()));
            state->selectTries = 0;
            ScheduleNavigation(state, column->folder.get(), 120);
        }
    } catch (...) {
        state->building = false;
        LogHresult(L"OnRowSelected");
    }
}

