
// ---- What is remembered per folder ----
//
// Sort and group, the width of a column and whether the folder is shown in
// Columns. Kept across restarts in one value of the mod's storage, as lines of
// "path TAB sort TAB width TAB columns". 150 folders are kept; when a new one comes in, the one used longest ago goes
// (a folder is used when it is opened, not only when something about it changes).

struct FolderMemory {
    bool hasSpec = false;
    SortSpec spec;
    double width = 0;
    int columns = -1;  // -1 not known, 0 not shown in Columns, 1 shown in Columns.
};

std::mutex g_memoryMutex;
std::map<std::wstring, FolderMemory> g_memory;
std::map<std::wstring, size_t> g_memorySlot;       // Which numbered value holds each folder.
std::map<std::wstring, unsigned long long> g_memoryUsed;  // When each folder was last used (this run; loaded ones by their slot).
unsigned long long g_memoryClock = 0;
bool g_memoryLoaded = false;
constexpr size_t kMemoryLimit = 150;

std::wstring MemoryKey(std::wstring const& path) {
    std::wstring key = path;
    for (auto& c : key) {
        c = (wchar_t)towlower(c);
    }
    return key;
}

std::wstring GuidText(GUID const& guid) {
    wchar_t text[64] = L"";
    StringFromGUID2(guid, text, ARRAYSIZE(text));
    return text;
}

std::wstring SpecToText(SortSpec const& spec) {
    std::wstring text;
    for (auto& column : spec.columns) {
        text += GuidText(column.propkey.fmtid) + L"," + std::to_wstring(column.propkey.pid) + L"," +
                std::to_wstring((int)column.direction) + L";";
    }
    text += L"/";
    if (spec.grouped) {
        text += GuidText(spec.groupKey.fmtid) + L"," + std::to_wstring(spec.groupKey.pid) + L"," +
                (spec.groupAscending ? L"1" : L"0");
    }
    return text;
}

// One "{guid},pid,number" field.
bool ParseKeyField(std::wstring const& field, PROPERTYKEY* key, int* number) {
    size_t first = field.find(L',');
    size_t second = first == std::wstring::npos ? first : field.find(L',', first + 1);
    if (second == std::wstring::npos) {
        return false;
    }
    GUID guid;
    if (FAILED(CLSIDFromString(field.substr(0, first).c_str(), &guid))) {
        return false;
    }
    key->fmtid = guid;
    key->pid = (DWORD)wcstoul(field.substr(first + 1, second - first - 1).c_str(), nullptr, 10);
    *number = (int)wcstol(field.substr(second + 1).c_str(), nullptr, 10);
    return true;
}

bool SpecFromText(std::wstring const& text, SortSpec* spec) {
    size_t slash = text.find(L'/');
    if (slash == std::wstring::npos) {
        return false;
    }
    SortSpec result;
    size_t start = 0;
    std::wstring columns = text.substr(0, slash);
    while (start < columns.size()) {
        size_t end = columns.find(L';', start);
        if (end == std::wstring::npos) {
            end = columns.size();
        }
        PROPERTYKEY key{};
        int direction = 0;
        if (ParseKeyField(columns.substr(start, end - start), &key, &direction)) {
            SORTCOLUMN sort{};
            sort.propkey = key;
            sort.direction = (SORTDIRECTION)direction;
            result.columns.push_back(sort);
        }
        start = end + 1;
    }
    std::wstring group = text.substr(slash + 1);
    if (!group.empty()) {
        PROPERTYKEY key{};
        int ascending = 1;
        if (ParseKeyField(group, &key, &ascending)) {
            result.grouped = true;
            result.groupKey = key;
            result.groupAscending = ascending != 0;
        }
    }
    *spec = result;
    return true;
}

// One value per folder ("folderMemory.0", "folderMemory.1", ...) and a count: a single long value
// would be cut at 32,767 characters, and Windhawk's portable mode loses line breaks.
std::vector<std::wstring> g_memoryWritten;  // What each numbered value holds now.

