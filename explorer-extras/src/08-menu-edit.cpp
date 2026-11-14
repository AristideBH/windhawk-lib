bool TryShowNewTab(WindowState* state);
void OnTick();
// ---- Right-click menu ----

// ---- Rename in place and clipboard, which the shell menu can't do here ----

void CancelRename(WindowState* state) {
    if (!state->rename) {
        return;
    }
    auto rename = std::move(state->rename);
    rename->done = true;
    g_renaming = false;
    try {
        if (rename->box) {
            rename->box.KeyDown(rename->keyToken);
            rename->box.LostFocus(rename->lostToken);
        }
        if (rename->grid && rename->box) {
            uint32_t index = 0;
            if (rename->grid.Children().IndexOf(rename->box, index)) {
                rename->grid.Children().RemoveAt(index);
            }
        }
        if (rename->text) {
            rename->text.Visibility(mux::Visibility::Visible);
        }
    } catch (...) {
        LogHresult(L"CancelRename");
    }
}

// ---- Undo for deletes and renames ----

std::wstring FilePathOf(PCIDLIST_ABSOLUTE folder, PCUITEMID_CHILD child) {
    Pidl absolute(ILCombine(folder, child));
    wchar_t path[MAX_PATH] = L"";
    if (absolute && SHGetPathFromIDListW(absolute.get(), path)) {
        return path;
    }
    return L"";
}

void PushUndo(WindowState* state, UndoEntry&& entry) {
    state->redo.clear();  // A new action ends the redo history.
    state->undo.push_back(std::move(entry));
    if (state->undo.size() > 20) {
        state->undo.erase(state->undo.begin());
    }
}

// Remember a delete: the paths that are gone now. A delete that didn't happen
// leaves nothing to undo.
void RememberDeletion(WindowState* state, std::vector<std::wstring> const& paths) {
    UndoEntry entry;
    entry.deletion = true;
    for (auto& path : paths) {
        if (!path.empty() && !PathFileExistsW(path.c_str())) {
            entry.paths.push_back(path);
        }
    }
    if (!entry.paths.empty()) {
        PushUndo(state, std::move(entry));
    }
}

// Puts deleted files back from the Recycle Bin: the newest bin item for each
// original path gets the bin's own "undelete" command.
HRESULT RestoreFromRecycleBin(WindowState* state,
                              std::vector<std::wstring> const& paths) {
    PIDLIST_ABSOLUTE bin = nullptr;
    HRESULT hr = SHParseDisplayName(L"shell:RecycleBinFolder", nullptr, &bin, 0,
                                    nullptr);
    if (FAILED(hr) || !bin) {
        return hr;
    }
    IShellFolder* folder = nullptr;
    hr = SHBindToObject(nullptr, bin, nullptr, IID_PPV_ARGS(&folder));
    CoTaskMemFree(bin);
    if (FAILED(hr) || !folder) {
        return hr;
    }
    IShellFolder2* folder2 = nullptr;
    folder->QueryInterface(IID_PPV_ARGS(&folder2));
    static const PROPERTYKEY kDeletedFrom = {
        {0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 2};
    static const PROPERTYKEY kDateDeleted = {
        {0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 3};

    std::vector<Pidl> best(paths.size());
    std::vector<double> bestDate(paths.size(), -1);
    IEnumIDList* list = nullptr;
    if (folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS |
                                         SHCONTF_INCLUDEHIDDEN,
                            &list) == S_OK &&
        list) {
        PITEMID_CHILD child = nullptr;
        int seen = 0;
        while (seen++ < 5000 && list->Next(1, &child, nullptr) == S_OK && child) {
            // The bin names an item by its original path. The original folder
            // and the plain name are the second way to say the same.
            std::wstring normal = DisplayName(folder, child);
            std::wstring name = [&]() {
                STRRET text = {};
                wchar_t buffer[MAX_PATH] = L"";
                if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_INFOLDER, &text)) &&
                    SUCCEEDED(StrRetToBufW(&text, child, buffer, MAX_PATH))) {
                    return std::wstring(buffer);
                }
                return std::wstring();
            }();
            std::wstring from;
            double date = 0;
            if (folder2) {
                VARIANT value;
                VariantInit(&value);
                if (SUCCEEDED(folder2->GetDetailsEx(child, &kDeletedFrom, &value)) &&
                    value.vt == VT_BSTR && value.bstrVal) {
                    from = value.bstrVal;
                }
                VariantClear(&value);
                if (SUCCEEDED(folder2->GetDetailsEx(child, &kDateDeleted, &value)) &&
                    value.vt == VT_DATE) {
                    date = value.date;
                }
                VariantClear(&value);
            }
            std::wstring joined = from.empty() ? L"" : from + L"\\" + name;
            for (size_t k = 0; k < paths.size(); k++) {
                if ((!normal.empty() && _wcsicmp(normal.c_str(), paths[k].c_str()) == 0) ||
                    (!joined.empty() && _wcsicmp(joined.c_str(), paths[k].c_str()) == 0)) {
                    if (date >= bestDate[k]) {
                        bestDate[k] = date;
                        best[k].reset(ILClone(child));
                    }
                }
            }
            CoTaskMemFree(child);
            child = nullptr;
        }
        list->Release();
    }
    if (folder2) {
        folder2->Release();
    }

    std::vector<PCUITEMID_CHILD> chosen;
    for (auto& item : best) {
        if (item) {
            chosen.push_back(item.get());
        }
    }
    Out(L"undo delete: %d of %d item(s) found in the Recycle Bin", (int)chosen.size(),
        (int)paths.size());
    hr = chosen.empty() ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : S_OK;
    if (!chosen.empty()) {
        IContextMenu* menu = nullptr;
        hr = folder->GetUIObjectOf(state->topLevel, (UINT)chosen.size(),
                                   chosen.data(), IID_IContextMenu, nullptr,
                                   (void**)&menu);
        if (SUCCEEDED(hr) && menu) {
            HMENU hmenu = CreatePopupMenu();
            hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL);
            if (SUCCEEDED(hr)) {
                CMINVOKECOMMANDINFO invoke = {};
                invoke.cbSize = sizeof(invoke);
                invoke.hwnd = state->topLevel;
                invoke.lpVerb = "undelete";
                invoke.nShow = SW_SHOWNORMAL;
                hr = menu->InvokeCommand(&invoke);
            }
            DestroyMenu(hmenu);
            menu->Release();
        }
    }
    folder->Release();
    return hr;
}

// Takes back the last delete or rename this mod knows about. False if there
// is none.
bool UndoLast(WindowState* state) {
    if (state->undo.empty()) {
        return false;
    }
    UndoEntry entry = std::move(state->undo.back());
    state->undo.pop_back();
    HRESULT hr;
    if (entry.created) {
        // The items that were moved into it go back first.
        for (size_t i = entry.moves.size(); i-- > 0;) {
            MoveFileW(entry.moves[i].second.c_str(), entry.moves[i].first.c_str());
        }
        // A new folder is taken back to the Recycle Bin, with what is in it.
        std::wstring names = entry.renamedTo;
        names.push_back(L'\0');
        names.push_back(L'\0');
        SHFILEOPSTRUCTW op = {};
        op.hwnd = state->topLevel;
        op.wFunc = FO_DELETE;
        op.pFrom = names.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
        int result = SHFileOperationW(&op);
        hr = result == 0 && !op.fAnyOperationsAborted ? S_OK : E_FAIL;
        Out(L"Undo new folder \"%s\": result %d", entry.renamedTo.c_str(), result);
    } else if (!entry.renames.empty()) {
        hr = S_OK;
        for (size_t i = entry.renames.size(); i-- > 0;) {
            if (!MoveFileW(entry.renames[i].second.c_str(), entry.renames[i].first.c_str())) {
                hr = HRESULT_FROM_WIN32(GetLastError());
            }
        }
        Out(L"Undo of %d renames: hr=0x%08lX", (int)entry.renames.size(), hr);
    } else if (entry.deletion) {
        hr = RestoreFromRecycleBin(state, entry.paths);
        Out(L"Undo delete of %d item(s): hr=0x%08lX", (int)entry.paths.size(), hr);
    } else {
        BOOL moved = MoveFileW(entry.renamedTo.c_str(), entry.renamedFrom.c_str());
        hr = moved ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        Out(L"Undo rename \"%s\" back to \"%s\": hr=0x%08lX", entry.renamedTo.c_str(),
            entry.renamedFrom.c_str(), hr);
    }
    if (SUCCEEDED(hr)) {
        state->redo.push_back(std::move(entry));
    }
    return true;
}

// Does again what Ctrl+Z took back: the delete goes to the Recycle Bin again,
// the rename is made again. False if there is nothing to redo.
bool RedoLast(WindowState* state) {
    if (state->redo.empty()) {
        return false;
    }
    UndoEntry entry = std::move(state->redo.back());
    state->redo.pop_back();
    HRESULT hr = S_OK;
    if (entry.created) {
        hr = CreateDirectoryW(entry.renamedTo.c_str(), nullptr)
                 ? S_OK
                 : HRESULT_FROM_WIN32(GetLastError());
        for (auto& pair : entry.moves) {
            MoveFileW(pair.first.c_str(), pair.second.c_str());
        }
        Out(L"Redo new folder \"%s\": hr=0x%08lX", entry.renamedTo.c_str(), hr);
    } else if (!entry.renames.empty()) {
        for (auto& pair : entry.renames) {
            if (!MoveFileW(pair.first.c_str(), pair.second.c_str())) {
                hr = HRESULT_FROM_WIN32(GetLastError());
            }
        }
        Out(L"Redo of %d renames: hr=0x%08lX", (int)entry.renames.size(), hr);
    } else if (entry.deletion) {
        std::wstring names;
        for (auto& path : entry.paths) {
            names += path;
            names.push_back(L'\0');
        }
        names.push_back(L'\0');
        SHFILEOPSTRUCTW op = {};
        op.hwnd = state->topLevel;
        op.wFunc = FO_DELETE;
        op.pFrom = names.c_str();
        op.fFlags = FOF_ALLOWUNDO;
        int result = SHFileOperationW(&op);
        hr = result == 0 && !op.fAnyOperationsAborted ? S_OK : E_FAIL;
        Out(L"Redo delete of %d item(s): result %d", (int)entry.paths.size(), result);
    } else {
        BOOL moved = MoveFileW(entry.renamedFrom.c_str(), entry.renamedTo.c_str());
        hr = moved ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        Out(L"Redo rename \"%s\" to \"%s\": hr=0x%08lX", entry.renamedFrom.c_str(),
            entry.renamedTo.c_str(), hr);
    }
    if (SUCCEEDED(hr)) {
        state->undo.push_back(std::move(entry));  // Straight, so the redo history stays.
    }
    return true;
}

// The selected items' paths on the clipboard as text, each in quotes on its
// own line, like Explorer's "Copy as path".
void CopyAsPath(WindowState* state, Column* column, std::vector<size_t> const& entries) {
    std::wstring text;
    for (size_t entry : entries) {
        if (entry >= column->entries.size()) {
            continue;
        }
        std::wstring path = FilePathOf(column->folder.get(), column->entries[entry].child.get());
        if (path.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += L"\r\n";
        }
        text += L"\"" + path + L"\"";
    }
    if (text.empty() || !OpenClipboard(state->topLevel)) {
        return;
    }
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
    Out(L"copied %d path(s) as text", (int)entries.size());
}

void CommitRename(WindowState* state) {
    if (!state->rename || state->rename->done) {
        return;
    }
    std::wstring newName = state->rename->box.Text().c_str();
    Column* column = state->rename->column;
    size_t entryIndex = state->rename->entry;
    std::vector<size_t> together = state->rename->multi;
    CancelRename(state);

    // Several items: the typed name, then a number, and each keeps its own
    // extension, as Explorer names them: "Name (1).txt", "Name (2).jpg".
    if (together.size() > 1 && !newName.empty()) {
        auto extensionOf = [](std::wstring const& name) {
            size_t dot = name.rfind(L'.');
            return dot != std::wstring::npos && dot > 0 ? name.substr(dot) : std::wstring();
        };
        std::wstring firstExtension =
            entryIndex < column->entries.size() && !column->entries[entryIndex].folder
                ? extensionOf(column->entries[entryIndex].name)
                : std::wstring();
        std::wstring base = newName;
        if (!firstExtension.empty() && base.size() > firstExtension.size() &&
            _wcsicmp(base.c_str() + base.size() - firstExtension.size(), firstExtension.c_str()) == 0) {
            base.resize(base.size() - firstExtension.size());
        }
        IShellFolder* parent = nullptr;
        HRESULT bound = ILIsEmpty(column->folder.get())
                            ? SHGetDesktopFolder(&parent)
                            : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                             IID_PPV_ARGS(&parent));
        if (FAILED(bound) || !parent) {
            return;
        }
        UndoEntry undo;
        int number = 0;
        for (size_t entry : together) {
            if (entry >= column->entries.size()) {
                continue;
            }
            number++;
            std::wstring name = base + L" (" + std::to_wstring(number) + L")" +
                                (column->entries[entry].folder
                                     ? std::wstring()
                                     : extensionOf(column->entries[entry].name));
            std::wstring oldPath = FilePathOf(column->folder.get(), column->entries[entry].child.get());
            PITEMID_CHILD renamed = nullptr;
            HRESULT hr = parent->SetNameOf(state->topLevel, column->entries[entry].child.get(),
                                           name.c_str(), SHGDN_INFOLDER, &renamed);
            Out(L"rename %d of %d, \"%s\" to \"%s\": hr=0x%08lX", number, (int)together.size(),
                column->entries[entry].name.c_str(), name.c_str(), hr);
            if (SUCCEEDED(hr) && renamed) {
                std::wstring newPath = FilePathOf(column->folder.get(), renamed);
                if (!oldPath.empty() && !newPath.empty()) {
                    undo.renames.emplace_back(oldPath, newPath);
                }
            }
            if (renamed) {
                CoTaskMemFree(renamed);
            }
        }
        parent->Release();
        if (!undo.renames.empty()) {
            PushUndo(state, std::move(undo));
        }
        RequestLoad(state, column);
        return;
    }
    if (newName.empty() || entryIndex >= column->entries.size() ||
        newName == column->entries[entryIndex].name) {
        return;
    }
    IShellFolder* parent = nullptr;
    HRESULT hr = ILIsEmpty(column->folder.get())
                     ? SHGetDesktopFolder(&parent)
                     : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                      IID_PPV_ARGS(&parent));
    if (FAILED(hr) || !parent) {
        return;
    }
    PITEMID_CHILD renamed = nullptr;
    std::wstring oldPath = FilePathOf(column->folder.get(),
                                      column->entries[entryIndex].child.get());
    hr = parent->SetNameOf(state->topLevel, column->entries[entryIndex].child.get(),
                           newName.c_str(), SHGDN_INFOLDER, &renamed);
    Out(L"rename \"%s\" to \"%s\": hr=0x%08lX",
        column->entries[entryIndex].name.c_str(), newName.c_str(), hr);
    parent->Release();
    if (SUCCEEDED(hr) && renamed) {
        // Show the new name now, then read the folder again for the order.
        std::wstring newPath = FilePathOf(column->folder.get(), renamed);
        if (!oldPath.empty() && !newPath.empty()) {
            UndoEntry entry;
            entry.renamedFrom = oldPath;
            entry.renamedTo = newPath;
            PushUndo(state, std::move(entry));
        }
        column->entries[entryIndex].name = newName;
        column->entries[entryIndex].child.reset(renamed);
        renamed = nullptr;
        column->selectedChild.reset(ILClone(column->entries[entryIndex].child.get()));
        if (entryIndex < column->texts.size() && column->texts[entryIndex]) {
            column->texts[entryIndex].Text(
                MiddleEllipsis(newName, NameCapacity(column)));
        }
        RequestLoad(state, column);
    }
    if (renamed) {
        CoTaskMemFree(renamed);
    }
}

