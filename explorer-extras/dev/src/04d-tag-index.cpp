// Sidebar: the index of tagged files. It answers "which files carry this tag" across the fixed local drives, which the
// streams themselves cannot (03a-labels-store.cpp keeps the tags on the files, one stream each).
//
// What it holds: for every file or folder that has the stream, its path and its tags (the same LabelTag list the stream
// holds). Everything else (sizes, dates, whether the file still exists) is asked of the file system when a list is made.
//
// How it stays current. One Explorer process owns the index (it holds a lock file); it walks the fixed drives once in the
// background (low I/O priority), keeps a directory watcher on each drive root (Windows reports a write to a named stream as
// "path:ExplorerExtras.Labels"), and saves the index a few seconds after a change. The other Explorer processes read the
// saved file when it is newer than what they hold, and apply the writes they make themselves.
//
// The data functions take the data they act on, so the self-test can run them on a folder and data of its own. The
// functions of the second half (TagIndexStart and the rest) act on the live index.

struct IndexedFile {
    std::wstring path;
    ULONGLONG size = 0;
    FILETIME modified{};
    bool isFolder = false;
};

// A change to a tag on every file that carries it. An empty newName removes the tag; a newColour of -1 keeps the colour.
struct TagRewrite {
    std::wstring oldName;
    std::wstring newName;
    int newColour = -1;
};

struct TagRewriteResult {
    size_t total = 0;
    size_t changed = 0;
    size_t failed = 0;
    bool cancelled = false;
};

struct TagIndexData {
    struct Entry {
        std::wstring path;
        std::vector<LabelTag> tags;
    };
    std::unordered_map<std::wstring, Entry> files;  // by the lower-case path
};

// A rename arrives as two notifications, the old name and the new one.
struct TagIndexNotifyState {
    std::wstring oldPath;
};

std::wstring TagIndexKey(std::wstring path) {
    if (!path.empty()) {
        CharLowerBuffW(path.data(), (DWORD)path.size());
    }
    return path;
}

// The name a tag goes by: its own, or the name the user gave its colour in the settings.
std::wstring TagShownName(LabelTag const& tag) {
    return tag.name.empty() ? LabelsColourName(tag.colour) : tag.name;
}

namespace {

// The form the file system accepts for a path of any length.
std::wstring TagIndexLong(std::wstring const& path) {
    if (path.size() >= 3 && path[1] == L':' && path.rfind(L"\\\\?\\", 0) != 0) {
        return L"\\\\?\\" + path;
    }
    return path;
}

bool TagIndexSameName(std::wstring const& a, std::wstring const& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

bool TagIndexHasTag(std::vector<LabelTag> const& tags, std::wstring const& name) {
    for (auto const& tag : tags) {
        std::wstring shown = TagShownName(tag);
        if (!shown.empty() && TagIndexSameName(shown, name)) {
            return true;
        }
    }
    return false;
}

bool TagIndexStartsWith(std::wstring const& text, std::wstring const& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

const wchar_t* const kTagIndexExcluded[] = {L"windows", L"program files", L"program files (x86)", L"programdata", L"$recycle.bin",
                                           L"system volume information"};

}  // namespace

// True for a path (relative to a drive root) in a folder nobody tags files in, whatever its case.
bool TagIndexExcludedBelowRoot(std::wstring const& relative) {
    size_t cut = relative.find(L'\\');
    std::wstring first = TagIndexKey(relative.substr(0, cut));
    for (auto const* name : kTagIndexExcluded) {
        if (first == name) {
            return true;
        }
    }
    return false;
}

void TagIndexSet(TagIndexData& data, std::wstring const& path, std::vector<LabelTag> const& tags) {
    if (tags.empty()) {
        data.files.erase(TagIndexKey(path));
        return;
    }
    TagIndexData::Entry entry;
    entry.path = path;
    entry.tags = tags;
    data.files[TagIndexKey(path)] = std::move(entry);
}

size_t TagIndexCountIn(TagIndexData const& data, std::wstring const& tag) {
    size_t count = 0;
    for (auto const& item : data.files) {
        if (TagIndexHasTag(item.second.tags, tag)) {
            count++;
        }
    }
    return count;
}

std::vector<std::wstring> TagIndexPathsWith(TagIndexData const& data, std::wstring const& tag) {
    std::vector<std::wstring> paths;
    for (auto const& item : data.files) {
        if (TagIndexHasTag(item.second.tags, tag)) {
            paths.push_back(item.second.path);
        }
    }
    return paths;
}

// The files that carry a tag, with their size, date and kind. An item that is gone is dropped from the data.
std::vector<IndexedFile> TagIndexFilesIn(TagIndexData& data, std::wstring const& tag, bool* pruned = nullptr) {
    std::vector<IndexedFile> files;
    for (auto const& path : TagIndexPathsWith(data, tag)) {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExW(TagIndexLong(path).c_str(), GetFileExInfoStandard, &info)) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                data.files.erase(TagIndexKey(path));
                if (pruned) {
                    *pruned = true;
                }
            }
            continue;
        }
        IndexedFile file;
        file.path = path;
        file.isFolder = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        file.size = ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        file.modified = info.ftLastWriteTime;
        files.push_back(std::move(file));
    }
    return files;
}

