// Labels: the self-test of the tag stream. It only exists in the debug build (EXTRAS_VERBOSE), runs once
// when the mod loads and writes one line to the log: "LABELS SELFTEST pass", or "LABELS SELFTEST FAIL <what>".
// tools/smoke-test-explorer-extras.ps1 reads that line.
#ifdef EXTRAS_VERBOSE

namespace {

std::wstring g_labelsFailure;

void LabelsExpect(bool ok, const wchar_t* what) {
    if (!ok && g_labelsFailure.empty()) {
        g_labelsFailure = what;
    }
}

bool LabelsSameTags(std::vector<LabelTag> const& a, std::vector<LabelTag> const& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].colour != b[i].colour || a[i].name != b[i].name) {
            return false;
        }
    }
    return true;
}

bool LabelsWriteTime(std::wstring const& path, FILETIME* time) {
    HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = GetFileTime(file, nullptr, nullptr, time) != 0;
    CloseHandle(file);
    return ok;
}

bool LabelsSetWriteTime(std::wstring const& path, FILETIME time) {
    HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = SetFileTime(file, nullptr, nullptr, &time) != 0;
    CloseHandle(file);
    return ok;
}

}  // namespace

void LabelsSelfTest() {
    // The text format.
    std::vector<LabelTag> tags = {{1, L"a:b"},
                                  {2, L"x\ty"},
                                  {3, L"line\nbreak"},
                                  {4, L"p|q"},
                                  {5, std::wstring(40, L'n')},
                                  {6, L"\U0001F600 smile"},
                                  {7, L""},
                                  {0, L"back\\slash\r"}};
    std::vector<LabelTag> back;
    LabelsExpect(LabelsParse(LabelsSerialize(tags), &back), L"round trip parses");
    LabelsExpect(LabelsSameTags(tags, back), L"round trip keeps every tag");

    std::vector<LabelTag> many(17, LabelTag{1, L"t"});
    LabelsExpect(LabelsParse(LabelsSerialize(many), &back) && back.size() == 16, L"17 tags are cut to 16");
    std::vector<LabelTag> longName = {LabelTag{1, std::wstring(41, L'x')}};
    LabelsExpect(LabelsParse(LabelsSerialize(longName), &back) && back.size() == 1 && back[0].name.size() == 40,
                 L"a 41 character name is cut to 40");
    LabelsExpect(!LabelsParse(L"v9\n1\tX", &back), L"a newer version is refused");
    LabelsExpect(!LabelsParse(L"1\tX", &back), L"no version line is refused");
    LabelsExpect(!LabelsParse(L"v1\nx\tY", &back), L"a colour that is not a digit is refused");
    LabelsExpect(!LabelsParse(L"v1\n9\tY", &back), L"a colour out of range is refused");
    LabelsExpect(LabelsParse(L"v1\n", &back) && back.empty(), L"a version line alone is an empty list");

    // A file and a folder, on the temp drive.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring dir = std::wstring(temp) + L"ee-labels-test-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\a.txt";
    std::wstring folder = dir + L"\\sub";
    CreateDirectoryW(folder.c_str(), nullptr);
    HANDLE created = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (created != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(created, "hello", 5, &written, nullptr);
        CloseHandle(created);
    }
    FILETIME old = {};
    SYSTEMTIME oldSystem = {2020, 1, 4, 2, 3, 4, 5, 0};
    SystemTimeToFileTime(&oldSystem, &old);
    LabelsExpect(LabelsSetWriteTime(file, old) && LabelsSetWriteTime(folder, old), L"set the old write times");
    LabelsExpect(LabelsVolumeSupportsStreams(file), L"the temp drive supports named streams");

    std::vector<LabelTag> mine = {{1, L"Urgent"}, {5, L""}};
    LabelsExpect(LabelsWriteFile(file, mine) == LabelsWrite::Ok, L"write to a file");
    LabelsExpect(LabelsReadFile(file, &back) && LabelsSameTags(mine, back), L"read back from a file");
    FILETIME after = {};
    LabelsExpect(LabelsWriteTime(file, &after) && CompareFileTime(&old, &after) == 0, L"a file's write time is unchanged");
    LabelsExpect(LabelsWriteFile(folder, mine) == LabelsWrite::Ok, L"write to a folder");
    LabelsExpect(LabelsReadFile(folder, &back) && LabelsSameTags(mine, back), L"read back from a folder");
    LabelsExpect(LabelsWriteTime(folder, &after) && CompareFileTime(&old, &after) == 0, L"a folder's write time is unchanged");
    LabelsExpect(LabelsReadFile(dir + L"\\missing.txt", &back) == false, L"a missing file cannot be read");

    LabelsExpect(LabelsWriteFile(file, {}) == LabelsWrite::Ok, L"writing an empty list");
    LabelsExpect(LabelsReadFile(file, &back) && back.empty(), L"an empty list leaves no tags");
    WIN32_FIND_STREAM_DATA stream = {};
    HANDLE find = FindFirstStreamW(file.c_str(), FindStreamInfoStandard, &stream, 0);
    int streams = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            streams++;
        } while (FindNextStreamW(find, &stream));
        FindClose(find);
    }
    LabelsExpect(streams == 1, L"an empty list removes the stream");

    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);
    LabelsExpect(LabelsWriteFile(file, mine) == LabelsWrite::ReadOnly, L"a read-only file is refused");
    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);

    DeleteFileW(file.c_str());
    RemoveDirectoryW(folder.c_str());
    RemoveDirectoryW(dir.c_str());

    if (g_labelsFailure.empty()) {
        Wh_Log(L"LABELS SELFTEST pass");
    } else {
        Wh_Log(L"LABELS SELFTEST FAIL %s", g_labelsFailure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