void BeginRename(WindowState* state, Column* column, size_t entryIndex);

// Several selected items are renamed together: the box opens on the first, and
// what is typed names all of them.
void BeginMultiRename(WindowState* state, Column* column, std::vector<size_t> const& entries) {
    if (entries.empty()) {
        return;
    }
    BeginRename(state, column, entries[0]);
    if (state->rename && entries.size() > 1) {
        state->rename->multi = entries;
        Out(L"renaming %d items together", (int)entries.size());
    }
}

void BeginRename(WindowState* state, Column* column, size_t entryIndex) {
    CancelRename(state);
    bool known = false;
    for (auto& c : state->columns) {
        known = known || c.get() == column;
    }
    if (known) {
        EnsureRowBuilt(column, entryIndex);  // A row that was never near the screen.
    }
    if (!known || entryIndex >= column->texts.size() ||
        !column->texts[entryIndex]) {
        return;
    }
    auto text = column->texts[entryIndex];
    auto grid = text.Parent().try_as<muxc::Grid>();
    if (!grid) {
        return;
    }
    const std::wstring name = column->entries[entryIndex].name;
    muxc::TextBox box;
    box.Text(name);
    box.VerticalAlignment(mux::VerticalAlignment::Center);
    box.MinHeight(0);
    box.Padding(mux::Thickness{4, 1, 4, 1});
    muxc::Grid::SetColumn(box, 1);
    muxc::Grid::SetColumnSpan(box, 2);

    auto rename = std::make_unique<RenameState>();
    rename->column = column;
    rename->entry = entryIndex;
    rename->grid = grid;
    rename->text = text;
    rename->box = box;
    rename->startedAt = GetTickCount64();
    rename->keyToken = box.KeyDown(
        [](Inspectable const&, mux::Input::KeyRoutedEventArgs const& args) {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->rename) {
                return;
            }
            using winrt::Windows::System::VirtualKey;
            try {
                if (args.Key() == VirtualKey::Enter) {
                    args.Handled(true);
                    CommitRename(state);
                } else if (args.Key() == VirtualKey::Escape) {
                    args.Handled(true);
                    CancelRename(state);
                }
            } catch (...) {
                LogHresult(L"rename key");
            }
        });
    rename->lostToken = box.LostFocus(
        [](Inspectable const&, mux::RoutedEventArgs const&) {
            WindowState* state = StateForThisThread(false);
            if (!state || !state->rename) {
                return;
            }
            try {
                // Closing the flyout hands focus back to the list. Take it
                // again if that happens right after the box opened.
                if (GetTickCount64() - state->rename->startedAt < 400) {
                    state->rename->box.Focus(mux::FocusState::Programmatic);
                } else {
                    CommitRename(state);
                }
            } catch (...) {
                LogHresult(L"rename lost focus");
            }
        });
    text.Visibility(mux::Visibility::Collapsed);
    grid.Children().Append(box);
    state->rename = std::move(rename);
    g_renaming = true;
    box.Focus(mux::FocusState::Programmatic);
    size_t dot = name.rfind(L'.');
    bool keepExtension = !column->entries[entryIndex].folder &&
                         dot != std::wstring::npos && dot > 0;
    box.SelectionStart(0);
    box.SelectionLength((uint32_t)(keepExtension ? dot : name.size()));
    Out(L"rename started for \"%s\"", name.c_str());
}

// After a delete: rows whose file is gone are hidden at once. The reload that
// follows removes them for good, or brings them back if the delete didn't
// happen.
void HideDeletedRows(Column* column, std::vector<size_t> const& entries) {
    try {
        auto items = column->list.Items();
        for (size_t entry : entries) {
            if (entry >= column->entries.size()) {
                continue;
            }
            Pidl absolute(ILCombine(column->folder.get(),
                                    column->entries[entry].child.get()));
            wchar_t path[MAX_PATH] = L"";
            if (!absolute || !SHGetPathFromIDListW(absolute.get(), path) ||
                !path[0] || PathFileExistsW(path)) {
                continue;
            }
            for (size_t r = 0; r < column->rowToEntry.size() && r < items.Size();
                 r++) {
                if (column->rowToEntry[r] == (int)entry) {
                    if (auto item = items.GetAt((uint32_t)r)
                                        .try_as<muxc::ListViewItem>()) {
                        item.Visibility(mux::Visibility::Collapsed);
                    }
                    break;
                }
            }
        }
    } catch (...) {
        LogHresult(L"HideDeletedRows");
    }
}

// Cut and Copy from the shell's menu don't work without a view behind them,
// so the item's data object goes on the clipboard here.
bool CopyOrCut(WindowState* state, Column* column,
               std::vector<size_t> const& entries, bool cut) {
    IShellFolder* parent = nullptr;
    HRESULT hr = ILIsEmpty(column->folder.get())
                     ? SHGetDesktopFolder(&parent)
                     : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                      IID_PPV_ARGS(&parent));
    if (FAILED(hr) || !parent) {
        return false;
    }
    std::vector<PCUITEMID_CHILD> children;
    for (size_t entry : entries) {
        if (entry < column->entries.size()) {
            children.push_back(column->entries[entry].child.get());
        }
    }
    IDataObject* data = nullptr;
    hr = children.empty()
             ? E_INVALIDARG
             : parent->GetUIObjectOf(state->topLevel, (UINT)children.size(),
                                     children.data(), IID_IDataObject, nullptr,
                                     (void**)&data);
    parent->Release();
    if (FAILED(hr) || !data) {
        return false;
    }
    if (cut) {
        static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
        if (memory) {
            if (DWORD* effect = (DWORD*)GlobalLock(memory)) {
                *effect = DROPEFFECT_MOVE;
                GlobalUnlock(memory);
            }
            FORMATETC fmt = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1,
                             TYMED_HGLOBAL};
            STGMEDIUM medium = {};
            medium.tymed = TYMED_HGLOBAL;
            medium.hGlobal = memory;
            if (FAILED(data->SetData(&fmt, &medium, TRUE))) {
                GlobalFree(memory);
            }
        }
    }
    hr = OleSetClipboard(data);
    data->Release();
    if (SUCCEEDED(hr)) {
        state->cutPaths.clear();
        state->cutFolder.reset();
        state->cutFolderPath.clear();
        if (cut) {
            wchar_t folderPath[MAX_PATH] = L"";
            if (SHGetPathFromIDListW(column->folder.get(), folderPath)) {
                state->cutFolderPath = folderPath;
            }
            state->cutFolder.reset(ILClone(column->folder.get()));
            for (PCUITEMID_CHILD child : children) {
                std::wstring path = FilePathOf(column->folder.get(), child);
                if (!path.empty()) {
                    state->cutPaths.push_back(path);
                }
            }
        }
        state->cutSeq = GetClipboardSequenceNumber();
        ApplyCutState(state);
    }
    Out(L"%s %d item(s): hr=0x%08lX", cut ? L"cut" : L"copy",
        (int)children.size(), hr);
    return SUCCEEDED(hr);
}

// Lets go of the shell menu and every handler on the flyout's items.
void CloseContextMenu(WindowState* state) {
    if (!state->context) {
        return;
    }
    auto context = std::move(state->context);
    try {
        if (context->watch) {
            context->watch.Stop();
            context->watch = nullptr;
        }
        if (context->hover) {
            context->hover.Stop();
            context->hover = nullptr;
        }
        if (context->openSub) {
            try {
                context->openSub.Hide();
            } catch (...) {
            }
            context->openSub = nullptr;
        }
        for (auto& hook : context->hooks) {
            if (hook.first) {
                hook.first.Click(hook.second);
            }
        }
        for (auto& hook : context->buttonHooks) {
            if (hook.first) {
                hook.first.Click(hook.second);
            }
        }
        if (context->flyout) {
            context->flyout.Closed(context->closedToken);
            context->flyout.Hide();
        }
    } catch (...) {
        LogHresult(L"CloseContextMenu");
    }
    context->hooks.clear();
    context->buttonHooks.clear();
    context->flyout = nullptr;
    if (context->hmenu) {
        DestroyMenu(context->hmenu);
    }
    if (context->menu) {
        context->menu->Release();
    }
}

std::wstring MenuText(HMENU menu, UINT position, MENUITEMINFOW* info) {
    wchar_t text[256] = L"";
    *info = {};
    info->cbSize = sizeof(*info);
    info->fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID | MIIM_STATE |
                  MIIM_SUBMENU;
    info->dwTypeData = text;
    info->cch = ARRAYSIZE(text) - 1;
    if (!GetMenuItemInfoW(menu, position, TRUE, info)) {
        return L"";
    }
    // Without the & accelerator marks and the shortcut text after the tab.
    std::wstring result;
    for (const wchar_t* p = text; *p && *p != L'\t'; p++) {
        if (*p == L'&') {
            if (p[1] == L'&') {
                result += L'&';
                p++;
            }
            continue;
        }
        result += *p;
    }
    return result;
}

int ArchiveParentColumn(WindowState* state, Column* column);

// Runs one command of the shell's menu for the items the menu was made for.
void RunContextCommand(UINT id) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->context || g_unloading) {
        return;
    }
    try {
                    ContextMenuState* context = state->context.get();
                    // For an undo of a delete: the paths, before they are gone.
                    std::vector<std::wstring> pathsBefore;
                    for (size_t entry : context->entries) {
                        if (context->column && entry < context->column->entries.size()) {
                            pathsBefore.push_back(FilePathOf(
                                context->column->folder.get(),
                                context->column->entries[entry].child.get()));
                        }
                    }
                    wchar_t verb[64] = L"";
                    context->menu->GetCommandString(
                        id - context->idFirst, GCS_VERBW, nullptr,
                        (char*)verb, ARRAYSIZE(verb));
                    Out(L"context menu verb \"%s\"", verb);
                    if (!_wcsicmp(verb, L"copy") || !_wcsicmp(verb, L"cut")) {
                        CopyOrCut(state, context->column, context->entries,
                                  !_wcsicmp(verb, L"cut"));
                        return;
                    }
                    if (!_wcsicmp(verb, L"rename") && state->queue &&
                        context->entries.size() == 1) {
                        Column* column = context->column;
                        size_t entry = context->entries[0];
                        // After the flyout has closed and given focus back.
                        state->queue.TryEnqueue([column, entry]() {
                            WindowState* state = StateForThisThread(false);
                            if (state && state->on && !g_unloading) {
                                try {
                                    BeginRename(state, column, entry);
                                } catch (...) {
                                    LogHresult(L"BeginRename");
                                }
                            }
                        });
                        return;
                    }
                    CMINVOKECOMMANDINFOEX invoke = {};
                    invoke.cbSize = sizeof(invoke);
                    invoke.hwnd = context->owner;
                    invoke.lpVerb = MAKEINTRESOURCEA(id - context->idFirst);
                    invoke.lpVerbW = MAKEINTRESOURCEW(id - context->idFirst);
                    invoke.nShow = SW_SHOWNORMAL;
                    invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                    GetCursorPos(&invoke.ptInvoke);
                    HRESULT hr = context->menu->InvokeCommand(
                        (CMINVOKECOMMANDINFO*)&invoke);
                    Out(L"context menu command %u (%s) -> hr=0x%08lX", id, verb, hr);
                    // The command may have changed the folder. Read it now
                    // instead of waiting for the change notification.
                    if (!_wcsicmp(verb, L"delete")) {
                        HideDeletedRows(context->column, context->entries);
                        if (SUCCEEDED(hr) && !(GetKeyState(VK_SHIFT) & 0x8000)) {
                            RememberDeletion(state, pathsBefore);
                        }
                    }
                    ReloadAll(state);
    } catch (...) {
        LogHresult(L"running a context menu command");
    }
}

// The shell fills some submenus (Open with, Send to, New) only when they are about to be
// shown: a window message to the menu's handler, WM_INITMENUPOPUP, asks for it.
void InitSubmenu(WindowState* state, HMENU submenu, int position) {
    if (!state->context || !state->context->menu || !submenu) {
        return;
    }
    IContextMenu2* menu2 = nullptr;
    if (SUCCEEDED(state->context->menu->QueryInterface(IID_PPV_ARGS(&menu2))) && menu2) {
        menu2->HandleMenuMsg(WM_INITMENUPOPUP, (WPARAM)submenu, MAKELPARAM(position, 0));
        menu2->Release();
    }
}

