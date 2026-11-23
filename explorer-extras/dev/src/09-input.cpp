// ---- Selection by a rectangle ----

muxc::ScrollViewer FindListScroller(mux::DependencyObject const& node, int depth) {
    if (!node || depth > 8) {
        return nullptr;
    }
    if (auto viewer = node.try_as<muxc::ScrollViewer>()) {
        return viewer;
    }
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        if (auto found = FindListScroller(mux::Media::VisualTreeHelper::GetChild(node, i), depth + 1)) {
            return found;
        }
    }
    return nullptr;
}

// The list scrolled: the rows that came near the screen get their content.
void OnListViewChanged(Inspectable const& sender) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        auto viewer = sender.try_as<muxc::ScrollViewer>();
        for (auto& c : state->columns) {
            if (viewer && c->viewer && c->viewer == viewer && c->filled) {
                BuildVisibleRows(c.get());
                return;
            }
        }
    } catch (...) {
        LogHresult(L"OnListViewChanged");
    }
}

// Where each row sits in the list's scrolled content. Computed from the row
// heights and corrected by a row that is on screen, since rows off screen may
// not have been laid out.
void MeasureRows(Column* column, muxc::ListView const& list, double offset,
                 std::vector<double>* top, std::vector<double>* bottom) {
    auto items = list.Items();
    size_t count = std::min<size_t>(items.Size(), column->rowToEntry.size());
    std::vector<double> cumulative(count + 1, 0.0);
    std::vector<char> shown(count, 0);
    for (size_t r = 0; r < count; r++) {
        double height = 0;
        if (auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
            if (item.Visibility() == mux::Visibility::Visible) {
                shown[r] = 1;
                height = column->rowToEntry[r] >= 0 ? column->rowHeight
                         : column->rowGroup[r] >= 0 ? column->rowHeight + 4
                                                    : column->rowHeight;
            }
        }
        cumulative[r + 1] = cumulative[r] + height;
    }
    double base = 12;  // The list's top padding, if no row can be measured.
    for (size_t r = 0; r < count; r++) {
        auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>();
        if (item && shown[r] && item.ActualHeight() > 0) {
            double y = item.TransformToVisual(list)
                           .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                           .Y;
            base = y + offset - cumulative[r];
            break;
        }
    }
    top->assign(count, 0);
    bottom->assign(count, 0);
    for (size_t r = 0; r < count; r++) {
        (*top)[r] = shown[r] ? base + cumulative[r] : -1;
        (*bottom)[r] = shown[r] ? base + cumulative[r + 1] : -1;
    }
}

void EndBand(WindowState* state, bool apply) {
    auto& band = state->band;
    bool wasActive = band.active;
    Column* column = band.column;
    band.pending = false;
    band.active = false;
    band.column = nullptr;
    for (size_t i = band.locked.size(); i-- > 0;) {  // Backwards: the first note is the original.
        auto& lock = band.locked[i];
        try {
            lock.viewer.VerticalScrollMode(lock.v);
            lock.viewer.HorizontalScrollMode(lock.h);
        } catch (...) {
        }
    }
    band.locked.clear();
    band.touch = false;
    band.recaptures = 0;
    if (band.panning) {
        // The finger lifts: the columns carry on a little, by the speed it had.
        band.panning = false;
        if (state->scroller && std::abs(band.panVelocity) > 0.3) {
            try {
                double target = std::clamp(state->scroller.HorizontalOffset() - band.panVelocity * 320.0, 0.0, state->scroller.ScrollableWidth());
                state->scroller.ChangeView(target, nullptr, nullptr, false);
            } catch (...) {
            }
        }
        band.panVelocity = 0;
    }
    band.top.clear();
    band.bottom.clear();
    band.base.clear();
    if (state->bandBox) {
        state->bandBox.Opacity(0);
    }
    if (wasActive && apply) {
        for (size_t i = 0; i < state->columns.size(); i++) {
            if (state->columns[i].get() == column) {
                // Now that the selection is settled, tell the rest: Explorer's
                // view, the stale look, the new column.
                column->list.ReleasePointerCaptures();
                OnRowSelected(column->list);
                break;
            }
        }
    }
}

void OnBandPressed(muxc::ListView const& list, mux::Input::PointerRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading || state->rename) {
        return;
    }
    try {
        auto point = args.GetCurrentPoint(list);
        // A second finger down while a finger is waiting to draw a rectangle (or is panning): this
        // is a two-finger gesture, for the system. The rectangle is given up and the scrolling that
        // was switched off is given back. (A second press used to lock the scrollers again, in their
        // locked state, and they stayed off afterwards.)
        if (args.Pointer().PointerDeviceType() == winrt::Microsoft::UI::Input::PointerDeviceType::Touch && state->band.touch &&
            (state->band.pending || state->band.active || state->band.panning)) {
            if (state->band.column) {
                state->band.column->list.ReleasePointerCaptures();
            }
            EndBand(state, false);
            LogVerbose(L"touch: a second finger, the rectangle is given up for the two-finger scroll");
            return;
        }
        // The middle button on a folder opens it in a new tab.
        if (point.Properties().IsMiddleButtonPressed()) {
            mux::DependencyObject node =
                args.OriginalSource().try_as<mux::DependencyObject>();
            for (int i = 0; node && i < 20; i++) {
                if (auto row = node.try_as<muxc::ListViewItem>()) {
                    int at = ColumnIndexOfList(state, list);
                    int rowIndex = list.IndexFromContainer(row);
                    if (at >= 0 && rowIndex >= 0) {
                        Column* column = state->columns[at].get();
                        int entry = rowIndex < (int)column->rowToEntry.size()
                                        ? column->rowToEntry[rowIndex]
                                        : -1;
                        if (entry >= 0 && column->entries[entry].folder) {
                            auto& hit = column->entries[entry];
                            Pidl target(hit.linkTarget
                                            ? ILClone(hit.linkTarget.get())
                                            : ILCombine(column->folder.get(), hit.child.get()));
                            OpenFolderInNewTab(state, target.get());
                            args.Handled(true);
                        }
                    }
                    return;
                }
                node = mux::Media::VisualTreeHelper::GetParent(node);
            }
            return;
        }
        if (!point.Properties().IsLeftButtonPressed()) {
            return;
        }
        // On a row, the list selects as usual. Empty space, the gutter and
        // group headers start a rectangle.
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        for (int i = 0; node && i < 20; i++) {
            if (auto row = node.try_as<muxc::ListViewItem>()) {
                OnDragPressed(state, list, row, args);
                return;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        Column* column = state->columns[index].get();
        auto scroller = FindListScroller(list);
        {
            // For the log: what was pressed, and where, so a strip that doesn't
            // start a rectangle can be traced.
            static int logged = 0;
            if (logged++ < 30) {
                auto at = point.Position();
                std::wstring source = L"?";
                try {
                    source = winrt::get_class_name(args.OriginalSource()).c_str();
                } catch (...) {
                }
                Wh_Log(L"press on empty space of column %d: x=%.0f of %.0f, y=%.0f, source=%s",
                    index + 1, (double)at.X, list.ActualWidth(), (double)at.Y,
                    source.c_str());
            }
        }
        auto& band = state->band;
        band.pending = true;
        band.active = false;
        band.additive = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        band.column = column;
        band.start = point.Position();
        band.startOffset = scroller ? scroller.VerticalOffset() : 0;
        band.first = band.last = -1;
        band.touch = args.Pointer().PointerDeviceType() == winrt::Microsoft::UI::Input::PointerDeviceType::Touch;
        if (band.touch) {
            // A finger on the empty space below the rows draws the rectangle instead of panning:
            // the scrolling of the list and of what is above it is switched off until it ends.
            band.recaptures = 0;
            auto lockOne = [&band](muxc::ScrollViewer const& viewer) {
                for (auto const& done : band.locked) {
                    if (done.viewer == viewer) {
                        return;  // Already locked: its first note is the one to give back.
                    }
                }
                DragState::LockedScroller lock;
                lock.viewer = viewer;
                lock.v = viewer.VerticalScrollMode();
                lock.h = viewer.HorizontalScrollMode();
                viewer.VerticalScrollMode(muxc::ScrollMode::Disabled);
                viewer.HorizontalScrollMode(muxc::ScrollMode::Disabled);
                band.locked.push_back(lock);
            };
            if (scroller) {
                lockOne(scroller);
            }
            mux::DependencyObject up = list;
            for (int i = 0; up && i < 40; i++) {
                up = mux::Media::VisualTreeHelper::GetParent(up);
                if (auto viewer = up.try_as<muxc::ScrollViewer>()) {
                    lockOne(viewer);
                }
            }
            list.CapturePointer(args.Pointer());
            LogVerbose(L"touch: pressed on the empty space of column %d, a rectangle may start (scrollers locked=%d)", index + 1,
                (int)band.locked.size());
        }
    } catch (...) {
        LogHresult(L"OnBandPressed");
    }
}

// Returns true while a rectangle is being drawn, so the hover code stands aside.
bool OnBandMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args) {
    auto& band = state->band;
    if (!band.pending && !band.active && !band.panning) {
        return false;
    }
    if (band.column != column) {
        return band.active || band.panning;
    }
    auto point = args.GetCurrentPoint(list);
    if (!point.Properties().IsLeftButtonPressed()) {
        EndBand(state, band.active);
        return false;
    }
    if (band.panning) {
        // The columns follow the finger sideways.
        try {
            double x = args.GetCurrentPoint(state->scroller).Position().X;
            double target = std::clamp(band.panStartOffset - (x - band.panStartX), 0.0, state->scroller.ScrollableWidth());
            state->scroller.ChangeView(target, nullptr, nullptr, true);
            ULONGLONG now = GetTickCount64();
            if (now > band.panLastAt) {
                double speed = (x - band.panLastX) / (double)(now - band.panLastAt);
                band.panVelocity = 0.6 * speed + 0.4 * band.panVelocity;
                band.panLastX = x;
                band.panLastAt = now;
            }
        } catch (...) {
        }
        return true;
    }
    auto at = point.Position();
    if (band.pending) {
        double moved = std::abs(at.X - band.start.X) + std::abs(at.Y - band.start.Y);
        if (moved < 6) {
            return false;  // Still a click.
        }
        // A finger that goes mostly sideways is scrolling the columns, not drawing a rectangle.
        if (band.touch && state->scroller && moved >= 10 && std::abs(at.X - band.start.X) > 1.8 * std::abs(at.Y - band.start.Y)) {
            band.pending = false;
            band.panning = true;
            band.panStartX = args.GetCurrentPoint(state->scroller).Position().X;
            band.panStartOffset = state->scroller.HorizontalOffset();
            band.panLastX = band.panStartX;
            band.panLastAt = GetTickCount64();
            band.panVelocity = 0;
            list.CapturePointer(args.Pointer());
            LogVerbose(L"touch: a sideways drag on the empty space, the columns scroll");
            return true;
        }
        // A drag: take the pointer, remember the rows, start from the selection
        // the user wants to keep.
        band.pending = false;
        band.active = true;
        if (band.touch) {
            LogVerbose(L"touch: the finger moved, the rectangle starts");
        }
        list.CapturePointer(args.Pointer());
        auto scroller = FindListScroller(list);
        MeasureRows(column, list, scroller ? scroller.VerticalOffset() : 0, &band.top, &band.bottom);
        band.base.clear();
        auto items = list.Items();
        for (uint32_t r = 0; r < items.Size(); r++) {
            auto item = items.GetAt(r).try_as<muxc::ListViewItem>();
            if (band.additive && item && item.IsSelected()) {
                band.base.push_back((int)r);
            }
        }
        if (!band.additive) {
            bool wasBuilding = state->building;
            state->building = true;
            list.SelectedItems().Clear();
            state->building = wasBuilding;
        }
        LiftStale(state, ColumnIndexOfList(state, list));
        SetActiveColumn(state, ColumnIndexOfList(state, list));
        Wh_Log(L"selection rectangle started in column %d", ColumnIndexOfList(state, list) + 1);
    }

    auto scroller = FindListScroller(list);
    double offset = scroller ? scroller.VerticalOffset() : 0;
    double view = scroller ? scroller.ViewportHeight() : list.ActualHeight();
    // Near the top or bottom edge the list scrolls.
    if (scroller) {
        if (at.Y < 24 && offset > 0) {
            scroller.ChangeView(nullptr, std::max(0.0, offset - 22), nullptr, true);
        } else if (at.Y > view - 24) {
            scroller.ChangeView(nullptr, offset + 22, nullptr, true);
        }
    }
    // The rectangle in content coordinates, and the rows it touches.
    double startY = band.start.Y + band.startOffset;
    double nowY = at.Y + offset;
    double yMin = std::min(startY, nowY), yMax = std::max(startY, nowY);
    int first = -1, last = -1;
    for (size_t r = 0; r < band.top.size(); r++) {
        if (band.top[r] < 0 || band.bottom[r] <= yMin || band.top[r] >= yMax) {
            continue;
        }
        if (column->rowToEntry[r] < 0) {
            continue;  // Headers and the "more items" row are not selected.
        }
        if (first < 0) {
            first = (int)r;
        }
        last = (int)r;
    }
    if (first != band.first || last != band.last) {
        bool wasBuilding = state->building;
        state->building = true;
        auto items = list.Items();
        // Between the first and last covered row, the rows that are items.
        auto selected = list.SelectedItems();
        if (!band.additive) {
            selected.Clear();
        } else {
            selected.Clear();
            for (int r : band.base) {
                list.SelectRange(winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(r, 1));
            }
        }
        if (first >= 0) {
            list.SelectRange(winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(
                first, (uint32_t)(last - first + 1)));
            // Headers inside the range are dropped again.
            for (int r = first; r <= last; r++) {
                if (column->rowToEntry[r] < 0 || items.GetAt(r).try_as<muxc::ListViewItem>().Visibility() !=
                                                      mux::Visibility::Visible) {
                    list.DeselectRange(winrt::Microsoft::UI::Xaml::Data::ItemIndexRange(r, 1));
                }
            }
        }
        state->building = wasBuilding;
        band.first = first;
        band.last = last;
    }

    // The rectangle on screen, in the strip's coordinates.
    try {
        double width = list.ActualWidth(), height = list.ActualHeight();
        double startX = band.start.X, nowX = at.X;
        double x0 = std::max(0.0, std::min(startX, nowX));
        double x1 = std::min(width, std::max(startX, nowX));
        double y0 = std::max(0.0, std::min(startY - offset, (double)at.Y));
        double y1 = std::min(height, std::max(startY - offset, (double)at.Y));
        auto origin = list.TransformToVisual(state->strip)
                          .TransformPoint(winrt::Windows::Foundation::Point{(float)x0, (float)y0});
        muxc::Canvas::SetLeft(state->bandBox, origin.X);
        muxc::Canvas::SetTop(state->bandBox, origin.Y);
        state->bandBox.Width(std::max(1.0, x1 - x0));
        state->bandBox.Height(std::max(1.0, y1 - y0));
        state->bandBox.Opacity(1);
    } catch (...) {
        LogHresult(L"drawing the selection rectangle");
    }
    return true;
}

