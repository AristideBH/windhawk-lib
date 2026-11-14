
// ---- Drag and drop between the columns ----
//
// Rows are dragged with the pointer inside the panel. The mod follows the
// pointer itself, shows a label and lights the folder it would drop on, then
// hands the dragged items to that folder's own drop target, so the shell does the
// move, copy or link, with its own conflict and progress dialogs, as it does
// for Explorer. Ctrl copies, Shift moves and Alt links; without a key, the
// shell chooses (a move on the same drive, a copy to another). Escape cancels.

// The column and folder row under a point of the panel, or false.
bool DragTargetAt(WindowState* state, winrt::Windows::Foundation::Point where, Column** column,
                  int* entry) {
    auto elements = mux::Media::VisualTreeHelper::FindElementsInHostCoordinates(where, state->rootGrid);
    for (auto element : elements) {
        muxc::ListViewItem row{nullptr};
        mux::DependencyObject node = element;
        for (int i = 0; node && i < 40; i++) {
            if (!row) {
                if (auto found = node.try_as<muxc::ListViewItem>()) {
                    row = found;
                }
            }
            if (auto list = node.try_as<muxc::ListView>()) {
                int at = ColumnIndexOfList(state, list);
                if (at < 0) {
                    break;
                }
                *column = state->columns[at].get();
                *entry = -1;
                if (row) {
                    int rowIndex = list.IndexFromContainer(row);
                    if (rowIndex >= 0 && rowIndex < (int)(*column)->rowToEntry.size()) {
                        *entry = (*column)->rowToEntry[rowIndex];
                    }
                }
                return true;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
    }
    return false;
}

// The absolute folder a drop on this column or row would go to.
Pidl DragTargetFolder(Column* column, int entry) {
    if (entry >= 0 && (size_t)entry < column->entries.size()) {
        auto& hit = column->entries[entry];
        return Pidl(hit.linkTarget ? ILClone(hit.linkTarget.get())
                                   : ILCombine(column->folder.get(), hit.child.get()));
    }
    return Pidl(ILClone(column->folder.get()));
}

// Whether the dragged items can be dropped there.
bool DragTargetAllowed(WindowState* state, Column* column, int entry) {
    auto& d = state->drag;
    if (!d.column) {
        return false;
    }
    if (entry >= 0) {
        if ((size_t)entry >= column->entries.size()) {
            return false;
        }
        auto& hit = column->entries[entry];
        if (!hit.folder || hit.archive || hit.launcher) {
            return false;
        }
        if (column == d.column &&
            std::find(d.entries.begin(), d.entries.end(), (size_t)entry) != d.entries.end()) {
            return false;  // A folder onto itself.
        }
    } else {
        if (column == d.column && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            return false;  // Already there. With Ctrl it is a copy in the same folder.
        }
        if (ArchiveParentColumn(state, column) >= 0) {
            return false;
        }
    }
    Pidl target = DragTargetFolder(column, entry);
    wchar_t path[MAX_PATH] = L"";
    if (!target || !SHGetPathFromIDListW(target.get(), path) || !path[0]) {
        return false;  // Not a folder on disk.
    }
    // A folder cannot go into itself or what is inside it.
    for (size_t index : d.entries) {
        if (index >= d.column->entries.size() || !d.column->entries[index].folder) {
            continue;
        }
        std::wstring source = FilePathOf(d.column->folder.get(), d.column->entries[index].child.get());
        if (!source.empty() && _wcsnicmp(path, source.c_str(), source.size()) == 0 &&
            (path[source.size()] == L'\0' || path[source.size()] == L'\\')) {
            return false;
        }
    }
    return true;
}

void DragClearTarget(WindowState* state) {
    auto& d = state->drag;
    try {
        if (d.frame) {
            d.frame.Visibility(mux::Visibility::Collapsed);
        }
    } catch (...) {
        LogHresult(L"clearing the drop target");
    }
    d.targetColumn = nullptr;
    d.targetEntry = -1;
    d.targetOk = false;
}

// The label comes in and goes out the way a flyout does: a short fade with a
// slight scale from the middle. On the way out it is removed when it has faded.
void AnimateGhost(mux::FrameworkElement const& ghost, bool entering) {
    try {
        namespace anim = mux::Media::Animation;
        mux::Media::ScaleTransform scale;
        ghost.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
        ghost.RenderTransform(scale);
        anim::CubicEase ease;
        ease.EasingMode(entering ? anim::EasingMode::EaseOut : anim::EasingMode::EaseIn);
        auto length = mux::DurationHelper::FromTimeSpan(std::chrono::milliseconds(entering ? 150 : 100));
        anim::Storyboard board;
        auto add = [&](const wchar_t* property, double from, double to) {
            anim::DoubleAnimation step;
            step.From(from);
            step.To(to);
            step.Duration(length);
            step.EasingFunction(ease);
            step.EnableDependentAnimation(true);
            step.FillBehavior(entering ? anim::FillBehavior::Stop : anim::FillBehavior::HoldEnd);
            anim::Storyboard::SetTarget(step, ghost);
            anim::Storyboard::SetTargetProperty(step, property);
            board.Children().Append(step);
        };
        add(L"Opacity", entering ? 0.0 : 1.0, entering ? 1.0 : 0.0);
        add(L"(UIElement.RenderTransform).(ScaleTransform.ScaleX)", entering ? 0.92 : 1.0,
            entering ? 1.0 : 0.96);
        add(L"(UIElement.RenderTransform).(ScaleTransform.ScaleY)", entering ? 0.92 : 1.0,
            entering ? 1.0 : 0.96);
        board.Begin();
    } catch (...) {
        LogHresult(L"animating the drag label");
    }
}

void FadeOutGhost(muxc::Canvas const& overlay, mux::FrameworkElement const& ghost) {
    auto remove = [overlay, ghost]() {
        try {
            uint32_t index = 0;
            if (overlay.Children().IndexOf(ghost, index)) {
                overlay.Children().RemoveAt(index);
            }
        } catch (...) {
        }
    };
    if (g_unloading) {
        remove();
        return;
    }
    try {
        AnimateGhost(ghost, false);
        auto timer = overlay.DispatcherQueue().CreateTimer();
        timer.Interval(std::chrono::milliseconds(130));
        timer.IsRepeating(false);
        timer.Tick([remove](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
            sender.Stop();
            remove();
        });
        timer.Start();
    } catch (...) {
        remove();
    }
}

void DragEnd(WindowState* state) {
    auto& d = state->drag;
    DragClearTarget(state);
    state->autoOn = false;
    try {
        if (d.ghost && state->overlay) {
            FadeOutGhost(state->overlay, d.ghost);
        }
        if (d.frame && state->overlay) {
            uint32_t index = 0;
            if (state->overlay.Children().IndexOf(d.frame, index)) {
                state->overlay.Children().RemoveAt(index);
            }
        }
        if (d.active && d.column && d.column->list) {
            d.column->list.ReleasePointerCaptures();
        }
    } catch (...) {
        LogHresult(L"ending a drag");
    }
    d = DragState();
}

void CancelDrag(WindowState* state) {
    if (state->drag.pending || state->drag.active) {
        DragEnd(state);
    }
}

void StartOutDrag(WindowState* state);

// A press on a row: a drag may start from it once the pointer moves.
void OnDragPressed(WindowState* state, muxc::ListView const& list, muxc::ListViewItem const& row,
                   mux::Input::PointerRoutedEventArgs const& args) {
    if (!args.GetCurrentPoint(list).Properties().IsLeftButtonPressed()) {
        return;
    }
    int at = ColumnIndexOfList(state, list);
    if (at < 0 || ArchiveParentColumn(state, state->columns[at].get()) >= 0) {
        return;
    }
    Column* column = state->columns[at].get();
    int rowIndex = list.IndexFromContainer(row);
    int entry = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size() ? column->rowToEntry[rowIndex] : -1;
    if (entry < 0) {
        return;
    }
    DragEnd(state);
    auto& d = state->drag;
    d.pending = true;
    d.column = column;
    d.pressedEntry = entry;
    d.start = args.GetCurrentPoint(list).Position();
    static int logged = 0;
    if (logged++ < 6) {
        Out(L"drag may start: pressed on row %d (entry %d) of column %d", rowIndex, entry, at + 1);
    }
}

// Puts the accent frame around the folder row, or the column, a drop would go
// to. Run again whenever the pointer moves or the columns scroll, since the row
// moves under a frame that stays still.
void DragPlaceFrame(WindowState* state) {
    auto& d = state->drag;
    if (!d.targetOk || !d.targetColumn || !state->overlay) {
        if (d.frame) {
            d.frame.Visibility(mux::Visibility::Collapsed);
        }
        return;
    }
    try {
        Column* column = d.targetColumn;
        mux::FrameworkElement target{nullptr};
        if (d.targetEntry >= 0) {
            int rowIndex = column->entryToRow[d.targetEntry];
            target = column->list.Items().GetAt((uint32_t)rowIndex).try_as<muxc::ListViewItem>();
        } else {
            target = column->cell;
        }
        if (!target) {
            if (d.frame) {
                d.frame.Visibility(mux::Visibility::Collapsed);
            }
            return;
        }
        auto at = target.TransformToVisual(state->overlay).TransformPoint(winrt::Windows::Foundation::Point{0, 0});
        double left = at.X, top = at.Y, width = target.ActualWidth(), height = target.ActualHeight();
        if (d.targetEntry >= 0) {
            // A row that is half scrolled out of its column is framed only where it shows.
            auto listAt = column->list.TransformToVisual(state->overlay)
                              .TransformPoint(winrt::Windows::Foundation::Point{0, 0});
            double listTop = listAt.Y, listBottom = listAt.Y + column->list.ActualHeight();
            if (top < listTop) {
                height -= listTop - top;
                top = listTop;
            }
            if (top + height > listBottom) {
                height = listBottom - top;
            }
        }
        if (width < 4 || height < 4) {
            if (d.frame) {
                d.frame.Visibility(mux::Visibility::Collapsed);
            }
            return;
        }
        if (!d.frame) {
            winrt::Windows::UI::Color tint = state->colorsSet ? state->pillColor
                                                              : winrt::Windows::UI::Color{255, 76, 194, 255};
            muxc::Border frame;
            frame.BorderBrush(Brush(255, tint.R, tint.G, tint.B));
            frame.BorderThickness(mux::Thickness{1.5, 1.5, 1.5, 1.5});
            frame.Background(Brush(12, tint.R, tint.G, tint.B));
            frame.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
            frame.IsHitTestVisible(false);
            state->overlay.Children().InsertAt(0, frame);  // Under the label.
            d.frame = frame;
        }
        muxc::Canvas::SetLeft(d.frame, left);
        muxc::Canvas::SetTop(d.frame, top);
        d.frame.Width(width);
        d.frame.Height(height);
        d.frame.Visibility(mux::Visibility::Visible);
    } catch (...) {
        LogHresult(L"framing the drop target");
    }
}

// Lights the folder row, or the column, a drop would go to.
void DragLight(WindowState* state, Column*, int) {
    DragPlaceFrame(state);
}

// Finds the target under a point of the panel (host coordinates) for the drag
// of rows, and lights it when the drop is allowed. Also run by the tick while
// a column scrolls under a pointer that does not move.
void DragRefreshTarget(WindowState* state, winrt::Windows::Foundation::Point where) {
    auto& d = state->drag;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    Column* targetColumn = nullptr;
    int targetEntry = -1;
    bool found = false;
    try {
        found = DragTargetAt(state, where, &targetColumn, &targetEntry);
    } catch (...) {
        LogHresult(L"finding the drop target");
    }
    if (!found || targetColumn != d.targetColumn || targetEntry != d.targetEntry ||
        ctrl != d.lastCtrl) {
        d.lastCtrl = ctrl;
        DragClearTarget(state);
        if (found) {
            d.targetColumn = targetColumn;
            d.targetEntry = targetEntry;
            d.targetOk = DragTargetAllowed(state, targetColumn, targetEntry);
            if (d.targetOk) {
                DragLight(state, targetColumn, targetEntry);
            }
        }
    }
    if (d.targetOk) {
        DragPlaceFrame(state);
    }
    if (d.ghost) {
        d.ghost.Opacity(d.targetOk || !found ? 1.0 : 0.8);
    }
}

// The label's content, as it is now.
GhostSpec SpecOf(Column* column, std::vector<size_t> const& entries) {
    GhostSpec spec;
    spec.count = entries.size();
    spec.name = entries.size() == 1 && entries[0] < column->entries.size()
                    ? column->entries[entries[0]].name
                    : std::to_wstring(entries.size()) + L" items";
    for (size_t at = 0; at < std::min<size_t>(3, entries.size()); ++at) {
        mux::Media::ImageSource source{nullptr};
        size_t entry = entries[at];
        try {
            if (entry < column->iconHosts.size() && column->iconHosts[entry] &&
                column->iconHosts[entry].Children().Size() > 0) {
                if (auto image = column->iconHosts[entry].Children().GetAt(0).try_as<muxc::Image>()) {
                    source = image.Source();
                }
            }
        } catch (...) {
        }
        spec.icons.push_back(source);
    }
    return spec;
}

// Builds the label that follows the pointer, on the surface of a WinUI tooltip
// (no stroke of its own): up to three icons of what is dragged, stacked, the
// name or the count, and a line saying what a drop would do there. It goes into
// the drag state and the overlay; the caller keeps the rest.
void ShowGhost(WindowState* state, GhostSpec const& spec) {
    auto& d = state->drag;
    muxc::ToolTip ghost;
    ghost.BorderThickness(mux::Thickness{0, 0, 0, 0});
    ghost.Padding(mux::Thickness{10, 8, 14, 8});
    ghost.IsHitTestVisible(false);
    muxc::StackPanel content;
    content.Orientation(muxc::Orientation::Horizontal);
    content.Spacing(10);
    size_t shown = std::max<size_t>(1, std::min<size_t>(3, spec.count));
    const double iconSize = 32;  // As big as the icon of Explorer's own drag image.
    muxc::Grid stack;
    stack.Width(iconSize + 6.0 * (shown - 1));
    stack.Height(iconSize + 4.0 * (shown - 1));
    stack.VerticalAlignment(mux::VerticalAlignment::Center);
    for (size_t at = shown; at-- > 0;) {  // Back to front.
        mux::UIElement piece{nullptr};
        if (at < spec.icons.size() && spec.icons[at]) {
            muxc::Image image;
            image.Source(spec.icons[at]);
            image.Width(iconSize);
            image.Height(iconSize);
            image.Stretch(mux::Media::Stretch::Uniform);
            piece = image;
        } else {
            muxc::FontIcon glyph;
            glyph.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            glyph.Glyph(L"");
            glyph.FontSize(26);
            glyph.Width(iconSize);
            glyph.Height(iconSize);
            piece = glyph;
        }
        auto frame = piece.as<mux::FrameworkElement>();
        if (shown == 1) {
            frame.HorizontalAlignment(mux::HorizontalAlignment::Center);
            frame.VerticalAlignment(mux::VerticalAlignment::Center);
        } else {
            frame.HorizontalAlignment(mux::HorizontalAlignment::Left);
            frame.VerticalAlignment(mux::VerticalAlignment::Bottom);
            frame.Margin(mux::Thickness{6.0 * at, 0, 0, 4.0 * at});
            piece.Opacity(at == 0 ? 1.0 : 0.85);
        }
        stack.Children().Append(piece);
    }
    content.Children().Append(stack);
    muxc::StackPanel texts;
    texts.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::TextBlock text;
    text.FontSize(12);
    text.MaxWidth(240);
    text.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    texts.Children().Append(text);
    muxc::StackPanel row;
    row.Orientation(muxc::Orientation::Horizontal);
    row.Spacing(5);
    row.Opacity(0.7);
    row.Visibility(mux::Visibility::Collapsed);
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.FontSize(11);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    row.Children().Append(icon);
    muxc::TextBlock action;
    action.FontSize(11);
    action.MaxWidth(220);
    action.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    row.Children().Append(action);
    texts.Children().Append(row);
    content.Children().Append(texts);
    ghost.Content(content);
    d.ghost = ghost;
    d.ghostText = text;
    d.ghostAction = action;
    d.ghostIcon = icon;
    d.ghostRow = row;
    d.labelName = spec.name;
    state->overlay.Children().Append(ghost);
    AnimateGhost(ghost, true);
}

// The label goes out, with its animation, and the drag state forgets it.
void RemoveGhost(WindowState* state) {
    auto& d = state->drag;
    if (d.ghost && state->overlay) {
        FadeOutGhost(state->overlay, d.ghost);
    }
    d.ghost = nullptr;
    d.ghostText = nullptr;
    d.ghostAction = nullptr;
    d.ghostIcon = nullptr;
    d.ghostRow = nullptr;
}

// The name of a folder, as the shell shows it.
std::wstring FolderDisplayName(PCIDLIST_ABSOLUTE pidl) {
    std::wstring result;
    PWSTR text = nullptr;
    if (pidl && SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &text)) && text) {
        result = text;
        CoTaskMemFree(text);
    }
    return result;
}