void FillFlyoutItems(WindowState* state, HMENU menu,
                     winrt::Windows::Foundation::Collections::IVector<
                         muxc::MenuFlyoutItemBase> const& items,
                     int depth, std::set<int> const* skip = nullptr) {
    // Inside an archive only Copy is offered. Properties on such items took
    // Explorer down in the 0.7.2 test, and Delete, Cut, Rename, Send to and
    // the like have no meaning there.
    bool restricted = state->context && state->context->insideArchive;
    int count = GetMenuItemCount(menu);
    bool needSeparator = false;
    for (int i = 0; i < count && i < 80; i++) {
        MENUITEMINFOW info;
        std::wstring text = MenuText(menu, i, &info);
        if (info.fType & MFT_SEPARATOR) {
            needSeparator = items.Size() > 0;
            continue;
        }
        if (text.empty()) {
            continue;
        }
        if (skip && skip->count(i)) {
            continue;  // Shown elsewhere in the menu.
        }
        if (needSeparator) {
            items.Append(muxc::MenuFlyoutSeparator());
            needSeparator = false;
        }
        if (restricted) {
            if (info.hSubMenu) {
                continue;
            }
            wchar_t verb[64] = L"";
            if (state->context->menu) {
                state->context->menu->GetCommandString(info.wID - state->context->idFirst,
                                                       GCS_VERBW, nullptr, (char*)verb,
                                                       ARRAYSIZE(verb));
            }
            if (_wcsicmp(verb, L"copy") != 0) {
                continue;
            }
        }
        if (info.hSubMenu) {
            if (depth >= 2) {
                continue;
            }
            muxc::MenuFlyoutSubItem sub;
            sub.Text(text);
            InitSubmenu(state, info.hSubMenu, i);
            FillFlyoutItems(state, info.hSubMenu, sub.Items(), depth + 1);
            if (sub.Items().Size() > 0) {
                items.Append(sub);
            }
            continue;
        }
        muxc::MenuFlyoutItem item;
        item.Text(text);
        item.IsEnabled(!(info.fState & (MFS_DISABLED | MFS_GRAYED)));
        item.Tag(winrt::box_value((uint32_t)info.wID));
        auto token = item.Click(
            [](Inspectable const& sender, mux::RoutedEventArgs const&) {
                auto item = sender.try_as<muxc::MenuFlyoutItem>();
                if (item) {
                    RunContextCommand(winrt::unbox_value<uint32_t>(item.Tag()));
                }
            });
        state->context->hooks.emplace_back(item, token);
        items.Append(item);
    }
}

HRESULT InvokeVerbOnFolder(WindowState* state, PCIDLIST_ABSOLUTE folder,
                           const char* verb);
HRESULT PasteIntoFolder(WindowState* state, PCIDLIST_ABSOLUTE folder);
void BeginRename(WindowState* state, Column* column, size_t entryIndex);

// Is there something to paste? Files or folders on the clipboard.
bool ClipboardHasFiles() {
    IDataObject* data = nullptr;
    if (FAILED(OleGetClipboard(&data)) || !data) {
        return false;
    }
    static UINT idList = RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
    FORMATETC list = {(CLIPFORMAT)idList, nullptr, DVASPECT_CONTENT, -1,
                      TYMED_HGLOBAL};
    FORMATETC drop = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    bool has = data->QueryGetData(&list) == S_OK ||
               data->QueryGetData(&drop) == S_OK;
    data->Release();
    return has;
}

// A new folder in a column's folder, named like Explorer names it. Only for
// folders on disk. Then the column reloads and the name is put in edit mode.
void CreateNewFolder(WindowState* state, Column* column) {
    wchar_t path[MAX_PATH] = L"";
    if (!SHGetPathFromIDListW(column->folder.get(), path) || !path[0]) {
        Out(L"new folder: this folder isn't on disk");
        return;
    }
    std::wstring name = L"New folder";
    for (int i = 1; i < 100; i++) {
        std::wstring candidate =
            i == 1 ? name : name + L" (" + std::to_wstring(i) + L")";
        std::wstring full = std::wstring(path) + L"\\" + candidate;
        if (!PathFileExistsW(full.c_str())) {
            if (CreateDirectoryW(full.c_str(), nullptr)) {
                UndoEntry undo;
                undo.created = true;
                undo.renamedTo = full;
                PushUndo(state, std::move(undo));
                state->renameName = candidate;
                state->renameColumn = column;
                RequestLoad(state, column);
                Out(L"new folder \"%s\" created", candidate.c_str());
            } else {
                Out(L"new folder: CreateDirectory failed, gle=%lu",
                    GetLastError());
            }
            return;
        }
    }
}

// Makes a folder in the column's folder and moves the selected items into it,
// then opens its name for editing. The move is the shell's own, so conflicts
// and progress behave as in Explorer.
void NewFolderWithSelection(WindowState* state, Column* column,
                            std::vector<size_t> const& entries) {
    wchar_t path[MAX_PATH] = L"";
    if (entries.empty() || !SHGetPathFromIDListW(column->folder.get(), path) || !path[0]) {
        Out(L"new folder with selection: not a folder on disk, or nothing selected");
        return;
    }
    std::vector<std::wstring> from;
    for (size_t entry : entries) {
        if (entry < column->entries.size()) {
            std::wstring item = FilePathOf(column->folder.get(), column->entries[entry].child.get());
            if (!item.empty()) {
                from.push_back(item);
            }
        }
    }
    if (from.empty()) {
        return;
    }
    std::wstring name = L"New folder";
    std::wstring candidate, full;
    for (int i = 1; i < 100; i++) {
        candidate = i == 1 ? name : name + L" (" + std::to_wstring(i) + L")";
        full = std::wstring(path) + L"\\" + candidate;
        if (!PathFileExistsW(full.c_str())) {
            break;
        }
    }
    if (!CreateDirectoryW(full.c_str(), nullptr)) {
        Out(L"new folder with selection: CreateDirectory failed, gle=%lu", GetLastError());
        return;
    }
    std::wstring names;
    for (auto& item : from) {
        names += item;
        names.push_back(L'\0');
    }
    names.push_back(L'\0');
    std::wstring target = full;
    target.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.hwnd = state->topLevel;
    op.wFunc = FO_MOVE;
    op.pFrom = names.c_str();
    op.pTo = target.c_str();
    op.fFlags = FOF_ALLOWUNDO;
    int result = SHFileOperationW(&op);
    Out(L"new folder \"%s\" with %d item(s): result %d", candidate.c_str(), (int)from.size(), result);

    UndoEntry undo;
    undo.created = true;
    undo.renamedTo = full;
    for (auto& item : from) {
        const wchar_t* leaf = wcsrchr(item.c_str(), L'\\');
        std::wstring moved = full + L"\\" + (leaf ? leaf + 1 : item.c_str());
        if (PathFileExistsW(moved.c_str())) {
            undo.moves.emplace_back(item, moved);
        }
    }
    PushUndo(state, std::move(undo));
    state->renameName = candidate;
    state->renameColumn = column;
    RequestLoad(state, column);
}

// Opens a folder in a new tab of this window: Ctrl+T makes the tab, and once
// Explorer has built it, the folder is shown in it.
void OpenFolderInNewTab(WindowState* state, PCIDLIST_ABSOLUTE folder) {
    if (!folder || FolderGone(folder)) {
        return;
    }
    state->openTabFolder.reset(ILClone(folder));
    state->openTabUntil = GetTickCount64() + 4000;
    state->newTab = nullptr;
    INPUT keys[4] = {};
    keys[0].type = INPUT_KEYBOARD;
    keys[0].ki.wVk = VK_CONTROL;
    keys[1].type = INPUT_KEYBOARD;
    keys[1].ki.wVk = 'T';
    keys[2] = keys[1];
    keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
    keys[3] = keys[0];
    keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, keys, sizeof(INPUT));
    Out(L"open in a new tab: \"%s\"", PathOf(folder).c_str());
    // Watched every 10 ms, not at the tick's pace: the folder is shown in the tab
    // and the columns follow the moment it exists.
    try {
        auto timer = state->queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(10));
        timer.Tick([](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
            WindowState* now = StateForThisThread(false);
            if (!now || g_unloading || TryShowNewTab(now)) {
                sender.Stop();
                if (now && !g_unloading) {
                    OnTick();  // Notices the new folder at once.
                }
            }
        });
        timer.Start();
    } catch (...) {
        LogHresult(L"watching for the new tab");
    }
}

// Extracts an archive into a folder next to it, named like the archive, with the
// shell's own copy: conflicts and progress behave as in Explorer.
void ExtractHere(WindowState* state, Column* column, size_t entryIndex, bool named = true) {
    if (entryIndex >= column->entries.size() || !column->entries[entryIndex].archive) {
        return;
    }
    Pidl archive(ILCombine(column->folder.get(), column->entries[entryIndex].child.get()));
    std::wstring archivePath = FilePathOf(column->folder.get(), column->entries[entryIndex].child.get());
    if (!archive || archivePath.empty()) {
        return;
    }
    std::wstring target = archivePath;
    if (named) {
        size_t dot = target.rfind(L'.');
        if (dot != std::wstring::npos && dot > target.rfind(L'\\')) {
            target.resize(dot);
        }
        for (int i = 2; PathFileExistsW(target.c_str()) && i < 100; i++) {
            target = archivePath.substr(0, archivePath.rfind(L'.')) + L" (" + std::to_wstring(i) + L")";
        }
        if (!CreateDirectoryW(target.c_str(), nullptr)) {
            Out(L"extract: can't make \"%s\", gle=%lu", target.c_str(), GetLastError());
            return;
        }
    } else {
        target = PathOf(column->folder.get());  // Into the folder that holds the archive.
        if (target.empty()) {
            return;
        }
    }
    IShellFolder* source = nullptr;
    HRESULT hr = SHBindToObject(nullptr, archive.get(), nullptr, IID_PPV_ARGS(&source));
    IShellItem* destination = nullptr;
    if (SUCCEEDED(hr) && source) {
        hr = SHCreateItemFromParsingName(target.c_str(), nullptr, IID_PPV_ARGS(&destination));
    }
    IFileOperation* operation = nullptr;
    if (SUCCEEDED(hr) && destination) {
        hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    }
    int copied = 0;
    if (SUCCEEDED(hr) && operation) {
        operation->SetOwnerWindow(state->topLevel);
        operation->SetOperationFlags(FOFX_NOCOPYHOOKS | FOF_NOCONFIRMMKDIR);
        IEnumIDList* list = nullptr;
        if (source->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &list) == S_OK && list) {
            PITEMID_CHILD child = nullptr;
            while (list->Next(1, &child, nullptr) == S_OK && child) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(SHCreateItemWithParent(nullptr, source, child, IID_PPV_ARGS(&item))) && item) {
                    if (SUCCEEDED(operation->CopyItem(item, destination, nullptr, nullptr))) {
                        copied++;
                    }
                    item->Release();
                }
                CoTaskMemFree(child);
                child = nullptr;
            }
            list->Release();
        }
        if (copied > 0) {
            hr = operation->PerformOperations();
        }
    }
    Out(L"extract \"%s\": %d item(s), hr=0x%08lX", target.c_str(), copied, hr);
    if (copied > 0 && named) {
        const wchar_t* leaf = wcsrchr(target.c_str(), L'\\');
        state->selectNames.assign(1, leaf ? leaf + 1 : target.c_str());
        state->selectNamesColumn = column;
    }
    if (operation) {
        operation->Release();
    }
    if (destination) {
        destination->Release();
    }
    if (source) {
        source->Release();
    }
    RequestLoad(state, column);
}

// For a column that shows the inside of an archive: the column before the
// archive, on disk, which holds it. -1 if this column is not inside one.
int ArchiveParentColumn(WindowState* state, Column* column) {
    int index = -1;
    for (size_t i = 0; i < state->columns.size(); i++) {
        if (state->columns[i].get() == column) {
            index = (int)i;
        }
    }
    if (index <= 0) {
        return -1;
    }
    // The shell gives a folder inside an archive a path too ("C:\x\a.zip\sub"),
    // so look for a part of the path that is a file, not a folder.
    std::wstring path = PathOf(column->folder.get());
    if (path.size() < 3 || path[1] != L':') {
        return -1;
    }
    std::wstring archive;
    for (std::wstring part = path; part.size() > 3;) {
        DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            archive = part;
            break;
        }
        size_t slash = part.rfind(L'\\');
        if (slash == std::wstring::npos) {
            break;
        }
        part.resize(slash);
    }
    if (archive.empty()) {
        return -1;
    }
    // The column that shows the archive itself; the one before it holds the
    // archive (its own name can be a library name, not a path).
    for (int j = index; j >= 1; j--) {
        std::wstring shown = PathOf(state->columns[j]->folder.get());
        if (_wcsicmp(shown.c_str(), archive.c_str()) == 0) {
            return j - 1;
        }
    }
    return -1;
}

// Copies the picked items of an archive that is open in a column into the
// folder that holds the archive, with the shell's own copy, and selects them.
void ExtractSelected(WindowState* state, Column* column, std::vector<size_t> const& entries) {
    int parentIndex = ArchiveParentColumn(state, column);
    if (parentIndex < 0 || entries.empty()) {
        return;
    }
    Column* parent = state->columns[parentIndex].get();
    IShellFolder* source = nullptr;
    HRESULT hr = SHBindToObject(nullptr, column->folder.get(), nullptr, IID_PPV_ARGS(&source));
    IShellItem* destination = nullptr;
    if (SUCCEEDED(hr) && source) {
        hr = SHCreateItemFromIDList(parent->folder.get(), IID_PPV_ARGS(&destination));
    }
    IFileOperation* operation = nullptr;
    if (SUCCEEDED(hr) && destination) {
        hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    }
    int copied = 0;
    std::vector<std::wstring> names;
    if (SUCCEEDED(hr) && operation) {
        operation->SetOwnerWindow(state->topLevel);
        operation->SetOperationFlags(FOFX_NOCOPYHOOKS | FOF_NOCONFIRMMKDIR);
        for (size_t entry : entries) {
            if (entry >= column->entries.size()) {
                continue;
            }
            IShellItem* item = nullptr;
            if (SUCCEEDED(SHCreateItemWithParent(nullptr, source, column->entries[entry].child.get(),
                                                 IID_PPV_ARGS(&item))) &&
                item) {
                if (SUCCEEDED(operation->CopyItem(item, destination, nullptr, nullptr))) {
                    copied++;
                    // The name the copy will have: the item's own file name.
                    PWSTR leaf = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &leaf)) && leaf) {
                        names.push_back(leaf);
                        CoTaskMemFree(leaf);
                    }
                }
                item->Release();
            }
        }
        if (copied > 0) {
            hr = operation->PerformOperations();
        }
    }
    Out(L"extract selected: %d item(s) beside the archive, hr=0x%08lX", copied, hr);
    if (operation) {
        operation->Release();
    }
    if (destination) {
        destination->Release();
    }
    if (source) {
        source->Release();
    }
    if (copied > 0 && SUCCEEDED(hr)) {
        state->selectNames = names;
        state->selectNamesColumn = parent;
        RequestLoad(state, parent);
    }
}