void OnBandReleased(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        if (OnDragReleased(state)) {
            return;
        }
        if (state->band.pending || state->band.active || state->band.panning) {
            EndBand(state, state->band.active);
        }
    } catch (...) {
        state->band = {};
        state->drag = DragState();
        LogHresult(L"OnBandReleased");
    }
}

// A double-click on the empty space of a column goes up one folder: the column
// before it becomes the active one, or, in the first column, Explorer is taken
// to the parent folder, which is then added in front of the columns.
void OnEmptyDoubleTapped(muxc::ListView const& list,
                         mux::Input::DoubleTappedRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading || !g_doubleClickUp) {
        return;
    }
    try {
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        Column* column = state->columns[index].get();
        // A group header takes the double-click for itself.
        if (HeaderRowAt(column, list, args.GetPosition(list)) >= 0) {
            return;
        }
        // Up from the active column, whichever column was double-clicked.
        int from = state->active >= 0 && state->active < (int)state->columns.size()
                       ? state->active
                       : index;
        if (from > 0) {
            Column* previous = state->columns[from - 1].get();
            LiftStale(state, from - 1);
            SetActiveColumn(state, from - 1, false);
            ScheduleNavigation(state, previous->folder.get());
            state->focusColumn = previous;
            state->focusUntil = GetTickCount64() + 1500;
            previous->list.Focus(mux::FocusState::Pointer);
            // The active column goes to the middle of the view.
            CenterColumn(state, from - 1);
            state->cursorFixUntil = GetTickCount64() + 2500;
            Wh_Log(L"double-click on empty space: back to column %d", from);
            return;
        }
        column = state->columns[0].get();
        if (ILIsEmpty(column->folder.get())) {
            return;  // The top of the namespace.
        }
        Pidl parent(ILClone(column->folder.get()));
        ILRemoveLastID(parent.get());
        if (!parent || IsInControlPanel(parent.get())) {
            return;
        }
        NavigateExplorer(state, parent.get());
        Wh_Log(L"double-click on empty space: up from the first column");
    } catch (...) {
        LogHresult(L"OnEmptyDoubleTapped");
    }
}

// Opens or collapses the group of a header row.
// The end of an opening or closing group: closed rows are taken out of the
// layout, and every row gets its normal height back.
void FinishGroupAnimation(Column* column) {
    if (column->animGroup < 0) {
        return;
    }
    int group = column->animGroup;
    bool collapse = column->animCollapse;
    column->animGroup = -1;
    column->animDoneAt = 0;
    try {
        auto items = column->list.Items();
        for (size_t r = 0; r < column->rowGroup.size() && r < items.Size(); r++) {
            if (column->rowGroup[r] != group || column->rowToEntry[r] < 0) {
                continue;
            }
            if (auto item = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
                item.Visibility(collapse ? mux::Visibility::Collapsed
                                         : mux::Visibility::Visible);
                item.MinHeight(column->rowHeight);
                item.Height(column->rowHeight);
                item.Opacity(1.0);
            }
        }
    } catch (...) {
        LogHresult(L"FinishGroupAnimation");
    }
}

void ToggleGroup(WindowState* state, Column* column, int headerRow) {
    if (column->animGroup >= 0) {
        FinishGroupAnimation(column);  // The last one first.
    }
    if (headerRow < 0 || headerRow >= (int)column->rowGroup.size()) {
        return;
    }
    int group = column->rowGroup[headerRow];
    if (group < 0 || group >= (int)column->groupTitles.size()) {
        return;
    }
    const std::wstring title = column->groupTitles[group];
    auto at = std::find(column->collapsed.begin(), column->collapsed.end(), title);
    bool collapse = at == column->collapsed.end();
    if (collapse) {
        column->collapsed.push_back(title);
    } else {
        column->collapsed.erase(at);
    }
    auto items = column->list.Items();
    auto selected = column->list.SelectedItems();
    std::vector<muxc::ListViewItem> rows;
    bool wasBuilding = state->building;
    state->building = true;
    for (size_t r = 0; r < column->rowGroup.size() && r < items.Size(); r++) {
        if (column->rowGroup[r] != group || column->rowToEntry[r] < 0) {
            continue;
        }
        auto object = items.GetAt((uint32_t)r);
        if (auto item = object.try_as<muxc::ListViewItem>()) {
            rows.push_back(item);
        }
        uint32_t index = 0;
        if (collapse && selected.IndexOf(object, index)) {
            selected.RemoveAt(index);  // A hidden row can't stay selected.
        }
    }
    state->building = wasBuilding;

    // The rows slide shut or open and fade. A very large group just switches.
    namespace anim = mux::Media::Animation;
    bool animate = !rows.empty() && rows.size() <= 60;
    if (!animate) {
        for (auto& item : rows) {
            item.Visibility(collapse ? mux::Visibility::Collapsed
                                     : mux::Visibility::Visible);
        }
    } else {
        try {
            anim::CubicEase ease;
            ease.EasingMode(anim::EasingMode::EaseInOut);
            auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(200));
            anim::Storyboard board;
            double full = column->rowHeight;
            for (auto& item : rows) {
                // Where the row ends up, so the animation can stop on it.
                item.MinHeight(0);
                item.Visibility(mux::Visibility::Visible);
                item.Height(collapse ? 0.0 : full);
                item.Opacity(collapse ? 0.0 : 1.0);

                anim::DoubleAnimation height;
                height.From(collapse ? full : 0.0);
                height.To(collapse ? 0.0 : full);
                height.Duration(length);
                height.EasingFunction(ease);
                height.EnableDependentAnimation(true);
                height.FillBehavior(anim::FillBehavior::Stop);
                anim::Storyboard::SetTarget(height, item);
                anim::Storyboard::SetTargetProperty(height, L"Height");
                board.Children().Append(height);

                anim::DoubleAnimation fade;
                fade.From(collapse ? 1.0 : 0.0);
                fade.To(collapse ? 0.0 : 1.0);
                fade.Duration(length);
                fade.FillBehavior(anim::FillBehavior::Stop);
                anim::Storyboard::SetTarget(fade, item);
                anim::Storyboard::SetTargetProperty(fade, L"Opacity");
                board.Children().Append(fade);
            }
            board.Begin();
            column->animGroup = group;
            column->animCollapse = collapse;
            column->animDoneAt = GetTickCount64() + 260;
        } catch (...) {
            LogHresult(L"animating a group");
            for (auto& item : rows) {
                item.Visibility(collapse ? mux::Visibility::Collapsed
                                         : mux::Visibility::Visible);
                item.MinHeight(column->rowHeight);
                item.Height(column->rowHeight);
                item.Opacity(1.0);
            }
        }
    }

    if (headerRow < (int)column->headerIcons.size() && column->headerIcons[headerRow]) {
        try {
            auto turn = column->headerIcons[headerRow]
                            .RenderTransform()
                            .try_as<mux::Media::RotateTransform>();
            if (turn) {
                double from = turn.Angle();
                double to = collapse ? 0.0 : 90.0;
                turn.Angle(to);  // Where it stays.
                anim::CubicEase ease;
                ease.EasingMode(anim::EasingMode::EaseOut);
                anim::DoubleAnimation spin;
                spin.From(from);
                spin.To(to);
                spin.Duration(mux::DurationHelper::FromTimeSpan(
                    std::chrono::milliseconds(180)));
                spin.EasingFunction(ease);
                spin.FillBehavior(anim::FillBehavior::Stop);
                anim::Storyboard::SetTarget(spin, turn);
                anim::Storyboard::SetTargetProperty(spin, L"Angle");
                anim::Storyboard board;
                board.Children().Append(spin);
                board.Begin();
            }
        } catch (...) {
            LogHresult(L"turning a group chevron");
        }
    }
    Wh_Log(L"group \"%s\" %s", title.c_str(), collapse ? L"collapsed" : L"opened");
}

