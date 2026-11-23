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

// What an editor writes back as Keywords (it showed the merged list): `written` without the labels that the file did not have as
// keywords (`real` is what the file has). A label that is also a real keyword is kept, as is anything else. Pure.
std::vector<std::wstring> LabelsKeywordsToWrite(std::vector<std::wstring> const& written, std::vector<std::wstring> const& real,
                                                std::vector<std::wstring> const& labels) {
    auto in = [](std::vector<std::wstring> const& list, std::wstring const& text) {
        for (auto const& item : list) {
            if (_wcsicmp(item.c_str(), text.c_str()) == 0) {
                return true;
            }
        }
        return false;
    };
    std::vector<std::wstring> kept;
    for (auto const& keyword : written) {
        if (in(labels, keyword) && !in(real, keyword)) {
            continue;  // Ours: it must not become a keyword of the file.
        }
        kept.push_back(keyword);
    }
    return kept;
}

namespace {

using LabelsGetValue_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFPROPERTYKEY, PROPVARIANT*);
LabelsGetValue_t LabelsGetValue_Original = nullptr;
std::mutex g_keywordsAskedMutex;
std::set<std::wstring> g_keywordsAsked;  // Folders whose Keywords were asked before their labels were read.

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

// The label names of the item of `store`, as Keywords values. Empty when it has none, is not a file system item, or its folder is not
// read yet (then `*waiting` is set and the folder remembered, to be read again by Explorer when it is).
std::vector<std::wstring> LabelsNamesOfStore(void* store, bool* waiting, bool fromStream = false) {
    std::vector<std::wstring> labels;
    *waiting = false;
    std::wstring path = LabelsStoreText(store, PKEY_ParsingPath);
    std::wstring name = LabelsStoreText(store, PKEY_ItemNameDisplay);
    if (path.size() < 4 || path[1] != L':' || name.empty()) {
        return labels;  // Not a file system item.
    }
    if (fromStream) {
        // A write is rare and the editor showed what the list was, so the item's own stream is read: the folder cache may have
        // dropped the folder since (16 folders), and the labels would then not be taken out.
        std::vector<LabelTag> tags;
        if (LabelsReadFile(path, &tags)) {
            for (auto const& tag : tags) {
                std::wstring shown = tag.name.empty() ? LabelsColourName(tag.colour) : tag.name;
                labels.push_back(shown.empty() ? std::wstring(L"Gray") : shown);
            }
        }
        return labels;
    }
    std::wstring folder = path;
    PathRemoveFileSpecW(folder.data());
    folder.resize(wcslen(folder.c_str()));
    auto data = LabelsFolderFor(folder);
    if (!data->complete) {
        *waiting = true;
        std::lock_guard<std::mutex> lock(g_keywordsAskedMutex);
        g_keywordsAsked.insert(folder);
        return labels;
    }
    auto found = data->byName.find(name);
    if (found == data->byName.end()) {
        return labels;
    }
    for (auto const& tag : found->second) {
        std::wstring shown = tag.name.empty() ? LabelsColourName(tag.colour) : tag.name;
        labels.push_back(shown.empty() ? std::wstring(L"Gray") : shown);
    }
    return labels;
}

// The strings of a PROPVARIANT that holds Keywords (a vector, or one string).
std::vector<std::wstring> LabelsStrings(PROPVARIANT const& value) {
    std::vector<std::wstring> strings;
    if (value.vt == (VT_VECTOR | VT_LPWSTR)) {
        for (ULONG i = 0; i < value.calpwstr.cElems; i++) {
            if (value.calpwstr.pElems[i]) {
                strings.push_back(value.calpwstr.pElems[i]);
            }
        }
    } else if (value.vt == VT_LPWSTR && value.pwszVal) {
        strings.push_back(value.pwszVal);
    }
    return strings;
}

