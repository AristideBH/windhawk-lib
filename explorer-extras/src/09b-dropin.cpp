
// ---- Drops coming into the columns, and the scroll while dragging ----
//
// Files dragged in from another Explorer window, the desktop or another
// application land on the panel's window. The panel registers its own OLE drop
// target there and hands everything to the folder under the pointer: the folder
// row, or the column's own folder. The folder's own drop target decides what
// the pointer shows (move, copy, link, nothing) and does the work, with its own
// conflict and progress dialogs, exactly as for a drop in an Explorer view, so
// virtual items and long operations need nothing from us. The row or column it
// would land on is lit as in the drag of rows between columns.

// Whether a drop may go to this column or this folder row.
bool InboundTargetAllowed(WindowState* state, Column* column, int entry, const Pidl& target) {
    if (entry >= 0) {
        if ((size_t)entry >= column->entries.size()) {
            return false;
        }
        auto& hit = column->entries[entry];
        if (!hit.folder || hit.archive || hit.launcher) {
            return false;
        }
    } else if (ArchiveParentColumn(state, column) >= 0) {
        return false;
    }
    wchar_t path[MAX_PATH] = L"";
    return target && SHGetPathFromIDListW(target.get(), path) && path[0];  // A folder on disk.
}

// A point of the screen in the panel's host coordinates.
winrt::Windows::Foundation::Point HostPointOf(WindowState* state, POINTL pt) {
    HWND bridgeWindow = (HWND)state->bridge.WindowId().Value;
    POINT client = {pt.x, pt.y};
    ScreenToClient(bridgeWindow, &client);
    double scale = 1.0;
    if (auto xamlRoot = state->rootGrid.XamlRoot()) {
        scale = xamlRoot.RasterizationScale();
    }
    if (scale <= 0) {
        scale = 1.0;
    }
    return winrt::Windows::Foundation::Point{(float)(client.x / scale), (float)(client.y / scale)};
}

struct PanelDropTarget : IDropTarget {
    LONG refs = 1;
    IDataObject* data = nullptr;
    IDropTarget* folder = nullptr;  // The folder's own target, between its DragEnter and DragLeave.
    Pidl folderPidl;
    DWORD lastKeys = 0;
    POINTL lastPoint{};
    DWORD lastWanted = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
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

    // Ends the folder's part of the drag, without a drop.
    void LeaveFolder() {
        if (folder) {
            folder->DragLeave();
            folder->Release();
            folder = nullptr;
        }
        folderPidl.reset();
    }
    void DropData() {
        if (data) {
            data->Release();
            data = nullptr;
        }
    }

    // The pointer is at `pt` (screen). Settles the target and gives the effect.
    void Settle(DWORD keys, POINTL pt, DWORD* effect) {
        lastKeys = keys;
        lastPoint = pt;
        DWORD wanted = *effect;
        lastWanted = wanted;
        *effect = DROPEFFECT_NONE;
        WindowState* state = StateForThisThread(false);
        if (!state || !state->on || !state->bridge || !state->rootGrid || !data) {
            LeaveFolder();
            return;
        }
        Column* column = nullptr;
        int entry = -1;
        bool found = false;
        try {
            winrt::Windows::Foundation::Point where = HostPointOf(state, pt);
            state->autoPoint = where;
            state->autoOn = true;
            found = DragTargetAt(state, where, &column, &entry);
        } catch (...) {
            LogHresult(L"finding the drop target of a drop coming in");
        }
        Pidl target;
        if (found && column) {
            target = DragTargetFolder(column, entry);
            if (!InboundTargetAllowed(state, column, entry, target)) {
                target.reset();
            }
        }
        if (!target) {
            LeaveFolder();
            DragClearTarget(state);
            return;
        }
        if (!folder || !folderPidl || !ILIsEqual(folderPidl.get(), target.get())) {
            LeaveFolder();
            IShellFolder* shellFolder = nullptr;
            IDropTarget* created = nullptr;
            if (SUCCEEDED(SHBindToObject(nullptr, target.get(), nullptr, IID_PPV_ARGS(&shellFolder))) &&
                shellFolder) {
                shellFolder->CreateViewObject(state->topLevel, IID_PPV_ARGS(&created));
                shellFolder->Release();
            }
            if (!created) {
                DragClearTarget(state);
                return;
            }
            DWORD offered = wanted;
            if (FAILED(created->DragEnter(data, keys, pt, &offered))) {
                created->Release();
                DragClearTarget(state);
                return;
            }
            folder = created;
            folderPidl = std::move(target);
            *effect = offered;
        } else {
            DWORD offered = wanted;
            if (SUCCEEDED(folder->DragOver(keys, pt, &offered))) {
                *effect = offered;
            }
        }
        auto& d = state->drag;
        bool ok = *effect != DROPEFFECT_NONE;
        if (column != d.targetColumn || entry != d.targetEntry || ok != d.targetOk) {
            DragClearTarget(state);
            d.targetColumn = column;
            d.targetEntry = entry;
            d.targetOk = ok;
            if (ok) {
                DragLight(state, column, entry);
            }
        } else if (ok) {
            DragPlaceFrame(state);
        }
    }