void OnColumnBackgroundTapped(muxc::ListView const& list,
                              mux::Input::TappedRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        // A click on a group header opens or collapses it.
        {
            int hit = ColumnIndexOfList(state, list);
            if (hit >= 0) {
                Column* column = state->columns[hit].get();
                int header = HeaderRowAt(column, list, args.GetPosition(list));
                if (header >= 0) {
                    ToggleGroup(state, column, header);
                    return;
                }
            }
        }
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        for (int i = 0; node && i < 20; i++) {
            if (auto row = node.try_as<muxc::ListViewItem>()) {
                // A row. The selection handler deals with a new selection. A
                // click on the name of the item that was already the only
                // selected one starts a rename, after the double-click time.
                int at = ColumnIndexOfList(state, list);
                if (at >= 0) {
                    Column* column = state->columns[at].get();
                    int rowIndex = list.IndexFromContainer(row);
                    int entry = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size()
                                    ? column->rowToEntry[rowIndex]
                                    : -1;
                    ULONGLONG now = GetTickCount64();
                    bool onName = false;
                    if (entry >= 0 && (size_t)entry < column->texts.size() &&
                        column->texts[entry]) {
                        auto hitText = args.OriginalSource().try_as<muxc::TextBlock>();
                        onName = hitText && hitText == column->texts[entry];
                    }
                    // A rename by click needs the column to have been the active one before this
                    // click: the first click on a selected item in another column only focuses
                    // the column, the second one (after the pause) renames.
                    if (entry >= 0 && onName && !state->rename && state->activeAtPress == at &&
                        now >= state->slowRenameBlockUntil &&
                        now - column->selectionChangedAt > 350 &&
                        !(GetKeyState(VK_CONTROL) & 0x8000) &&
                        !(GetKeyState(VK_SHIFT) & 0x8000)) {
                        std::vector<size_t> picked = SelectedEntries(column, list);
                        if (picked.size() == 1 && picked[0] == (size_t)entry) {
                            state->slowRenameAt = now + GetDoubleClickTime() + 120;
                            state->slowRenameColumn = column;
                            state->slowRenameEntry = (size_t)entry;
                        }
                    }
                }
                return;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        Column* column = state->columns[index].get();
        LiftStale(state, index);
        state->building = true;
        list.SelectedItems().Clear();
        state->building = false;
        column->selectedChild.reset();
        state->selectFolder.reset();
        state->selectChild.reset();
        state->selectExtra.clear();
        SetActiveColumn(state, index);
        ScheduleNavigation(state, column->folder.get());
        state->focusColumn = column;
        state->focusUntil = GetTickCount64() + 1500;
        list.Focus(mux::FocusState::Pointer);
        if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
            ClearExplorerSelection(browser);
        }
        Wh_Log(L"column %d chosen by a click on its empty space", index + 1);
    } catch (...) {
        state->building = false;
        LogHresult(L"OnColumnBackgroundTapped");
    }
}

// Selects the next item whose name starts with what was just typed. Typing
// the same letter again steps through the items with that letter.
void TypeToJump(Column* column, muxc::ListView const& list, WindowState* state,
                wchar_t ch) {
    ULONGLONG now = GetTickCount64();
    if (now - state->typedAt > 1000) {
        state->typed.clear();
    }
    state->typedAt = now;
    state->typed += ch;

    std::wstring needle = state->typed;
    bool repeated = needle.size() > 1 &&
                    needle.find_first_not_of(needle[0]) == std::wstring::npos;
    int current = list.SelectedIndex();
    int start;
    if (needle.size() == 1 || repeated) {
        needle = needle.substr(0, 1);
        start = current < 0 ? 0 : current + 1;
    } else {
        start = current < 0 ? 0 : current;
    }
    int rows = (int)column->rowToEntry.size();
    for (int i = 0; i < rows; i++) {
        int row = (start + i) % rows;
        int entry = column->rowToEntry[row];
        if (entry < 0) {
            continue;
        }
        if (auto item = list.Items().GetAt(row).try_as<muxc::ListViewItem>()) {
            if (item.Visibility() == mux::Visibility::Collapsed) {
                continue;
            }
        }
        if (StrCmpNIW(column->entries[entry].name.c_str(), needle.c_str(),
                      (int)needle.size()) == 0) {
            list.SelectedIndex(row);
            list.ScrollIntoView(list.Items().GetAt(row));
            return;
        }
    }
}

// Right or Enter on a folder enters its column, Left goes back to the parent
// column, Enter on a file opens it.
void OnColumnKey(muxc::ListView const& list,
                 mux::Input::KeyRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    if (state->rename) {
        return;  // Typing into the rename box.
    }
    state->slowRenameAt = 0;
    try {
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        auto key = args.Key();
        using winrt::Windows::System::VirtualKey;
        if (key != VirtualKey::Control && key != VirtualKey::Shift && key != VirtualKey::Menu &&
            key != VirtualKey::LeftControl && key != VirtualKey::RightControl &&
            key != VirtualKey::LeftShift && key != VirtualKey::RightShift &&
            key != VirtualKey::LeftMenu && key != VirtualKey::RightMenu) {
            HoverOff(state);
        }
        Column* column = state->columns[index].get();
        int row = list.SelectedIndex();
        int entry = row >= 0 && row < (int)column->rowToEntry.size()
                        ? column->rowToEntry[row]
                        : -1;
        bool onFolder = entry >= 0 && column->entries[entry].folder;

        // Standard file shortcuts, on the selected items.
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        std::vector<size_t> picked = SelectedEntries(column, list);
        auto keep = [&]() {
            state->focusColumn = column;
            state->focusUntil = GetTickCount64() + 1500;
            args.Handled(true);
        };
        // The keyboard on a group header, or on its way there. A setting, off by
        // default. Turned off while a header has it, the header is let go.
        if (!g_headerKeyboard && column->focusHeader >= 0) {
            SetHeaderFocus(column, list, -1);
        }
        if (g_headerKeyboard) {
            int header = column->focusHeader;
            if (header >= 0 && header < (int)column->rowGroup.size()) {
                int group = column->rowGroup[header];
                bool closed = group >= 0 && group < (int)column->groupTitles.size() &&
                              std::find(column->collapsed.begin(), column->collapsed.end(),
                                        column->groupTitles[group]) != column->collapsed.end();
                auto goTo = [&](int target) {
                    if (target < 0) {
                        return;
                    }
                    if (column->rowToEntry[target] < 0) {
                        SetHeaderFocus(column, list, target);
                    } else {
                        SetHeaderFocus(column, list, -1);
                        list.SelectedIndex(target);
                        list.ScrollIntoView(list.Items().GetAt((uint32_t)target));
                    }
                };
                if (key == VirtualKey::Left && !closed) {
                    ToggleGroup(state, column, header);
                    keep();
                    return;
                }
                if (key == VirtualKey::Right) {
                    if (closed) {
                        ToggleGroup(state, column, header);
                    } else {
                        goTo(NextHeaderOrItemRow(column, list, header, 1));
                    }
                    keep();
                    return;
                }
                if (key == VirtualKey::Enter || key == VirtualKey::Space) {
                    ToggleGroup(state, column, header);
                    keep();
                    return;
                }
                if (key == VirtualKey::Down || key == VirtualKey::Up) {
                    goTo(NextHeaderOrItemRow(column, list, header,
                                             key == VirtualKey::Down ? 1 : -1));
                    keep();
                    return;
                }
                // Any other key leaves the header and does its usual job.
                SetHeaderFocus(column, list, -1);
            } else if (key == VirtualKey::Up && !ctrl && !alt &&
                       !(GetKeyState(VK_SHIFT) & 0x8000) && picked.size() == 1 && row > 0 &&
                       column->rowToEntry[row - 1] < 0 && column->rowGroup[row - 1] >= 0) {
                // Up from the first item of a group lands on its header.
                bool wasBuilding = state->building;
                state->building = true;
                list.SelectedItems().Clear();
                state->building = wasBuilding;
                SetHeaderFocus(column, list, row - 1);
                keep();
                return;
            }
        }
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // Everyday keys.
        if (key == VirtualKey::Escape && state->drag.active) {
            CancelDrag(state);
            args.Handled(true);
            return;
        }
        // Down in a column where nothing is selected (a column just given the keyboard from the
        // sidebar, or cleared with Escape) selects the first item, not the second.
        if (key == VirtualKey::Down && !ctrl && !alt && !shift && picked.empty()) {
            for (size_t r = 0; r < column->rowToEntry.size() && r < list.Items().Size(); r++) {
                if (column->rowToEntry[r] < 0) {
                    continue;
                }
                auto item = list.Items().GetAt((uint32_t)r).try_as<muxc::ListViewItem>();
                if (item && item.Visibility() == mux::Visibility::Collapsed) {
                    continue;
                }
                list.SelectedIndex((int)r);
                list.ScrollIntoView(list.Items().GetAt((uint32_t)r));
                keep();
                return;
            }
        }
        if (key == VirtualKey::F5 && !ctrl && !alt) {
            ReloadAll(state);
            Wh_Log(L"F5: the columns are read again");
            keep();
            return;
        }
        if (key == VirtualKey::Escape && !ctrl && !alt && !picked.empty()) {
            list.SelectedItems().Clear();
            keep();
            return;
        }
        if (key == VirtualKey::Back && !ctrl && !alt && !shift) {
            // Up one column; in the first one, Explorer goes to the parent folder.
            if (index > 0) {
                SetActiveColumn(state, index - 1);
                state->columns[index - 1]->list.Focus(mux::FocusState::Keyboard);
                state->focusColumn = state->columns[index - 1].get();
                state->focusUntil = GetTickCount64() + 1500;
                args.Handled(true);
            } else if (!ILIsEmpty(column->folder.get())) {
                Pidl parent(ILClone(column->folder.get()));
                ILRemoveLastID(parent.get());
                if (parent && !IsInControlPanel(parent.get())) {
                    NavigateExplorer(state, parent.get());
                }
                args.Handled(true);
            }
            return;
        }
        if (((key == VirtualKey::Application) || (key == VirtualKey::F10 && shift)) &&
            !picked.empty()) {
            // The flyout of the selected item, below its row.
            auto selectedRow = list.Items().GetAt((uint32_t)row).try_as<muxc::ListViewItem>();
            winrt::Windows::Foundation::Point where{40, 40};
            if (selectedRow) {
                where = selectedRow.TransformToVisual(list).TransformPoint(
                    winrt::Windows::Foundation::Point{40, (float)selectedRow.ActualHeight()});
            }
            ShowItemFlyout(state, column, list, picked, where);
            args.Handled(true);
            return;
        }
        if (ctrl && shift && !alt && key == VirtualKey::N) {
            CreateNewFolder(state, column);
            keep();
            return;
        }
        if (ctrl && alt && !shift && key == VirtualKey::N && !picked.empty()) {
            NewFolderWithSelection(state, column, picked);
            keep();
            return;
        }
        if (ctrl && shift && !alt && key == VirtualKey::C) {
            if (picked.empty()) {
                // Nothing selected: the column's own folder.
                wchar_t folder[MAX_PATH] = L"";
                if (SHGetPathFromIDListW(column->folder.get(), folder) && folder[0]) {
                    std::wstring text = std::wstring(L"\"") + folder + L"\"";
                    if (OpenClipboard(state->topLevel)) {
                        EmptyClipboard();
                        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
                        if (memory) {
                            if (wchar_t* target = (wchar_t*)GlobalLock(memory)) {
                                memcpy(target, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
                                GlobalUnlock(memory);
                                if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                                    GlobalFree(memory);
                                }
                            } else {
                                GlobalFree(memory);
                            }
                        }
                        CloseClipboard();
                        Wh_Log(L"copied the column's folder path as text");
                    }
                }
            } else {
                CopyAsPath(state, column, picked);
            }
            keep();
            return;
        }
        if (ctrl && !alt && (key == VirtualKey::Y || (shift && key == VirtualKey::Z)) &&
            RedoLast(state)) {
            ReloadAll(state);
            keep();
            return;
        }
        if (ctrl && !alt && (key == VirtualKey::C || key == VirtualKey::X)) {
            if (!picked.empty()) {
                CopyOrCut(state, column, picked, key == VirtualKey::X);
            }
            keep();
            return;
        }
        if (ctrl && !alt && key == VirtualKey::V) {
            // A folder selected in the column takes the paste. Otherwise it
            // goes into the column's own folder.
            Pidl target(ILClone(column->folder.get()));
            if (picked.size() == 1 && column->entries[picked[0]].folder) {
                auto& into = column->entries[picked[0]];
                target.reset(into.linkTarget
                                 ? ILClone(into.linkTarget.get())
                                 : ILCombine(column->folder.get(), into.child.get()));
            }
            HRESULT hr = target ? PasteIntoFolder(state, target.get()) : E_FAIL;
            Wh_Log(L"Ctrl+V into %s: hr=0x%08lX", PathOf(target.get()).c_str(), hr);
            keep();
            return;
        }
        if (key == VirtualKey::Delete && !picked.empty()) {
            std::vector<std::wstring> before;
            for (size_t entry : picked) {
                before.push_back(FilePathOf(column->folder.get(),
                                            column->entries[entry].child.get()));
            }
            bool permanent = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HRESULT hr = InvokeVerbOnItems(state, column, picked, "delete");
            Wh_Log(L"Delete %d item(s): hr=0x%08lX", (int)picked.size(), hr);
            if (SUCCEEDED(hr) && !permanent) {
                RememberDeletion(state, before);
            }
            HideDeletedRows(column, picked);
            ReloadAll(state);
            keep();
            return;
        }
        if (key == VirtualKey::F2 && !picked.empty()) {
            BeginMultiRename(state, column, picked);
            args.Handled(true);
            return;
        }
        if (alt && key == VirtualKey::Enter && !picked.empty()) {
            InvokeVerbOnItems(state, column, picked, "properties");
            args.Handled(true);
            return;
        }
        if (ctrl && !alt && !(shift && key == VirtualKey::Z) &&
            (key == VirtualKey::Z || key == VirtualKey::Y)) {
            // The mod's own undo first: deletes and renames done here. Anything
            // else is left to Explorer's view.
            if (key == VirtualKey::Z && UndoLast(state)) {
                ReloadAll(state);
            } else if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
                IShellView* view = nullptr;
                if (SUCCEEDED(browser->QueryActiveShellView(&view)) && view) {
                    MSG msg = {};
                    msg.hwnd = state->topLevel;
                    msg.message = WM_KEYDOWN;
                    msg.wParam = (WPARAM)(key == VirtualKey::Z ? 'Z' : 'Y');
                    msg.lParam = 1;
                    HRESULT hr = view->TranslateAccelerator(&msg);
                    Wh_Log(L"%s passed to Explorer's view: hr=0x%08lX",
                        key == VirtualKey::Z ? L"Undo" : L"Redo", hr);
                    view->Release();
                }
            }
            keep();
            return;
        }

        // Letters and digits jump to a name. Ctrl and Alt shortcuts are left
        // alone.
        int code = (int)key;
        wchar_t typedChar = 0;
        {
            // The character this key makes on the layout in use (an accented letter, a digit that needs
            // Shift on AZERTY), not the virtual-key code, which is the same for every layout.
            BYTE keyboard[256] = {};
            wchar_t made[4] = {};
            if (GetKeyboardState(keyboard) &&
                ToUnicodeEx((UINT)code, args.KeyStatus().ScanCode, keyboard, made, 4, 4, GetKeyboardLayout(0)) == 1 &&
                (iswalnum(made[0]) || made[0] == L'_')) {
                typedChar = made[0];
            }
        }
        if (!typedChar && code >= 0x60 && code <= 0x69) {
            typedChar = (wchar_t)(L'0' + (code - 0x60));
        }
        if (typedChar && !(GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_MENU) & 0x8000)) {
            TypeToJump(column, list, state, typedChar);
            keep();
            return;
        }

        if (key == VirtualKey::Left) {
            if (index > 0) {
                SetActiveColumn(state, index - 1);
                state->columns[index - 1]->list.Focus(mux::FocusState::Keyboard);
                state->focusColumn = state->columns[index - 1].get();
                state->focusUntil = GetTickCount64() + 1500;
                args.Handled(true);
            }
            return;
        }
        if (key == VirtualKey::Enter && entry >= 0 && !onFolder) {
            OnRowDoubleTapped(list);
            args.Handled(true);
            return;
        }
        if ((key == VirtualKey::Right || key == VirtualKey::Enter) && onFolder &&
            index + 1 < (int)state->columns.size()) {
            Column* child = state->columns[index + 1].get();
            if (child->list.SelectedIndex() < 0) {
                // Land on the first real row, past any group header.
                for (size_t r = 0; r < child->rowToEntry.size(); r++) {
                    if (child->rowToEntry[r] >= 0) {
                        child->list.SelectedIndex((int)r);
                        break;
                    }
                }
            }
            SetActiveColumn(state, index + 1);
            child->list.Focus(mux::FocusState::Keyboard);
            state->focusColumn = child;
            state->focusUntil = GetTickCount64() + 1500;
            args.Handled(true);
        }
    } catch (...) {
        LogHresult(L"OnColumnKey");
    }
}