// The tags of every file in a tree. The walk skips reparse points, files that live in the cloud and, under a drive root,
// the system folders. False when it was asked to stop. `seen` counts what was visited.
bool TagIndexScanRoot(TagIndexData& data, std::wstring root, bool skipSystem, bool ensureTags, std::atomic<bool> const& cancel,
                      std::atomic<size_t>* seen) {
    while (root.size() > 3 && root.back() == L'\\') {
        root.pop_back();
    }
    size_t rootLength = root.size();
    std::vector<std::wstring> stack{root};
    while (!stack.empty()) {
        if (cancel || g_unloading) {
            return false;
        }
        std::wstring dir = std::move(stack.back());
        stack.pop_back();
        std::wstring pattern = TagIndexLong(dir);
        if (pattern.back() != L'\\') {
            pattern += L'\\';
        }
        pattern += L'*';
        WIN32_FIND_DATAW found;
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) {
                continue;
            }
            if (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                continue;
            }
            bool isDir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            std::wstring full = dir;
            if (full.back() != L'\\') {
                full += L'\\';
            }
            full += found.cFileName;
            if (isDir && skipSystem && dir.size() == rootLength && TagIndexExcludedBelowRoot(found.cFileName)) {
                continue;
            }
            // A file that is only in the cloud: opening it would download it.
            if (!isDir && (found.dwFileAttributes & (FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))) {
                continue;
            }
            if (seen) {
                (*seen)++;
            }
            if (isDir) {
                stack.push_back(full);
            }
            HANDLE probe = CreateFileW((TagIndexLong(full) + kLabelsStream).c_str(), FILE_READ_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (probe == INVALID_HANDLE_VALUE) {
                continue;
            }
            CloseHandle(probe);
            std::vector<LabelTag> tags;
            if (LabelsReadFileEx(TagIndexLong(full), &tags) == LabelsReadResult::Ok && !tags.empty()) {
                if (ensureTags) {
                    for (auto const& tag : tags) {
                        std::wstring shown = TagShownName(tag);
                        if (!shown.empty()) {
                            TagListEnsure(shown, tag.colour);
                        }
                    }
                }
                TagIndexSet(data, full, tags);
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    return true;
}

// The entries of a drive are replaced by what a walk of it found.
void TagIndexReplaceRoot(TagIndexData& data, std::wstring const& root, TagIndexData&& found) {
    std::wstring prefix = TagIndexKey(root);
    for (auto it = data.files.begin(); it != data.files.end();) {
        if (TagIndexStartsWith(it->first, prefix)) {
            it = data.files.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& item : found.files) {
        data.files[item.first] = std::move(item.second);
    }
}

// The change on the files: read each one again, change the tags that match, write it back (the times are kept, a file left
// with no tag loses its stream). Stops when `cancel` is set. `onUpdated` hears of every file that was read (the tags it has now).
TagRewriteResult TagIndexRewriteFiles(std::vector<std::wstring> const& paths, TagRewrite const& job,
                                      std::function<void(size_t, size_t)> const& progress, std::atomic<bool> const& cancel,
                                      std::function<void(std::wstring const&, std::vector<LabelTag> const&)> const& onUpdated) {
    TagRewriteResult result;
    result.total = paths.size();
    for (size_t i = 0; i < paths.size(); i++) {
        if (cancel || g_unloading) {
            result.cancelled = true;
            break;
        }
        std::wstring const& path = paths[i];
        std::vector<LabelTag> tags;
        if (LabelsReadFileEx(TagIndexLong(path), &tags) != LabelsReadResult::Ok) {
            result.failed++;
            if (progress) {
                progress(i + 1, paths.size());
            }
            continue;
        }
        std::vector<LabelTag> out;
        bool touched = false;
        for (auto const& tag : tags) {
            std::wstring shown = TagShownName(tag);
            if (shown.empty() || !TagIndexSameName(shown, job.oldName)) {
                out.push_back(tag);
                continue;
            }
            touched = true;
            if (job.newName.empty()) {
                continue;
            }
            LabelTag changed = tag;
            changed.name = job.newName;
            if (job.newColour >= 0) {
                changed.colour = (uint8_t)std::clamp(job.newColour, 0, 7);
            }
            if (!TagIndexHasTag(out, job.newName)) {
                out.push_back(changed);
            }
        }
        if (touched) {
            // A tag renamed onto a name the file already has (before or after it) leaves one tag, the first.
            std::vector<LabelTag> unique;
            for (auto const& tag : out) {
                std::wstring shown = TagShownName(tag);
                if (shown.empty() || !TagIndexHasTag(unique, shown)) {
                    unique.push_back(tag);
                }
            }
            out.swap(unique);
        }
        if (!touched) {
            if (onUpdated) {
                onUpdated(path, tags);
            }
        } else if (LabelsWriteFile(TagIndexLong(path), out) == LabelsWrite::Ok) {
            result.changed++;
            if (onUpdated) {
                onUpdated(path, out);
            }
        } else {
            result.failed++;
        }
        if (progress) {
            progress(i + 1, paths.size());
        }
    }
    return result;
}

// One notification of a directory watcher, with its full path. Returns the path of the file whose stream has to be read again
// (a write to our stream), or an empty string. A removed item leaves the data, a renamed one (a folder with what is under
// it) moves in it.
std::wstring TagIndexHandleNotification(TagIndexData& data, std::wstring const& fullPath, int action, TagIndexNotifyState& state) {
    size_t colon = fullPath.rfind(L':');
    if (colon != std::wstring::npos && colon > 1) {
        if (TagIndexSameName(fullPath.substr(colon), kLabelsStream)) {
            return fullPath.substr(0, colon);
        }
        return L"";
    }
    if (action == FILE_ACTION_REMOVED) {
        std::wstring key = TagIndexKey(fullPath);
        std::wstring below = key + L"\\";
        for (auto it = data.files.begin(); it != data.files.end();) {
            if (it->first == key || TagIndexStartsWith(it->first, below)) {
                it = data.files.erase(it);
            } else {
                ++it;
            }
        }
    } else if (action == FILE_ACTION_RENAMED_OLD_NAME) {
        state.oldPath = fullPath;
    } else if (action == FILE_ACTION_RENAMED_NEW_NAME && !state.oldPath.empty()) {
        std::wstring oldKey = TagIndexKey(state.oldPath);
        std::wstring below = oldKey + L"\\";
        std::vector<TagIndexData::Entry> moved;
        for (auto it = data.files.begin(); it != data.files.end();) {
            if (it->first == oldKey || TagIndexStartsWith(it->first, below)) {
                TagIndexData::Entry entry = std::move(it->second);
                entry.path = fullPath + entry.path.substr(state.oldPath.size());
                moved.push_back(std::move(entry));
                it = data.files.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& entry : moved) {
            data.files[TagIndexKey(entry.path)] = std::move(entry);
        }
        state.oldPath.clear();
    }
    return L"";
}

// The saved text: "v1", an "S" line with the time of the last full walk, then for each item an "F" line with its path and a "T"
// line for each tag (colour digit, name), the path and the name written with the escapes of the label streams.
std::wstring TagIndexSerialize(TagIndexData const& data, ULONGLONG scanDone) {
    std::wstring text = L"v1\nS\t" + std::to_wstring(scanDone) + L"\n";
    for (auto const& item : data.files) {
        text += L"F\t" + LabelsEscape(item.second.path) + L"\n";
        for (auto const& tag : item.second.tags) {
            text += L"T\t";
            text += static_cast<wchar_t>(L'0' + std::min<int>(tag.colour, 7));
            text += L"\t" + LabelsEscape(tag.name) + L"\n";
        }
    }
    return text;
}

bool TagIndexParse(std::wstring const& text, TagIndexData* out, ULONGLONG* scanDone) {
    out->files.clear();
    *scanDone = 0;
    size_t lineEnd = text.find(L'\n');
    if (text.substr(0, lineEnd) != L"v1") {
        return false;
    }
    std::wstring currentPath;
    std::vector<LabelTag> currentTags;
    auto flush = [&]() {
        if (!currentPath.empty()) {
            TagIndexSet(*out, currentPath, currentTags);
        }
        currentPath.clear();
        currentTags.clear();
    };
    size_t start = lineEnd == std::wstring::npos ? text.size() : lineEnd + 1;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring line = text.substr(start, end - start);
        start = end + 1;
        if (line.size() < 3 || line[1] != L'\t') {
            if (line.empty()) {
                continue;
            }
            out->files.clear();
            return false;
        }
        if (line[0] == L'S') {
            *scanDone = wcstoull(line.c_str() + 2, nullptr, 10);
        } else if (line[0] == L'F') {
            flush();
            if (!LabelsUnescape(std::wstring_view(line).substr(2), &currentPath) || currentPath.empty()) {
                out->files.clear();
                return false;
            }
        } else if (line[0] == L'T' && !currentPath.empty() && line.size() >= 4 && line[2] >= L'0' && line[2] <= L'7' &&
                   line[3] == L'\t') {
            LabelTag tag;
            tag.colour = (uint8_t)(line[2] - L'0');
            if (!LabelsUnescape(std::wstring_view(line).substr(4), &tag.name)) {
                out->files.clear();
                return false;
            }
            currentTags.push_back(std::move(tag));
        } else {
            out->files.clear();
            return false;
        }
    }
    flush();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The live index.

// The stream filters of ReadDirectoryChangesW, which this toolchain's headers may not define.
#ifndef FILE_NOTIFY_CHANGE_STREAM_NAME
#define FILE_NOTIFY_CHANGE_STREAM_NAME 0x00000200
#endif
#ifndef FILE_NOTIFY_CHANGE_STREAM_SIZE
#define FILE_NOTIFY_CHANGE_STREAM_SIZE 0x00000400
#endif
#ifndef FILE_NOTIFY_CHANGE_STREAM_WRITE
#define FILE_NOTIFY_CHANGE_STREAM_WRITE 0x00000800
#endif

namespace {

std::mutex g_tagIndexMutex;
TagIndexData g_tagIndex;
ULONGLONG g_tagIndexScanDone = 0;     // The time of the last full walk, as a FILETIME number.
ULONGLONG g_tagIndexLoadedStamp = 0;  // The last-write time of the saved file that was last read.
bool g_tagIndexDirty = false;
ULONGLONG g_tagIndexDirtyAt = 0;      // GetTickCount64 of the last change.

std::mutex g_tagIndexRunMutex;        // Start and stop.
bool g_tagIndexStarted = false;
std::atomic<bool> g_tagIndexStop{false};
std::atomic<bool> g_tagIndexRescan{false};
std::atomic<bool> g_tagIndexOwner{false};
std::atomic<bool> g_tagIndexReady{false};
std::atomic<size_t> g_tagIndexSeen{0};
HANDLE g_tagIndexStopEvent = nullptr;
HANDLE g_tagIndexLock = INVALID_HANDLE_VALUE;
std::vector<std::thread> g_tagIndexThreads;

ULONGLONG TagIndexNow() {
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    return ((ULONGLONG)time.dwHighDateTime << 32) | time.dwLowDateTime;
}

std::wstring TagIndexFolder() {
    wchar_t buffer[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        return L"";
    }
    std::wstring dir = std::wstring(buffer) + L"\\ExplorerExtras";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

bool TagIndexReadFile(std::wstring const& path, std::wstring* text, ULONGLONG* stamp) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    FILETIME written{};
    GetFileTime(file, nullptr, nullptr, &written);
    *stamp = ((ULONGLONG)written.dwHighDateTime << 32) | written.dwLowDateTime;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::string raw((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    bool ok = size.QuadPart == 0 || (size.QuadPart < (256LL << 20) && ReadFile(file, raw.data(), (DWORD)raw.size(), &read, nullptr) && read == raw.size());
    CloseHandle(file);
    if (!ok) {
        return false;
    }
    int wide = raw.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), nullptr, 0);
    text->assign((size_t)wide, L'\0');
    if (wide) {
        MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), text->data(), wide);
    }
    return true;
}

// Reads the saved index when it is newer than what is held. The caller holds the mutex.
void TagIndexLoadLocked(bool force) {
    std::wstring folder = TagIndexFolder();
    if (folder.empty()) {
        return;
    }
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW((folder + L"\\tagindex.txt").c_str(), GetFileExInfoStandard, &info)) {
        return;
    }
    ULONGLONG stamp = ((ULONGLONG)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
    if (!force && stamp <= g_tagIndexLoadedStamp) {
        return;
    }
    std::wstring text;
    ULONGLONG readStamp = 0;
    TagIndexData data;
    ULONGLONG scanDone = 0;
    if (TagIndexReadFile(folder + L"\\tagindex.txt", &text, &readStamp) && TagIndexParse(text, &data, &scanDone)) {
        g_tagIndex = std::move(data);
        g_tagIndexScanDone = scanDone;
        g_tagIndexLoadedStamp = readStamp;
    }
}

void TagIndexSaveLocked() {
    std::wstring folder = TagIndexFolder();
    if (folder.empty()) {
        return;
    }
    std::wstring text = TagIndexSerialize(g_tagIndex, g_tagIndexScanDone);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8((size_t)bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), utf8.data(), bytes, nullptr, nullptr);
    std::wstring target = folder + L"\\tagindex.txt";
    std::wstring temp = target + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr) && written == utf8.size();
    CloseHandle(file);
    if (ok && MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        g_tagIndexDirty = false;
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &info)) {
            g_tagIndexLoadedStamp = ((ULONGLONG)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
        }
    } else {
        DeleteFileW(temp.c_str());
    }
}