    IDropTargetHelper* helper = nullptr;  // Draws the image of a drag that comes from elsewhere.
    bool helperActive = false;

    // The label: our own drag coming back gets the label it had in the panel, a
    // drag from elsewhere gets the shell's image, by its helper.
    void Over(DWORD keys, POINTL pt, DWORD* effect) {
        Settle(keys, pt, effect);
        WindowState* state = StateForThisThread(false);
        if (!state || !state->on || !state->bridge || !state->rootGrid) {
            return;
        }
        try {
            if (state->outDragActive) {
                if (!state->drag.ghost) {
                    ShowGhost(state, state->outSpec);
                }
                auto where = HostPointOf(state, pt);
                muxc::Canvas::SetLeft(state->drag.ghost, where.X + 16);
                muxc::Canvas::SetTop(state->drag.ghost, where.Y + 16);
                DragUpdateLabel(state);
                state->drag.ghost.Opacity(state->drag.targetOk || !state->drag.targetColumn ? 1.0 : 0.8);
            } else if (helperActive && helper) {
                POINT p = {pt.x, pt.y};
                helper->DragOver(&p, *effect);
            }
        } catch (...) {
            LogHresult(L"the label of a drag coming in");
        }
    }
    void HideLabel() {
        if (helper && helperActive) {
            helper->DragLeave();
        }
        helperActive = false;
        if (WindowState* state = StateForThisThread(false)) {
            if (state->drag.ghost) {
                RemoveGhost(state);
            }
        }
    }

    // After a scroll under a pointer that did not move: judge the target again.
    void Refresh() {
        if (!data) {
            return;
        }
        DWORD effect = lastWanted;
        Over(lastKeys, lastPoint, &effect);
    }

    STDMETHODIMP DragEnter(IDataObject* object, DWORD keys, POINTL pt, DWORD* effect) override {
        DropData();
        data = object;
        if (data) {
            data->AddRef();
        }
        WindowState* state = StateForThisThread(false);
        if (state && state->on && !state->outDragActive && object) {
            if (!helper) {
                CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper));
            }
            if (helper && state->bridge) {
                POINT p = {pt.x, pt.y};
                helperActive = SUCCEEDED(helper->DragEnter((HWND)state->bridge.WindowId().Value, object, &p,
                                                           *effect));
            }
        }
        Over(keys, pt, effect);
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD keys, POINTL pt, DWORD* effect) override {
        Over(keys, pt, effect);
        return S_OK;
    }
    STDMETHODIMP DragLeave() override {
        HideLabel();
        LeaveFolder();
        DropData();
        if (WindowState* state = StateForThisThread(false)) {
            DragClearTarget(state);
            state->autoOn = false;
        }
        return S_OK;
    }
    STDMETHODIMP Drop(IDataObject* object, DWORD keys, POINTL pt, DWORD* effect) override {
        if (!data && object) {
            data = object;
            data->AddRef();
        }
        DWORD wanted = *effect;
        Over(keys, pt, effect);
        WindowState* state = StateForThisThread(false);
        if (helper && helperActive) {
            POINT p = {pt.x, pt.y};
            helper->Drop(data, &p, *effect);
            helperActive = false;
        }
        if (state && state->drag.ghost) {
            RemoveGhost(state);
        }
        if (folder && *effect != DROPEFFECT_NONE) {
            DWORD result = wanted;
            HRESULT hr = folder->Drop(data, keys, pt, &result);
            Out(L"drop coming in on \"%s\": effect %lu, hr=0x%08lX", PathOf(folderPidl.get()).c_str(),
                (unsigned long)result, hr);
            *effect = result;
            folder->Release();  // The drop ends the folder's part: no DragLeave.
            folder = nullptr;
            folderPidl.reset();
        } else {
            LeaveFolder();
            *effect = DROPEFFECT_NONE;
        }
        DropData();
        if (state) {
            DragClearTarget(state);
            state->autoOn = false;
            ReloadAll(state);
        }
        return S_OK;
    }
};