void ChooseColumns(WindowState* state);
void LeaveColumns(WindowState* state);
void RegisterPanelDropTarget(WindowState* state);
void RevokePanelDropTarget(WindowState* state);
void DragAutoScroll(WindowState* state);
void HoverTick(WindowState* state);
void SpringLoadTick(WindowState* state);
void PillHide(Column* column);
void UpdateLastEdge(WindowState* state);
void SyncViewButtonIcon(WindowState* state);

void HidePanel(WindowState* state, const wchar_t* why) {
    if (!state->source) {
        state->on = false;
        return;
    }
    Wh_Log(L"panel off: %s", why);
    state->on = false;
    RestoreViewButtonIcon(state);
    HoverOn(state);
    g_panelWindow = nullptr;
    RevokePanelDropTarget(state);
    CancelRename(state);
    CloseContextMenu(state);
    try {
        if (state->fadeTimer) {
            state->fadeTimer.Stop();  // A removal waiting behind a fade must not run after the panel or the mod is gone.
            state->fadeTimer = nullptr;
        }
        if (state->timer) {
            state->timer.Stop();
            state->timer.Tick(state->tickToken);
            state->timer = nullptr;
        }
        state->building = true;
        RemoveColumnsFrom(state, 0);
        state->building = false;
        if (state->rootGrid && state->wheelHandler) {
            state->rootGrid.RemoveHandler(
                mux::UIElement::PointerWheelChangedEvent(),
                state->wheelHandler);
        }
        if (state->strip && state->stripWheelHandler) {
            state->strip.RemoveHandler(
                mux::UIElement::PointerWheelChangedEvent(),
                state->stripWheelHandler);
        }
        state->stripWheelHandler = nullptr;
        state->rootGrid = nullptr;
        state->wheelHandler = nullptr;
        state->source.Content(nullptr);
        state->source.Close();
    } catch (...) {
        LogHresult(L"HidePanel");
    }
    ReleaseWorker(state->worker);
    state->worker = nullptr;
    state->source = nullptr;
    state->bridge = nullptr;
    state->scroller = nullptr;
    state->strip = nullptr;
    state->pill = nullptr;
    state->overlay = nullptr;
    state->bandBox = nullptr;
    state->band = {};
    state->lastSeen.reset();
    state->expected.reset();
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    state->columnsTabs.clear();
    state->seenTabs.clear();
    state->tabSnapshots.clear();
    state->currentTab = nullptr;
    state->softHidden = false;
    if (state->notifyWindow) {
        DestroyWindow(state->notifyWindow);
        state->notifyWindow = nullptr;
    }
    state->reloadDue = 0;
    state->active = -1;
    state->lastFileList = nullptr;
    state->specCheckAt = 0;
    state->dragColumn = -1;
    state->lastRect = {};
}

// The Explorer window was closed while its panel was showing. Windows has
// already destroyed the panel's window along with it, and closing or
// releasing the panel's XAML objects at that point crashes Explorer. So they
// are let go without being released: a small leak, once per closed window.

void AbandonWindow(WindowState* state) {
    Wh_Log(L"window closed with the panel on, letting its objects go");
    AbandonDetails(state);
    state->on = false;
    (void)state->context.release();  // Never destroyed, like the columns.
    (void)state->rename.release();
    g_renaming = false;
    g_panelWindow = nullptr;
    // The window's drop registration went with the window; the target is never freed, like the columns. A later window of this
    // thread, which reuses the state, registers its own.
    state->panelDrop = nullptr;
    state->panelDropWindow = nullptr;
    try {
        if (state->timer) {
            state->timer.Stop();
            state->timer.Tick(state->tickToken);
        }
    } catch (...) {
    }
    ReleaseWorker(state->worker);
    state->worker = nullptr;
    for (auto& column : state->columns) {
        if (column->notifyId) {
            SHChangeNotifyDeregister(column->notifyId);
            column->notifyId = 0;
        }
        if (column->cancel) {
            *column->cancel = true;
        }
        column.release();  // Never destroyed, on purpose.
    }
    state->columns.clear();
    if (state->notifyWindow) {
        DestroyWindow(state->notifyWindow);
        state->notifyWindow = nullptr;
    }
    for (auto& sibling : state->siblings) {
        (void)winrt::detach_abi(sibling.item);
    }
    state->siblings.clear();
    try {
        if (state->entryTimer) {
            state->entryTimer.Stop();
            state->entryTimer.Tick(state->entryToken);
        }
    } catch (...) {
    }
    (void)winrt::detach_abi(state->entryTimer);
    (void)winrt::detach_abi(state->xamlRoot);
    state->entryStopped = false;
    state->entryTries = 0;
    state->restoreSettle = 0;
    (void)winrt::detach_abi(state->timer);
    (void)winrt::detach_abi(state->source);
    (void)winrt::detach_abi(state->bridge);
    (void)winrt::detach_abi(state->scroller);
    (void)winrt::detach_abi(state->rootGrid);
    (void)winrt::detach_abi(state->wheelHandler);
    (void)winrt::detach_abi(state->stripWheelHandler);
    (void)winrt::detach_abi(state->strip);
    (void)winrt::detach_abi(state->pill);
    (void)winrt::detach_abi(state->overlay);
    (void)winrt::detach_abi(state->bandBox);
    (void)winrt::detach_abi(state->menuItem);
    (void)winrt::detach_abi(state->menuItems);
    state->topLevel = nullptr;
    state->lastSeen.reset();
    state->expected.reset();
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    state->lastFileList = nullptr;
    state->reloadDue = 0;
    state->specCheckAt = 0;
    state->active = -1;
    state->dragColumn = -1;
    state->lastRect = {};
}

// Explorer's own view moved to `current` without this mod asking. Keep the
// columns showing the path from the first column down to that folder.
// Explorer moved to a column's folder on its own (the toolbar's Up, Back). That
// column's selected item is selected in Explorer's view too, so the toolbar and
// the panes still act on what the column shows as selected.
void MirrorColumnSelection(WindowState* state, Column* column) {
    if (column && column->selectedChild) {
        state->selectFolder.reset(ILClone(column->folder.get()));
        state->selectChild.reset(ILClone(column->selectedChild.get()));
        state->selectExtra.clear();
        state->selectTries = 0;
    }
}