std::wstring MemoryLine(std::wstring const& key, FolderMemory const& memory) {
    return key + L"|" + (memory.hasSpec ? SpecToText(memory.spec) : L"") + L"|" +
           std::to_wstring((int)memory.width) + L"|" + std::to_wstring(memory.columns);
}

bool ParseMemoryLine(std::wstring const& line, wchar_t separator, std::wstring* key, FolderMemory* memory) {
    size_t a = line.find(separator);
    size_t b = a == std::wstring::npos ? a : line.find(separator, a + 1);
    size_t c = b == std::wstring::npos ? b : line.find(separator, b + 1);
    if (c == std::wstring::npos) {
        return false;
    }
    *key = line.substr(0, a);
    *memory = FolderMemory();
    std::wstring spec = line.substr(a + 1, b - a - 1);
    if (!spec.empty() && SpecFromText(spec, &memory->spec)) {
        memory->hasSpec = true;
    }
    memory->width = wcstod(line.substr(b + 1, c - b - 1).c_str(), nullptr);
    memory->columns = (int)wcstol(line.substr(c + 1).c_str(), nullptr, 10);
    return !key->empty();
}

std::wstring MemoryValueName(size_t index) {
    return L"folderMemory." + std::to_wstring(index);
}

// Writes the value of one slot (when it changed).
void WriteMemorySlotLocked(size_t slot, std::wstring const& line) {
    if (slot >= g_memoryWritten.size()) {
        g_memoryWritten.resize(slot + 1);
    }
    if (g_memoryWritten[slot] != line) {
        Wh_SetStringValue(MemoryValueName(slot).c_str(), line.c_str());
        g_memoryWritten[slot] = line;
    }
    Wh_SetIntValue(L"folderMemory.count", (int)g_memoryWritten.size());
}

// Every slot, after the first load of the single value.
void SaveFolderMemoryLocked() {
    for (auto const& entry : g_memorySlot) {
        auto found = g_memory.find(entry.first);
        if (found != g_memory.end()) {
            WriteMemorySlotLocked(entry.second, MemoryLine(entry.first, found->second));
        }
    }
    Wh_SetIntValue(L"folderMemory.count", (int)g_memoryWritten.size());
}

void LoadFolderMemoryLocked() {
    if (g_memoryLoaded) {
        return;
    }
    g_memoryLoaded = true;
    int count = Wh_GetIntValue(L"folderMemory.count", -1);
    if (count < 0) {
        // The single value of the first builds: read once, and written as numbered values.
        std::vector<wchar_t> buffer(120000);
        size_t length = Wh_GetStringValue(L"folderMemory", buffer.data(), buffer.size());
        if (!length) {
            return;
        }
        std::wstring text(buffer.data(), length);
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find(L'\n', start);
            if (end == std::wstring::npos) {
                end = text.size();
            }
            std::wstring key;
            FolderMemory memory;
            if (ParseMemoryLine(text.substr(start, end - start), L'\t', &key, &memory) && g_memorySlot.size() < kMemoryLimit) {
                g_memory[key] = memory;
                size_t slot = g_memorySlot.size();
                g_memorySlot[key] = slot;
                g_memoryUsed[key] = ++g_memoryClock;
            }
            start = end + 1;
        }
        SaveFolderMemoryLocked();
        Wh_DeleteValue(L"folderMemory");
        return;
    }
    std::vector<wchar_t> buffer(4096);
    for (int i = 0; i < count && i < (int)kMemoryLimit; i++) {
        size_t length = Wh_GetStringValue(MemoryValueName(i).c_str(), buffer.data(), buffer.size());
        std::wstring line(buffer.data(), length);
        std::wstring key;
        FolderMemory memory;
        if (length && ParseMemoryLine(line, L'|', &key, &memory)) {
            g_memory[key] = memory;
            g_memorySlot[key] = (size_t)i;
            g_memoryUsed[key] = ++g_memoryClock;
            g_memoryWritten.push_back(line);
        } else {
            g_memoryWritten.push_back(L"");
        }
    }
}

