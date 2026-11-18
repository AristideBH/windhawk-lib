// Labels in Explorer's own Sort by and Group by. Explorer reads the value of a property for every item through
// IPropertyStore::GetValue, and Group by Tags (System.Keywords, the native Tags column) takes its groups from there, so
// serving the labels' names as Keywords makes the native menu work with no schema and no extra code in Explorer: an item
// with two labels is in both groups. Found with probes/probe-sort-group.wh.cpp (see the surfaces skill). The tags the file
// already has (a photo's keywords) are kept, and the labels added to them.
//
// The store of an item has no item identity, but it answers System.ParsingPath and System.ItemNameDisplay itself: the folder
// cache (04b) is looked up with those. A folder that is not read yet serves nothing and is remembered, and when its read ends
// Explorer is told the folder changed, so the groups are made again. After a tag is written Explorer is told the item changed.

std::atomic<bool> g_labelsGroup{true};  // Serve the labels as Keywords.

// The values of Keywords for an item: the keywords it has, then the label names that are not among them (any case), each once,
// empty names dropped. Pure.
std::vector<std::wstring> LabelsKeywordValues(std::vector<std::wstring> const& existing, std::vector<std::wstring> const& labels) {
    std::vector<std::wstring> values;
    auto has = [&values](std::wstring const& text) {
        for (auto const& value : values) {
            if (_wcsicmp(value.c_str(), text.c_str()) == 0) {
                return true;
            }
        }
        return false;
    };
    for (auto const& keyword : existing) {
        if (!keyword.empty()) {
            values.push_back(keyword);
        }
    }
    for (auto const& label : labels) {
        if (!label.empty() && !has(label)) {
            values.push_back(label);
        }
    }
    return values;
}

// The groups of a folder in Columns: each label is a group, an item is in the group of each of its labels (like Explorer's own
// Group by Tags), and the items with no label are in a last group. `perEntry` is the label names of each item. Pure.
struct LabelsGroupPlan {
    std::vector<std::wstring> names;
    std::vector<std::vector<size_t>> members;  // The items of each group, in their order.
};

LabelsGroupPlan LabelsPlanGroups(std::vector<std::vector<std::wstring>> const& perEntry, bool ascending, std::wstring const& none) {
    LabelsGroupPlan plan;
    std::vector<std::wstring> labels;  // The distinct labels (any case), as first seen.
    std::vector<std::vector<size_t>> of;
    std::vector<size_t> untagged;
    for (size_t i = 0; i < perEntry.size(); i++) {
        if (perEntry[i].empty()) {
            untagged.push_back(i);
        }
        for (auto const& label : perEntry[i]) {
            size_t at = 0;
            while (at < labels.size() && _wcsicmp(labels[at].c_str(), label.c_str()) != 0) {
                at++;
            }
            if (at == labels.size()) {
                labels.push_back(label);
                of.emplace_back();
            }
            if (of[at].empty() || of[at].back() != i) {
                of[at].push_back(i);
            }
        }
    }
    std::vector<size_t> order(labels.size());
    for (size_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        int cmp = StrCmpLogicalW(labels[a].c_str(), labels[b].c_str());
        return ascending ? cmp < 0 : cmp > 0;
    });
    for (size_t i : order) {
        plan.names.push_back(labels[i]);
        plan.members.push_back(of[i]);
    }
    if (!untagged.empty()) {
        plan.names.push_back(none);
        plan.members.push_back(untagged);
    }
    return plan;
}

namespace {

using LabelsGetValue_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFPROPERTYKEY, PROPVARIANT*);
LabelsGetValue_t LabelsGetValue_Original = nullptr;
[[clang::no_destroy]] std::mutex g_keywordsAskedMutex;
[[clang::no_destroy]] std::set<std::wstring> g_keywordsAsked;  // Folders whose Keywords were asked before their labels were read.

