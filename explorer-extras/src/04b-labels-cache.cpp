// Labels: the tags of the items of a folder, read once by a thread of the mod and kept in a cache that the
// paint hook only reads (it never touches the disk). The key of an item is its displayed name, the same text
// that the file list gives back as the accessible name of a row. Two items that show the same name (hidden
// extensions) cannot be told apart in the list, so a name like that is "ambiguous" and gets no dot.

struct LabelsFolder {
    std::unordered_map<std::wstring, std::vector<LabelTag>> byName;
    std::unordered_set<std::wstring> ambiguous;  // Names shown by more than one item, at least one of them tagged.
    std::unordered_map<std::wstring, std::wstring> typeNames;  // The type text ("Text file") of each tagged item.
    std::unordered_map<std::wstring, int> counts;  // How many items show each name, tagged or not.
    std::unordered_set<std::wstring> folders;    // The items that are folders (a Tiles or Content row has no detail line then).
    bool streamsSupported = true;                // The volume can carry the stream.
    bool complete = false;                       // The folder was read to its end.
};

// What one item of a folder gave: its displayed name, and its tags (empty when it has none).
struct LabelsItem {
    std::wstring name;
    std::vector<LabelTag> tags;
    bool folder = false;
    std::wstring typeName;  // What Explorer calls the type of the item, for tagged items only.
};

// Builds the cache entry of a folder from what its items gave. Pure.
LabelsFolder LabelsCollect(std::vector<LabelsItem> const& items) {
    LabelsFolder folder;
    std::unordered_map<std::wstring, int> seen;
    std::unordered_set<std::wstring> tagged;
    for (auto const& item : items) {
        seen[item.name]++;
        if (item.folder) {
            folder.folders.insert(item.name);
        }
        if (!item.tags.empty()) {
            tagged.insert(item.name);
            folder.byName[item.name] = item.tags;
            folder.typeNames[item.name] = item.typeName;
        }
    }
    for (auto const& name : tagged) {
        if (seen[name] > 1) {
            folder.ambiguous.insert(name);
            folder.byName.erase(name);
            folder.typeNames.erase(name);
        }
    }
    folder.counts = std::move(seen);
    folder.complete = true;
    return folder;
}

// The entry of a folder after one item's tags changed. An item that shares its displayed name with another one
// (hidden extensions) cannot be told from it in the list: its name is ambiguous, whichever one was tagged. Pure.
LabelsFolder LabelsFolderWithTags(LabelsFolder base, std::wstring const& name, std::vector<LabelTag> const& tags,
                                  std::wstring const& typeName) {
    auto counted = base.counts.find(name);
    if (counted != base.counts.end() && counted->second > 1) {
        if (!tags.empty()) {
            base.ambiguous.insert(name);
        }
        base.byName.erase(name);
        base.typeNames.erase(name);
        return base;
    }
    if (tags.empty()) {
        base.byName.erase(name);
        base.typeNames.erase(name);
    } else {
        base.byName[name] = tags;
        base.typeNames[name] = typeName;
    }
    return base;
}

// A folder that was read is read again after this long, when it is on show (a tag set by another program, a rename).
bool LabelsShouldRefresh(ULONGLONG readAt, ULONGLONG now) {
    return now - readAt >= 20000;
}

// The tag a window filters its list by. A colour-only filter (no name) matches any tag of that colour, a named
// one matches the name, whatever the case and the colour.
struct LabelsFilter {
    bool on = false;
    uint8_t colour = 0;
    std::wstring name;
    std::wstring folder;  // The folder it was set in: showing another folder ends it.
};