void SyncToFolder(WindowState* state, Pidl const& current) {
    state->pendingNav.reset();
    state->selectFolder.reset();
    state->selectChild.reset();

    // Already a column: only the active column moves.
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (ILIsEqual(current.get(), state->columns[i]->folder.get())) {
            Wh_Log(L"Explorer moved to column %d", (int)i + 1);
            LiftStale(state, (int)i);
            // One scroll only, to the middle: a second one started by
            // SetActiveColumn would cut the first short and make it stutter.
            SetActiveColumn(state, (int)i, false);
            MirrorColumnSelection(state, state->columns[i].get());
            // Up and Back in the toolbar: the column Explorer went to is put
            // in the middle of the view, like a double-click up does.
            CenterColumn(state, (int)i);
            return;
        }
    }
    if (!state->columns.empty()) {
        PCIDLIST_ABSOLUTE root = state->columns[0]->folder.get();

        // Below the first column: open the chain of columns down to it.
        if (ILIsParent(root, current.get(), FALSE)) {
            int deepest = 0;
            for (int i = (int)state->columns.size() - 1; i >= 0; i--) {
                if (ILIsParent(state->columns[i]->folder.get(), current.get(),
                               FALSE)) {
                    deepest = i;
                    break;
                }
            }
            state->building = true;
            RemoveColumnsFrom(state, deepest + 1);
            state->building = false;
            PCUIDLIST_RELATIVE rest =
                ILFindChild((PIDLIST_ABSOLUTE)state->columns[deepest]->folder.get(),
                            (PIDLIST_ABSOLUTE)current.get());
            Pidl walk(ILClone(state->columns[deepest]->folder.get()));
            int parent = deepest;
            while (rest && !ILIsEmpty(rest) && state->columns.size() < 40) {
                Pidl step(ILCloneFirst(rest));
                if (!step) {
                    break;
                }
                SelectChildInColumn(state, state->columns[parent].get(),
                                    step.get());
                Pidl next(ILCombine(walk.get(), step.get()));
                if (!next) {
                    break;
                }
                walk.reset(ILClone(next.get()));
                AddColumn(state, std::move(next));
                parent++;
                rest = ILGetNext(rest);
            }
            Wh_Log(L"Explorer moved below the first column, %d columns now",
                (int)state->columns.size());
            SetActiveColumn(state, (int)state->columns.size() - 1);
            return;
        }

        // The parent of the first column, which is what Up does: add it in
        // front, with the old first column selected in it.
        if (ILIsParent(current.get(), root, TRUE)) {
            Pidl childId(ILClone(ILFindLastID(root)));
            AddColumn(state, Pidl(ILClone(current.get())), true);
            state->columns[0]->selectedChild = std::move(childId);
            Wh_Log(L"Explorer moved up, parent added as first column");
            SetActiveColumn(state, 0);
            MirrorColumnSelection(state, state->columns[0].get());
            try {
                state->scroller.ChangeView(0.0, nullptr, nullptr);
            } catch (...) {
            }
            return;
        }
    }
    if (RememberedColumns(PathOf(current.get())) == 0) {
        Wh_Log(L"Explorer went to a folder that was left in its own layout, Columns off for the tab");
        LeaveColumns(state);
        return;
    }
    Wh_Log(L"Explorer navigated elsewhere (\"%s\"), rebuilding",
        PathOf(current.get()).c_str());
    RebuildFromExplorer(state);
    SetActiveColumn(state, 0);
    FocusColumnsAfterSidebar(state);
}

// What Explorer's own view shows changed.
void OnExplorerFolderChanged(WindowState* state, Pidl const& current) {
    state->lastSeen.reset(ILClone(current.get()));
    state->specSyncDue = GetTickCount64() + 700;
    // A click on a flat entry of the navigation pane starts the columns over
    // at that folder, even when it is inside the current chain.
    if (g_panePidl) {
        bool arrived = GetTickCount64() - g_paneAt < 3000 &&
                       ILIsEqual(current.get(), g_panePidl);
        CoTaskMemFree(g_panePidl);
        g_panePidl = nullptr;
        if (arrived && g_sidebarRoot && IsFlatSidebarTarget(current.get())) {
            Wh_Log(L"sidebar click: \"%s\" becomes the first column",
                PathOf(current.get()).c_str());
            state->expected.reset();
            if (RememberedColumns(PathOf(current.get())) == 0) {
                Wh_Log(L"this folder was left in Explorer's own layout, Columns off for the tab");
                LeaveColumns(state);
                return;
            }
            RebuildFromExplorer(state);
            SetActiveColumn(state, 0);
            FocusColumnsAfterSidebar(state);
            return;
        }
    }
    if (state->expected) {
        bool same = ILIsEqual(current.get(), state->expected.get());
        if (!same) {
            // A folder opened from Home or Quick access is a link: Explorer reports the folder's own
            // id, which names the same place on disk.
            wchar_t wanted[MAX_PATH] = L"", got[MAX_PATH] = L"";
            same = SHGetPathFromIDListW(state->expected.get(), wanted) && SHGetPathFromIDListW(current.get(), got) && wanted[0] &&
                   _wcsicmp(wanted, got) == 0;
        }
        if (same) {
            state->expected.reset();
            return;
        }
    }
    state->expected.reset();
    SyncToFolder(state, current);
}

bool LastLayoutWasColumns();

bool TabListHas(std::vector<HWND> const& tabs, HWND tab) {
    return std::find(tabs.begin(), tabs.end(), tab) != tabs.end();
}

void TabListRemove(std::vector<HWND>& tabs, HWND tab) {
    tabs.erase(std::remove(tabs.begin(), tabs.end(), tab), tabs.end());
}

// Tabs that were closed are forgotten.
void PruneTabs(WindowState* state) {
    for (auto* list : {&state->columnsTabs, &state->seenTabs}) {
        for (size_t i = 0; i < list->size();) {
            if (!IsWindow((*list)[i])) {
                list->erase(list->begin() + i);
            } else {
                i++;
            }
        }
    }
}

// The panel is one per window. It is hidden, not torn down, while a tab that
// is not in Columns is showing.
void SoftHidePanel(WindowState* state) {
    if (!state->bridge || state->softHidden) {
        return;
    }
    CancelRename(state);
    CloseContextMenu(state);
    try {
        state->bridge.Hide();
        if (state->menuItem) {
            state->menuItem.IsChecked(false);
        }
    } catch (...) {
        LogHresult(L"SoftHidePanel");
    }
    state->softHidden = true;
    g_panelWindow = nullptr;
    {
        IShellBrowser* browser = BrowserOf(state->topLevel);
        Pidl where = browser ? CurrentFolder(browser) : Pidl();
        state->hiddenFolder = where ? PathOf(where.get()) : std::wstring();
    }
    Wh_Log(L"panel hidden: this tab is not in Columns");
}

