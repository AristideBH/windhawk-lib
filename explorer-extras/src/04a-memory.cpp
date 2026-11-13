
// ---- What is remembered per folder ----
//
// Sort and group, the width of a column and whether the folder is shown in
// Columns. Kept across restarts in one value of the mod's storage, as lines of
// "path TAB sort TAB width TAB columns". The most recent 150 folders are kept.

struct FolderMemory {
    bool hasSpec = false;
    SortSpec spec;
    double width = 0;
    int columns = -1;  // -1 not known, 0 not shown in Columns, 1 shown in Columns.
};

std::mutex g_memoryMutex;
[[clang::no_destroy]] std::map<std::wstring, FolderMemory> g_memory;
[[clang::no_destroy]] std::vector<std::wstring> g_memoryOrder;  // Oldest first.
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

void SaveFolderMemoryLocked() {
    std::wstring text;
    for (auto& key : g_memoryOrder) {
        auto found = g_memory.find(key);
        if (found == g_memory.end()) {
            continue;
        }
        auto& memory = found->second;
        text += key + L"\t" + (memory.hasSpec ? SpecToText(memory.spec) : L"") + L"\t" +
                std::to_wstring((int)memory.width) + L"\t" + std::to_wstring(memory.columns) + L"\n";
    }
    Wh_SetStringValue(L"folderMemory", text.c_str());
}

void LoadFolderMemoryLocked() {
    if (g_memoryLoaded) {
        return;
    }
    g_memoryLoaded = true;
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
        std::wstring line = text.substr(start, end - start);
        start = end + 1;
        size_t a = line.find(L'\t');
        size_t b = a == std::wstring::npos ? a : line.find(L'\t', a + 1);
        size_t c = b == std::wstring::npos ? b : line.find(L'\t', b + 1);
        if (c == std::wstring::npos) {
            continue;
        }
        std::wstring key = line.substr(0, a);
        FolderMemory memory;
        std::wstring spec = line.substr(a + 1, b - a - 1);
        if (!spec.empty() && SpecFromText(spec, &memory.spec)) {
            memory.hasSpec = true;
        }
        memory.width = wcstod(line.substr(b + 1, c - b - 1).c_str(), nullptr);
        memory.columns = (int)wcstol(line.substr(c + 1).c_str(), nullptr, 10);
        g_memory[key] = memory;
        g_memoryOrder.push_back(key);
    }
}

FolderMemory GetFolderMemory(std::wstring const& path) {
    std::lock_guard<std::mutex> lock(g_memoryMutex);
    LoadFolderMemoryLocked();
    auto found = g_memory.find(MemoryKey(path));
    FolderMemory memory = found == g_memory.end() ? FolderMemory() : found->second;
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
    g_memory[key] = memory;
    g_memoryOrder.erase(std::remove(g_memoryOrder.begin(), g_memoryOrder.end(), key),
                        g_memoryOrder.end());
    g_memoryOrder.push_back(key);
    while (g_memoryOrder.size() > kMemoryLimit) {
        g_memory.erase(g_memoryOrder.front());
        g_memoryOrder.erase(g_memoryOrder.begin());
    }
    SaveFolderMemoryLocked();
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