std::wstring Shortened(std::wstring text, size_t most) {
    if (text.size() > most) {
        text = text.substr(0, most - 1) + L"…";
    }
    return text;
}

// The two lines of the label: what is dragged, and what a drop would do.
void DragUpdateLabel(WindowState* state) {
    auto& d = state->drag;
    if (!d.ghostText) {
        return;
    }
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    d.ghostText.Text(Shortened(d.labelName, 40).c_str());
    if (!d.ghostAction) {
        return;
    }
    std::wstring line;
    const wchar_t* glyph = L"";
    if (d.targetColumn) {
        if (!d.targetOk) {
            line = L"Can’t drop here";
            glyph = L"";  // Forbidden.
        } else {
            std::wstring target =
                d.targetEntry >= 0 && (size_t)d.targetEntry < d.targetColumn->entries.size()
                    ? d.targetColumn->entries[d.targetEntry].name
                    : FolderDisplayName(d.targetColumn->folder.get());
            const wchar_t* verb = L"Drop in ";
            glyph = L"";  // Move to a folder.
            if (ctrl && !shift) {
                verb = L"Copy to ";
                glyph = L"";
            } else if (alt) {
                verb = L"Link in ";
                glyph = L"";
            } else if (shift) {
                verb = L"Move to ";
            }
            line = std::wstring(verb) + Shortened(target, 28);
        }
    }
    if (line.empty()) {
        d.ghostRow.Visibility(mux::Visibility::Collapsed);
    } else {
        d.ghostIcon.Glyph(glyph);
        d.ghostAction.Text(line.c_str());
        d.ghostRow.Visibility(mux::Visibility::Visible);
    }
}