FolderMemory GetFolderMemory(std::wstring const& path) {
    std::lock_guard<std::mutex> lock(g_memoryMutex);
    LoadFolderMemoryLocked();
    std::wstring usedKey = MemoryKey(path);
    auto found = g_memory.find(usedKey);
    FolderMemory memory = found == g_memory.end() ? FolderMemory() : found->second;
    if (found != g_memory.end()) {
        g_memoryUsed[usedKey] = ++g_memoryClock;  // Opened: it stays. Nothing is written for that.
    }
    // What the settings turn off is not given out.
    if (!g_rememberSort) {
        memory.hasSpec = false;
    }
    if (!g_rememberWidth) {
        memory.width = 0;
    }
    if (!g_rememberLayout) {
        memory.columns = -1;
    }
    return memory;
}

// Updates one folder's entry and writes the store when something changed.
template <typename Change>
void ChangeFolderMemory(std::wstring const& path, Change change) {
    if (path.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_memoryMutex);
    LoadFolderMemoryLocked();
    std::wstring key = MemoryKey(path);
    FolderMemory memory;
    auto found = g_memory.find(key);
    if (found != g_memory.end()) {
        memory = found->second;
    }
    FolderMemory before = memory;
    change(memory);
    bool same = before.hasSpec == memory.hasSpec && before.width == memory.width &&
                before.columns == memory.columns &&
                (!memory.hasSpec || SameSpec(before.spec, memory.spec));
    if (same && found != g_memory.end()) {
        return;
    }
    size_t slot;
    auto held = g_memorySlot.find(key);
    if (held != g_memorySlot.end()) {
        slot = held->second;
    } else if (g_memorySlot.size() < kMemoryLimit) {
        // The slots are numbered from 0: the first free one is taken.
        std::vector<char> taken(g_memorySlot.size() + 1, 0);
        for (auto const& entry : g_memorySlot) {
            if (entry.second < taken.size()) {
                taken[entry.second] = 1;
            }
        }
        slot = 0;
        while (slot < taken.size() && taken[slot]) {
            slot++;
        }
    } else {
        // Full: the folder used longest ago gives up its slot, the only value written.
        auto oldest = g_memoryUsed.end();
        for (auto at = g_memoryUsed.begin(); at != g_memoryUsed.end(); ++at) {
            if (oldest == g_memoryUsed.end() || at->second < oldest->second) {
                oldest = at;
            }
        }
        slot = g_memorySlot[oldest->first];
        g_memorySlot.erase(oldest->first);
        g_memory.erase(oldest->first);
        g_memoryUsed.erase(oldest);
    }
    g_memory[key] = memory;
    g_memorySlot[key] = slot;
    g_memoryUsed[key] = ++g_memoryClock;
    WriteMemorySlotLocked(slot, MemoryLine(key, memory));
}

void RememberFolderSpec(std::wstring const& path, SortSpec const& spec) {
    if (!g_rememberSort) {
        return;
    }
    ChangeFolderMemory(path, [&](FolderMemory& memory) {
        memory.hasSpec = true;
        memory.spec = spec;
    });
}

void RememberFolderWidth(std::wstring const& path, double width) {
    if (!g_rememberWidth) {
        return;
    }
    ChangeFolderMemory(path, [&](FolderMemory& memory) { memory.width = width; });
}

void RememberFolderColumns(std::wstring const& path, int columns) {
    if (!g_rememberLayout) {
        return;
    }
    ChangeFolderMemory(path, [&](FolderMemory& memory) { memory.columns = columns; });
}

int RememberedColumns(std::wstring const& path) {
    return GetFolderMemory(path).columns;
}
