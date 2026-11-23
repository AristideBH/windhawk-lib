// Sidebar tag index: the self-test of the index data, the walk, the rewrite pass and the change notifications. It only exists
// in the debug build (EXTRAS_VERBOSE), runs once when the mod loads and writes one line to the log: "TAGINDEX SELFTEST pass",
// or "TAGINDEX SELFTEST FAIL <what>". tools/smoke-test-explorer-extras.ps1 reads that line. It works on a folder of its own
// in the temp folder and on index data of its own: the saved tag list and the live index are not touched.
#ifdef EXTRAS_VERBOSE

namespace {

std::wstring g_indexFailure;

void IndexExpect(bool ok, const wchar_t* what) {
    if (!ok && g_indexFailure.empty()) {
        g_indexFailure = what;
    }
}

bool IndexWriteTime(std::wstring const& path, FILETIME* time) {
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = GetFileTime(file, nullptr, nullptr, time) != 0;
    CloseHandle(file);
    return ok;
}

bool IndexMakeFile(std::wstring const& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    WriteFile(file, "x", 1, &written, nullptr);
    CloseHandle(file);
    return true;
}

}  // namespace

void TagIndexSelfTest() {
    std::atomic<bool> never{false};
    std::atomic<bool> always{true};

    // The saved text keeps a path with a tab and a backslash, the tags and the time of the last full scan.
    TagIndexData text;
    TagIndexSet(text, L"C:\\odd\ttab\\file.txt", {{1, L"A"}, {2, L"two\nlines"}});
    TagIndexData reread;
    ULONGLONG when = 0;
    IndexExpect(TagIndexParse(TagIndexSerialize(text, 77), &reread, &when) && when == 77 && reread.files.size() == 1,
                L"the saved text parses back");
    auto back = reread.files.begin()->second;
    IndexExpect(back.path == L"C:\\odd\ttab\\file.txt" && back.tags.size() == 2 && back.tags[1].name == L"two\nlines",
                L"the saved text keeps the path and the tags");
    IndexExpect(!TagIndexParse(L"v9\n", &reread, &when) && !TagIndexParse(L"v1\nT\t1\tx\n", &reread, &when),
                L"a newer version, and a tag before any file, are refused");

    // A folder of our own with tagged files, a tagged sub folder and an untagged file.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring dir = std::wstring(temp) + L"extras-index-test";
    std::wstring sub = dir + L"\\sub";
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW(sub.c_str(), nullptr);
    std::wstring a = dir + L"\\a.txt", b = sub + L"\\b.txt", c = sub + L"\\c.txt", plain = dir + L"\\plain.txt";
    IndexMakeFile(a);
    IndexMakeFile(b);
    IndexMakeFile(c);
    IndexMakeFile(plain);
    bool written = LabelsWriteFile(a, {{1, L"Work"}, {2, L"Urgent"}}) == LabelsWrite::Ok &&
                   LabelsWriteFile(b, {{1, L"work"}}) == LabelsWrite::Ok &&
                   LabelsWriteFile(c, {{3, L"Other"}}) == LabelsWrite::Ok &&
                   LabelsWriteFile(sub, {{1, L"Work"}}) == LabelsWrite::Ok;
    IndexExpect(written, L"the test files take their tags (is the temp drive NTFS?)");

    TagIndexData data;
    std::atomic<size_t> seen{0};
    IndexExpect(TagIndexScanRoot(data, dir, false, false, never, &seen), L"the walk finishes");
    IndexExpect(data.files.size() == 4, L"the walk finds the three files and the folder, not the untagged file");
    IndexExpect(TagIndexCountIn(data, L"WORK") == 3 && TagIndexCountIn(data, L"urgent") == 1 && TagIndexCountIn(data, L"nope") == 0,
                L"a tag is counted by its name in any case");
    IndexExpect(seen.load() >= 5, L"the walk counts what it visited");
    auto files = TagIndexFilesIn(data, L"work");
    bool folderSeen = false;
    for (auto const& file : files) {
        folderSeen = folderSeen || (file.isFolder && file.path == sub);
    }
    IndexExpect(files.size() == 3 && folderSeen, L"the files of a tag come with their kind");

    TagIndexData stopped;
    IndexExpect(!TagIndexScanRoot(stopped, dir, false, false, always, nullptr), L"a walk that is asked to stop says so");

    // Rename keeps the file's times, folds a name the file already has, and a recolour changes the digit.
    FILETIME before{}, after{};
    IndexWriteTime(a, &before);
    TagRewrite rename;
    rename.oldName = L"Work";
    rename.newName = L"Job";
    auto touchedPaths = TagIndexPathsWith(data, L"Work");
    auto onUpdated = [&data](std::wstring const& path, std::vector<LabelTag> const& tags) { TagIndexSet(data, path, tags); };
    size_t lastDone = 0;
    TagRewriteResult result = TagIndexRewriteFiles(touchedPaths, rename, [&lastDone](size_t done, size_t) { lastDone = done; }, never, onUpdated);
    IndexExpect(result.total == 3 && result.changed == 3 && result.failed == 0 && lastDone == 3, L"a rename reaches the three items");
    std::vector<LabelTag> tags;
    LabelsReadFile(a, &tags);
    IndexExpect(tags.size() == 2 && tags[0].name == L"Job" && tags[0].colour == 1 && tags[1].name == L"Urgent",
                L"the name changed on disk, the colour and the other tag stayed");
    IndexWriteTime(a, &after);
    IndexExpect(CompareFileTime(&before, &after) == 0, L"a rewrite keeps the last-write time");
    IndexExpect(TagIndexCountIn(data, L"Work") == 0 && TagIndexCountIn(data, L"Job") == 3, L"the index follows the rename");

    TagRewrite recolour;
    recolour.oldName = L"job";
    recolour.newName = L"Job";
    recolour.newColour = 5;
    TagIndexRewriteFiles(TagIndexPathsWith(data, L"Job"), recolour, nullptr, never, onUpdated);
    LabelsReadFile(b, &tags);
    IndexExpect(tags.size() == 1 && tags[0].colour == 5 && tags[0].name == L"Job", L"a recolour rewrites the colour digit");

    // A rename to a name the file already has folds the two into one.
    LabelsWriteFile(c, {{3, L"Other"}, {4, L"Job"}});
    TagIndexSet(data, c, {{3, L"Other"}, {4, L"Job"}});
    TagRewrite fold;
    fold.oldName = L"Other";
    fold.newName = L"job";
    TagIndexRewriteFiles(TagIndexPathsWith(data, L"Other"), fold, nullptr, never, onUpdated);
    LabelsReadFile(c, &tags);
    IndexExpect(tags.size() == 1, L"a rename onto a name the file has keeps one tag");

    // Delete removes the tag from every file and the stream when it was the last.
    TagRewrite remove;
    remove.oldName = L"Job";
    TagRewriteResult removed = TagIndexRewriteFiles(TagIndexPathsWith(data, L"Job"), remove, nullptr, never, onUpdated);
    LabelsReadFile(a, &tags);
    IndexExpect(removed.changed == 4 && tags.size() == 1 && tags[0].name == L"Urgent", L"a delete strips the tag and keeps the others");
    LabelsReadFile(b, &tags);
    IndexExpect(tags.empty() && TagIndexCountIn(data, L"Job") == 0, L"a delete of the last tag removes the stream");

    TagRewriteResult cancelled = TagIndexRewriteFiles(TagIndexPathsWith(data, L"Urgent"), remove, nullptr, always, onUpdated);
    IndexExpect(cancelled.cancelled && cancelled.changed == 0, L"a rewrite that is asked to stop does nothing more");

    // Notifications: a rename moves the entry (a folder takes its children), a delete drops it, a stream event asks for a read.
    TagIndexData notes;
    TagIndexSet(notes, L"C:\\data\\old\\f.txt", {{1, L"A"}});
    TagIndexSet(notes, L"C:\\data\\keep.txt", {{1, L"A"}});
    TagIndexNotifyState state;
    TagIndexHandleNotification(notes, L"C:\\data\\old", 4, state);
    TagIndexHandleNotification(notes, L"C:\\data\\new", 5, state);
    IndexExpect(notes.files.count(TagIndexKey(L"C:\\data\\new\\f.txt")) == 1 && notes.files.size() == 2,
                L"a renamed folder takes the entries under it");
    TagIndexHandleNotification(notes, L"C:\\data\\keep.txt", 2, state);
    IndexExpect(notes.files.size() == 1, L"a delete drops the entry");
    IndexExpect(TagIndexHandleNotification(notes, L"C:\\data\\x.txt:ExplorerExtras.Labels", 8, state) == L"C:\\data\\x.txt",
                L"a stream event names the file to read again");
    IndexExpect(TagIndexHandleNotification(notes, L"C:\\data\\x.txt:Zone.Identifier", 8, state).empty(),
                L"another stream is ignored");
    IndexExpect(TagIndexExcludedBelowRoot(L"Windows\\System32\\x") && TagIndexExcludedBelowRoot(L"program files\\a") &&
                    !TagIndexExcludedBelowRoot(L"Users\\me"),
                L"system folders are left out, in any case");

    DeleteFileW(a.c_str());
    DeleteFileW(b.c_str());
    DeleteFileW(c.c_str());
    DeleteFileW(plain.c_str());
    RemoveDirectoryW(sub.c_str());
    RemoveDirectoryW(dir.c_str());

    if (g_indexFailure.empty()) {
        Wh_Log(L"TAGINDEX SELFTEST pass");
    } else {
        Wh_Log(L"TAGINDEX SELFTEST FAIL %s", g_indexFailure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