// Moves of the pointer. True while a drag is under way, so the hover code
// stands aside.
bool OnDragMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args) {
    auto& d = state->drag;
    if (!d.pending && !d.active) {
        return false;
    }
    if (d.column != column) {
        return d.active;
    }
    auto point = args.GetCurrentPoint(list);
    if (!point.Properties().IsLeftButtonPressed()) {
        DragEnd(state);
        return false;
    }
    auto at = point.Position();
    if (d.pending) {
        if (std::abs(at.X - d.start.X) + std::abs(at.Y - d.start.Y) < 8) {
            return false;  // Still a click.
        }
        std::vector<size_t> picked = SelectedEntries(column, list);
        if (std::find(picked.begin(), picked.end(), (size_t)d.pressedEntry) == picked.end()) {
            picked.assign(1, (size_t)d.pressedEntry);
        }
        d.entries = picked;
        d.pending = false;
        d.active = true;
        state->slowRenameAt = 0;
        Out(L"drag: moved far enough, taking the pointer");
        list.CapturePointer(args.Pointer());
        Out(L"drag: pointer taken, making the label");
        ShowGhost(state, SpecOf(column, d.entries));
        Out(L"drag started from column %d with %d item(s)", ColumnIndexOfList(state, list) + 1,
            (int)d.entries.size());
    }

    // Where the label goes, and the target under the pointer.
    try {
        auto overlayAt = args.GetCurrentPoint(state->overlay).Position();
        muxc::Canvas::SetLeft(d.ghost, overlayAt.X + 16);
        muxc::Canvas::SetTop(d.ghost, overlayAt.Y + 16);
    } catch (...) {
    }
    state->autoPoint = args.GetCurrentPoint(nullptr).Position();
    state->autoOn = true;
    DragRefreshTarget(state, state->autoPoint);
    DragUpdateLabel(state);
    // Past the edge of the panel the drag goes on as a real one.
    if (g_dragOut && state->rootGrid) {
        auto inRoot = args.GetCurrentPoint(state->rootGrid).Position();
        if (inRoot.X < 0 || inRoot.Y < 0 || inRoot.X > state->rootGrid.ActualWidth() ||
            inRoot.Y > state->rootGrid.ActualHeight()) {
            StartOutDrag(state);
            return true;
        }
    }
    return true;
}