void RunBackgroundAction(WindowState* state, Column* column,
                         const std::wstring& action) {
    bool known = false;
    for (auto& c : state->columns) {
        known = known || c.get() == column;
    }
    if (!known) {
        return;
    }
    if (action == L"paste") {
        HRESULT hr = PasteIntoFolder(state, column->folder.get());
        Out(L"background Paste: hr=0x%08lX", hr);
    } else if (action == L"newfolder") {
        CreateNewFolder(state, column);
    } else if (action == L"refresh") {
        RequestLoad(state, column);
    } else if (action == L"properties") {
        HRESULT hr = InvokeVerbOnFolder(state, column->folder.get(), "properties");
        Out(L"background Properties: hr=0x%08lX", hr);
    }
}

void ShowBackgroundMenu(WindowState* state, Column* column, muxc::ListView const& list,
                        mux::Input::RightTappedRoutedEventArgs const& args);

// The item's flyout: the shell's own commands for the picked items, at a point
// of the list. Used by the right-click and by the Menu key.
// One entry of the shell's menu for the picked items.
struct MenuRow {
    int pos = 0;
    UINT id = 0;
    std::wstring text;
    std::wstring verb;
    HMENU sub = nullptr;
    bool enabled = true;
};

// A Fluent glyph for a shell verb, or an empty string.
const wchar_t* GlyphForVerb(std::wstring const& verb) {
    static const struct {
        const wchar_t* verb;
        const wchar_t* glyph;
    } table[] = {
        {L"cut", L""},        {L"copy", L""},
        {L"rename", L""},     {L"delete", L""},
        {L"open", L""},       {L"openas", L""},
        {L"properties", L""}, {L"pintohome", L""},
        {L"copyaspath", L""}, {L"edit", L""},
        {L"print", L""},      {L"setdesktopwallpaper", L""},
    };
    for (auto& entry : table) {
        if (_wcsicmp(verb.c_str(), entry.verb) == 0) {
            return entry.glyph;
        }
    }
    if (StrStrIW(verb.c_str(), L"share")) {
        return L"";
    }
    return L"";
}

// The shortcut shown at the right of a list entry.
const wchar_t* ShortcutForVerb(std::wstring const& verb) {
    if (_wcsicmp(verb.c_str(), L"open") == 0) {
        return L"Enter";
    }
    if (_wcsicmp(verb.c_str(), L"properties") == 0) {
        return L"Alt+Enter";
    }
    if (_wcsicmp(verb.c_str(), L"copyaspath") == 0) {
        return L"Ctrl+Shift+C";
    }
    return L"";
}

// The name of the icon file Windows ships for a shell verb, among the two-tone
// SVGs of Explorer's own toolbar and menus. Empty when there is none.
const wchar_t* OfficialIconName(std::wstring const& verb) {
    static const struct {
        const wchar_t* verb;
        const wchar_t* file;
    } table[] = {
        {L"cut", L"windows.cut"},
        {L"copy", L"windows.copy"},
        {L"rename", L"windows.rename"},
        {L"delete", L"windows.ribbondelete"},
        {L"openas", L"windows.openwith"},
        {L"properties", L"windows.properties"},
        {L"pintohome", L"windows.pintohome"},
        {L"pintohomefile", L"windows.pintohomefile"},
        {L"copyaspath", L"windows.copyaspath"},
        {L"paste", L"windows.paste"},
        {L"setdesktopwallpaper", L"windows.setdesktopwallpaper"},
        {L"link", L"link"},
        {L"print", L"print"},
    };
    for (auto& entry : table) {
        if (_wcsicmp(verb.c_str(), entry.verb) == 0) {
            return entry.file;
        }
    }
    if (StrStrIW(verb.c_str(), L"share")) {
        return L"Windows.ModernShare";
    }
    return L"";
}

// The icon Windows draws for that file, in the colours of the current theme. The
// files are read straight from the File Explorer package.
mux::UIElement OfficialIcon(const wchar_t* name, double size) {
    if (!name || !name[0]) {
        return nullptr;
    }
    try {
        std::wstring path =
            std::wstring(L"C:/Windows/SystemApps/MicrosoftWindows.Client.FileExp_cw5n1h2txyewy/"
                         L"FileExplorerExtensions/Assets/images/contrast-standard/") +
            (IsDarkMode() ? L"theme-dark/" : L"theme-light/") + name + L".svg";
        if (!PathFileExistsW(path.c_str())) {
            return nullptr;
        }
        mux::Media::Imaging::SvgImageSource source{
            winrt::Windows::Foundation::Uri(winrt::hstring(L"file:///" + path))};
        source.RasterizePixelWidth(size * 3);
        source.RasterizePixelHeight(size * 3);
        muxc::ImageIcon icon;
        icon.Source(source);
        icon.Width(size);
        icon.Height(size);
        return icon;
    } catch (...) {
        LogHresult(L"OfficialIcon");
    }
    return nullptr;
}

// A plain glyph icon. The accent part of a two-tone icon comes from the official
// files; a glyph has no such part to colour.
mux::UIElement MakeTwoToneIcon(WindowState* state, const wchar_t* glyph, double size, bool enabled) {
    muxc::Grid host;
    host.Width(size + 2);
    host.Height(size + 2);
    muxc::FontIcon icon;
    icon.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
    icon.Glyph(glyph);
    icon.FontSize(size);
    icon.Opacity(enabled ? 1.0 : 0.5);
    icon.HorizontalAlignment(mux::HorizontalAlignment::Center);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    host.Children().Append(icon);
    return host;
}

// The icon Explorer's toolbar uses for a command with this label: on a button of
// the bar, or on an entry of one of its menus.
mux::Controls::IconElement FindToolbarIcon(mux::DependencyObject const& node, const wchar_t* label,
                                           int depth, int& visited);

mux::Controls::IconElement IconInMenuItems(
    winrt::Windows::Foundation::Collections::IVector<muxc::MenuFlyoutItemBase> const& items,
    const wchar_t* label, int depth) {
    for (uint32_t i = 0; i < items.Size() && i < 60; i++) {
        auto base = items.GetAt(i);
        if (auto item = base.try_as<muxc::MenuFlyoutItem>()) {
            if (item.Icon() && _wcsicmp(item.Text().c_str(), label) == 0) {
                return item.Icon();
            }
        } else if (auto sub = base.try_as<muxc::MenuFlyoutSubItem>()) {
            if (sub.Icon() && _wcsicmp(sub.Text().c_str(), label) == 0) {
                return sub.Icon();
            }
            if (depth < 2) {
                if (auto found = IconInMenuItems(sub.Items(), label, depth + 1)) {
                    return found;
                }
            }
        }
    }
    return nullptr;
}

mux::Controls::IconElement FindToolbarIcon(mux::DependencyObject const& node, const wchar_t* label,
                                           int depth, int& visited) {
    if (!node || depth > 40 || visited > 3000) {
        return nullptr;
    }
    visited++;
    if (auto button = node.try_as<muxc::AppBarButton>()) {
        if (button.Icon() && _wcsicmp(button.Label().c_str(), label) == 0) {
            return button.Icon();
        }
    }
    if (auto button = node.try_as<muxc::Button>()) {
        if (auto flyout = button.Flyout()) {
            if (auto menu = flyout.try_as<muxc::MenuFlyout>()) {
                if (auto found = IconInMenuItems(menu.Items(), label, 0)) {
                    return found;
                }
            }
        }
    }
    int count = mux::Media::VisualTreeHelper::GetChildrenCount(node);
    for (int i = 0; i < count; i++) {
        if (auto found = FindToolbarIcon(mux::Media::VisualTreeHelper::GetChild(node, i), label,
                                         depth + 1, visited)) {
            return found;
        }
    }
    return nullptr;
}

// A copy of the icon Explorer's toolbar uses for a command, so the menu matches
// the toolbar. Null when the toolbar has no such command or the icon is of a kind
// that is not copied.
mux::UIElement ToolbarIcon(WindowState* state, const wchar_t* label, double size) {
    try {
        if (!state->xamlRoot) {
            return nullptr;
        }
        auto content = state->xamlRoot.Content();
        auto start = content ? content.try_as<mux::DependencyObject>() : nullptr;
        int visited = 0;
        auto icon = FindToolbarIcon(start, label, 0, visited);
        if (!icon) {
            return nullptr;
        }
        if (auto image = icon.try_as<muxc::ImageIcon>()) {
            muxc::ImageIcon copy;
            copy.Source(image.Source());
            copy.Width(size);
            copy.Height(size);
            return copy;
        }
        if (auto font = icon.try_as<muxc::FontIcon>()) {
            muxc::FontIcon copy;
            copy.FontFamily(font.FontFamily());
            copy.Glyph(font.Glyph());
            copy.FontSize(size);
            return copy;
        }
        Out(L"toolbar icon of \"%s\" is a kind that is not copied", label);
    } catch (...) {
        LogHresult(L"ToolbarIcon");
    }
    return nullptr;
}

// ---- Compress to ----
//
// Windows' own tar.exe (bsdtar) writes zip, 7z and tar, so the three need no code of ours
// for the format. The archive is made next to the items, named after the item (or Archive
// for several), on a thread of its own; when it is done the column loads again and the
// new archive is selected.
std::atomic<int> g_compressJobs{0};

void CompressItems(WindowState* state, Column* column, std::vector<size_t> const& picked, int kind) {
    std::wstring dir = PathOf(column->folder.get());
    std::vector<std::wstring> names;
    bool singleFolder = false;
    for (size_t index : picked) {
        if (index >= column->entries.size()) {
            continue;
        }
        std::wstring path = FilePathOf(column->folder.get(), column->entries[index].child.get());
        if (path.empty()) {
            return;  // Not on disk.
        }
        const wchar_t* leaf = wcsrchr(path.c_str(), L'\\');
        names.push_back(leaf ? leaf + 1 : path);
        singleFolder = column->entries[index].folder;
    }
    if (names.empty() || dir.empty()) {
        return;
    }
    const wchar_t* extension = kind == 0 ? L".zip" : kind == 1 ? L".7z" : L".tar";
    std::wstring stem = names.size() == 1 ? names[0] : L"Archive";
    if (names.size() == 1 && !singleFolder) {
        size_t dot = stem.rfind(L'.');
        if (dot != std::wstring::npos && dot > 0) {
            stem.resize(dot);
        }
    }
    std::wstring destination = dir + L"\\" + stem + extension;
    for (int i = 2; PathFileExistsW(destination.c_str()) && i < 1000; i++) {
        destination = dir + L"\\" + stem + L" (" + std::to_wstring(i) + L")" + extension;
    }
    wchar_t system[MAX_PATH] = L"";
    GetSystemDirectoryW(system, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(system) + L"\\tar.exe\" -c ";
    command += kind == 0 ? L"--format zip " : kind == 1 ? L"--format 7zip " : L"";
    command += L"-f \"" + destination + L"\" -C \"" + dir + L"\"";
    for (auto& name : names) {
        command += L" \"" + name + L"\"";
    }
    auto queue = state->queue;
    g_compressJobs++;
    Out(L"compress: %s", command.c_str());
    std::thread([command, destination, dir, queue]() mutable {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        DWORD code = 1;
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process)) {
            WaitForSingleObject(process.hProcess, 10 * 60 * 1000);
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
        Out(L"compress: \"%s\" done, exit code %lu", destination.c_str(), code);
        try {
            queue.TryEnqueue([destination, dir]() {
                WindowState* now = StateForThisThread(false);
                if (!now || !now->on || g_unloading) {
                    return;
                }
                const wchar_t* leaf = wcsrchr(destination.c_str(), L'\\');
                for (auto& column : now->columns) {
                    if (column && _wcsicmp(PathOf(column->folder.get()).c_str(), dir.c_str()) == 0) {
                        now->selectNames.assign(1, leaf ? leaf + 1 : destination.c_str());
                        now->selectNamesColumn = column.get();
                        RequestLoad(now, column.get());
                        return;
                    }
                }
            });
        } catch (...) {
        }
        g_compressJobs--;
    }).detach();
}

// The icon of the app that opens a file: the program the file type's "open" verb runs, its
// icon read from the shell at 32 px and shown at 16. Null when there is none to show.
mux::UIElement AppIconOf(std::wstring const& path) {
    try {
        const wchar_t* extension = PathFindExtensionW(path.c_str());
        std::wstring program;
        if (_wcsicmp(extension, L".exe") == 0 || _wcsicmp(extension, L".lnk") == 0) {
            program = path;
        } else if (extension[0]) {
            wchar_t buffer[MAX_PATH * 2] = L"";
            DWORD size = ARRAYSIZE(buffer);
            if (FAILED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, extension, L"open", buffer, &size))) {
                return nullptr;
            }
            program = buffer;
        }
        if (program.empty() || StrStrIW(program.c_str(), L"ApplicationFrameHost")) {
            return nullptr;
        }
        IShellItem* item = nullptr;
        if (FAILED(SHCreateItemFromParsingName(program.c_str(), nullptr, IID_PPV_ARGS(&item))) || !item) {
            return nullptr;
        }
        IShellItemImageFactory* factory = nullptr;
        HBITMAP bitmap = nullptr;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) && factory) {
            factory->GetImage(SIZE{32, 32}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap);
            factory->Release();
        }
        item->Release();
        if (!bitmap) {
            return nullptr;
        }
        IconBits bits;
        bool ok = ReadBitmapBits(bitmap, &bits);
        DeleteObject(bitmap);
        if (!ok) {
            return nullptr;
        }
        mux::Media::Imaging::WriteableBitmap image(bits.width, bits.height);
        auto buffer = image.PixelBuffer();
        winrt::com_ptr<IBufferByteAccessLocal> access;
        winrt::check_hresult(((::IUnknown*)winrt::get_abi(buffer))->QueryInterface(kIidBufferByteAccess, access.put_void()));
        uint8_t* pixels = nullptr;
        winrt::check_hresult(access->Buffer(&pixels));
        if (!pixels || buffer.Capacity() < bits.bgra.size()) {
            return nullptr;
        }
        memcpy(pixels, bits.bgra.data(), bits.bgra.size());
        buffer.Length((uint32_t)bits.bgra.size());
        image.Invalidate();
        muxc::Image icon;
        icon.Source(image);
        icon.Width(16);
        icon.Height(16);
        icon.Stretch(mux::Media::Stretch::Uniform);
        return icon;
    } catch (...) {
        LogHresult(L"the icon of the app that opens a file");
    }
    return nullptr;
}