// A text property of the item through the same store, or empty.
std::wstring LabelsStoreText(void* store, REFPROPERTYKEY key) {
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring text;
    if (SUCCEEDED(LabelsGetValue_Original(store, key, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
        text = value.pwszVal;
    }
    PropVariantClear(&value);
    return text;
}

HRESULT LabelsServeKeywords(void* store, HRESULT hr, PROPVARIANT* out) {
    std::wstring path = LabelsStoreText(store, PKEY_ParsingPath);
    std::wstring name = LabelsStoreText(store, PKEY_ItemNameDisplay);
    if (path.size() < 4 || path[1] != L':' || name.empty()) {
        return hr;  // Not a file system item.
    }
    std::wstring folder = path;
    PathRemoveFileSpecW(folder.data());
    folder.resize(wcslen(folder.c_str()));
    auto data = LabelsFolderFor(folder);
    if (!data->complete) {
        std::lock_guard<std::mutex> lock(g_keywordsAskedMutex);
        g_keywordsAsked.insert(folder);
        return hr;
    }
    auto found = data->byName.find(name);
    if (found == data->byName.end() || found->second.empty()) {
        return hr;
    }
    std::vector<std::wstring> existing;
    if (SUCCEEDED(hr) && out->vt == (VT_VECTOR | VT_LPWSTR)) {
        for (ULONG i = 0; i < out->calpwstr.cElems; i++) {
            if (out->calpwstr.pElems[i]) {
                existing.push_back(out->calpwstr.pElems[i]);
            }
        }
    } else if (SUCCEEDED(hr) && out->vt == VT_LPWSTR && out->pwszVal) {
        existing.push_back(out->pwszVal);
    }
    std::vector<std::wstring> labels;
    for (auto const& tag : found->second) {
        std::wstring shown = tag.name.empty() ? LabelsColourName(tag.colour) : tag.name;
        labels.push_back(shown.empty() ? std::wstring(L"Gray") : shown);
    }
    std::vector<std::wstring> values = LabelsKeywordValues(existing, labels);
    std::vector<PCWSTR> pointers;
    for (auto const& value : values) {
        pointers.push_back(value.c_str());
    }
    PROPVARIANT served;
    if (pointers.empty() || FAILED(InitPropVariantFromStringVector(pointers.data(), (ULONG)pointers.size(), &served))) {
        return hr;
    }
    PropVariantClear(out);
    *out = served;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE LabelsGetValue_Hook(void* store, REFPROPERTYKEY key, PROPVARIANT* out) {
    HRESULT hr = LabelsGetValue_Original(store, key, out);
    if (!out || g_unloading || !g_labels || !g_labelsGroup || !IsEqualPropertyKey(key, PKEY_Keywords)) {
        return hr;
    }
    try {
        return LabelsServeKeywords(store, hr, out);
    } catch (...) {
        return hr;
    }
}

}  // namespace

// Hooks GetValue of the store that Explorer uses for the items of the file system: found from the store of one file of the
// Windows folder (the class is the same for every file and folder, whatever its type). False, with a log line, when not.
bool LabelsInstallGroupHook() {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    PIDLIST_ABSOLUTE folderId = nullptr;
    IShellFolder* folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Windows, 0, nullptr, &folderId)) && folderId &&
        SUCCEEDED(SHBindToObject(nullptr, folderId, nullptr, IID_PPV_ARGS(&folder))) && folder) {
        IEnumIDList* list = nullptr;
        if (SUCCEEDED(folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &list)) && list) {
            PITEMID_CHILD child = nullptr;
            if (list->Next(1, &child, nullptr) == S_OK && child) {
                PIDLIST_ABSOLUTE full = ILCombine(folderId, child);
                IShellItem2* item = nullptr;
                if (full && SUCCEEDED(SHCreateItemFromIDList(full, IID_PPV_ARGS(&item))) && item) {
                    IPropertyStore* store = nullptr;
                    if (SUCCEEDED(item->GetPropertyStore(GPS_DEFAULT, IID_PPV_ARGS(&store))) && store) {
                        void* target = (*(void***)store)[5];  // IPropertyStore: 3 GetCount, 4 GetAt, 5 GetValue.
                        ok = Wh_SetFunctionHook(target, (void*)LabelsGetValue_Hook, (void**)&LabelsGetValue_Original) != FALSE;
                        store->Release();
                    }
                    item->Release();
                }
                if (full) {
                    ILFree(full);
                }
                ILFree(child);
            }
            list->Release();
        }
        folder->Release();
    }
    if (folderId) {
        ILFree(folderId);
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    if (ok) {
        Wh_Log(L"labels: Sort by and Group by tags are on (property store hook installed)");
    } else {
        Wh_Log(L"labels: the property store hook could not be set, Sort by and Group by tags are off");
    }
    return ok;
}