bool LabelsMatches(LabelsFilter const& filter, std::vector<LabelTag> const& tags) {
    if (!filter.on) {
        return true;
    }
    for (auto const& tag : tags) {
        if (filter.name.empty() ? tag.colour == filter.colour : _wcsicmp(tag.name.c_str(), filter.name.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

namespace {

struct LabelsSlot {
    std::shared_ptr<const LabelsFolder> data = std::make_shared<LabelsFolder>();
    std::shared_ptr<std::atomic<bool>> cancel;  // Of the read that is running, null when none is.
    ULONGLONG used = 0;
    ULONGLONG readAt = 0;  // When the data was published.
};

std::mutex g_labelsCacheMutex;
[[clang::no_destroy]] std::map<std::wstring, LabelsSlot> g_labelsCache;
std::atomic<int> g_labelsReadsRunning{0};
std::atomic<void (*)(std::wstring const& folderPath)> g_labelsLoaded{nullptr};
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
        // Hidden items are read when Explorer shows them: one that is not shown must not make a visible name ambiguous.
        SHCONTF flags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
        if (ShowHiddenFiles()) {
            flags |= SHCONTF_INCLUDEHIDDEN;
        }
        if (folder->EnumObjects(nullptr, flags, &list) == S_OK && list) {
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
                SFGAOF attributes = SFGAO_FOLDER | SFGAO_STREAM;
                item.folder = SUCCEEDED(folder->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&child, &attributes)) &&
                              (attributes & SFGAO_FOLDER) && !(attributes & SFGAO_STREAM);
                std::wstring file = ParsingName(folder, child);
                std::vector<LabelTag> tags;
                if (!file.empty() && LabelsReadFile(file, &tags)) {
                    item.tags = std::move(tags);
                }
                if (!item.tags.empty()) {
                    SHFILEINFOW info = {};
                    if (SHGetFileInfoW(file.c_str(), 0, &info, sizeof(info), SHGFI_TYPENAME)) {
                        item.typeName = info.szTypeName;
                    }
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
            found->second.readAt = GetTickCount64();
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
    auto loaded = g_labelsLoaded.load();
    if (publish && loaded && !g_unloading) {
        loaded(path);
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

// The displayed name of an item, from its path.
std::wstring LabelsDisplayNameOf(std::wstring const& itemPath) {
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
    return name;
}

// The cache entry of the item's folder gets the item's new tags at once (after a successful write).
void LabelsSetTags(std::wstring const& itemPath, std::vector<LabelTag> const& tags) {
    std::wstring folderPath = itemPath;
    PathRemoveFileSpecW(folderPath.data());
    folderPath.resize(wcslen(folderPath.c_str()));
    std::wstring name = LabelsDisplayNameOf(itemPath);
    if (name.empty()) {
        return;
    }
    std::wstring typeName;
    if (!tags.empty()) {
        SHFILEINFOW info = {};
        if (SHGetFileInfoW(itemPath.c_str(), 0, &info, sizeof(info), SHGFI_TYPENAME)) {
            typeName = info.szTypeName;
        }
    }
    std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
    auto found = g_labelsCache.find(folderPath);
    if (found == g_labelsCache.end() || !found->second.data->complete) {
        return;
    }
    found->second.data = std::make_shared<LabelsFolder>(LabelsFolderWithTags(*found->second.data, name, tags, typeName));
}

// A folder is not on show any more: its read, if it is still running, gives up.
void LabelsCancelFolder(std::wstring const& folderPath) {
    std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
    auto found = g_labelsCache.find(folderPath);
    if (found != g_labelsCache.end() && found->second.cancel) {
        *found->second.cancel = true;
    }
}

// A folder on show that was read a while ago is read again, on a thread, and the old data is used meanwhile.
void LabelsRefreshIfStale(std::wstring const& folderPath) {
    std::lock_guard<std::mutex> lock(g_labelsCacheMutex);
    auto found = g_labelsCache.find(folderPath);
    if (found != g_labelsCache.end() && found->second.data->complete && !found->second.cancel &&
        LabelsShouldRefresh(found->second.readAt, GetTickCount64())) {
        LabelsStartRead(folderPath, found->second);
    }
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