void TagIndexMarkDirtyLocked() {
    g_tagIndexDirty = true;
    g_tagIndexDirtyAt = GetTickCount64();
}

std::vector<std::wstring> TagIndexFixedRoots() {
    std::vector<std::wstring> roots;
    wchar_t drives[512];
    DWORD length = GetLogicalDriveStringsW(ARRAYSIZE(drives), drives);
    for (wchar_t* drive = drives; length && *drive; drive += wcslen(drive) + 1) {
        if (GetDriveTypeW(drive) == DRIVE_FIXED && LabelsVolumeSupportsStreams(drive)) {
            roots.push_back(drive);
        }
    }
    return roots;
}

// A stream write seen by a watcher: read the file again and bring the index in line.
void TagIndexReread(std::wstring const& path) {
    std::vector<LabelTag> tags;
    LabelsReadResult result = LabelsReadFileEx(TagIndexLong(path), &tags);
    if (result == LabelsReadResult::Ok || result == LabelsReadResult::Failed) {
        for (auto const& tag : tags) {
            std::wstring shown = TagShownName(tag);
            if (!shown.empty()) {
                TagListEnsure(shown, tag.colour);
            }
        }
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        TagIndexSet(g_tagIndex, path, result == LabelsReadResult::Ok ? tags : std::vector<LabelTag>());
        TagIndexMarkDirtyLocked();
#ifdef EXTRAS_VERBOSE
        Wh_Log(L"TAGINDEX watcher read %s: %d tags", path.c_str(), (int)tags.size());
#endif
    }
}