// Hands the items to the folder's drop target. All the arguments are copies, so
// nothing depends on the columns still being as they were.
void DropItems(WindowState* state, Pidl sourceFolder, std::vector<Pidl> children, Pidl targetFolder,
               DWORD keyState) {
    IShellFolder* source = nullptr;
    HRESULT hr = ILIsEmpty(sourceFolder.get())
                     ? SHGetDesktopFolder(&source)
                     : SHBindToObject(nullptr, sourceFolder.get(), nullptr, IID_PPV_ARGS(&source));
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && source) {
        std::vector<PCUITEMID_CHILD> items;
        for (auto& child : children) {
            items.push_back(child.get());
        }
        hr = source->GetUIObjectOf(state->topLevel, (UINT)items.size(), items.data(), IID_IDataObject,
                                   nullptr, (void**)&data);
    }
    if (source) {
        source->Release();
    }
    IShellFolder* target = nullptr;
    if (SUCCEEDED(hr) && data) {
        hr = SHBindToObject(nullptr, targetFolder.get(), nullptr, IID_PPV_ARGS(&target));
    }
    IDropTarget* drop = nullptr;
    if (SUCCEEDED(hr) && target) {
        hr = target->CreateViewObject(state->topLevel, IID_PPV_ARGS(&drop));
    }
    if (target) {
        target->Release();
    }
    if (FAILED(hr) || !drop || !data) {
        Out(L"drop: no data object or drop target, hr=0x%08lX", hr);
        if (drop) {
            drop->Release();
        }
        if (data) {
            data->Release();
        }
        return;
    }
    POINT cursor;
    GetCursorPos(&cursor);
    POINTL where = {cursor.x, cursor.y};
    DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
    hr = drop->DragEnter(data, keyState, where, &effect);
    if (SUCCEEDED(hr) && effect != DROPEFFECT_NONE) {
        DWORD offered = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
        drop->DragOver(keyState, where, &offered);
        DWORD result = offered;
        hr = drop->Drop(data, keyState, where, &result);
        Out(L"dropped %d item(s) on \"%s\": effect %lu, hr=0x%08lX", (int)children.size(),
            PathOf(targetFolder.get()).c_str(), (unsigned long)result, hr);
    } else {
        drop->DragLeave();
        Out(L"the folder takes no drop, hr=0x%08lX effect=%lu", hr, (unsigned long)effect);
    }
    data->Release();
    drop->Release();
    ReloadAll(state);
}