// Glyphs of the rows the mockup draws, by verb, for when no official icon file exists.
const wchar_t* MockupGlyphForVerb(std::wstring const& verb) {
    static const struct {
        const wchar_t* verb;
        const wchar_t* glyph;
    } table[] = {
        {L"cut", L"\uE8C6"},    {L"copy", L"\uE8C8"},       {L"paste", L"\uE77F"},
        {L"rename", L"\uE8AC"}, {L"delete", L"\uE74D"},     {L"properties", L"\uE946"},
        {L"open", L"\uE8E5"},   {L"openas", L"\uE7AC"},     {L"copyaspath", L"\uF89A"},
        {L"edit", L"\uE70F"},   {L"print", L"\uE749"},      {L"play", L"\uE768"},
    };
    for (auto& entry : table) {
        if (_wcsicmp(verb.c_str(), entry.verb) == 0) {
            return entry.glyph;
        }
    }
    if (StrStrIW(verb.c_str(), L"share")) {
        return L"\uE72D";
    }
    return GlyphForVerb(verb);
}

// The icon file of the main action of a file's format.
const wchar_t* FormatActionIcon(std::wstring const& verb) {
    static const struct {
        const wchar_t* verb;
        const wchar_t* file;
    } table[] = {
        {L"setdesktopwallpaper", L"windows.setdesktopwallpaper"},
        {L"Windows.PowerShell.Run", L"windows.powershell.run"},
        {L"runas", L"RunAsAdmin"},
        {L"print", L"print"},
        {L"mount", L"windows.mount"},
        {L"install", L"windows.install"},
    };
    for (auto& entry : table) {
        if (_wcsicmp(verb.c_str(), entry.verb) == 0) {
            return entry.file;
        }
    }
    return L"";
}

// ---- The two flyouts, built from the same parts ----
//
// The item menu and the folder (background) menu are one look: a header row of
// quick actions with labels, then separated groups of rows with an icon, a name,
// a right-aligned shortcut hint and, for a submenu, an arrow. Everything that
// decides how it looks is in `MenuStyle`, so a change of spacing, icon size or
// colour is made in one place.

struct MenuStyle {
    double minWidth = 320;
    mux::Thickness rootMargin{0, 0, 0, 0};
    // The quick row: 40 px buttons 4 px apart, 8 px around, a 16 px icon, no label.
    double quickButton = 40;
    double quickGap = 4;
    double quickPadding = 8;
    double quickIcon = 16;
    // The list (tightened from the mockup's 40, 16 and 8 to 35, 12 and 4: a 36 px pitch): rows 1 px apart, 4 px at the sides, the
    // highlight inset 5 and 3 px, the icon 16 px at 16, the name at 44, the hint and the
    // arrow ending 16 px from the right.
    double rowHeight = 33;
    double rowGap = 1;
    mux::Thickness listPadding{4, 4, 4, 4};
    double rowIcon = 16;
    double iconLeft = 16;
    double textLeft = 44;
    double tailRight = 16;
    double nameSize = 14;
    double hintSize = 12;
    double arrowSize = 12;
    double hintOpacity = 0.79;
    double separatorHeight = 10;
    winrt::Windows::UI::Color separatorColor{21, 255, 255, 255};
    double corner = 4;
    double presenterCorner = 7;
};

// A quick action of the header row.
struct MenuQuick {
    std::wstring label;  // The tooltip's fallback.
    std::wstring tip;
    const wchar_t* glyph = L"";
    std::wstring icon;   // The official icon file, without its extension.
    bool enabled = true;
    bool pinned = false;  // At the right end of the row, apart from the others.
    bool glyphOnly = false;  // Draw the glyph, whatever icon file or toolbar icon there is.
    std::function<void()> action;
};

// A row of the list.
struct MenuRowSpec {
    std::wstring text;
    const wchar_t* glyph = L"";
    std::wstring icon;
    std::wstring shortcut;
    bool enabled = true;
    bool glyphOnly = false;
    mux::UIElement custom{nullptr};  // An icon made by the caller (the app that opens a file).
    std::function<void()> action;
    muxc::MenuFlyout submenu{nullptr};
};

// Looks of the flyout that are the same for every menu, loaded once from XAML: the
// template of a row (a highlight inset in it, the hover and press fills), the template
// of a quick button, and the style of the flyout's presenter without its padding.
[[clang::no_destroy]] thread_local muxc::ControlTemplate g_rowTemplate{nullptr};
[[clang::no_destroy]] thread_local muxc::ControlTemplate g_quickTemplate{nullptr};
[[clang::no_destroy]] thread_local mux::Style g_presenterStyle{nullptr};

muxc::ControlTemplate LoadButtonTemplate(const wchar_t* inset) {
    std::wstring xaml =
        L"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
        L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
        L"<Grid Background='Transparent'>"
        L"<Border x:Name='Base' Margin='";
    xaml += inset;
    xaml +=
        L"' CornerRadius='3' Background='Transparent'/>"
        L"<ContentPresenter x:Name='Presenter' Content='{TemplateBinding Content}' "
        L"Padding='{TemplateBinding Padding}' HorizontalContentAlignment='{TemplateBinding HorizontalContentAlignment}' "
        L"VerticalContentAlignment='Center'/>"
        L"<VisualStateManager.VisualStateGroups>"
        L"<VisualStateGroup x:Name='CommonStates'>"
        L"<VisualState x:Name='Normal'/>"
        L"<VisualState x:Name='PointerOver'><VisualState.Setters>"
        L"<Setter Target='Base.Background' Value='{ThemeResource SubtleFillColorSecondaryBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"<VisualState x:Name='Pressed'><VisualState.Setters>"
        L"<Setter Target='Base.Background' Value='{ThemeResource SubtleFillColorTertiaryBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"<VisualState x:Name='Disabled'><VisualState.Setters>"
        L"<Setter Target='Presenter.Opacity' Value='0.4'/>"
        L"</VisualState.Setters></VisualState>"
        L"</VisualStateGroup>"
        L"<VisualStateGroup x:Name='FocusStates'>"
        L"<VisualState x:Name='Unfocused'/>"
        L"<VisualState x:Name='Focused'><VisualState.Setters>"
        L"<Setter Target='Base.Background' Value='{ThemeResource SubtleFillColorSecondaryBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"</VisualStateGroup>"
        L"</VisualStateManager.VisualStateGroups>"
        L"</Grid></ControlTemplate>";
    return winrt::unbox_value<muxc::ControlTemplate>(mux::Markup::XamlReader::Load(xaml));
}

struct MenuBuilder {
    WindowState* state;
    MenuStyle style;
    muxc::StackPanel root;
    muxc::StackPanel list{nullptr};  // The rows, once the first is added.
    bool anyRows = false;
    bool separatorWanted = false;
    bool toolbarIcons = true;  // Look for the toolbar's own icon of a label before drawing a glyph.
    bool styled = false;       // The presenter has no padding of its own.
    muxc::Grid quickHeader{nullptr};  // The quick row and the line under it, to put them under the list
    mux::UIElement quickLine{nullptr};  // when the menu opens upwards.

    MenuBuilder(WindowState* owner, double minWidth, bool toolbarFallback)
        : state(owner), toolbarIcons(toolbarFallback) {
        style.minWidth = minWidth;
        try {
            if (!g_rowTemplate) {
                g_rowTemplate = LoadButtonTemplate(L"5,3,5,3");
                g_quickTemplate = LoadButtonTemplate(L"1,1,1,1");
                g_presenterStyle = winrt::unbox_value<mux::Style>(mux::Markup::XamlReader::Load(
                    L"<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
                    L"TargetType='FlyoutPresenter'><Setter Property='Padding' Value='0'/>"
                    L"<Setter Property='MinWidth' Value='0'/><Setter Property='CornerRadius' Value='7'/></Style>"));
            }
            styled = true;
        } catch (...) {
            LogHresult(L"the flyout's templates");
        }
        root.Orientation(muxc::Orientation::Vertical);
        root.MinWidth(style.minWidth);
        root.Margin(styled ? style.rootMargin : mux::Thickness{-12, -11, -12, -13});  // The flyout's own padding, taken back.
        root.XYFocusKeyboardNavigation(mux::Input::XYFocusKeyboardNavigationMode::Enabled);
    }