void TagIndexWatchRoot(std::wstring root) {
    HANDLE dir = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dir == INVALID_HANDLE_VALUE) {
        return;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::vector<BYTE> buffer(64 * 1024);
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_STREAM_NAME |
                         FILE_NOTIFY_CHANGE_STREAM_SIZE | FILE_NOTIFY_CHANGE_STREAM_WRITE;
    TagIndexNotifyState state;
    while (!g_tagIndexStop && !g_unloading) {
        ResetEvent(overlapped.hEvent);
        if (!ReadDirectoryChangesW(dir, buffer.data(), (DWORD)buffer.size(), TRUE, filter, nullptr, &overlapped, nullptr)) {
            break;
        }
        HANDLE waits[2] = {overlapped.hEvent, g_tagIndexStopEvent};
        DWORD waited = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        DWORD bytes = 0;
        if (waited != WAIT_OBJECT_0) {
            CancelIoEx(dir, &overlapped);
            GetOverlappedResult(dir, &overlapped, &bytes, TRUE);
            break;
        }
        if (!GetOverlappedResult(dir, &overlapped, &bytes, FALSE) || bytes == 0) {
            g_tagIndexRescan = true;  // The notifications overflowed: walk the drive again.
            continue;
        }
        std::vector<std::wstring> reads;
        for (DWORD offset = 0;;) {
            auto* note = (FILE_NOTIFY_INFORMATION*)(buffer.data() + offset);
            std::wstring relative(note->FileName, note->FileNameLength / sizeof(wchar_t));
            std::wstring colonless = relative.substr(0, relative.rfind(L':') == std::wstring::npos ? relative.size() : relative.rfind(L':'));
            if (!TagIndexExcludedBelowRoot(colonless)) {
                std::wstring full = root + relative;
                std::wstring read;
                {
                    std::lock_guard<std::mutex> lock(g_tagIndexMutex);
                    std::wstring before = state.oldPath;
                    read = TagIndexHandleNotification(g_tagIndex, full, (int)note->Action, state);
                    if (note->Action == FILE_ACTION_REMOVED || note->Action == FILE_ACTION_RENAMED_NEW_NAME) {
                        TagIndexMarkDirtyLocked();
                    }
                }
                if (!read.empty() && std::find(reads.begin(), reads.end(), read) == reads.end()) {
                    reads.push_back(read);
                }
            }
            if (!note->NextEntryOffset) {
                break;
            }
            offset += note->NextEntryOffset;
        }
        for (auto const& path : reads) {
            TagIndexReread(path);
        }
    }
    CloseHandle(overlapped.hEvent);
    CloseHandle(dir);
}