// Puts the effect a target of the shell takes when no key says otherwise into the
// dragged data.
void SetPreferredEffect(IDataObject* data, DWORD effect) {
    static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!memory) {
        return;
    }
    if (DWORD* value = (DWORD*)GlobalLock(memory)) {
        *value = effect;
        GlobalUnlock(memory);
    }
    FORMATETC wanted = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = memory;
    if (FAILED(data->SetData(&wanted, &medium, TRUE))) {
        GlobalFree(memory);
    }
}

// The drag source of a real drag. It ends the drag on Escape or when the button
// goes up, and notes what the target under the pointer last offered and which
// window it was, for the log.
struct OutDropSource : IDropSource {
    LONG refs = 1;
    DWORD lastEffect = 0;
    DWORD dropKeys = 0;
    DWORD graceKeys = 0;  // The keys of a moment ago that still count at the release.
    IDataObject* data = nullptr;  // The dragged data, not owned.
    ULONGLONG ctrlAt = 0, shiftAt = 0, altAt = 0;  // When each key was last seen down.
    HWND dropWindow = nullptr;
    POINT dropPoint{};
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDropSource) {
            *out = static_cast<IDropSource*>(this);
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
    STDMETHODIMP QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) {
            return DRAGDROP_S_CANCEL;
        }
        ULONGLONG now = GetTickCount64();
        if (keys & MK_CONTROL) {
            ctrlAt = now;
        }
        if (keys & MK_SHIFT) {
            shiftAt = now;
        }
        if (keys & MK_ALT) {
            altAt = now;
        }
        if (!(keys & MK_LBUTTON)) {
            // A key let go a moment before the button still counts: the two
            // rarely come up in the same instant, and the wrong one would turn
            // a copy into a move.
            const ULONGLONG grace = 400;
            DWORD wanted = DROPEFFECT_NONE;
            if (now - altAt < grace) {
                wanted = DROPEFFECT_LINK;
            } else if (now - ctrlAt < grace && now - shiftAt >= grace) {
                wanted = DROPEFFECT_COPY;
            } else if (now - shiftAt < grace) {
                wanted = DROPEFFECT_MOVE;
            }
            graceKeys = wanted == DROPEFFECT_COPY   ? MK_CONTROL
                        : wanted == DROPEFFECT_MOVE ? MK_SHIFT
                        : wanted == DROPEFFECT_LINK ? (MK_CONTROL | MK_SHIFT)
                                                    : 0;
            if (wanted != DROPEFFECT_NONE && data) {
                SetPreferredEffect(data, wanted);
                Out(L"drag out: the keys of a moment ago ask for effect %lu", (unsigned long)wanted);
            }
            POINT cursor;
            GetCursorPos(&cursor);
            dropPoint = cursor;
            dropKeys = keys;
            dropWindow = WindowFromPoint(cursor);
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }
    HWND probeWindow = nullptr;
    DWORD probeEffect = 0xFFFFFFFF;
    // Probe (0.13.7): logs when the window under the pointer or the effect it
    // offers changes, and the tree item under the pointer over a tree view.
    int feedbackCalls = 0;
    void Probe(DWORD effect) {
        POINT cursor;
        GetCursorPos(&cursor);
        HWND under = WindowFromPoint(cursor);
        if (under == probeWindow && effect == probeEffect) {
            return;
        }
        probeWindow = under;
        probeEffect = effect;
        std::wstring cls = under ? ClassNameOf(under) : L"?";
        std::wstring item;
        if (under && cls == L"SysTreeView32") {
            TVHITTESTINFO hit = {};
            hit.pt = cursor;
            ScreenToClient(under, &hit.pt);
            HTREEITEM node = (HTREEITEM)SendMessageW(under, TVM_HITTEST, 0, (LPARAM)&hit);
            if (node) {
                wchar_t text[128] = L"";
                TVITEMW info = {};
                info.mask = TVIF_TEXT;
                info.hItem = node;
                info.pszText = text;
                info.cchTextMax = ARRAYSIZE(text);
                SendMessageW(under, TVM_GETITEMW, 0, (LPARAM)&info);
                item = text;
            } else {
                item = L"(no item)";
            }
        }
        Out(L"drag probe: over \"%s\" %p%s%s, effect offered %lu", cls.c_str(), under,
            item.empty() ? L"" : L" item ", item.c_str(), (unsigned long)effect);
    }
    STDMETHODIMP GiveFeedback(DWORD effect) override {
        lastEffect = effect;
        feedbackCalls++;
        Probe(effect);
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// The child of a shell item whose display name is `name`, or null. The caller
// releases it.
IShellItem* FindChildByName(IShellItem* parent, const std::wstring& name) {
    IEnumShellItems* items = nullptr;
    if (FAILED(parent->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))) || !items) {
        return nullptr;
    }
    IShellItem* found = nullptr;
    IShellItem* child = nullptr;
    ULONG fetched = 0;
    while (!found && items->Next(1, &child, &fetched) == S_OK && fetched == 1) {
        PWSTR text = nullptr;
        if (SUCCEEDED(child->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
            if (_wcsicmp(text, name.c_str()) == 0) {
                found = child;
                found->AddRef();
            }
            CoTaskMemFree(text);
        }
        child->Release();
        child = nullptr;
    }
    items->Release();
    return found;
}