void SoftShowPanel(WindowState* state) {
    if (!state->bridge || !state->softHidden) {
        return;
    }
    try {
        state->bridge.Show();
        HWND bridgeWindow = (HWND)state->bridge.WindowId().Value;
        SetWindowPos(bridgeWindow, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        g_panelWindow = bridgeWindow;
        if (state->menuItem) {
            state->menuItem.IsChecked(true);
        }
    } catch (...) {
        LogHresult(L"SoftShowPanel");
    }
    state->softHidden = false;
    state->lastRect = {};  // The tick places it again.
    Wh_Log(L"panel shown again: this tab is in Columns");
}

// The columns of the tab that is leaving are kept, so they come back with it.
void SaveTabSnapshot(WindowState* state, HWND tab) {
    // Tabs that were closed are forgotten.
    for (size_t i = 0; i < state->tabSnapshots.size();) {
        if (!IsWindow(state->tabSnapshots[i].tab) || state->tabSnapshots[i].tab == tab) {
            state->tabSnapshots.erase(state->tabSnapshots.begin() + i);
        } else {
            i++;
        }
    }
    if (!tab || !IsWindow(tab) || state->columns.empty()) {
        return;
    }
    TabSnapshot snapshot;
    snapshot.tab = tab;
    snapshot.active = state->active;
    for (auto& column : state->columns) {
        TabColumnSnapshot saved;
        saved.folder.reset(ILClone(column->folder.get()));
        if (column->selectedChild) {
            saved.selected.reset(ILClone(column->selectedChild.get()));
        }
        saved.width = column->list ? column->list.Width() : 0;
        saved.userSized = column->userSized;
        {
            auto scroller = column->list ? FindListScroller(column->list, 0) : nullptr;
            saved.scroll = scroller ? scroller.VerticalOffset() : 0;
        }
        snapshot.columns.push_back(std::move(saved));
    }
    Wh_Log(L"tab left: %d column(s) kept", (int)snapshot.columns.size());
    state->tabSnapshots.push_back(std::move(snapshot));
}

// Puts the columns of a tab back, when Explorer's view of that tab is still
// on the folder its active column showed. Returns false if there is nothing
// to put back, so the caller builds the columns from Explorer's folder.
bool RestoreTabSnapshot(WindowState* state, HWND tab, IShellBrowser* browser,
                        Pidl const& current) {
    for (size_t i = 0; i < state->tabSnapshots.size(); i++) {
        if (state->tabSnapshots[i].tab != tab) {
            continue;
        }
        TabSnapshot snapshot = std::move(state->tabSnapshots[i]);
        state->tabSnapshots.erase(state->tabSnapshots.begin() + i);
        if (snapshot.columns.empty() || snapshot.active < 0 ||
            snapshot.active >= (int)snapshot.columns.size() ||
            !snapshot.columns[0].folder || FolderGone(snapshot.columns[0].folder.get())) {
            return false;
        }
        auto& activeFolder = snapshot.columns[snapshot.active].folder;
        if (!ILIsEqual(activeFolder.get(), current.get()) &&
            _wcsicmp(PathOf(activeFolder.get()).c_str(), PathOf(current.get()).c_str()) != 0) {
            Wh_Log(L"tab shown again, but its folder changed: the columns start over");
            return false;
        }
        state->lastSeen.reset(ILClone(current.get()));
        state->expected.reset();
        state->pendingNav.reset();
        state->selectFolder.reset();
        state->selectChild.reset();
        {
            SortSpec fresh;
            if (ReadSortSpec(browser, &fresh)) {
                state->spec = fresh;
            }
        }
        if (state->swapOld) {
            CompleteSwap(state);
        }
        state->building = true;
        RemoveColumnsFrom(state, 0);
        state->building = false;
        for (auto& saved : snapshot.columns) {
            AddColumn(state, std::move(saved.folder));
            Column* column = state->columns.back().get();
            if (saved.selected) {
                column->selectedChild = std::move(saved.selected);
            }
            column->restoreScroll = saved.scroll;
            if (saved.userSized && saved.width > 0 && column->list) {
                column->list.Width(saved.width);
                column->userSized = true;
            }
        }
        SetActiveColumn(state, snapshot.active);
        Wh_Log(L"tab shown again: %d column(s) put back", (int)state->columns.size());
        return true;
    }
    return false;
}

// Another tab came to the front. Returns true when the tick has nothing more to
// do for this round.
bool HandleTabSwitch(WindowState* state, HWND tab, HWND fileList) {
    HWND old = state->currentTab;
    state->currentTab = tab;
    // What was pending belongs to the tab that left: a navigation after a
    // pause, a selection to mirror, a folder Explorer was asked to open.
    state->pendingNav.reset();
    state->expected.reset();
    state->selectFolder.reset();
    state->selectChild.reset();
    state->selectExtra.clear();
    state->focusColumn = nullptr;
    PruneTabs(state);
    bool known = TabListHas(state->seenTabs, tab);
    if (!known) {
        state->seenTabs.push_back(tab);
        // A tab seen for the first time takes the layout chosen last.
        if (g_restore && LastLayoutWasColumns()) {
            state->columnsTabs.push_back(tab);
        }
    }
    bool newIsColumns = TabListHas(state->columnsTabs, tab);
    Wh_Log(L"tab switch: %p to %p (%s)", old, tab, newIsColumns ? L"columns" : L"not columns");
    if (old && TabListHas(state->columnsTabs, old) && !state->softHidden) {
        SaveTabSnapshot(state, old);
    }
    if (!newIsColumns) {
        SoftHidePanel(state);
        return true;
    }
    bool wasHidden = state->softHidden;
    if (wasHidden) {
        SoftShowPanel(state);
    }
    IShellBrowser* browser = BrowserOf(state->topLevel);
    Pidl shown = browser ? CurrentFolder(browser) : Pidl();
    if (shown && RestoreTabSnapshot(state, tab, browser, shown)) {
        state->lastFileList = fileList;
        return true;
    }
    // No columns kept for this tab: start from the folder it shows. The old
    // columns are cleared first, since a rebuild would keep them on screen
    // until the new ones are in, and they belong to another tab.
    state->building = true;
    RemoveColumnsFrom(state, 0);
    state->building = false;
    RebuildFromExplorer(state);
    SetActiveColumn(state, 0);
    state->lastFileList = fileList;
    return true;
}

// A toolbar command that changes the selection of Explorer's own view, Select
// all, Select none, Invert selection or a Paste that selects what it made, acts
// on the view under the panel. Some time after a command, the active column
// takes that selection when Explorer shows the column's folder and nothing else
// is changing the selection.
void SyncSelectionFromExplorer(WindowState* state) {
    if (!state->on || state->active < 0 || state->active >= (int)state->columns.size()) {
        return;
    }
    Column* column = state->columns[state->active].get();
    if (!column->list || !column->filled || state->selectChild || state->pendingNav || state->rename ||
        state->expected || GetTickCount64() - column->selectionChangedAt < 1000) {
        return;
    }
    IShellBrowser* browser = BrowserOf(state->topLevel);
    if (!browser) {
        return;
    }
    Pidl current = CurrentFolder(browser);
    if (!current || !ILIsEqual(current.get(), column->folder.get())) {
        return;
    }
    auto selection = ExplorerSelection(browser);
    std::vector<size_t> wanted;
    if (!column->entries.empty() && selection.size() == column->entries.size()) {
        for (size_t i = 0; i < column->entries.size(); i++) {
            wanted.push_back(i);
        }
    } else if (!selection.empty()) {
        // By the bytes of the item id first (one pass over each list), so an inverted selection in a big
        // folder does not make the window wait; an item whose id differs in bytes is looked for the slow way.
        auto bytesOf = [](PCUITEMID_CHILD id) { return std::string((const char*)id, ILGetSize(id)); };
        std::unordered_map<std::string, size_t> byBytes;
        byBytes.reserve(column->entries.size());
        for (size_t i = 0; i < column->entries.size(); i++) {
            byBytes.emplace(bytesOf(column->entries[i].child.get()), i);
        }
        size_t slowWork = 0;
        for (auto& child : selection) {
            auto found = byBytes.find(bytesOf(child.get()));
            if (found != byBytes.end()) {
                wanted.push_back(found->second);
                continue;
            }
            slowWork += column->entries.size();
            if (slowWork > 4000000) {
                return;  // Too much to match here.
            }
            for (size_t i = 0; i < column->entries.size(); i++) {
                if (ILIsEqual(column->entries[i].child.get(), child.get())) {
                    wanted.push_back(i);
                    break;
                }
            }
        }
        std::sort(wanted.begin(), wanted.end());
    }
    auto shown = SelectedEntries(column, column->list);
    std::sort(shown.begin(), shown.end());
    // A group header can be selected too, and it is no entry: the same entries with a header selected is
    // not the same selection (Invert selection, or Select none, must clear it).
    if (shown == wanted && column->list.SelectedItems().Size() == shown.size()) {
        return;
    }
    try {
        auto selected = column->list.SelectedItems();
        auto items = column->list.Items();
        state->building = true;
        selected.Clear();
        for (size_t index : wanted) {
            int row = column->entryToRow[index];
            if (row >= 0 && (uint32_t)row < items.Size()) {
                selected.Append(items.GetAt((uint32_t)row));
            }
        }
        state->building = false;
        LogVerbose(L"the toolbar changed Explorer's selection: %d items now selected in column %d", (int)wanted.size(),
            state->active + 1);
        OnRowSelected(column->list);
    } catch (...) {
        state->building = false;
        LogHresult(L"taking Explorer's selection after a toolbar command");
    }
}

// "Open in a new tab": once the new tab exists, shows the folder in it. True
// when there is nothing left to wait for.
bool TryShowNewTab(WindowState* state) {
    if (!state->openTabFolder) {
        return true;
    }
    if (GetTickCount64() >= state->openTabUntil) {
        Wh_Log(L"the new tab did not open in time");
        state->openTabFolder.reset();
        state->newTab = nullptr;
        return true;
    }
    if (state->newTab && IsWindow(state->newTab)) {
        DWORD_PTR result = 0;
        if (SendMessageTimeoutW(state->newTab, WM_USER + 7, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500,
                                &result) &&
            result) {
            IShellBrowser* tabBrowser = (IShellBrowser*)result;
            g_ourBrowse = true;
            HRESULT hr = tabBrowser->BrowseObject(state->openTabFolder.get(),
                                                  SBSP_ABSOLUTE | SBSP_SAMEBROWSER | SBSP_ACTIVATE_NOFOCUS);
            g_ourBrowse = false;
            Wh_Log(L"the new tab shows its folder: hr=0x%08lX", hr);
            if (SUCCEEDED(hr)) {
                state->openTabFolder.reset();
                state->newTab = nullptr;
                return true;
            }
        }
    }
    return false;
}

void OnTick() {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || !state->bridge) {
        return;
    }
    // A dialog, a menu or a drag of the mod is open below this tick: what it uses stays where it is. Only what takes the panel or the
    // window down waits; the rest (the auto-scroll and the spring-loading of a drag that comes back over the panel) goes on.
    bool teardown = !IsWindow(state->topLevel) || g_unloading || !g_enabled;
    if (teardown && t_modalDepth > 0) {
        return;
    }
    if (!IsWindow(state->topLevel)) {
        AbandonWindow(state);
        return;
    }
    if (g_unloading || !g_enabled) {
        HidePanel(state, L"mod unloading or feature off");
        return;
    }
    DragAutoScroll(state);
    try {
        if (!IsWindow(state->topLevel)) {
            AbandonWindow(state);
            return;
        }
        state->ticks++;
        ULONGLONG now = GetTickCount64();
        bool busy = state->building || state->pendingNav || state->selectChild || state->keepChild || state->rename || state->drag.column ||
                    state->reloadDue || state->postActionAt || state->postActionAt2 || state->syncSelAt ||
                    state->specCheckAt || state->swapDeadline || now < state->watchEditUntil || now < state->openTabUntil;
        RetimeTick(state->timer, &state->tickMs, TickIntervalMs(state->topLevel, busy));
        HoverTick(state);
        SpringLoadTick(state);
        SyncViewButtonIcon(state);
        UpdateLastEdge(state);

        HWND fileList = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");

        // Columns per tab: another tab came to the front.
        HWND tab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
        if (tab && tab != state->currentTab && HandleTabSwitch(state, tab, fileList)) {
            return;
        }
        if (state->softHidden) {
            // This tab is not in Columns. Only watch where it goes: a folder that
            // was last shown in Columns turns Columns on again.
            if (state->ticks % 4 == 0) {
                IShellBrowser* hiddenBrowser = BrowserOf(state->topLevel);
                Pidl where = hiddenBrowser ? CurrentFolder(hiddenBrowser) : Pidl();
                if (where) {
                    std::wstring path = PathOf(where.get());
                    if (_wcsicmp(path.c_str(), state->hiddenFolder.c_str()) != 0) {
                        state->hiddenFolder = path;
                        if (RememberedColumns(path) == 1) {
                            Wh_Log(L"this folder was shown in Columns last time, Columns on for the tab");
                            ChooseColumns(state);
                        }
                    }
                }
            }
            return;
        }

        // A tab made after the panel is a window above it, and covers it: the
        // panel would show but get no mouse. Keep it on top.
        {
            HWND panelWindow = (HWND)state->bridge.WindowId().Value;
            if (panelWindow && GetTickCount64() > g_paneLeaveUntil && GetWindow(panelWindow, GW_HWNDPREV)) {
                SetWindowPos(panelWindow, HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                static int logged = 0;
                if (logged++ < 8) {
                    LogVerbose(L"the panel was covered by a sibling window, raised again");
                }
            }
        }

        // Keep the panel on the file list. Plain Win32, cheap.
        RECT rect;
        if (fileList && FileListRect(state->topLevel, fileList, &rect) &&
            !EqualRect(&rect, &state->lastRect)) {
            state->lastRect = rect;
            state->bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
                rect.left, rect.top, rect.right - rect.left,
                rect.bottom - rect.top});
        }

        // Explorer follows the active column, after the pause.
        if (state->pendingNav && now >= state->navDue) {
            Pidl target = std::move(state->pendingNav);
            NavigateExplorer(state, target.get());
        }

        // A watched folder changed on disk: read the columns again.
        // At most one reload every two seconds, however busy the folder is.
        size_t shown = 0;
        for (auto& c : state->columns) {
            shown += c->entries.size();
        }
        // Half a second between reloads, two seconds when the columns hold a
        // lot of rows.
        ULONGLONG gap = shown > 3000 ? 2000 : 500;
        if (state->reloadDue && now >= state->reloadDue && !state->rename &&
            now - state->lastReload >= gap) {
            state->reloadDue = 0;
            state->lastReload = now;
            Wh_Log(L"a column's folder changed, reloading");
            ReloadAll(state);
        }

        // Explorer shows its busy cursor while it navigates, and a pointer that
        // stands still over the panel keeps it. Put the arrow back.
        if (state->cursorFixUntil && now < state->cursorFixUntil) {
            CURSORINFO info = {sizeof(info)};
            if (GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) &&
                (info.hCursor == LoadCursorW(nullptr, IDC_APPSTARTING) ||
                 info.hCursor == LoadCursorW(nullptr, IDC_WAIT))) {
                HWND under = WindowFromPoint(info.ptScreenPos);
                if (under && GetAncestor(under, GA_ROOT) == state->topLevel) {
                    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                }
            }
        } else {
            state->cursorFixUntil = 0;
        }

        // The resize handle appears once the pointer has rested on an edge. It
        // goes away when the pointer is no longer on that edge, also when the
        // columns moved under a pointer that stood still, which sends no event.
        {
            HWND bridgeWindow = state->bridge ? (HWND)state->bridge.WindowId().Value : nullptr;
            POINT cursor = {};
            bool haveCursor = bridgeWindow && GetCursorPos(&cursor) &&
                              ScreenToClient(bridgeWindow, &cursor);
            double scale = 1.0;
            try {
                if (state->rootGrid && state->rootGrid.XamlRoot()) {
                    scale = state->rootGrid.XamlRoot().RasterizationScale();
                }
            } catch (...) {
            }
            for (auto& column : state->columns) {
                if (!column->pill || !column->grip) {
                    continue;
                }
                bool shown = column->pill.Opacity() > 0 || column->hoverSince != 0;
                if (!shown || state->dragColumn >= 0) {
                    continue;
                }
                bool over = false;
                if (haveCursor && scale > 0) {
                    try {
                        auto box = column->grip.TransformToVisual(state->rootGrid)
                                       .TransformBounds(winrt::Windows::Foundation::Rect{
                                           0, 0, (float)column->grip.ActualWidth(),
                                           (float)column->grip.ActualHeight()});
                        double x = cursor.x / scale, y = cursor.y / scale;
                        over = x >= box.X && x <= box.X + box.Width && y >= box.Y &&
                               y <= box.Y + box.Height;
                    } catch (...) {
                    }
                }
                if (!over) {
                    PillHide(column.get());  // The pointer left without the event.
                    column->hoverSince = 0;
                }
            }
        }

        // A menu command ran. Take new sort, group, hidden-files or compact
        // settings if any of them changed.
        if (state->specCheckAt && now >= state->specCheckAt) {
            state->specCheckAt = 0;
            bool hidden = ShowHiddenFiles(), compact = IsCompactMode();
            if (hidden != state->shownHidden ||
                compact != state->shownCompact) {
                Wh_Log(L"Explorer setting changed: hidden=%d compact=%d",
                    hidden ? 1 : 0, compact ? 1 : 0);
                state->shownHidden = hidden;
                state->shownCompact = compact;
                ReloadAll(state);
            }
            IShellBrowser* browser = BrowserOf(state->topLevel);
            SortSpec after;
            Column* activeColumn = state->active >= 0 && state->active < (int)state->columns.size()
                                       ? state->columns[state->active].get()
                                       : nullptr;
            SortSpec const& effective =
                activeColumn && activeColumn->hasSpec ? activeColumn->spec : state->spec;
            if (browser && ReadSortSpec(browser, &after) &&
                !SameSpec(after, state->specBeforeMenu) && !SameSpec(after, effective)) {
                Wh_Log(L"sort or group changed in the Sort menu: %d sort columns, grouped=%d",
                    (int)after.columns.size(), after.grouped ? 1 : 0);
                if (activeColumn && g_rememberSort) {
                    // Only the column the user is in changes, and it is remembered.
                    activeColumn->spec = after;
                    activeColumn->hasSpec = true;
                    RememberFolderSpec(PathOf(activeColumn->folder.get()), after);
                    RequestLoad(state, activeColumn);
                } else {
                    state->spec = after;
                    ReloadAll(state);
                }
            }
        }

        // Explorer showed the active column's folder a moment ago: its sort
        // for that folder is the one to show, and to remember.
        if (state->specSyncDue && now >= state->specSyncDue) {
            state->specSyncDue = 0;
            Column* activeColumn = state->active >= 0 && state->active < (int)state->columns.size()
                                       ? state->columns[state->active].get()
                                       : nullptr;
            IShellBrowser* browser = BrowserOf(state->topLevel);
            if (activeColumn && browser && g_rememberSort) {
                Pidl shown = CurrentFolder(browser);
                SortSpec read;
                std::wstring path = PathOf(activeColumn->folder.get());
                if (shown && _wcsicmp(PathOf(shown.get()).c_str(), path.c_str()) == 0 &&
                    ReadSortSpec(browser, &read)) {
                    SortSpec const& effective =
                        activeColumn->hasSpec ? activeColumn->spec : state->spec;
                    if (!SameSpec(read, effective)) {
                        Wh_Log(L"column \"%s\" takes the sort Explorer has for it", path.c_str());
                        activeColumn->spec = read;
                        activeColumn->hasSpec = true;
                        RequestLoad(state, activeColumn);
                    }
                    RememberFolderSpec(path, read);
                }
            }
        }

        if (state->swapOld && now >= state->swapDeadline) {
            Wh_Log(L"the new first column didn't load in time, swapping anyway");
            CompleteSwap(state);
        }

        // A rename ends, without applying, as soon as the keyboard focus leaves the panel: a click
        // in the sidebar, the toolbar, the details pane or another app.
        if (state->rename && now - state->rename->startedAt > 400) {
            HWND focus = GetFocus();
            HWND panelWindow = (HWND)state->bridge.WindowId().Value;
            bool inside = focus && (focus == panelWindow || IsChild(panelWindow, focus));
            if (focus && !inside) {
                wchar_t name[80] = L"";
                GetClassNameW(focus, name, ARRAYSIZE(name));
                inside = wcsstr(name, L"PopupWindowSiteBridge") != nullptr;  // The text box's own menu.
            }
            if (!inside) {
                CancelRename(state);
            }
        }

        // The slow second click on a selected name: time to rename.
        if (state->slowRenameAt && now >= state->slowRenameAt) {
            state->slowRenameAt = 0;
            Column* column = state->slowRenameColumn;
            bool known = false;
            for (auto& c : state->columns) {
                known = known || c.get() == column;
            }
            if (known && !state->rename) {
                std::vector<size_t> picked = SelectedEntries(column, column->list);
                if (picked.size() == 1 && picked[0] == state->slowRenameEntry) {
                    BeginRename(state, column, picked[0]);
                }
            }
        }

        // Scrollbars of the columns, once a column has its template, and again
        // when the setting changes. The compact style is a transform, so it
        // costs nothing to keep.
        for (auto& c : state->columns) {
            int wanted = 1;
            // A column that never had the compact style is left exactly as
            // Explorer's template made it.
            if (c->scrollStyle == -1 && wanted == 0) {
                c->scrollStyle = 0;
            }
            if (c->filled && c->scrollStyle != wanted && c->list) {
                try {
                    // Remembered only when a bar was found: the template may
                    // not be built yet.
                    if (StyleScrollBars(c->list, wanted == 1) > 0) {
                        c->scrollStyle = wanted;
                        c->list.Padding(mux::Thickness{5, 12, 10.0, 4});
                        c->gutterRight = 10;
                    }
                } catch (...) {
                    LogHresult(L"scrollbar style");
                }
            }
        }

        // Rows near the screen have their content: the scroller of each list is
        // watched once it exists, and the rows are topped up here as well, since
        // the list lays out a moment after it is filled.
        for (auto& c : state->columns) {
            if (!c->filled || !c->list) {
                continue;
            }
            if (!c->viewer) {
                auto viewer = FindListScroller(c->list);
                if (viewer) {
                    c->viewer = viewer;
                    c->viewToken = viewer.ViewChanged(
                        [](Inspectable const& sender,
                           muxc::ScrollViewerViewChangedEventArgs const&) {
                            OnListViewChanged(sender);
                        });
                }
            }
            BuildVisibleRows(c.get());
            RequestIcons(state, c.get());
        }

        // A compact scrollbar floats over the right edge. While it shows, the
        // free strip for the selection rectangle is 7 px wider, so 10 px stay
        // free beside it. With no bar there is no extra gap.
        {
            for (auto& c : state->columns) {
                if (!c->list || !c->filled) {
                    continue;
                }
                auto viewer = FindListScroller(c->list);
                bool barShown = viewer && viewer.ComputedVerticalScrollBarVisibility() ==
                                              mux::Visibility::Visible;
                double wanted = barShown ? 23.0 : 10.0;  // 7 for the bar, 16 free beside it
                if (c->gutterRight != wanted) {
                    c->gutterRight = wanted;
                    c->list.Padding(mux::Thickness{5, 12, wanted, 4});
                }
            }
        }

        // A changed setting that affects rows already on screen.
        if (state->styleVersion != g_styleVersion) {
            state->styleVersion = g_styleVersion;
            for (auto& c : state->columns) {
                auto rows = c->list.Items();
                for (uint32_t r = 0; r < rows.Size(); r++) {
                    if (auto item = rows.GetAt(r).try_as<muxc::ListViewItem>()) {
                        ApplyFocusStyle(item);
                    }
                }
            }
        }

        // The theme changed: take the list's colour again.
        if (state->rootGrid && fileList &&
            (state->shownMode != g_backgroundMode ||
             (state->ticks % 20 == 0 && IsDarkMode() != state->shownDark))) {
            state->shownMode = g_backgroundMode;
            bool nowDark = IsDarkMode();
            state->shownDark = nowDark;
            state->rootGrid.RequestedTheme(nowDark ? mux::ElementTheme::Dark
                                                   : mux::ElementTheme::Light);
            state->rootGrid.Background(PanelBackground(fileList, nowDark));
            Wh_Log(L"theme changed, panel background taken again");
        }

        // The system accent or the theme changed: the pill and the selection
        // rectangle take the new colour.
        if (state->ticks % 20 == 0 && state->pill && state->bandBox) {
            try {
                bool darkNow = IsDarkMode();
                winrt::Windows::UI::ViewManagement::UISettings colours;
                auto pillShade = colours.GetColorValue(
                    darkNow ? winrt::Windows::UI::ViewManagement::UIColorType::AccentLight2
                            : winrt::Windows::UI::ViewManagement::UIColorType::AccentDark1);
                auto accent = colours.GetColorValue(
                    winrt::Windows::UI::ViewManagement::UIColorType::Accent);
                auto same = [](winrt::Windows::UI::Color const& a, winrt::Windows::UI::Color const& b) {
                    return a.R == b.R && a.G == b.G && a.B == b.B;
                };
                if (!state->colorsSet || !same(pillShade, state->pillColor) ||
                    !same(accent, state->accentColor)) {
                    if (state->colorsSet) {
                        Wh_Log(L"accent or theme changed, the pill and the selection rectangle follow");
                    }
                    state->colorsSet = true;
                    state->pillColor = pillShade;
                    state->accentColor = accent;
                    state->pill.Background(mux::Media::SolidColorBrush(pillShade));
                    state->bandBox.Background(Brush(48, accent.R, accent.G, accent.B));
                    state->bandBox.BorderBrush(Brush(170, accent.R, accent.G, accent.B));
                }
            } catch (...) {
            }
        }

        // Open in a new tab: the tab is there, show the folder in it.
        TryShowNewTab(state);

        // A column resized or moved: the pill follows, without animation.
        UpdatePill(state, false);

        // The clipboard changed, by this mod or from outside (Cut in Explorer's
        // toolbar): the dimmed rows follow what is on it.
        {
            DWORD seq = GetClipboardSequenceNumber();
            if (seq != state->clipSeen) {
                state->clipSeen = seq;
                if (seq != state->cutSeq) {
                    SyncCutFromClipboard(state);
                }
            }
        }

        // A group that finished opening or closing.
        for (auto& c : state->columns) {
            if (c->animGroup >= 0 && now >= c->animDoneAt) {
                FinishGroupAnimation(c.get());
            }
        }

        // After a toolbar command: read again, once soon and once later.
        if (state->syncSelAt && now >= state->syncSelAt) {
            state->syncSelAt = 0;
            SyncSelectionFromExplorer(state);
        }
        if (state->postActionAt && now >= state->postActionAt) {
            state->postActionAt = 0;
            ReloadAll(state);
        }
        if (state->postActionAt2 && now >= state->postActionAt2) {
            state->postActionAt2 = 0;
            SyncSelectionFromExplorer(state);
            ReloadAll(state);
        }

        // Explorer's own rename box, started from the toolbar, sits under the
        // panel where nobody sees it. Close it and rename in the column.
        if (state->watchEditUntil) {
            if (now >= state->watchEditUntil) {
                state->watchEditUntil = 0;
            } else if (HWND edit = FindNativeRenameBox(state->topLevel)) {
                state->watchEditUntil = 0;
                // The name in the box is the item that was just made (a New folder, a New file): it is
                // what the column renames once it lists it. The box is closed with its name kept.
                wchar_t made[MAX_PATH] = L"";
                GetWindowTextW(edit, made, MAX_PATH);
                SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
                SendMessageW(edit, WM_KEYUP, VK_ESCAPE, 0);
                if (state->active >= 0 && state->active < (int)state->columns.size() &&
                    !state->rename) {
                    Column* column = state->columns[state->active].get();
                    if (made[0]) {
                        Wh_Log(L"Explorer's own rename box found, \"%s\" is renamed in the column once it is listed", made);
                        state->renameName = made;
                        state->renameColumn = column;
                        RequestLoad(state, column);
                    } else {
                        std::vector<size_t> picked = SelectedEntries(column, column->list);
                        if (!picked.empty()) {
                            BeginMultiRename(state, column, picked);
                        }
                    }
                }
            }
        }
        // Explorer's view takes focus when it follows a column. Keep it in
        // the column the user is typing in.
        if (state->focusColumn && GetTickCount64() < state->focusUntil &&
            !state->rename && !state->context) {
            bool known = false;
            for (auto& c : state->columns) {
                known = known || c.get() == state->focusColumn;
            }
            // Only when focus has left the panel, and then once: focusing a
            // list again would move its keyboard cursor.
            HWND panel = state->bridge ? (HWND)state->bridge.WindowId().Value
                                       : nullptr;
            HWND focused = GetFocus();
            bool inside = panel && focused &&
                          (focused == panel || IsChild(panel, focused));
            // Not while the strip is scrolling: giving the list the keyboard
            // brings it into view, which cut the centring short.
            if (known && panel && !inside && now >= state->scrollQuietUntil) {
                state->focusColumn->list.Focus(mux::FocusState::Programmatic);
                LogVerbose(L"focus taken back from Explorer's view");
            }
        } else {
            state->focusColumn = nullptr;
        }

        // Ask the shell where Explorer is only when something suggests a
        // change: the file list window was replaced, a request is pending,
        // or as a slow safety net.
        bool selectPending = state->selectChild && !state->pendingNav;
        bool check = fileList != state->lastFileList || state->expected ||
                     selectPending || state->keepChild || state->ticks % 20 == 0;
        if (!check) {
            return;
        }
        IShellBrowser* browser = BrowserOf(state->topLevel);
        if (!browser) {
            return;
        }
        Pidl current = CurrentFolder(browser);
        if (!current) {
            return;
        }
        bool listReplaced =
            state->lastFileList && fileList != state->lastFileList;
        state->lastFileList = fileList;
        if (!state->lastSeen ||
            !ILIsEqual(current.get(), state->lastSeen.get())) {
            OnExplorerFolderChanged(state, current);
            if (!state->on) {
                return;
            }
        } else if (listReplaced && !state->expected) {
            // Same folder, new file list window: taken as a Refresh.
            Wh_Log(L"file list replaced in the same folder, reloading");
            ReloadAll(state);
        }

        // Mirror the selected file into Explorer's own view. The view fills
        // asynchronously, so this retries for a few seconds.
        if (state->selectChild && state->selectFolder && !state->pendingNav &&
            !state->expected &&
            ILIsEqual(current.get(), state->selectFolder.get())) {
            if (SelectInExplorer(browser, state->selectChild.get())) {
                if (!state->selectExtra.empty()) {
                    AddToSelectionInExplorer(browser, state->selectExtra);
                    state->selectExtra.clear();
                } else {
                    WatchSelectionInExplorer(state);
                }
                state->selectFolder.reset();
                state->selectChild.reset();
            } else if (++state->selectTries > 40) {
                Wh_Log(L"selection could not be mirrored into Explorer's view");
                state->selectFolder.reset();
                state->selectChild.reset();
                state->selectExtra.clear();
            }
        }
        // The selection set a moment ago still stands? Explorer can put back the one it remembers for the folder.
        if (state->keepChild && state->keepFolder) {
            ULONGLONG now = GetTickCount64();
            if (now >= state->keepUntil) {
                state->keepChild.reset();
                state->keepFolder.reset();
            } else if (now >= state->keepNext && !state->pendingNav && !state->expected && ILIsEqual(current.get(), state->keepFolder.get())) {
                state->keepNext = now + 250;
                auto chosen = ExplorerSelection(browser);
                if (chosen.size() != 1 || !ILIsEqual(chosen[0].get(), state->keepChild.get())) {
                    Wh_Log(L"Explorer's view took back its own selection, ours is set again");
                    SelectInExplorer(browser, state->keepChild.get());
                }
            }
        }
    } catch (...) {
        state->building = false;
        LogHresult(L"OnTick");
    }
}