void TagIndexScanAll() {
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    g_tagIndexSeen = 0;
    std::atomic<bool> noCancel{false};
    for (auto const& root : TagIndexFixedRoots()) {
        TagIndexData found;
        std::atomic<bool>& stop = g_tagIndexStop;
        if (!TagIndexScanRoot(found, root, true, true, stop, &g_tagIndexSeen)) {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
            return;
        }
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        TagIndexReplaceRoot(g_tagIndex, root, std::move(found));
        TagIndexMarkDirtyLocked();
    }
    {
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        g_tagIndexScanDone = TagIndexNow();
        TagIndexSaveLocked();
        Wh_Log(L"TAGINDEX scan done: %llu items visited, %llu tagged", (unsigned long long)g_tagIndexSeen.load(),
               (unsigned long long)g_tagIndex.files.size());
    }
    g_tagIndexReady = true;
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
}

void TagIndexMaintain() {
    const ULONGLONG kTenMinutes = 10ULL * 60 * 10000000;
    bool due;
    {
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        due = g_tagIndexScanDone == 0 || TagIndexNow() - g_tagIndexScanDone > kTenMinutes;
        if (!due) {
            g_tagIndexReady = true;
        }
    }
    if (due) {
        TagIndexScanAll();
    }
    while (!g_tagIndexStop && !g_unloading) {
        if (WaitForSingleObject(g_tagIndexStopEvent, 1000) == WAIT_OBJECT_0) {
            break;
        }
        if (g_tagIndexRescan.exchange(false)) {
            TagIndexScanAll();
        }
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        if (g_tagIndexDirty && GetTickCount64() - g_tagIndexDirtyAt >= 5000) {
            TagIndexSaveLocked();
        }
    }
}