// A folder called `name` among the places a pinned folder usually comes from: the
// known folders themselves, then what lies in the Desktop, the user's profile and
// This PC. The caller releases it.
IShellItem* FindInUsualPlaces(const std::wstring& name) {
    const KNOWNFOLDERID* ids[] = {&FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Pictures,
                                  &FOLDERID_Music,     &FOLDERID_Videos,    &FOLDERID_Desktop,
                                  &FOLDERID_Profile};
    std::vector<IShellItem*> places;
    for (const KNOWNFOLDERID* id : ids) {
        IShellItem* place = nullptr;
        if (SUCCEEDED(SHGetKnownFolderItem(*id, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&place))) && place) {
            places.push_back(place);
        }
    }
    PIDLIST_ABSOLUTE computer = nullptr;
    if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &computer)) && computer) {
        IShellItem* place = nullptr;
        if (SUCCEEDED(SHCreateItemFromIDList(computer, IID_PPV_ARGS(&place))) && place) {
            places.push_back(place);
        }
        CoTaskMemFree(computer);
    }
    IShellItem* found = nullptr;
    for (IShellItem* place : places) {
        if (found) {
            break;
        }
        PWSTR text = nullptr;
        if (SUCCEEDED(place->GetDisplayName(SIGDN_NORMALDISPLAY, &text)) && text) {
            if (_wcsicmp(text, name.c_str()) == 0) {
                found = place;
                found->AddRef();
            }
            CoTaskMemFree(text);
        }
        if (!found) {
            found = FindChildByName(place, name);
        }
    }
    for (IShellItem* place : places) {
        place->Release();
    }
    return found;
}

// The folder a navigation pane tree item stands for, found by walking the names
// of the item and its parents from the root: the tree keeps no pidl we can read.
// A root that is not under the Desktop is taken to be Home (Quick access).
Pidl FolderOfNavItem(HWND tree, HTREEITEM node) {
    std::vector<std::wstring> names;
    for (HTREEITEM walk = node; walk;
         walk = (HTREEITEM)SendMessageW(tree, TVM_GETNEXTITEM, TVGN_PARENT, (LPARAM)walk)) {
        wchar_t text[256] = L"";
        TVITEMW info = {};
        info.mask = TVIF_TEXT;
        info.hItem = walk;
        info.pszText = text;
        info.cchTextMax = ARRAYSIZE(text);
        SendMessageW(tree, TVM_GETITEMW, 0, (LPARAM)&info);
        names.insert(names.begin(), text);
    }
    if (names.empty()) {
        return Pidl();
    }
    IShellItem* current = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&current))) ||
        !current) {
        return Pidl();
    }
    size_t index = 0;
    if (IShellItem* root = FindChildByName(current, names[0])) {
        current->Release();
        current = root;
        index = 1;
    } else {
        current->Release();
        current = nullptr;
        // Home: the pinned folders are roots of the tree themselves, or children of
        // a root named Home, so look for the first name in Home, else skip it.
        if (FAILED(SHCreateItemFromParsingName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}", nullptr,
                                               IID_PPV_ARGS(&current))) ||
            !current) {
            Out(L"drag out: no folder found for the navigation pane item \"%s\"", names.back().c_str());
            return Pidl();
        }
        if (IShellItem* pinned = FindChildByName(current, names[0])) {
            current->Release();
            current = pinned;
        } else if (names.size() < 2) {
            current->Release();
            Out(L"drag out: no folder found for the navigation pane item \"%s\"", names.back().c_str());
            return Pidl();
        }
        index = 1;
    }
    for (; index < names.size(); ++index) {
        IShellItem* next = FindChildByName(current, names[index]);
        if (!next) {
            next = FindInUsualPlaces(names[index]);
        }
        current->Release();
        current = next;
        if (!current) {
            Out(L"drag out: no folder found for the navigation pane item \"%s\" (stopped at \"%s\", chain of %d, first \"%s\")",
                names.back().c_str(), names[index].c_str(), (int)names.size(), names[0].c_str());
            return Pidl();
        }
    }
    Pidl result;
    PWSTR path = nullptr;
    if (SUCCEEDED(current->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(path, nullptr, &pidl, 0, nullptr)) && pidl) {
            result.reset(pidl);
        }
        CoTaskMemFree(path);
    }
    if (!result) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(current, &pidl)) && pidl) {
            result.reset(pidl);
        }
    }
    current->Release();
    return result;
}

