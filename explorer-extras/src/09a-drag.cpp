
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
        if (d.litRow) {
            if (d.litRowBrush) {
                d.litRow.Background(d.litRowBrush);
            } else {
                d.litRow.ClearValue(muxc::Control::BackgroundProperty());
            }
        }
        if (d.litColumn && d.litColumn->cell) {
            d.litColumn->cell.Background(d.litCellBrush);
        }
    } catch (...) {
        LogHresult(L"clearing the drop target");
    }
    d.litRow = nullptr;
    d.litRowBrush = nullptr;
    d.litColumn = nullptr;
    d.litCellBrush = nullptr;
    d.targetColumn = nullptr;
    d.targetEntry = -1;
    d.targetOk = false;
}

void DragEnd(WindowState* state) {
    auto& d = state->drag;
    DragClearTarget(state);
    try {
        if (d.ghost && state->overlay) {
            uint32_t index = 0;
            if (state->overlay.Children().IndexOf(d.ghost, index)) {
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
        // The label that follows the pointer.
        muxc::Border ghost;
        ghost.Background(Brush(235, 40, 40, 40));
        ghost.BorderBrush(Brush(120, 128, 128, 128));
        ghost.BorderThickness(mux::Thickness{1, 1, 1, 1});
        ghost.CornerRadius(mux::CornerRadius{4, 4, 4, 4});
        ghost.Padding(mux::Thickness{8, 3, 8, 3});
        ghost.IsHitTestVisible(false);
        muxc::TextBlock text;
        text.FontSize(12);
        text.Foreground(Brush(255, 255, 255, 255));
        ghost.Child(text);
        d.ghost = ghost;
        d.ghostText = text;
        state->overlay.Children().Append(ghost);
        Out(L"drag started from column %d with %d item(s)", ColumnIndexOfList(state, list) + 1,
            (int)d.entries.size());
    }

    // The label: what is dragged and what the keys ask for.
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    std::wstring name = d.entries.size() == 1 && d.entries[0] < column->entries.size()
                            ? column->entries[d.entries[0]].name
                            : std::to_wstring(d.entries.size()) + L" items";
    if (name.size() > 28) {
        name = name.substr(0, 27) + L"…";
    }
    const wchar_t* action = ctrl && !shift ? L"Copy" : alt ? L"Link" : shift ? L"Move" : L"";
    d.ghostText.Text(action[0] ? (name + L"  ·  " + action).c_str() : name.c_str());
    try {
        auto overlayAt = args.GetCurrentPoint(state->overlay).Position();
        muxc::Canvas::SetLeft(d.ghost, overlayAt.X + 14);
        muxc::Canvas::SetTop(d.ghost, overlayAt.Y + 14);
    } catch (...) {
    }

    // The target under the pointer.
    Column* targetColumn = nullptr;
    int targetEntry = -1;
    bool found = false;
    try {
        found = DragTargetAt(state, args.GetCurrentPoint(nullptr).Position(), &targetColumn, &targetEntry);
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
                winrt::Windows::UI::Color tint = state->colorsSet ? state->pillColor
                                                                  : winrt::Windows::UI::Color{255, 76, 194, 255};
                try {
                    if (targetEntry >= 0) {
                        int rowIndex = targetColumn->entryToRow[targetEntry];
                        auto row = targetColumn->list.Items().GetAt((uint32_t)rowIndex).try_as<muxc::ListViewItem>();
                        if (row) {
                            d.litRow = row;
                            d.litRowBrush = row.Background();
                            row.Background(Brush(80, tint.R, tint.G, tint.B));
                        }
                    } else if (targetColumn->cell) {
                        d.litColumn = targetColumn;
                        d.litCellBrush = targetColumn->cell.Background();
                        targetColumn->cell.Background(Brush(40, tint.R, tint.G, tint.B));
                    }
                } catch (...) {
                    LogHresult(L"lighting the drop target");
                }
            }
        }
    }
    d.ghost.Opacity(d.targetOk || !found ? 1.0 : 0.55);
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

// The drag source of a real drag. It ends the drag on Escape or when the button
// goes up, and notes what the target under the pointer last offered and which
// window it was, for the log.
struct OutDropSource : IDropSource {
    LONG refs = 1;
    DWORD lastEffect = 0;
    HWND dropWindow = nullptr;
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
        if (!(keys & MK_LBUTTON)) {
            POINT cursor;
            GetCursorPos(&cursor);
            dropWindow = WindowFromPoint(cursor);
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD effect) override {
        lastEffect = effect;
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// A real drag, for when the pointer leaves the panel: the shell's own drag loop
// carries the items to whatever is under the pointer, another Explorer window,
// the desktop or another application. Started after the event that noticed it,
// and with the panel's own drag already ended.
void DoOutDrag(WindowState* state, Pidl sourceFolder, std::vector<Pidl> children) {
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
    Out(L"drag out: %d item(s) leave the panel, starting the shell's drag", (int)children.size());
    OutDropSource* dropSource = new OutDropSource();
    hr = SHDoDragDrop(state->topLevel, data, dropSource,
                      DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    {
        wchar_t title[80] = L"";
        DWORD processId = 0;
        if (dropSource->dropWindow) {
            GetWindowTextW(dropSource->dropWindow, title, ARRAYSIZE(title));
            GetWindowThreadProcessId(dropSource->dropWindow, &processId);
        }
        Out(L"drag out ended: effect %lu, hr=0x%08lX; the target last offered effect %lu; dropped over a \"%s\" window \"%s\" of process %lu",
            (unsigned long)effect, hr, (unsigned long)dropSource->lastEffect,
            dropSource->dropWindow ? ClassNameOf(dropSource->dropWindow).c_str() : L"?", title,
            (unsigned long)processId);
    }
    dropSource->Release();
    data->Release();
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
    DragEnd(state);
    if (children.empty()) {
        return;
    }
    auto shared = std::make_shared<std::pair<Pidl, std::vector<Pidl>>>(std::move(sourceFolder),
                                                                       std::move(children));
    state->queue.TryEnqueue([shared]() {
        WindowState* now = StateForThisThread(false);
        if (!now || !now->on || g_unloading) {
            return;
        }
        try {
            DoOutDrag(now, std::move(shared->first), std::move(shared->second));
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