// A non-owner process reads the file the owner saved when it is newer than what it holds.
void TagIndexRefresh() {
    if (!g_tagIndexOwner) {
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        TagIndexLoadLocked(false);
        if (g_tagIndexScanDone != 0) {
            g_tagIndexReady = true;
        }
    }
}

}  // namespace

// Called when a feature needs the index (the sidebar, in the release build). The first Explorer process to ask owns the
// index: it walks the drives and watches them. Any other reads what it saved.
void TagIndexStart() {
    std::lock_guard<std::mutex> run(g_tagIndexRunMutex);
    if (g_tagIndexStarted) {
        return;
    }
    g_tagIndexStarted = true;
    g_tagIndexStop = false;
    g_tagIndexStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    {
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        TagIndexLoadLocked(true);
    }
    std::wstring folder = TagIndexFolder();
    if (!folder.empty()) {
        g_tagIndexLock = CreateFileW((folder + L"\\tagindex.lock").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    g_tagIndexOwner = g_tagIndexLock != INVALID_HANDLE_VALUE;
    Wh_Log(L"TAGINDEX start, owner=%d, %llu items loaded", g_tagIndexOwner ? 1 : 0, (unsigned long long)g_tagIndex.files.size());
    if (!g_tagIndexOwner) {
        return;
    }
    g_tagIndexThreads.emplace_back(TagIndexMaintain);
    for (auto const& root : TagIndexFixedRoots()) {
        g_tagIndexThreads.emplace_back(TagIndexWatchRoot, root);
    }
}

// Joins every thread of the index and saves it. Safe to call when it was never started.
void TagIndexStop() {
    std::lock_guard<std::mutex> run(g_tagIndexRunMutex);
    if (!g_tagIndexStarted) {
        return;
    }
    g_tagIndexStop = true;
    if (g_tagIndexStopEvent) {
        SetEvent(g_tagIndexStopEvent);
    }
    for (auto& thread : g_tagIndexThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    g_tagIndexThreads.clear();
    if (g_tagIndexOwner) {
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        if (g_tagIndexDirty) {
            TagIndexSaveLocked();
        }
    }
    if (g_tagIndexLock != INVALID_HANDLE_VALUE) {
        CloseHandle(g_tagIndexLock);
        g_tagIndexLock = INVALID_HANDLE_VALUE;
    }
    if (g_tagIndexStopEvent) {
        CloseHandle(g_tagIndexStopEvent);
        g_tagIndexStopEvent = nullptr;
    }
    g_tagIndexOwner = false;
    g_tagIndexStarted = false;
}

bool TagIndexReady() {
    TagIndexRefresh();
    return g_tagIndexReady;
}

// While the first walk runs: how many items it has visited.
size_t TagIndexProgress() {
    return g_tagIndexSeen;
}

std::vector<IndexedFile> TagIndexFiles(std::wstring const& tag) {
    TagIndexRefresh();
    std::lock_guard<std::mutex> lock(g_tagIndexMutex);
    bool pruned = false;
    auto files = TagIndexFilesIn(g_tagIndex, tag, &pruned);
    if (pruned) {
        TagIndexMarkDirtyLocked();
    }
    return files;
}

size_t TagIndexCount(std::wstring const& tag) {
    TagIndexRefresh();
    std::lock_guard<std::mutex> lock(g_tagIndexMutex);
    return TagIndexCountIn(g_tagIndex, tag);
}

// The labels write a file's tags through this, so the index knows at once (a watcher would tell it a moment later).
void TagIndexNoteWrite(std::wstring const& path, std::vector<LabelTag> const& tags) {
    for (auto const& tag : tags) {
        std::wstring shown = TagShownName(tag);
        if (!shown.empty()) {
            TagListEnsure(shown, tag.colour);
        }
    }
    std::lock_guard<std::mutex> lock(g_tagIndexMutex);
    TagIndexSet(g_tagIndex, path, tags);
    TagIndexMarkDirtyLocked();
}

// Rename, recolour or delete a tag on every file that has it, as the job says. Runs on the calling thread (a mod thread, never a
// window's), reports progress, and stops when `cancel` is set. The files keep their times.
TagRewriteResult TagIndexRewrite(TagRewrite const& job, std::function<void(size_t, size_t)> const& progress,
                                 std::atomic<bool> const& cancel) {
    TagIndexRefresh();
    std::vector<std::wstring> paths;
    {
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        paths = TagIndexPathsWith(g_tagIndex, job.oldName);
    }
    auto onUpdated = [](std::wstring const& path, std::vector<LabelTag> const& tags) {
        std::lock_guard<std::mutex> lock(g_tagIndexMutex);
        TagIndexSet(g_tagIndex, path, tags);
        TagIndexMarkDirtyLocked();
    };
    return TagIndexRewriteFiles(paths, job, progress, cancel, onUpdated);
}