// The shell's drag loop is driven by mouse move messages, and the panel's input
// (a XAML island) takes them as pointer messages, so the loop never sees the
// pointer move: a target hears of the release point only, and the navigation
// pane, the views of other windows and other applications show no hover and
// answer nothing until then. This thread posts the missing moves to the drag
// loop's thread while the drag runs, with the position and the keys as they are.
struct MoveNudger {
    std::atomic<bool> stop{false};
    std::atomic<int> sent{0};
    std::thread worker;
    void Start(DWORD threadId) {
        worker = std::thread([this, threadId]() {
            POINT last = {LONG_MIN, LONG_MIN};
            while (!stop.load()) {
                POINT now;
                if (GetCursorPos(&now) && (now.x != last.x || now.y != last.y)) {
                    last = now;
                    WPARAM keys = 0;
                    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) keys |= MK_LBUTTON;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keys |= MK_CONTROL;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keys |= MK_SHIFT;
                    if (PostThreadMessageW(threadId, WM_MOUSEMOVE, keys,
                                           MAKELPARAM((WORD)now.x, (WORD)now.y))) {
                        sent++;
                    }
                }
                Sleep(15);
            }
        });
    }
    void Stop() {
        stop = true;
        if (worker.joinable()) {
            worker.join();
        }
    }
    ~MoveNudger() { Stop(); }
};

// A real drag, for when the pointer leaves the panel: the shell's own drag loop
// carries the items to whatever is under the pointer, another Explorer window,
// the desktop or another application. Started after the event that noticed it,
// and with the panel's own drag already ended.
void DoOutDrag(WindowState* state, Pidl sourceFolder, std::vector<Pidl> children,
               DWORD preferred) {
    IShellFolder* source = nullptr;
    HRESULT hr = ILIsEmpty(sourceFolder.get())
                     ? SHGetDesktopFolder(&source)
                     : SHBindToObject(nullptr, sourceFolder.get(), nullptr, IID_PPV_ARGS(&source));
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && source) {
        std::vector<PCUITEMID_CHILD> items;
        for (auto& child : children) {
            items.push_back(child.get());
        }
        hr = source->GetUIObjectOf(state->topLevel, (UINT)items.size(), items.data(), IID_IDataObject,
                                   nullptr, (void**)&data);
    }
    if (source) {
        source->Release();
    }
    if (FAILED(hr) || !data) {
        Out(L"drag out: no data object, hr=0x%08lX", hr);
        return;
    }
    DWORD effect = DROPEFFECT_NONE;
    // The keys held as the pointer left the panel decide the default effect: a
    // target of the shell reads it from the data when no key says otherwise. The
    // drag loop does not see a key pressed after it has started.
    if (preferred != DROPEFFECT_NONE) {
        SetPreferredEffect(data, preferred);
    }
    Out(L"drag out: %d item(s) leave the panel, starting the shell's drag, default effect %lu",
        (int)children.size(), (unsigned long)preferred);
    // 0.13.9: the panel's pointer capture (XAML or Win32) may still hold the mouse,
    // so the drag loop would see no moves until the release.
    ReleaseCapture();
    OutDropSource* dropSource = new OutDropSource();
    dropSource->data = data;
    MoveNudger nudger;
    nudger.Start(GetCurrentThreadId());
    state->outDragActive = true;
    hr = SHDoDragDrop(state->topLevel, data, dropSource,
                      DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    state->outDragActive = false;
    RemoveGhost(state);
    nudger.Stop();
    Out(L"drag out: the drag loop asked for feedback %d time(s), %d pointer move(s) posted to it",
        dropSource->feedbackCalls, nudger.sent.load());
    {
        wchar_t title[80] = L"";
        DWORD processId = 0;
        if (dropSource->dropWindow) {
            GetWindowTextW(dropSource->dropWindow, title, ARRAYSIZE(title));
            GetWindowThreadProcessId(dropSource->dropWindow, &processId);
        }
        Out(L"drag out ended: effect %lu, hr=0x%08lX; keys at the drop 0x%lX; the target last offered effect %lu; dropped over a \"%s\" window \"%s\" of process %lu",
            (unsigned long)effect, hr, (unsigned long)dropSource->dropKeys,
            (unsigned long)dropSource->lastEffect,
            dropSource->dropWindow ? ClassNameOf(dropSource->dropWindow).c_str() : L"?", title,
            (unsigned long)processId);
    }
    if (dropSource->dropWindow) {
        HWND root = GetAncestor(dropSource->dropWindow, GA_ROOT);
        RECT rect{};
        wchar_t rootTitle[80] = L"";
        if (root) {
            GetWindowRect(root, &rect);
            GetWindowTextW(root, rootTitle, ARRAYSIZE(rootTitle));
        }
        Out(L"drag out: released at %ld,%ld over top-level \"%s\" \"%s\" at %ld,%ld to %ld,%ld",
            dropSource->dropPoint.x, dropSource->dropPoint.y,
            root ? ClassNameOf(root).c_str() : L"?", rootTitle, rect.left, rect.top, rect.right,
            rect.bottom);
    }
    // 0.14.0: the drag loop gets no pointer moves from the panel's input, so it only
    // tells a target about the release point, and the navigation pane's tree needs a
    // hovered item to take a drop. Dropped on that tree, the drop is done here, to
    // the folder the item under the pointer stands for, with the folder's own drop
    // target like a drop between columns.
    HWND treeWindow = dropSource->dropWindow;
    POINT treePoint = dropSource->dropPoint;
    DWORD treeKeys = dropSource->dropKeys & (MK_CONTROL | MK_SHIFT | MK_ALT);
    treeKeys |= dropSource->graceKeys;
    // A target that answered the drag with one effect has taken the drop itself;
    // only one that kept the whole offer (all three effects) has heard nothing.
    bool unanswered = (dropSource->lastEffect & (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK)) ==
                      (DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK);
    dropSource->Release();
    data->Release();
    DWORD treeProcess = 0;
    if (treeWindow && ClassNameOf(treeWindow) == L"SysTreeView32") {
        GetWindowThreadProcessId(treeWindow, &treeProcess);
    }
    if (treeProcess == GetCurrentProcessId() && !children.empty() && !unanswered) {
        Out(L"drag out: the navigation pane answered the drag itself, nothing to finish");
    } else if (treeProcess == GetCurrentProcessId() && !children.empty()) {
        TVHITTESTINFO hit = {};
        hit.pt = treePoint;
        ScreenToClient(treeWindow, &hit.pt);
        HTREEITEM node = (HTREEITEM)SendMessageW(treeWindow, TVM_HITTEST, 0, (LPARAM)&hit);
        if (!node) {
            Out(L"drag out: released on the navigation pane, but not on an item");
        } else {
            Pidl target = FolderOfNavItem(treeWindow, node);
            if (target) {
                // The shell may have done the move itself: look before doing it again.
                IShellFolder* sourceFolderObject = nullptr;
                HRESULT bound = ILIsEmpty(sourceFolder.get())
                                    ? SHGetDesktopFolder(&sourceFolderObject)
                                    : SHBindToObject(nullptr, sourceFolder.get(), nullptr,
                                                     IID_PPV_ARGS(&sourceFolderObject));
                bool stillThere = true;
                if (SUCCEEDED(bound) && sourceFolderObject) {
                    PCUITEMID_CHILD one = children[0].get();
                    ULONG attributes = SFGAO_VALIDATE;
                    stillThere = SUCCEEDED(sourceFolderObject->GetAttributesOf(1, &one, &attributes));
                    sourceFolderObject->Release();
                }
                std::wstring targetPath = PathOf(target.get());
                if (!stillThere) {
                    Out(L"drag out: the items are gone from their folder, nothing left to drop on the navigation pane item");
                } else if (_wcsicmp(targetPath.c_str(), PathOf(sourceFolder.get()).c_str()) == 0) {
                    Out(L"drag out: the navigation pane item is the folder the items came from, nothing to do");
                } else {
                    Out(L"drag out: finishing the drop on the navigation pane item, folder \"%s\", keys 0x%lX",
                        targetPath.c_str(), (unsigned long)treeKeys);
                    DropItems(state, std::move(sourceFolder), std::move(children), std::move(target),
                              treeKeys);
                }
            }
        }
    }
    ReloadAll(state);
}