void RegisterPanelDropTarget(WindowState* state) {
    if (state->panelDrop || !state->bridge) {
        return;
    }
    HWND window = (HWND)state->bridge.WindowId().Value;
    auto* target = new PanelDropTarget();
    HRESULT hr = RegisterDragDrop(window, target);
    if (hr == DRAGDROP_E_ALREADYREGISTERED) {
        RevokeDragDrop(window);  // The XAML island's own, which nothing here uses.
        hr = RegisterDragDrop(window, target);
    }
    if (FAILED(hr)) {
        Out(L"the panel could not take drops coming in, hr=0x%08lX", hr);
        target->Release();
        return;
    }
    state->panelDrop = target;
    state->panelDropWindow = window;
    Out(L"the panel takes drops coming in, window %p", window);
}

void RevokePanelDropTarget(WindowState* state) {
    if (!state->panelDrop) {
        return;
    }
    auto* target = static_cast<PanelDropTarget*>(state->panelDrop);
    state->panelDrop = nullptr;
    if (IsWindow(state->panelDropWindow)) {
        RevokeDragDrop(state->panelDropWindow);
    }
    state->panelDropWindow = nullptr;
    target->HideLabel();
    target->LeaveFolder();
    target->DropData();
    if (target->helper) {
        target->helper->Release();
        target->helper = nullptr;
    }
    target->Release();
}

// While something is dragged over the panel, a column near its top or bottom
// edge scrolls, and the strip near its left or right edge. Run by the tick; the
// speed grows as the pointer nears (or passes) the edge.
void DragAutoScroll(WindowState* state) {
    if (!state->autoOn || !state->rootGrid || !state->scroller) {
        return;
    }
    const double edge = 48, fastest = 28;
    auto at = state->autoPoint;
    bool scrolled = false;
    try {
        double width = state->rootGrid.ActualWidth();
        double dx = 0;
        if (at.X < edge) {
            dx = -std::min(1.0, (edge - at.X) / edge) * fastest;
        } else if (at.X > width - edge) {
            dx = std::min(1.0, (at.X - (width - edge)) / edge) * fastest;
        }
        if (dx != 0) {
            double offset = state->scroller.HorizontalOffset();
            double target = std::clamp(offset + dx, 0.0, state->scroller.ScrollableWidth());
            if (std::abs(target - offset) > 0.5) {
                state->scroller.ChangeView(target, nullptr, nullptr, true);
                scrolled = true;
            }
        }
        Column* column = nullptr;
        int entry = -1;
        if (DragTargetAt(state, at, &column, &entry) && column && column->list) {
            auto scroller = FindListScroller(column->list, 0);
            if (scroller) {
                double top = column->list.TransformToVisual(state->rootGrid)
                                 .TransformPoint(winrt::Windows::Foundation::Point{0, 0})
                                 .Y;
                double height = column->list.ActualHeight();
                double dy = 0;
                if (at.Y < top + edge) {
                    dy = -std::min(1.0, (top + edge - at.Y) / edge) * fastest;
                } else if (at.Y > top + height - edge) {
                    dy = std::min(1.0, (at.Y - (top + height - edge)) / edge) * fastest;
                }
                if (dy != 0) {
                    double offset = scroller.VerticalOffset();
                    double target = std::clamp(offset + dy, 0.0, scroller.ScrollableHeight());
                    if (std::abs(target - offset) > 0.5) {
                        scroller.ChangeView(nullptr, target, nullptr, true);
                        scrolled = true;
                    }
                }
            }
        }
    } catch (...) {
        LogHresult(L"scrolling while dragging");
        return;
    }
    if (scrolled) {
        // What is under the pointer has moved.
        if (state->drag.active) {
            DragRefreshTarget(state, at);
        } else if (state->panelDrop) {
            static_cast<PanelDropTarget*>(state->panelDrop)->Refresh();
        }
    }
}