    muxc::Button PlainButton() {
        muxc::Button button;
        button.Background(mux::Media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
        button.BorderThickness(mux::Thickness{0, 0, 0, 0});
        button.CornerRadius(mux::CornerRadius{style.corner, style.corner, style.corner, style.corner});
        return button;
    }

    // Runs `action` when the button is clicked, then closes the menu.
    void Clickable(muxc::Button const& button, std::function<void()> action, bool closeAfter) {
        auto token = button.Click([action, closeAfter](Inspectable const&, mux::RoutedEventArgs const&) {
            try {
                action();
                if (closeAfter) {
                    WindowState* open = StateForThisThread(false);
                    if (open && open->context && open->context->flyout) {
                        open->context->flyout.Hide();
                    }
                }
            } catch (...) {
                LogHresult(L"a flyout button");
            }
        });
        state->context->buttonHooks.emplace_back(button, token);
    }

    mux::UIElement IconFor(const std::wstring& file, const std::wstring& label, const wchar_t* glyph, double size,
                           bool enabled, bool glyphOnlyIfGiven) {
        mux::UIElement icon = OfficialIcon(file.c_str(), (int)size);
        if (!icon && toolbarIcons) {
            icon = ToolbarIcon(state, label.c_str(), (int)size);
        }
        if (!icon && (!glyphOnlyIfGiven || (glyph && glyph[0]))) {
            icon = MakeTwoToneIcon(state, glyph, (int)size, enabled);
        }
        return icon;
    }

    mux::UIElement Line(double height, double side) {
        muxc::Border line;
        line.Height(height);
        line.Margin(mux::Thickness{side, 0, side, 0});
        line.Background(mux::Media::SolidColorBrush(style.separatorColor));
        return line;
    }

    void EnsureList() {
        if (list) {
            return;
        }
        list = muxc::StackPanel();
        list.Orientation(muxc::Orientation::Vertical);
        list.Spacing(style.rowGap);
        list.Padding(styled ? style.listPadding : mux::Thickness{4, 4, 4, 4});
        root.Children().Append(list);
    }

    // The gap between two groups of rows: 16 px with a thin line in the middle. It is drawn
    // only before a row that follows other rows.
    void Separator() {
        EnsureList();
        muxc::Grid gap;
        gap.Height(style.separatorHeight);
        auto line = Line(1, 0).as<mux::FrameworkElement>();
        line.VerticalAlignment(mux::VerticalAlignment::Center);
        gap.Children().Append(line);
        list.Children().Append(gap);
    }

    // The header row: icons only, 40 px buttons, and the pinned ones at the right end.
    void QuickRow(std::vector<MenuQuick> const& quick) {
        if (quick.empty()) {
            return;
        }
        muxc::Grid header;
        header.Padding(mux::Thickness{style.quickPadding, style.quickPadding, style.quickPadding, style.quickPadding});
        muxc::ColumnDefinition leftColumn, rightColumn;
        leftColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        rightColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
        header.ColumnDefinitions().Append(leftColumn);
        header.ColumnDefinitions().Append(rightColumn);
        muxc::StackPanel left;
        left.Orientation(muxc::Orientation::Horizontal);
        left.Spacing(style.quickGap);
        muxc::Grid::SetColumn(left, 0);
        header.Children().Append(left);
        for (size_t q = 0; q < quick.size(); q++) {
            auto button = PlainButton();
            if (g_quickTemplate) {
                button.Template(g_quickTemplate);
            }
            button.Width(style.quickButton);
            button.Height(style.quickButton);
            button.Padding(mux::Thickness{0, 0, 0, 0});
            button.HorizontalContentAlignment(mux::HorizontalAlignment::Center);
            mux::UIElement icon = quick[q].glyphOnly
                                      ? MakeTwoToneIcon(state, quick[q].glyph, (int)style.quickIcon, quick[q].enabled)
                                      : IconFor(quick[q].icon, quick[q].label, quick[q].glyph, style.quickIcon,
                                                quick[q].enabled, false);
            icon.as<mux::FrameworkElement>().HorizontalAlignment(mux::HorizontalAlignment::Center);
            icon.as<mux::FrameworkElement>().VerticalAlignment(mux::VerticalAlignment::Center);
            button.Content(icon);
            button.IsEnabled(quick[q].enabled);
            std::wstring tip = quick[q].tip.empty() ? quick[q].label : quick[q].tip;
            if (!tip.empty()) {
                muxc::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring{tip}));
            }
            if (quick[q].pinned) {
                muxc::Grid::SetColumn(button, 1);
                header.Children().Append(button);
            } else {
                left.Children().Append(button);
            }
            if (quick[q].action) {
                Clickable(button, quick[q].action, true);
            }
        }
        root.Children().Append(header);
        quickHeader = header;
        quickLine = Line(1, 0);  // Across the whole menu.
        root.Children().Append(quickLine);
        anyRows = true;
        separatorWanted = false;
    }

    // A list row: icon, name, shortcut and, for a submenu, an arrow.
    void Row(MenuRowSpec const& spec) {
        EnsureList();
        if (separatorWanted && anyRows) {
            Separator();
        }
        separatorWanted = false;
        auto button = PlainButton();
        if (g_rowTemplate) {
            button.Template(g_rowTemplate);
        }
        button.Height(style.rowHeight);
        button.HorizontalAlignment(mux::HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(mux::HorizontalAlignment::Stretch);
        button.Padding(mux::Thickness{0, 0, 0, 0});
        button.IsEnabled(spec.enabled);
        muxc::Grid content;
        muxc::ColumnDefinition iconColumn, nameColumn, tailColumn;
        iconColumn.Width(mux::GridLength{style.textLeft, mux::GridUnitType::Pixel});
        nameColumn.Width(mux::GridLength{1, mux::GridUnitType::Star});
        tailColumn.Width(mux::GridLength{0, mux::GridUnitType::Auto});
        content.ColumnDefinitions().Append(iconColumn);
        content.ColumnDefinitions().Append(nameColumn);
        content.ColumnDefinitions().Append(tailColumn);
        mux::UIElement rowIcon = spec.custom ? spec.custom
                                 : spec.glyphOnly ? MakeTwoToneIcon(state, spec.glyph, (int)style.rowIcon, spec.enabled)
                                                  : IconFor(spec.icon, spec.text, spec.glyph, style.rowIcon, spec.enabled,
                                                            toolbarIcons);
        if (mux::UIElement icon = rowIcon) {
            muxc::Grid slot;
            slot.Width(style.rowIcon);
            slot.Height(style.rowIcon);
            slot.HorizontalAlignment(mux::HorizontalAlignment::Left);
            slot.VerticalAlignment(mux::VerticalAlignment::Center);
            slot.Margin(mux::Thickness{style.iconLeft, 0, 0, 0});
            auto frame = icon.as<mux::FrameworkElement>();
            frame.HorizontalAlignment(mux::HorizontalAlignment::Center);
            frame.VerticalAlignment(mux::VerticalAlignment::Center);
            slot.Children().Append(icon);
            muxc::Grid::SetColumn(slot, 0);
            content.Children().Append(slot);
        }
        bool tail = !spec.shortcut.empty() || spec.submenu;
        muxc::TextBlock label;
        label.Text(spec.text);
        label.FontSize(style.nameSize);
        label.VerticalAlignment(mux::VerticalAlignment::Center);
        label.TextTrimming(mux::TextTrimming::CharacterEllipsis);
        label.Margin(mux::Thickness{0, 0, tail ? 8.0 : style.tailRight, 0});
        muxc::Grid::SetColumn(label, 1);
        content.Children().Append(label);
        if (!spec.shortcut.empty()) {
            muxc::TextBlock hint;
            hint.Text(spec.shortcut);
            hint.FontSize(style.hintSize);
            hint.Opacity(style.hintOpacity);
            hint.Margin(mux::Thickness{0, 0, style.tailRight, 0});
            hint.VerticalAlignment(mux::VerticalAlignment::Center);
            muxc::Grid::SetColumn(hint, 2);
            content.Children().Append(hint);
        }
        if (spec.submenu) {
            muxc::FontIcon arrow;
            arrow.FontFamily(mux::Media::FontFamily(L"Segoe Fluent Icons"));
            arrow.Glyph(L"");
            arrow.FontSize(style.arrowSize);
            arrow.Opacity(style.hintOpacity);
            arrow.Margin(mux::Thickness{0, 0, style.tailRight, 0});
            arrow.HorizontalAlignment(mux::HorizontalAlignment::Right);
            arrow.VerticalAlignment(mux::VerticalAlignment::Center);
            muxc::Grid::SetColumn(arrow, 2);
            content.Children().Append(arrow);
            button.Flyout(spec.submenu);
            // Like a cascading menu: beside the row, top aligned with it.
            spec.submenu.Placement(muxc::Primitives::FlyoutPlacementMode::RightEdgeAlignedTop);
            auto sub = spec.submenu;
            spec.submenu.Opened([sub](Inspectable const&, Inspectable const&) {
                WindowState* open = StateForThisThread(false);
                if (open && open->context) {
                    open->context->openSub = sub;
                }
            });
            spec.submenu.Closed([](Inspectable const&, Inspectable const&) {
                // A choice made in the submenu closes the whole menu. A submenu replaced by
                // another, as the pointer moves down the rows, does not.
                WindowState* open = StateForThisThread(false);
                if (open && open->context && open->context->flyout) {
                    if (GetTickCount64() < open->context->switchUntil) {
                        return;
                    }
                    open->context->flyout.Hide();
                }
            });
            // Right arrow opens it from the keyboard.
            button.KeyDown([sub, button](Inspectable const&, mux::Input::KeyRoutedEventArgs const& keyArgs) {
                if (keyArgs.Key() == winrt::Windows::System::VirtualKey::Right) {
                    try {
                        sub.ShowAt(button);
                    } catch (...) {
                    }
                    keyArgs.Handled(true);
                }
            });
        }
        // The pointer on a row: the submenu of another row goes away, and this row's own opens
        // after a short moment if the pointer stays.
        {
            auto hovering = std::make_shared<bool>(false);
            auto sub = spec.submenu;
            button.PointerEntered([hovering, sub, button](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
                *hovering = true;
                WindowState* st = StateForThisThread(false);
                if (!st || !st->context || !st->queue || g_unloading) {
                    return;
                }
                auto& ctx = *st->context;
                if (ctx.openSub && (!sub || ctx.openSub != sub)) {
                    ctx.switchUntil = GetTickCount64() + 400;
                    try {
                        ctx.openSub.Hide();
                    } catch (...) {
                    }
                    ctx.openSub = nullptr;
                }
                if (sub && ctx.openSub != sub) {
                    if (ctx.hover) {
                        ctx.hover.Stop();
                    }
                    auto timer = st->queue.CreateTimer();
                    ctx.hover = timer;
                    timer.Interval(std::chrono::milliseconds(200));
                    timer.IsRepeating(false);
                    timer.Tick([hovering, sub, button](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
                        sender.Stop();
                        WindowState* now = StateForThisThread(false);
                        if (!*hovering || !now || !now->context || g_unloading || now->context->openSub == sub) {
                            return;
                        }
                        try {
                            // Transient: a submenu that is not light dismissed, so the rows of the
                            // menu under it still see the pointer and another submenu can replace it.
                            muxc::Primitives::FlyoutShowOptions shown;
                            shown.Placement(muxc::Primitives::FlyoutPlacementMode::RightEdgeAlignedTop);
                            shown.ShowMode(muxc::Primitives::FlyoutShowMode::Transient);
                            sub.ShowAt(button, shown);
                            static int logged = 0;
                            if (logged++ < 3) {
                                Out(L"menu: a submenu opened on hover");
                            }
                        } catch (...) {
                            LogHresult(L"opening a submenu on hover");
                        }
                    });
                    timer.Start();
                }
            });
            button.PointerExited([hovering](Inspectable const&, mux::Input::PointerRoutedEventArgs const&) {
                *hovering = false;
            });
        }
        button.Content(content);
        list.Children().Append(button);
        if (spec.action) {
            Clickable(button, spec.action, true);
        }
        anyRows = true;
    }

    // Puts the content in the flyout, hands the flyout to the state so it can be
    // closed, and shows it at `position` of `anchor`.
    void Show(muxc::Flyout const& flyout, mux::UIElement const& anchor, winrt::Windows::Foundation::Point position,
              std::optional<winrt::Windows::Foundation::Rect> avoid = std::nullopt) {
        flyout.Content(root);
        if (styled && g_presenterStyle) {
            flyout.FlyoutPresenterStyle(g_presenterStyle);
        }
        state->context->flyout = flyout;
        // Opened upwards, the quick row goes under the list so that it stays next to the pointer.
        {
            auto rootCopy = root;
            auto header = quickHeader;
            auto line = quickLine;
            flyout.Opened([rootCopy, header, line, anchor, position](Inspectable const&, Inspectable const&) {
                if (!header) {
                    return;
                }
                try {
                    auto origin = rootCopy.TransformToVisual(anchor).TransformPoint(winrt::Windows::Foundation::Point{0, 0});
                    if (origin.Y + 8 < position.Y) {
                        auto kids = rootCopy.Children();
                        uint32_t at = 0;
                        if (kids.IndexOf(header, at)) {
                            kids.RemoveAt(at);
                        }
                        if (kids.IndexOf(line, at)) {
                            kids.RemoveAt(at);
                        }
                        kids.Append(line);
                        kids.Append(header);
                    }
                } catch (...) {
                    LogHresult(L"placing the quick row");
                }
            });
        }
        // A click outside the menu closes it, also outside this window's panel, where the
        // flyout's own light dismiss does not see it.
        try {
            auto rootCopy = root;
            auto timer = state->queue.CreateTimer();
            timer.Interval(std::chrono::milliseconds(40));
            timer.Tick([rootCopy](mud::DispatcherQueueTimer const& sender, Inspectable const&) {
                WindowState* st = StateForThisThread(false);
                if (!st || !st->context || !st->context->flyout || g_unloading) {
                    sender.Stop();
                    return;
                }
                auto& ctx = *st->context;
                bool down = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000;
                if (down && !ctx.wasDown && !ctx.openSub && st->rootGrid && st->bridge) {
                    try {
                        auto topLeft = rootCopy.TransformToVisual(nullptr).TransformPoint(winrt::Windows::Foundation::Point{0, 0});
                        double scale = st->rootGrid.XamlRoot() ? st->rootGrid.XamlRoot().RasterizationScale() : 1.0;
                        POINT corner = {(LONG)(topLeft.X * scale), (LONG)(topLeft.Y * scale)};
                        ClientToScreen((HWND)st->bridge.WindowId().Value, &corner);
                        RECT box = {corner.x - 3, corner.y - 3, corner.x + (LONG)(rootCopy.ActualWidth() * scale) + 3,
                                    corner.y + (LONG)(rootCopy.ActualHeight() * scale) + 3};
                        POINT cursor;
                        if (GetCursorPos(&cursor) && !PtInRect(&box, cursor)) {
                            ctx.flyout.Hide();
                        }
                    } catch (...) {
                    }
                }
                ctx.wasDown = down;
            });
            state->context->watch = timer;
            timer.Start();
        } catch (...) {
            LogHresult(L"watching for a click outside the menu");
        }
        state->context->closedToken = flyout.Closed([](Inspectable const&, Inspectable const&) {
            WindowState* open = StateForThisThread(false);
            if (!open || g_unloading || !open->queue) {
                return;
            }
            // After the click has been handled.
            try {
                open->queue.TryEnqueue([]() {
                    WindowState* now = StateForThisThread(false);
                    if (now && !g_unloading) {
                        CloseContextMenu(now);
                    }
                });
            } catch (...) {
            }
        });
        muxc::Primitives::FlyoutShowOptions options;
        options.Position(position);
        if (avoid) {
            // The clicked row stays uncovered: the menu goes under it, or over it when there is
            // no room below, and the quick row ends up on the side next to it.
            options.ExclusionRect(*avoid);
            options.Placement(muxc::Primitives::FlyoutPlacementMode::BottomEdgeAlignedLeft);
        }
        flyout.ShowAt(anchor, options);
    }
};