// After a tag was written: Explorer reads the item again, so its group changes.
void LabelsGroupNotifyItem(std::wstring const& itemPath) {
    if (g_labelsGroup && !g_unloading) {
        SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, itemPath.c_str(), nullptr);
        // Columns does not watch for an item's update (Explorer sends them all the time): its columns are read again, on each
        // window's own thread, so a grouping by Tags puts the item in its new group.
        std::lock_guard<std::mutex> lock(g_statesMutex);
        for (auto& state : g_states) {
            if (state->queue && state->on) {
                try {
                    state->queue.TryEnqueue([]() {
                        WindowState* now = StateForThisThread(false);
                        if (now && now->on && !g_unloading) {
                            now->reloadDue = GetTickCount64() + 200;
                        }
                    });
                } catch (...) {
                }
            }
        }
    }
}

// A folder was read: when Explorer asked for its Keywords before that, it reads the folder again.
void LabelsGroupFolderLoaded(std::wstring const& folderPath) {
    bool asked;
    {
        std::lock_guard<std::mutex> lock(g_keywordsAskedMutex);
        asked = g_keywordsAsked.erase(folderPath) > 0;
    }
    if (asked && g_labelsGroup && !g_unloading) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, folderPath.c_str(), nullptr);
    }
}

// Groups the entries of a folder by their labels, for Columns, when it is grouped by Tags (Keywords). False, and nothing changed,
// when it is not that grouping, the feature is off, or the folder's labels are not read yet (the folder is then remembered, and
// when its read ends Explorer is told it changed, which refreshes the column).
bool LabelsGroupEntries(std::wstring const& folderPath, SortSpec const& spec, FolderResult* result) {
    if (!spec.grouped || !IsEqualPropertyKey(spec.groupKey, PKEY_Keywords) || !g_labels || !g_labelsGroup || g_unloading ||
        folderPath.empty() || result->entries.empty()) {
        return false;
    }
    auto data = LabelsFolderFor(folderPath);
    if (!data->complete) {
        std::lock_guard<std::mutex> lock(g_keywordsAskedMutex);
        g_keywordsAsked.insert(folderPath);
        return false;
    }
    std::vector<std::vector<std::wstring>> perEntry;
    for (auto const& entry : result->entries) {
        std::vector<std::wstring> names;
        auto found = data->byName.find(entry.name);
        if (found != data->byName.end()) {
            for (auto const& tag : found->second) {
                std::wstring shown = tag.name.empty() ? LabelsColourName(tag.colour) : tag.name;
                names.push_back(shown.empty() ? std::wstring(L"Gray") : shown);
            }
        }
        perEntry.push_back(std::move(names));
    }
    LabelsGroupPlan plan = LabelsPlanGroups(perEntry, spec.groupAscending, L"Unspecified");
    std::vector<Entry> regrouped;
    result->groupOf.clear();
    result->groupNames = plan.names;
    for (size_t g = 0; g < plan.names.size(); g++) {
        for (size_t i : plan.members[g]) {
            Entry const& from = result->entries[i];
            Entry copy;
            copy.child.reset(ILClone(from.child.get()));
            copy.name = from.name;
            copy.folder = from.folder;
            copy.launcher = from.launcher;
            copy.archive = from.archive;
            copy.remote = from.remote;
            if (from.linkTarget) {
                copy.linkTarget.reset(ILClone(from.linkTarget.get()));
            }
            regrouped.push_back(std::move(copy));
            result->groupOf.push_back((int)g);
        }
    }
    result->entries = std::move(regrouped);
    return true;
}
