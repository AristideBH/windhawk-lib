// Labels: the tags of the items of a folder, read once by a thread of the mod and kept in a cache that the
// paint hook only reads (it never touches the disk). The key of an item is its displayed name, the same text
// that the file list gives back as the accessible name of a row. Two items that show the same name (hidden
// extensions) cannot be told apart in the list, so a name like that is "ambiguous" and gets no dot.

struct LabelsFolder {
    std::unordered_map<std::wstring, std::vector<LabelTag>> byName;
    std::unordered_set<std::wstring> ambiguous;  // Names shown by more than one item, at least one of them tagged.
    bool streamsSupported = true;                // The volume can carry the stream.
    bool complete = false;                       // The folder was read to its end.
};

// What one item of a folder gave: its displayed name, and its tags (empty when it has none).
struct LabelsItem {
    std::wstring name;
    std::vector<LabelTag> tags;
};

// Builds the cache entry of a folder from what its items gave. Pure.
LabelsFolder LabelsCollect(std::vector<LabelsItem> const& items) {
    LabelsFolder folder;
    std::unordered_map<std::wstring, int> seen;
    std::unordered_set<std::wstring> tagged;
    for (auto const& item : items) {
        seen[item.name]++;
        if (!item.tags.empty()) {
            tagged.insert(item.name);
            folder.byName[item.name] = item.tags;
        }
    }
    for (auto const& name : tagged) {
        if (seen[name] > 1) {
            folder.ambiguous.insert(name);
            folder.byName.erase(name);
        }
    }
    folder.complete = true;
    return folder;
}

namespace {

struct LabelsSlot {
    std::shared_ptr<const LabelsFolder> data = std::make_shared<LabelsFolder>();
    std::shared_ptr<std::atomic<bool>> cancel;  // Of the read that is running, null when none is.
    ULONGLONG used = 0;
};

std::mutex g_labelsCacheMutex;
[[clang::no_destroy]] std::map<std::wstring, LabelsSlot> g_labelsCache;
std::atomic<int> g_labelsReadsRunning{0};
void (*g_labelsLoaded)(std::wstring const& folderPath) = nullptr;
constexpr size_t kLabelsCachedFolders = 16;

// Reads one folder on a thread of the mod. Gives up as soon as `cancel` is set or the mod unloads.
void LabelsRead(std::wstring path, std::shared_ptr<std::atomic<bool>> cancel) {
    g_labelsReadsRunning++;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<LabelsItem> items;
    bool finished = false;
    bool supported = LabelsVolumeSupportsStreams(path);
    PIDLIST_ABSOLUTE pidl = nullptr;
    IShellFolder* folder = nullptr;
    if (!supported) {
        finished = true;
    } else if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) && pidl &&
               SUCCEEDED(SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(&folder))) && folder) {
        IEnumIDList* list = nullptr;
        if (folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &list) == S_OK && list) {
            finished = true;
            PITEMID_CHILD child = nullptr;
            int count = 0;
            while (list->Next(1, &child, nullptr) == S_OK && child) {
                if (*cancel || g_unloading) {
                    CoTaskMemFree(child);
                    finished = false;
                    break;
                }
                LabelsItem item;
                item.name = DisplayName(folder, child);
                std::wstring file = ParsingName(folder, child);
                std::vector<LabelTag> tags;
                if (!file.empty() && LabelsReadFile(file, &tags)) {
                    item.tags = std::move(tags);
                }
                items.push_back(std::move(item));
                CoTaskMemFree(child);
                child = nullptr;
                if (++count >= kReadCap) {
                    break;
                }
            }
            list->Release();
        }
        folder->Release();
    }
    if (pidl) {
        ILFree(pidl);
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    bool publish = false;
    if (finished && !*cancel) {
        auto data = std::make_shared<LabelsFolder>(supported ? LabelsCollect(items) : LabelsFolder());
        data->streamsSupported = supported;
        data->complete = true;
        std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
        auto found = g_labelsCache.find(path);
        if (found != g_labelsCache.end() && found->second.cancel == cancel) {
            found->second.data = data;
            found->second.cancel.reset();
            publish = true;
        }
    } else {
        std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
        auto found = g_labelsCache.find(path);
        if (found != g_labelsCache.end() && found->second.cancel == cancel) {
            found->second.cancel.reset();  // Cancelled: the old data stays, and a new read can start.
        }
    }
    g_labelsReadsRunning--;
    if (publish && g_labelsLoaded && !g_unloading) {
        g_labelsLoaded(path);
    }
}