// The shell's context menu for one item, shown the way Explorer's own is: a row
// of quick actions with icons on top, the usual commands below, and the rest
// of the shell's entries behind "Show more options". The shell's entries are
// copied into XAML, since a native popup would run a modal loop inside a XAML
// handler, which crashed Explorer before.
void ShowItemFlyout(WindowState* state, Column* column, muxc::ListView const& list,
                    std::vector<size_t> const& picked,
                    winrt::Windows::Foundation::Point position) {
    try {
        CloseContextMenu(state);
        IShellFolder* parent = nullptr;
        HRESULT hr = ILIsEmpty(column->folder.get())
                         ? SHGetDesktopFolder(&parent)
                         : SHBindToObject(nullptr, column->folder.get(),
                                          nullptr, IID_PPV_ARGS(&parent));
        if (FAILED(hr) || !parent) {
            Out(L"context menu: can't bind the folder, hr=0x%08lX", hr);
            return;
        }
        std::vector<PCUITEMID_CHILD> children;
        for (size_t entry : picked) {
            children.push_back(column->entries[entry].child.get());
        }
        IContextMenu* menu = nullptr;
        hr = parent->GetUIObjectOf(state->topLevel, (UINT)children.size(),
                                   children.data(), IID_IContextMenu, nullptr,
                                   (void**)&menu);
        parent->Release();
        if (FAILED(hr) || !menu) {
            Out(L"context menu: no IContextMenu, hr=0x%08lX", hr);
            return;
        }
        HMENU hmenu = CreatePopupMenu();
        hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF,
                                    CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME);
        if (FAILED(hr)) {
            DestroyMenu(hmenu);
            menu->Release();
            Out(L"context menu: QueryContextMenu hr=0x%08lX", hr);
            return;
        }

        state->context = std::make_unique<ContextMenuState>();
        state->context->menu = menu;
        state->context->hmenu = hmenu;
        state->context->owner = state->topLevel;
        state->context->column = column;
        state->context->entries = picked;
        bool insideArchive = ArchiveParentColumn(state, column) >= 0;
        state->context->insideArchive = insideArchive;

        // The shell's entries, with the verb of each.
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
            menu->GetCommandString(info.wID - state->context->idFirst, GCS_VERBW, nullptr,
                                   (char*)verb, ARRAYSIZE(verb));
            row.verb = verb;
            Out(L"menu entry %d: id=%u verb=\"%s\" text=\"%s\"%s", i, row.id, verb, text.c_str(),
                row.sub ? L" (submenu)" : L"");
            rows.push_back(std::move(row));
        }

        muxc::Flyout flyout;
        MenuBuilder builder(state, 300, true);
        auto& root = builder.root;
        std::set<int> used;  // Positions shown above "Show more options".

        auto clickable = [&](muxc::Button const& button, std::function<void()> action, bool closeAfter) {
            builder.Clickable(button, action, closeAfter);
        };
        auto plainButton = [&]() { return builder.PlainButton(); };

        wchar_t folderPath[MAX_PATH] = L"";
        bool onDisk = !insideArchive && SHGetPathFromIDListW(column->folder.get(), folderPath) &&
                      folderPath[0];
        bool single = picked.size() == 1;

        auto findRow = [&](const wchar_t* verb) -> MenuRow* {
            for (auto& row : rows) {
                if (used.count(row.pos)) {
                    continue;
                }
                bool match = verb ? _wcsicmp(row.verb.c_str(), verb) == 0
                                  : (StrStrIW(row.verb.c_str(), L"share") && !row.sub);
                if (match) {
                    return &row;
                }
            }
            return nullptr;
        };

        // The quick row: Cut, Copy, Paste (a folder takes it), Rename, Delete, Share, and the
        // properties pinned at the right end.
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
                item.glyphOnly = pinned;  // The properties button is the Info glyph.
                item.enabled = row->enabled;
                item.pinned = pinned;
                item.action = [id]() { RunContextCommand(id); };
                header.push_back(std::move(item));
            };
            const wchar_t* quickVerbs[] = {L"cut", L"copy", L"paste", L"rename", L"delete", nullptr};
            for (auto verb : quickVerbs) {
                if (insideArchive && !(verb && _wcsicmp(verb, L"copy") == 0)) {
                    continue;
                }
                if (MenuRow* row = findRow(verb)) {
                    addQuick(row, false);
                }
            }
            if (!insideArchive) {
                if (MenuRow* row = findRow(L"properties")) {
                    addQuick(row, true);
                }
            }
            builder.QuickRow(header);
        }

        bool& separatorWanted = builder.separatorWanted;
        auto addRow = [&](const wchar_t* text, const wchar_t* glyph, const wchar_t* shortcut,
                          bool enabled, std::function<void()> action,
                          muxc::MenuFlyout const& submenu, const wchar_t* iconFile) {
            MenuRowSpec spec;
            spec.text = text;
            spec.glyph = glyph ? glyph : L"";
            spec.icon = iconFile ? iconFile : L"";
            spec.shortcut = shortcut ? shortcut : L"";
            spec.enabled = enabled;
            spec.action = std::move(action);
            spec.submenu = submenu;
            builder.Row(spec);
        };
        auto addSubItem = [](muxc::MenuFlyout const& menu, std::wstring const& text, std::function<void()> action) {
            muxc::MenuFlyoutItem item;
            item.Text(text);
            item.Click([action](Inspectable const&, mux::RoutedEventArgs const&) {
                WindowState* now = StateForThisThread(false);
                if (now && !g_unloading) {
                    action();
                }
            });
            menu.Items().Append(item);
        };
        // A row of the shell's own, by verb: its name, its icon and its shortcut.
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

        if (insideArchive) {
            // Inside an archive: copying and extracting only.
            addRow(L"Extract selected here", L"", L"", true,
                   [column, picked]() { ExtractSelected(StateForThisThread(false), column, picked); },
                   muxc::MenuFlyout{nullptr}, L"windows.compressedfile.extract");
        } else {
            // First group: Open, Open in a new tab (a folder), Open with.
            if (MenuRow* row = findRow(L"open")) {
                mux::UIElement appIcon{nullptr};
                if (single && !column->entries[picked[0]].folder && onDisk) {
                    appIcon = AppIconOf(FilePathOf(column->folder.get(), column->entries[picked[0]].child.get()));
                }
                addShellRow(row, nullptr, appIcon);
            }
            if (single && column->entries[picked[0]].folder) {
                addRow(L"Open in new tab", L"\uE8A7", L"", true, [column, picked]() {
                    auto& picked0 = column->entries[picked[0]];
                    Pidl target(picked0.linkTarget ? ILClone(picked0.linkTarget.get())
                                                   : ILCombine(column->folder.get(), picked0.child.get()));
                    OpenFolderInNewTab(StateForThisThread(false), target.get());
                }, muxc::MenuFlyout{nullptr}, L"windows.opennewtab");
            }
            if (MenuRow* row = findRow(L"openas")) {
                addShellRow(row, nullptr);
            }

            // Second group: Extract (an archive), Compress to, New folder from selection, Copy as
            // path, and the main action of the file's format.
            separatorWanted = true;
            if (onDisk && single && column->entries[picked[0]].archive) {
                muxc::MenuFlyout extract;
                size_t index = picked[0];
                std::wstring stem = column->entries[index].name;
                size_t dot = stem.rfind(L'.');
                if (dot != std::wstring::npos && dot > 0) {
                    stem.resize(dot);
                }
                addSubItem(extract, L"Extract here", [column, index]() {
                    ExtractHere(StateForThisThread(false), column, index, false);
                });
                addSubItem(extract, L"Extract to \u201C" + stem + L"\\\u201D", [column, index]() {
                    ExtractHere(StateForThisThread(false), column, index, true);
                });
                if (MenuRow* all = findRow(L"extract")) {
                    used.insert(all->pos);
                    UINT id = all->id;
                    addSubItem(extract, L"Extract all\u2026", [id]() { RunContextCommand(id); });
                }
                addRow(L"Extract", L"\uF012", L"", true, nullptr, extract, L"windows.compressedfile.extract");
            }
            bool compressible = onDisk;
            for (size_t index : picked) {
                if (FilePathOf(column->folder.get(), column->entries[index].child.get()).empty()) {
                    compressible = false;
                }
            }
            if (compressible) {
                muxc::MenuFlyout compress;
                addSubItem(compress, L"ZIP file", [column, picked]() {
                    CompressItems(StateForThisThread(false), column, picked, 0);
                });
                addSubItem(compress, L"7z file", [column, picked]() {
                    CompressItems(StateForThisThread(false), column, picked, 1);
                });
                addSubItem(compress, L"TAR file", [column, picked]() {
                    CompressItems(StateForThisThread(false), column, picked, 2);
                });
                addRow(L"Compress to", L"\uF012", L"", true, nullptr, compress, L"Windows.Zip.Action");
            }
            if (onDisk) {
                addRow(L"New folder from selection", L"\uE8F4", L"", true, [column, picked]() {
                    NewFolderWithSelection(StateForThisThread(false), column, picked);
                }, muxc::MenuFlyout{nullptr}, L"newfolder");
            }
            if (MenuRow* row = findRow(L"copyaspath")) {
                addShellRow(row, nullptr);
            }
            // The main action of the file's format: the first of these the shell offers for it.
            const wchar_t* formatVerbs[] = {L"setdesktopwallpaper", L"Windows.PowerShell.Run", L"runas", L"edit",
                                            L"print",               L"play",                   L"mount", L"install"};
            for (auto verb : formatVerbs) {
                if (MenuRow* row = findRow(verb)) {
                    if (!row->sub) {
                        addShellRow(row, L"\uE734");
                        break;
                    }
                }
            }

            // Third group: everything else the shell offers. The app extensions of the newer
            // menu are not in the data the shell gives, so they are in here too.
            muxc::MenuFlyout more;
            FillFlyoutItems(state, hmenu, more.Items(), 0, &used);
            if (more.Items().Size() > 0) {
                separatorWanted = true;
                addRow(L"More options", L"\uF2C7", L"", true, nullptr, more, L"");
            }
        }

        // The row the menu was asked on, so that the menu does not cover it.
        std::optional<winrt::Windows::Foundation::Rect> rowBox;
        try {
            double best = 1e9;
            for (size_t entry : picked) {
                for (size_t r = 0; r < column->rowToEntry.size(); r++) {
                    if (column->rowToEntry[r] != (int)entry) {
                        continue;
                    }
                    auto container = list.ContainerFromIndex((int)r).try_as<muxc::ListViewItem>();
                    if (!container || container.ActualHeight() <= 0) {
                        continue;
                    }
                    auto box = container.TransformToVisual(list).TransformBounds(
                        winrt::Windows::Foundation::Rect{0, 0, (float)container.ActualWidth(), (float)container.ActualHeight()});
                    double away = position.Y < box.Y ? box.Y - position.Y : position.Y > box.Y + box.Height ? position.Y - box.Y - box.Height : 0;
                    if (away < best) {
                        best = away;
                        rowBox = box;
                    }
                }
            }
        } catch (...) {
        }
        builder.Show(flyout, list, position, rowBox);
        Out(L"context menu shown for %d item(s), first \"%s\": %u parts",
            (int)picked.size(), column->entries[picked[0]].name.c_str(), root.Children().Size());
    } catch (...) {
        LogHresult(L"ShowItemFlyout");
        CloseContextMenu(state);
    }
}

// Right-click on a column's empty space: the same look as the item menu, a row
// of quick actions on top and the rest listed below.
void ShowBackgroundMenu(WindowState* state, Column* column,
                        muxc::ListView const& list,
                        mux::Input::RightTappedRoutedEventArgs const& args) {
    CloseContextMenu(state);
    state->context = std::make_unique<ContextMenuState>();
    state->context->owner = state->topLevel;
    state->context->column = column;

    // The Desktop's properties are the Personalization page of Settings.
    bool desktop = ILIsEmpty(column->folder.get());
    if (!desktop) {
        PIDLIST_ABSOLUTE known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Desktop, 0, nullptr, &known)) && known) {
            desktop = ILIsEqual(known, column->folder.get());
            CoTaskMemFree(known);
        }
    }
    // Inside an archive being browsed, writing and properties make no sense.
    bool inArchive = ArchiveParentColumn(state, column) >= 0;

    muxc::Flyout flyout;
    MenuBuilder builder(state, 260, false);
    auto& root = builder.root;

    auto plainButton = [&]() { return builder.PlainButton(); };
    auto onClick = [&](muxc::Button const& button, std::wstring action) {
        builder.Clickable(button, [action]() {
            WindowState* now = StateForThisThread(false);
            if (!now || !now->context || g_unloading) {
                return;
            }
            RunBackgroundAction(now, now->context->column, action);
        }, true);
    };

    // The quick actions.
    struct Quick {
        const wchar_t* action;
        const wchar_t* label;
        const wchar_t* glyph;
        const wchar_t* icon;
        bool enabled;
    };
    Quick quick[] = {
        {L"paste", L"Paste", L"", L"windows.paste", ClipboardHasFiles() && !inArchive},
        {L"newfolder", L"New folder", L"", L"newfolder", !inArchive},
        {L"refresh", L"Refresh", L"", L"windows.refresh", true},
    };
    {
        std::vector<MenuQuick> header;
        for (auto& q : quick) {
            MenuQuick item;
            item.label = q.label;
            item.glyph = q.glyph;
            item.icon = q.icon;
            item.enabled = q.enabled;
            std::wstring action = q.action;
            item.action = [action]() {
                WindowState* now = StateForThisThread(false);
                if (now && now->context && !g_unloading) {
                    RunBackgroundAction(now, now->context->column, action);
                }
            };
            header.push_back(std::move(item));
        }
        builder.QuickRow(header);
    }

    // The list: the folder's properties.
    {
        MenuRowSpec spec;
        spec.text = desktop ? L"Personalize" : L"Folder properties";
        spec.icon = desktop ? L"Personalize" : L"";
        if (!desktop) {
            spec.glyph = L"\uE946";  // Info.
            spec.glyphOnly = true;
        }
        spec.enabled = !inArchive;
        spec.action = []() {
            WindowState* now = StateForThisThread(false);
            if (now && now->context && !g_unloading) {
                RunBackgroundAction(now, now->context->column, L"properties");
            }
        };
        builder.Row(spec);
    }

    builder.Show(flyout, list, args.GetPosition(list));
    Out(L"background menu shown for \"%s\"", PathOf(column->folder.get()).c_str());
}

void OnRowRightTapped(muxc::ListView const& list,
                      mux::Input::RightTappedRoutedEventArgs const& args) {
    if (args.PointerDeviceType() != winrt::Microsoft::UI::Input::PointerDeviceType::Mouse) {
        Out(L"touch: a right tap, device=%d", (int)args.PointerDeviceType());
    }
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        int columnIndex = ColumnIndexOfList(state, list);
        if (columnIndex < 0) {
            return;
        }
        Column* column = state->columns[columnIndex].get();

        // The row under the pointer: climb from what was hit to its item.
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        muxc::ListViewItem row{nullptr};
        for (int i = 0; node && i < 20; i++) {
            if (auto found = node.try_as<muxc::ListViewItem>()) {
                row = found;
                break;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
        if (!row) {
            // Empty space: the column's own menu, and the column becomes the
            // active one so Paste and the panes act on its folder.
            args.Handled(true);
            state->building = true;
            list.SelectedItems().Clear();
            state->building = false;
            column->selectedChild.reset();
            SetActiveColumn(state, columnIndex);
            ScheduleNavigation(state, column->folder.get());
            ShowBackgroundMenu(state, column, list, args);
            return;
        }
        int rowIndex = list.IndexFromContainer(row);
        int entryIndex = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size()
                             ? column->rowToEntry[rowIndex]
                             : -1;
        if (entryIndex < 0) {
            return;
        }
        args.Handled(true);
        // Right-click inside a selection acts on all of it. Outside, it
        // selects the item under the pointer first.
        std::vector<size_t> picked;
        if (row.IsSelected()) {
            picked = SelectedEntries(column, list);
        }
        if (picked.empty()) {
            list.SelectedIndex(rowIndex);
            picked.push_back((size_t)entryIndex);
        }

        ShowItemFlyout(state, column, list, picked, args.GetPosition(list));
    } catch (...) {
        LogHresult(L"OnRowRightTapped");
        if (state) {
            CloseContextMenu(state);
        }
    }
}

void OnRowDoubleTapped(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    state->slowRenameAt = 0;
    state->slowRenameBlockUntil = GetTickCount64() + 600;
    try {
        int columnIndex = ColumnIndexOfList(state, list);
        int row = list.SelectedIndex();
        if (columnIndex < 0 || row < 0) {
            return;
        }
        Column* column = state->columns[columnIndex].get();
        int entryIndex =
            row < (int)column->rowToEntry.size() ? column->rowToEntry[row] : -1;
        if (entryIndex < 0 || column->entries[entryIndex].folder) {
            return;
        }
        Pidl absolute(ILCombine(column->folder.get(),
                                column->entries[entryIndex].child.get()));
        if (absolute) {
            Out(L"open \"%s\"", column->entries[entryIndex].name.c_str());
            OpenItem(state->topLevel, absolute.get());
        }
    } catch (...) {
        LogHresult(L"OnRowDoubleTapped");
    }
}

// Runs a shell verb such as "delete" or "properties" on items of a column.
HRESULT InvokeVerbOnItems(WindowState* state, Column* column,
                          std::vector<size_t> const& entries,
                          const char* verb) {
    IShellFolder* parent = nullptr;
    HRESULT hr = ILIsEmpty(column->folder.get())
                     ? SHGetDesktopFolder(&parent)
                     : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                      IID_PPV_ARGS(&parent));
    if (FAILED(hr) || !parent) {
        return hr;
    }
    std::vector<PCUITEMID_CHILD> children;
    for (size_t entry : entries) {
        if (entry < column->entries.size()) {
            children.push_back(column->entries[entry].child.get());
        }
    }
    IContextMenu* menu = nullptr;
    hr = children.empty()
             ? E_INVALIDARG
             : parent->GetUIObjectOf(state->topLevel, (UINT)children.size(),
                                     children.data(), IID_IContextMenu, nullptr,
                                     (void**)&menu);
    parent->Release();
    if (FAILED(hr) || !menu) {
        return hr;
    }
    HMENU hmenu = CreatePopupMenu();
    hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF,
                                CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME);
    if (SUCCEEDED(hr)) {
        CMINVOKECOMMANDINFO invoke = {};
        invoke.cbSize = sizeof(invoke);
        invoke.hwnd = state->topLevel;
        invoke.lpVerb = verb;
        invoke.nShow = SW_SHOWNORMAL;
        // Shift+Delete deletes for good, with its confirmation.
        if (GetKeyState(VK_SHIFT) & 0x8000) {
            invoke.fMask |= CMIC_MASK_SHIFT_DOWN;
        }
        hr = menu->InvokeCommand(&invoke);
    }
    DestroyMenu(hmenu);
    menu->Release();
    return hr;
}