void ShowPanel(WindowState* state) {
    if (state->source || g_unloading || !g_enabled) {
        return;
    }
    HWND fileList = FindVisibleChild(state->topLevel, L"SHELLDLL_DefView");
    RECT rect;
    if (!fileList || !FileListRect(state->topLevel, fileList, &rect)) {
        Wh_Log(L"panel: no visible file list in this window");
        return;
    }
    const wchar_t* step = L"start";
    try {
        step = L"reading sort and group";
        state->spec = SortSpec{};
        if (IShellBrowser* browser = BrowserOf(state->topLevel)) {
            ReadSortSpec(browser, &state->spec);
        }
        Wh_Log(L"spec: %d sort columns, first direction %d, grouped=%d",
            (int)state->spec.columns.size(),
            state->spec.columns.empty() ? 0
                                        : (int)state->spec.columns[0].direction,
            state->spec.grouped ? 1 : 0);

        state->shownHidden = ShowHiddenFiles();
        state->shownCompact = IsCompactMode();
        Wh_Log(L"Explorer settings read: hidden=%d compact=%d, raw UseCompactMode=%lu",
            state->shownHidden ? 1 : 0, state->shownCompact ? 1 : 0,
            ReadExplorerDword(
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                L"UseCompactMode", 99));
        if (!state->notifyWindow) {
            state->notifyWindow = CreateNotifyWindow(&state->reloadDue);
        }

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
        // The strip and, over it, a layer for the pill. The pill scrolls
        // sideways with the columns and stays at the top when a column scrolls.
        bool dark = IsDarkMode();
        muxc::Grid content;
        content.Children().Append(strip);
        muxc::Canvas overlay;
        overlay.IsHitTestVisible(false);
        muxc::Border pill;
        pill.Width(16);
        pill.Height(3);
        pill.CornerRadius(mux::CornerRadius{1.5, 1.5, 1.5, 1.5});
        pill.Opacity(0);
        try {
            // The accent WinUI itself uses for fills: the lighter shade on a
            // dark theme, the darker one on a light theme.
            auto accent = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(
                dark ? winrt::Windows::UI::ViewManagement::UIColorType::AccentLight2
                     : winrt::Windows::UI::ViewManagement::UIColorType::AccentDark1);
            pill.Background(mux::Media::SolidColorBrush(accent));
        } catch (...) {
            pill.Background(Brush(255, 0, 120, 212));
        }
        muxc::Canvas::SetTop(pill, 5);
        muxc::Canvas::SetLeft(pill, 0);
        overlay.Children().Append(pill);
        // The selection rectangle: a soft accent fill with a thin edge.
        muxc::Border bandBox;
        bandBox.IsHitTestVisible(false);
        bandBox.Opacity(0);
        bandBox.CornerRadius(mux::CornerRadius{3, 3, 3, 3});
        bandBox.BorderThickness(mux::Thickness{1, 1, 1, 1});
        try {
            auto accent = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(
                winrt::Windows::UI::ViewManagement::UIColorType::Accent);
            bandBox.Background(Brush(48, accent.R, accent.G, accent.B));
            bandBox.BorderBrush(Brush(170, accent.R, accent.G, accent.B));
        } catch (...) {
            bandBox.Background(Brush(48, 0, 120, 212));
            bandBox.BorderBrush(Brush(170, 0, 120, 212));
        }
        overlay.Children().Append(bandBox);
        state->bandBox = bandBox;
        content.Children().Append(overlay);
        state->overlay = overlay;
        state->pill = pill;
        state->pillLeft = -1;
        state->pillUntil = 0;
        scroller.Content(content);

        muxc::Grid root;
        {
            root.Background(PanelBackground(fileList, dark));
            state->shownDark = dark;
            state->shownMode = g_backgroundMode;
        }
        root.RequestedTheme(dark ? mux::ElementTheme::Dark
                                 : mux::ElementTheme::Light);
        root.Children().Append(scroller);

        // The horizontal mouse wheel scrolls the strip sideways, across the
        // columns. Registered to also see events a column already handled, so
        // the columns' own vertical scrolling can't take it.
        mux::Input::PointerEventHandler wheel(
            [](Inspectable const&,
               mux::Input::PointerRoutedEventArgs const& args) {
                try {
                    WindowState* state = StateForThisThread(false);
                    if (!state || !state->scroller) {
                        return;
                    }
                    HoverOn(state);
                    auto props =
                        args.GetCurrentPoint(state->scroller).Properties();
                    if (!props.IsHorizontalMouseWheel()) {
                        return;
                    }
                    // Turns that come close together add up and glide to
                    // one target. Starting from the current offset on every
                    // turn would restart the glide each time and look
                    // jerky. After a pause, start from where the strip is.
                    ULONGLONG now = GetTickCount64();
                    double from = (now - state->wheelAt < 300)
                                      ? state->wheelTarget
                                      : state->scroller.HorizontalOffset();
                    double target = from + props.MouseWheelDelta();
                    double maxOffset = state->scroller.ScrollableWidth();
                    target = target < 0 ? 0 : (target > maxOffset ? maxOffset : target);
                    state->wheelTarget = target;
                    state->wheelAt = now;
                    state->scroller.ChangeView(target, nullptr, nullptr, false);
                    args.Handled(true);
                } catch (...) {
                    LogHresult(L"horizontal wheel");
                }
            });
        Inspectable wheelBoxed = winrt::box_value(wheel);
        AddRouted(root, mux::UIElement::PointerWheelChangedEvent(), wheelBoxed);
        state->rootGrid = root;
        state->wheelHandler = wheelBoxed;

        // The ordinary wheel only scrolls the column under the pointer. A
        // column that is already at its end, or has nothing to scroll, passes
        // the event up, and the strip's scroll viewer would turn it into a
        // sideways move. This handler sits on the strip, below that scroll
        // viewer and above every column, and marks vertical turns handled.
        mux::Input::PointerEventHandler verticalWheel(
            [](Inspectable const&,
               mux::Input::PointerRoutedEventArgs const& args) {
                try {
                    WindowState* state = StateForThisThread(false);
                    if (!state || !state->scroller) {
                        return;
                    }
                    if (!args.GetCurrentPoint(state->scroller)
                             .Properties()
                             .IsHorizontalMouseWheel()) {
                        args.Handled(true);
                    }
                } catch (...) {
                    LogHresult(L"vertical wheel");
                }
            });
        Inspectable verticalBoxed = winrt::box_value(verticalWheel);
        AddRouted(strip, mux::UIElement::PointerWheelChangedEvent(),
                         verticalBoxed);
        state->stripWheelHandler = verticalBoxed;

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
        g_panelWindow = (HWND)bridge.WindowId().Value;
        RegisterPanelDropTarget(state);
        state->lastFileList = fileList;
        state->scroller = scroller;
        state->strip = strip;
        state->on = true;
        state->softHidden = false;
        state->currentTab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
        if (state->currentTab) {
            if (!TabListHas(state->columnsTabs, state->currentTab)) {
                state->columnsTabs.push_back(state->currentTab);
            }
            if (!TabListHas(state->seenTabs, state->currentTab)) {
                state->seenTabs.push_back(state->currentTab);
            }
        }

        step = L"reading the folder";
        RebuildFromExplorer(state);
        SetActiveColumn(state, 0);

        step = L"starting the timer";
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(60));
        state->tickToken = timer.Tick(
            [](mud::DispatcherQueueTimer const&, Inspectable const&) {
                OnTick();
            });
        timer.Start();
        state->timer = timer;
        Wh_Log(L"panel on, window=%p", state->topLevel);
    } catch (...) {
        Wh_Log(L"panel failed at step: %s", step);
        LogHresult(L"ShowPanel");
        HidePanel(state, L"creation failed");
    }
}