void StartOutDrag(WindowState* state) {
    auto& d = state->drag;
    if (!d.active || !d.column) {
        return;
    }
    Pidl sourceFolder(ILClone(d.column->folder.get()));
    std::vector<Pidl> children;
    for (size_t index : d.entries) {
        if (index < d.column->entries.size()) {
            children.emplace_back(ILClone(d.column->entries[index].child.get()));
        }
    }
    Out(L"drag out: the pointer left the panel, ending the panel's own drag");
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    DWORD preferred = alt ? DROPEFFECT_LINK : (ctrl && !shift) ? DROPEFFECT_COPY
                      : shift ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
    state->outSpec = SpecOf(d.column, d.entries);
    DragEnd(state);
    if (children.empty()) {
        return;
    }
    auto shared = std::make_shared<std::pair<Pidl, std::vector<Pidl>>>(std::move(sourceFolder),
                                                                       std::move(children));
    state->queue.TryEnqueue([shared, preferred]() {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->on || g_unloading) {
            return;
        }
        try {
            DoOutDrag(now, std::move(shared->first), std::move(shared->second), preferred);
        } catch (...) {
            LogHresult(L"dragging out");
        }
    });
}

// The button went up. True if it ended a drag.
bool OnDragReleased(WindowState* state) {
    auto& d = state->drag;
    if (d.pending) {
        DragEnd(state);
        return false;
    }
    if (!d.active) {
        return false;
    }
    Column* sourceColumn = d.column;
    Column* targetColumn = d.targetColumn;
    int targetEntry = d.targetEntry;
    // Judged again with the keys as they are at the release.
    bool ok = d.targetColumn && DragTargetAllowed(state, d.targetColumn, d.targetEntry);
    std::vector<size_t> entries = d.entries;
    DragEnd(state);
    if (!ok || !targetColumn || !sourceColumn) {
        Out(L"drag ended outside a folder that takes it");
        return true;
    }
    Pidl sourceFolder(ILClone(sourceColumn->folder.get()));
    std::vector<Pidl> children;
    for (size_t index : entries) {
        if (index < sourceColumn->entries.size()) {
            children.emplace_back(ILClone(sourceColumn->entries[index].child.get()));
        }
    }
    Pidl targetFolder = DragTargetFolder(targetColumn, targetEntry);
    if (children.empty() || !targetFolder) {
        return true;
    }
    DWORD keyState = MK_LBUTTON;
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        keyState |= MK_CONTROL;
    }
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        keyState |= MK_SHIFT;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        keyState |= MK_ALT;
    }
    // After the event has finished, since the shell may show a dialog.
    auto shared = std::make_shared<std::tuple<Pidl, std::vector<Pidl>, Pidl>>(
        std::move(sourceFolder), std::move(children), std::move(targetFolder));
    state->queue.TryEnqueue([shared, keyState]() {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->on || g_unloading) {
            return;
        }
        try {
            DropItems(now, std::move(std::get<0>(*shared)), std::move(std::get<1>(*shared)),
                      std::move(std::get<2>(*shared)), keyState);
        } catch (...) {
            LogHresult(L"dropping items");
        }
    });
    return true;
}