// Paste into a folder, through that folder's item in its
// parent, which is the route the right-click menu's Paste took.
HRESULT InvokeVerbOnFolder(WindowState* state, PCIDLIST_ABSOLUTE folder,
                           const char* verb) {
    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD child = nullptr;
    HRESULT hr = SHBindToParent(folder, IID_PPV_ARGS(&parent),
                                &child);
    if (FAILED(hr) || !parent) {
        return hr;
    }
    IContextMenu* menu = nullptr;
    hr = parent->GetUIObjectOf(state->topLevel, 1, &child, IID_IContextMenu,
                               nullptr, (void**)&menu);
    parent->Release();
    if (FAILED(hr) || !menu) {
        return hr;
    }
    HMENU hmenu = CreatePopupMenu();
    hr = menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE);
    if (SUCCEEDED(hr)) {
        CMINVOKECOMMANDINFO invoke = {};
        invoke.cbSize = sizeof(invoke);
        invoke.hwnd = state->topLevel;
        invoke.lpVerb = verb;
        invoke.nShow = SW_SHOWNORMAL;
        hr = menu->InvokeCommand(&invoke);
    }
    DestroyMenu(hmenu);
    menu->Release();
    return hr;
}

// Paste the clipboard into a folder the way a drop does, through the folder's
// own drop target. The "paste" verb of the context menu made Explorer's view
// jump to another folder afterwards.
HRESULT PasteIntoFolder(WindowState* state, PCIDLIST_ABSOLUTE folder) {
    IShellFolder* target = nullptr;
    HRESULT hr = ILIsEmpty(folder)
                     ? SHGetDesktopFolder(&target)
                     : SHBindToObject(nullptr, folder, nullptr,
                                      IID_PPV_ARGS(&target));
    if (FAILED(hr) || !target) {
        return hr;
    }
    IDropTarget* drop = nullptr;
    hr = target->CreateViewObject(state->topLevel, IID_PPV_ARGS(&drop));
    target->Release();
    IDataObject* data = nullptr;
    if (SUCCEEDED(hr) && drop) {
        hr = OleGetClipboard(&data);
    }
    if (FAILED(hr) || !drop || !data) {
        if (drop) {
            drop->Release();
        }
        if (data) {
            data->Release();
        }
        Out(L"paste: no drop target or clipboard (hr=0x%08lX), using the verb", hr);
        return InvokeVerbOnFolder(state, folder, "paste");
    }

    // Cut items move, copied items copy.
    DWORD effect = DROPEFFECT_COPY;
    static UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    FORMATETC wanted = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1,
                        TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    if (data->GetData(&wanted, &medium) == S_OK) {
        if (medium.hGlobal) {
            if (DWORD* value = (DWORD*)GlobalLock(medium.hGlobal)) {
                if (*value & DROPEFFECT_MOVE) {
                    effect = DROPEFFECT_MOVE;
                }
                GlobalUnlock(medium.hGlobal);
            }
        }
        ReleaseStgMedium(&medium);
    }
    POINTL where = {0, 0};
    DWORD allowed = effect;
    hr = drop->DragEnter(data, MK_LBUTTON, where, &allowed);
    if (SUCCEEDED(hr) && allowed != DROPEFFECT_NONE) {
        hr = drop->Drop(data, 0, where, &allowed);
        if (SUCCEEDED(hr) && effect == DROPEFFECT_MOVE) {
            OleSetClipboard(nullptr);
        }
    } else {
        drop->DragLeave();
    }
    Out(L"paste as %s: hr=0x%08lX", effect == DROPEFFECT_MOVE ? L"move" : L"copy",
        hr);
    data->Release();
    drop->Release();
    ReloadAll(state);  // The target, and the source of a move.
    return hr;
}

// A click on the empty space of a column makes it the active one: Explorer's
// own view moves to its folder, so Paste and the panes act there. The columns
// to its right stay.
// The row of the group header at a point of the list, or -1.
int HeaderRowAt(Column* column, muxc::ListView const& list,
                winrt::Windows::Foundation::Point at) {
    auto items = list.Items();
    for (size_t r = 0; r < column->rowGroup.size() && r < items.Size(); r++) {
        if (column->rowToEntry[r] >= 0 || column->rowGroup[r] < 0) {
            continue;
        }
        auto header = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>();
        if (!header) {
            continue;
        }
        auto bounds = header.TransformToVisual(list).TransformBounds(
            winrt::Windows::Foundation::Rect{0, 0, (float)header.ActualWidth(),
                                              (float)header.ActualHeight()});
        if (at.X >= bounds.X && at.X <= bounds.X + bounds.Width &&
            at.Y >= bounds.Y && at.Y <= bounds.Y + bounds.Height) {
            return (int)r;
        }
    }
    return -1;
}

// One group header in its state: normal, under the pointer, or the one the
// keyboard is on, which is tinted a little stronger.
void PaintHeader(muxc::ListViewItem const& header, bool hover, bool focused) {
    header.Background(focused ? Brush(96, 150, 150, 150)
                              : hover ? Brush(64, 150, 150, 150) : Brush(0, 0, 0, 0));
    header.Opacity(focused || hover ? 1.0 : 0.92);
}

// Hover on the group headers: the one under the pointer is tinted.
void SetHeaderHover(Column* column, muxc::ListView const& list, int row) {
    if (row == column->hoverHeader) {
        return;
    }
    auto items = list.Items();
    auto paint = [&](int r, bool on) {
        if (r >= 0 && r < (int)items.Size()) {
            if (auto header = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
                PaintHeader(header, on, r == column->focusHeader);
            }
        }
    };
    paint(column->hoverHeader, false);
    column->hoverHeader = row;
    paint(row, true);
}

muxc::ToolTip BuildDetailTip(Column* column, size_t entryIndex);
bool OnBandMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args);

// The keyboard on a group header: it is tinted, and Left and Right collapse and
// open it. Headers cannot take real keyboard focus, so the mod keeps this
// itself.
void SetHeaderFocus(Column* column, muxc::ListView const& list, int row) {
    int old = column->focusHeader;
    column->focusHeader = row;
    auto items = list.Items();
    auto paint = [&](int r, bool on) {
        if (r >= 0 && r < (int)items.Size()) {
            if (auto header = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>()) {
                PaintHeader(header, !on && r == column->hoverHeader, on);
            }
        }
    };
    paint(old, false);
    paint(row, true);
    if (row >= 0 && row < (int)items.Size()) {
        list.ScrollIntoView(items.GetAt((uint32_t)row));
    }
}

// The next row in a direction that is a group header or a visible item.
int NextHeaderOrItemRow(Column* column, muxc::ListView const& list, int from, int step) {
    auto items = list.Items();
    for (int r = from + step; r >= 0 && r < (int)column->rowToEntry.size() && r < (int)items.Size();
         r += step) {
        bool header = column->rowToEntry[r] < 0 && column->rowGroup[r] >= 0;
        bool item = column->rowToEntry[r] >= 0;
        if (!header && !item) {
            continue;  // The "more items" row.
        }
        auto row = items.GetAt((uint32_t)r).try_as<muxc::ListViewItem>();
        if (row && row.Visibility() == mux::Visibility::Visible) {
            return r;
        }
    }
    return -1;
}

void OnColumnPointerMoved(muxc::ListView const& list,
                          mux::Input::PointerRoutedEventArgs const& args) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        int index = ColumnIndexOfList(state, list);
        if (index < 0) {
            return;
        }
        Column* column = state->columns[index].get();
        if (OnDragMoved(state, column, list, args)) {
            return;  // Rows are being dragged.
        }
        if (OnBandMoved(state, column, list, args)) {
            return;  // A rectangle is being drawn.
        }
        if (column->rowGroup.empty()) {
            return;
        }
        SetHeaderHover(column, list, HeaderRowAt(column, list,
                                                 args.GetCurrentPoint(list).Position()));

        // The first time the pointer is on a row: its detail tooltip.
        mux::DependencyObject node =
            args.OriginalSource().try_as<mux::DependencyObject>();
        for (int i = 0; node && i < 20; i++) {
            if (auto row = node.try_as<muxc::ListViewItem>()) {
                int rowIndex = list.IndexFromContainer(row);
                int entry = rowIndex >= 0 && rowIndex < (int)column->rowToEntry.size()
                                ? column->rowToEntry[rowIndex]
                                : -1;
                if (entry >= 0 && (size_t)entry < column->tipBuilt.size() &&
                    !column->tipBuilt[entry]) {
                    column->tipBuilt[entry] = 1;
                    muxc::ToolTipService::SetToolTip(
                        row, BuildDetailTip(column, (size_t)entry));
                }
                break;
            }
            node = mux::Media::VisualTreeHelper::GetParent(node);
        }
    } catch (...) {
        LogHresult(L"header hover");
    }
}

void OnColumnPointerExited(muxc::ListView const& list) {
    WindowState* state = StateForThisThread(false);
    if (!state || !state->on || g_unloading) {
        return;
    }
    try {
        int index = ColumnIndexOfList(state, list);
        if (index >= 0) {
            SetHeaderHover(state->columns[index].get(), list, -1);
        }
    } catch (...) {
    }
}

// The detail tooltip of a row: the name, then what the shell knows about the
// item, such as its type, size, dates, picture size or length. The labels and
// the values are the shell's own, as the Details view shows them. Read the first
// time the pointer is over the row, not for every row up front.
muxc::ToolTip BuildDetailTip(Column* column, size_t entryIndex) {
    const Entry& entry = column->entries[entryIndex];
    struct Line {
        std::wstring label;
        std::wstring value;
    };
    std::vector<Line> lines;

    IShellFolder* folder = nullptr;
    HRESULT hr = ILIsEmpty(column->folder.get())
                     ? SHGetDesktopFolder(&folder)
                     : SHBindToObject(nullptr, column->folder.get(), nullptr,
                                      IID_PPV_ARGS(&folder));
    IShellFolder2* folder2 = nullptr;
    if (SUCCEEDED(hr) && folder) {
        folder->QueryInterface(IID_PPV_ARGS(&folder2));
    }
    if (folder2) {
        // One pass over the folder's columns finds the number of each property.
        const PROPERTYKEY* wanted[] = {
            &PKEY_ItemTypeText,    &PKEY_Size,         &PKEY_DateModified,
            &PKEY_Image_Dimensions, &PKEY_Media_Duration, &PKEY_Title,
            &PKEY_Author,          &PKEY_Music_Artist, &PKEY_Music_AlbumTitle,
            &PKEY_Keywords,        &PKEY_Comment};
        const int count = ARRAYSIZE(wanted);
        int columnOf[ARRAYSIZE(wanted)];
        for (int& c : columnOf) {
            c = -1;
        }
        int misses = 0;
        for (UINT i = 0; i < 600 && misses < 30; i++) {
            SHCOLUMNID id{};
            if (FAILED(folder2->MapColumnToSCID(i, &id))) {
                misses++;
                continue;
            }
            misses = 0;
            for (int k = 0; k < count; k++) {
                if (columnOf[k] < 0 && IsEqualPropertyKey(id, *wanted[k])) {
                    columnOf[k] = (int)i;
                }
            }
        }
        PCUITEMID_CHILD child = entry.child.get();
        for (int k = 0; k < count && lines.size() < 8; k++) {
            if (columnOf[k] < 0) {
                continue;
            }
            SHELLDETAILS head{}, value{};
            wchar_t headText[128] = L"", valueText[512] = L"";
            if (SUCCEEDED(folder2->GetDetailsOf(nullptr, (UINT)columnOf[k], &head)) &&
                SUCCEEDED(StrRetToBufW(&head.str, nullptr, headText, ARRAYSIZE(headText))) &&
                SUCCEEDED(folder2->GetDetailsOf(child, (UINT)columnOf[k], &value)) &&
                SUCCEEDED(StrRetToBufW(&value.str, child, valueText, ARRAYSIZE(valueText))) &&
                headText[0] && valueText[0]) {
                lines.push_back({headText, valueText});
            }
        }
        folder2->Release();
    }
    if (folder) {
        folder->Release();
    }

    // The tooltip is never wider than the column of the row it describes.
    double columnWidth = column->list ? column->list.ActualWidth() : 0;
    if (columnWidth < 160) {
        columnWidth = 160;
    }
    const double padding = 24;  // The tooltip's own padding and border.
    double room = columnWidth - padding;
    muxc::StackPanel panel;
    panel.MaxWidth(room);
    muxc::TextBlock name;
    name.Text(entry.name);
    name.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    name.TextWrapping(mux::TextWrapping::Wrap);
    name.MaxWidth(std::min(323.0, room));
    panel.Children().Append(name);
    if (!lines.empty()) {
        muxc::Grid grid;
        grid.Margin(mux::Thickness{0, 6, 0, 0});
        muxc::ColumnDefinition labels, values;
        labels.Width(mux::GridLength{1, mux::GridUnitType::Auto});
        values.Width(mux::GridLength{1, mux::GridUnitType::Auto});
        grid.ColumnDefinitions().Append(labels);
        grid.ColumnDefinitions().Append(values);
        for (size_t i = 0; i < lines.size(); i++) {
            muxc::RowDefinition row;
            row.Height(mux::GridLength{1, mux::GridUnitType::Auto});
            grid.RowDefinitions().Append(row);
            muxc::TextBlock label;
            label.Text(lines[i].label);
            label.Opacity(0.65);
            label.FontSize(12);
            label.Margin(mux::Thickness{0, 1, 12, 1});
            muxc::Grid::SetRow(label, (int)i);
            muxc::TextBlock value;
            value.Text(lines[i].value);
            value.FontSize(12);
            value.Margin(mux::Thickness{0, 1, 0, 1});
            value.TextWrapping(mux::TextWrapping::Wrap);
            value.MaxWidth(std::max(60.0, std::min(221.0, room - 96)));
            muxc::Grid::SetRow(value, (int)i);
            muxc::Grid::SetColumn(value, 1);
            grid.Children().Append(label);
            grid.Children().Append(value);
        }
        panel.Children().Append(grid);
    }
    muxc::ToolTip tip;
    tip.MaxWidth(columnWidth);
    tip.Content(panel);
    return tip;
}

