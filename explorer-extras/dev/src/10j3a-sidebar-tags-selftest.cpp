// Sidebar Tags: the self-test of what a drop and a delete do (debug build only). It works on temp files and a tag of its own, and
// logs one line: "TAGS UI SELFTEST pass", or "TAGS UI SELFTEST FAIL <what>". tools/smoke-test-explorer-extras.ps1 reads that line.
// The files are in the temp folder, which the tag index leaves out, so the pass is run on the list of files directly.
#ifdef EXTRAS_VERBOSE

namespace {

void TagsUiSelfTestRun() {
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    std::wstring failure;
    auto expect = [&failure](bool ok, const wchar_t* what) {
        if (!ok && failure.empty()) {
            failure = what;
        }
    };
    for (auto const& old : TagListSnapshot()) {
        if (old.name.rfind(L"ee-ui-test-", 0) == 0) {
            TagListRemove(old.name);  // Left by a test run that was cut short.
        }
    }
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring dir = std::wstring(temp) + L"ee-tags-ui-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    std::vector<std::wstring> files;
    for (int i = 0; i < 3; i++) {
        std::wstring path = dir + L"\\f" + std::to_wstring(i) + L".txt";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        files.push_back(path);
    }
    std::wstring tag = L"ee-ui-test-" + std::to_wstring(GetCurrentProcessId());
    expect(TagListAdd(tag, 3), L"the test tag was added to the list");

    // A drop: one of the two files already has the tag, and both end with it once.
    LabelsApplyToSelection({files[0]}, LabelTag{3, tag}, true);
    TagsAddToItems({files[0], files[1]}, tag);
    for (int i = 0; i < 2; i++) {
        std::vector<LabelTag> tags;
        expect(LabelsReadFile(files[i], &tags) && tags.size() == 1 && tags[0].colour == 3 && tags[0].name == tag,
               L"a dropped file has the tag once");
    }
    auto latest = TagListRecent(1);
    expect(latest.size() == 1 && latest[0].name == tag, L"the tag dropped on is the latest used");
    TagsAddToItems({files[0]}, L"ee-ui-no-such-tag");
    std::vector<LabelTag> untouched;
    expect(LabelsReadFile(files[0], &untouched) && untouched.size() == 1, L"a tag that is not in the list is not added");

    // A delete over three files: it reports three, takes the tag off all of them and out of the list.
    TagsAddToItems(files, tag);
    std::atomic<bool> never{false};
    size_t lastTotal = 0;
    TagRewriteResult result = TagsDeletePass(tag, &files, [&lastTotal](size_t, size_t total) { lastTotal = total; }, never);
    expect(result.total == 3 && result.changed == 3 && result.failed == 0 && !result.cancelled, L"a delete changed three files");
    expect(lastTotal == 3, L"the delete reported its progress");
    for (auto const& path : files) {
        std::vector<LabelTag> tags;
        expect(LabelsReadFile(path, &tags) && tags.empty(), L"the file lost the tag");
    }
    bool listed = false;
    for (auto const& def : TagListSnapshot()) {
        listed = listed || TagNamesEqual(def.name, tag);
    }
    expect(!listed, L"the deleted tag is out of the list");

    // The checks on a name.
    expect(!TagNameValid(L""), L"an empty name is refused");
    expect(!TagNameValid(std::wstring(kLabelsMaxName + 1, L'x')), L"a name over the limit is refused");

    TagListRemove(tag);
    for (auto const& path : files) {
        DeleteFileW(path.c_str());
    }
    RemoveDirectoryW(dir.c_str());
    if (failure.empty()) {
        Wh_Log(L"TAGS UI SELFTEST pass");
    } else {
        Wh_Log(L"TAGS UI SELFTEST FAIL %s", failure.c_str());
    }
    if (com) {
        CoUninitialize();
    }
}

}  // namespace

void TagsUiSelfTest() {
    static std::atomic<bool> once{false};
    if (once.exchange(true)) {
        return;
    }
    StartModThread([]() { TagsUiSelfTestRun(); });
}

#endif  // EXTRAS_VERBOSE