// Starts a read for the slot (the cache lock is held by the caller).
void LabelsStartRead(std::wstring const& path, LabelsSlot& slot) {
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    slot.cancel = cancel;
    if (!StartModThread([path, cancel]() { LabelsRead(path, cancel); })) {
        slot.cancel.reset();
    }
}

}  // namespace

// Tells the cache what to call, on a thread of the mod, when a folder has been read.
void LabelsSetLoadedCallback(void (*callback)(std::wstring const& folderPath)) {
    g_labelsLoaded = callback;
}

// The tags of a folder's items. Never blocks: the first call returns an empty, not complete entry and starts
// the read; the entry is replaced when the read ends.
std::shared_ptr<const LabelsFolder> LabelsFolderFor(std::wstring const& folderPath) {
    std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
    auto found = g_labelsCache.find(folderPath);
    if (found == g_labelsCache.end()) {
        if (g_labelsCache.size() >= kLabelsCachedFolders) {
            auto oldest = g_labelsCache.end();
            for (auto at = g_labelsCache.begin(); at != g_labelsCache.end(); ++at) {
                if (!at->second.cancel && (oldest == g_labelsCache.end() || at->second.used < oldest->second.used)) {
                    oldest = at;
                }
            }
            if (oldest != g_labelsCache.end()) {
                g_labelsCache.erase(oldest);
            }
        }
        found = g_labelsCache.emplace(folderPath, LabelsSlot()).first;
    }
    LabelsSlot& slot = found->second;
    slot.used = GetTickCount64();
    if (!slot.data->complete && !slot.cancel) {
        LabelsStartRead(folderPath, slot);
    }
    return slot.data;
}

// Read again (navigation, the window got focus). The old data stays in use until the new read ends.
void LabelsInvalidate(std::wstring const& folderPath) {
    std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
    auto found = g_labelsCache.find(folderPath);
    if (found == g_labelsCache.end()) {
        return;
    }
    if (found->second.cancel) {
        *found->second.cancel = true;
    }
    LabelsStartRead(folderPath, found->second);
}

// The cache entry of the item's folder gets the item's new tags at once (after a successful write).
void LabelsSetTags(std::wstring const& itemPath, std::vector<LabelTag> const& tags) {
    std::wstring folderPath = itemPath;
    PathRemoveFileSpecW(folderPath.data());
    folderPath.resize(wcslen(folderPath.c_str()));
    PIDLIST_ABSOLUTE pidl = nullptr;
    std::wstring name;
    if (SUCCEEDED(SHParseDisplayName(itemPath.c_str(), nullptr, &pidl, 0, nullptr)) && pidl) {
        IShellFolder* parent = nullptr;
        PCUITEMID_CHILD child = nullptr;
        if (SUCCEEDED(SHBindToParent(pidl, IID_PPV_ARGS(&parent), &child)) && parent) {
            name = DisplayName(parent, child);
            parent->Release();
        }
        ILFree(pidl);
    }
    if (name.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
    auto found = g_labelsCache.find(folderPath);
    if (found == g_labelsCache.end()) {
        return;
    }
    auto copy = std::make_shared<LabelsFolder>(*found->second.data);
    if (tags.empty()) {
        copy->byName.erase(name);
    } else if (!copy->ambiguous.count(name)) {
        copy->byName[name] = tags;
    }
    found->second.data = copy;
}

// Every read that is running gives up. Called first when the mod unloads, before its threads are joined.
void LabelsCancelAll() {
    std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
    for (auto& entry : g_labelsCache) {
        if (entry.second.cancel) {
            *entry.second.cancel = true;
        }
    }
}

int LabelsReadsRunning() {
    return g_labelsReadsRunning;
}

// Empties the cache. Called when the mod unloads, after its threads are joined.
void LabelsClearCache() {
    std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
    g_labelsCache.clear();
}