HRESULT LabelsServeKeywords(void* store, HRESULT hr, PROPVARIANT* out) {
    bool waiting = false;
    std::vector<std::wstring> labels = LabelsNamesOfStore(store, &waiting);
    if (labels.empty()) {
        return hr;
    }
    std::vector<std::wstring> existing;
    if (SUCCEEDED(hr)) {
        existing = LabelsStrings(*out);
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

std::atomic<bool> g_labelsBothHooks{false};  // Keywords are served as labels only when the SetValue guard is in too.

HRESULT STDMETHODCALLTYPE LabelsGetValue_Hook(void* store, REFPROPERTYKEY key, PROPVARIANT* out) {
    HRESULT hr = LabelsGetValue_Original(store, key, out);
    if (!out || g_unloading || !g_labels || !g_labelsGroup || !g_labelsBothHooks || !IsEqualPropertyKey(key, PKEY_Keywords)) {
        return hr;
    }
    try {
        return LabelsServeKeywords(store, hr, out);
    } catch (...) {
        return hr;
    }
}

// IPropertyStore slot 6. An editor (Properties, Details, or the details pane) shows the Keywords it was served, labels included, and
// writes the whole list back: the labels are taken out of what is written, so they never become keywords of the file.
using LabelsSetValue_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFPROPERTYKEY, REFPROPVARIANT);
LabelsSetValue_t LabelsSetValue_Original = nullptr;

HRESULT STDMETHODCALLTYPE LabelsSetValue_Hook(void* store, REFPROPERTYKEY key, REFPROPVARIANT value) {
    if (!g_unloading && g_labels && g_labelsGroup && IsEqualPropertyKey(key, PKEY_Keywords) &&
        (value.vt == (VT_VECTOR | VT_LPWSTR) || value.vt == VT_LPWSTR)) {
        try {
            bool waiting = false;
            std::vector<std::wstring> labels = LabelsNamesOfStore(store, &waiting, true);
            if (!labels.empty()) {
                PROPVARIANT current;
                PropVariantInit(&current);
                std::vector<std::wstring> real;
                if (SUCCEEDED(LabelsGetValue_Original(store, key, &current))) {
                    real = LabelsStrings(current);
                }
                PropVariantClear(&current);
                std::vector<std::wstring> kept = LabelsKeywordsToWrite(LabelsStrings(value), real, labels);
                LogVerbose(L"GROUP SetValue Keywords: %d written, %d real, %d labels, %d kept", (int)LabelsStrings(value).size(),
                           (int)real.size(), (int)labels.size(), (int)kept.size());
                std::vector<PCWSTR> pointers;
                for (auto const& keyword : kept) {
                    pointers.push_back(keyword.c_str());
                }
                PROPVARIANT cleaned;
                PropVariantInit(&cleaned);
                if (pointers.empty()) {
                    return LabelsSetValue_Original(store, key, cleaned);  // VT_EMPTY: the keywords are cleared.
                }
                if (SUCCEEDED(InitPropVariantFromStringVector(pointers.data(), (ULONG)pointers.size(), &cleaned))) {
                    HRESULT hr = LabelsSetValue_Original(store, key, cleaned);
                    PropVariantClear(&cleaned);
                    return hr;
                }
            }
        } catch (...) {
        }
    }
    return LabelsSetValue_Original(store, key, value);
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
                        ok = WindhawkUtils::SetFunctionHook((LabelsGetValue_t)target, LabelsGetValue_Hook, &LabelsGetValue_Original) != FALSE;
                        void* setter = (*(void***)store)[6];  // 6 SetValue.
                        if (ok && !WindhawkUtils::SetFunctionHook((LabelsSetValue_t)setter, LabelsSetValue_Hook, &LabelsSetValue_Original)) {
                            Wh_Log(L"labels: the SetValue hook could not be set, Keywords are not served as labels");
                        } else {
                            g_labelsBothHooks = ok;
                        }
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
    // Explorer was served no labels while the folder was being read: when it has none, there is nothing for it to read again.
    if (asked && g_labelsGroup && !g_unloading && !LabelsFolderFor(folderPath)->byName.empty()) {
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