// "Columns" chosen in the View menu of the tab that is showing.
void ChooseColumns(WindowState* state) {
    if (!state->source) {
        ShowPanel(state);
        // Choosing Columns in a folder is remembered for it, also when this
        // builds the panel. Without this, a folder left in another layout
        // earlier stayed marked that way.
        if (state->on && state->lastSeen) {
            RememberFolderColumns(PathOf(state->lastSeen.get()), 1);
        }
        return;
    }
    HWND tab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
    if (tab) {
        if (!TabListHas(state->columnsTabs, tab)) {
            state->columnsTabs.push_back(tab);
        }
        if (!TabListHas(state->seenTabs, tab)) {
            state->seenTabs.push_back(tab);
        }
    }
    state->currentTab = tab;
    SoftShowPanel(state);
    RebuildFromExplorer(state);
    SetActiveColumn(state, 0);
    if (state->lastSeen) {
        RememberFolderColumns(PathOf(state->lastSeen.get()), 1);
    }
    if (state->menuItem) {
        state->menuItem.IsChecked(true);
    }
}

// One of Explorer's own layouts chosen for the tab that is showing.
void LeaveColumns(WindowState* state) {
    {
        IShellBrowser* browser = BrowserOf(state->topLevel);
        Pidl where = browser ? CurrentFolder(browser) : Pidl();
        if (where) {
            RememberFolderColumns(PathOf(where.get()), 0);
        }
    }
    HWND tab = FindVisibleChild(state->topLevel, L"ShellTabWindowClass");
    TabListRemove(state->columnsTabs, tab);
    for (size_t i = 0; i < state->tabSnapshots.size();) {
        if (state->tabSnapshots[i].tab == tab) {
            state->tabSnapshots.erase(state->tabSnapshots.begin() + i);
        } else {
            i++;
        }
    }
    PruneTabs(state);
    if (state->columnsTabs.empty()) {
        HidePanel(state, L"another layout chosen");
    } else {
        SoftHidePanel(state);
    }
}
