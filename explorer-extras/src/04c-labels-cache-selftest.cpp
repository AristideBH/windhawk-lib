// Labels: the self-test of the folder cache (debug build only). It runs on a thread of the mod, since it
// waits for reads. One log line: "LABELS CACHE SELFTEST pass" or "LABELS CACHE SELFTEST FAIL <what>".
#ifdef EXTRAS_VERBOSE

namespace {

std::wstring g_cacheFailure;

void CacheExpect(bool ok, const wchar_t* what) {
    if (!ok && g_cacheFailure.empty()) {
        g_cacheFailure = what;
    }
}

bool CacheTouch(std::wstring const& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(file);
    return true;
}

void CacheWaitComplete(std::wstring const& dir, std::shared_ptr<const LabelsFolder>* data) {
    for (int i = 0; i < 100; i++) {
        *data = LabelsFolderFor(dir);
        if ((*data)->complete) {
            return;
        }
        Sleep(20);
    }
}

void CacheSelfTestRun() {
    // The pure part: names shown by two items, one of them tagged.
    std::vector<LabelsItem> items = {{L"report", {LabelTag{1, L"x"}}}, {L"report", {}}, {L"solo", {LabelTag{2, L""}}}, {L"plain", {}},
                                     {L"dir", {LabelTag{3, L""}}, true}, {L"typed", {LabelTag{4, L""}}, false, L"Text file"}};
    LabelsFolder collected = LabelsCollect(items);
    CacheExpect(collected.complete, L"a collected folder is complete");
    CacheExpect(collected.ambiguous.count(L"report") == 1, L"two items named report are ambiguous");
    CacheExpect(!collected.byName.count(L"report"), L"an ambiguous name gets no tags");
    CacheExpect(collected.byName.count(L"solo") == 1 && !collected.byName.count(L"plain"), L"a unique tagged name keeps its tags");
    CacheExpect(collected.folders.count(L"dir") == 1 && !collected.folders.count(L"solo"), L"folders are known by name");
    CacheExpect(collected.typeNames.count(L"typed") == 1 && collected.typeNames[L"typed"] == L"Text file", L"the type text of a tagged item is kept");
    std::vector<LabelsItem> untaggedPair = {{L"same", {}}, {L"same", {}}};
    CacheExpect(LabelsCollect(untaggedPair).ambiguous.empty(), L"two untagged items of one name are not ambiguous");

    // A real folder.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring dir = std::wstring(temp) + L"ee-labels-cache-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring tagged = dir + L"\\b.txt";
    std::wstring folder = dir + L"\\sub";
    CacheTouch(dir + L"\\a.txt");
    CacheTouch(tagged);
    CreateDirectoryW(folder.c_str(), nullptr);
    LabelsExpect(LabelsWriteFile(tagged, {LabelTag{1, L"Urgent"}}) == LabelsWrite::Ok, L"tag the file");
    LabelsExpect(LabelsWriteFile(folder, {LabelTag{5, L""}}) == LabelsWrite::Ok, L"tag the folder");

    auto first = LabelsFolderFor(dir);
    CacheExpect(!first->complete, L"the first call returns an entry that is not complete yet");
    std::shared_ptr<const LabelsFolder> data;
    CacheWaitComplete(dir, &data);
    CacheExpect(data && data->complete, L"the read completes within 2 seconds");
    if (data) {
        bool file = data->byName.count(L"b.txt") || data->byName.count(L"b");
        CacheExpect(file && data->byName.count(L"sub") == 1, L"both tagged items are in the cache");
        CacheExpect(!data->byName.count(L"a.txt") && !data->byName.count(L"a"), L"the untagged item is not");
        CacheExpect(data->folders.count(L"sub") == 1, L"a read folder lists its subfolders");
        bool typed = (data->typeNames.count(L"b.txt") && !data->typeNames.at(L"b.txt").empty()) ||
                     (data->typeNames.count(L"b") && !data->typeNames.at(L"b").empty());
        CacheExpect(typed, L"a tagged file has its type text");
    }
    LabelsSetTags(tagged, {});
    data = LabelsFolderFor(dir);
    CacheExpect(!data->byName.count(L"b.txt") && !data->byName.count(L"b"), L"clearing an item's tags updates the cache at once");

    // A read that is cancelled in the middle keeps the old data and ends quickly.
    std::wstring big = dir + L"\\big";
    CreateDirectoryW(big.c_str(), nullptr);
    for (int i = 0; i < 1500; i++) {
        CacheTouch(big + L"\\f" + std::to_wstring(i) + L".txt");
    }
    auto before = LabelsFolderFor(big);
    CacheExpect(!before->complete, L"the big folder starts not complete");
    Sleep(5);
    LabelsCancelAll();
    ULONGLONG waited = GetTickCount64();
    while (LabelsReadsRunning() > 0 && GetTickCount64() - waited < 1000) {
        Sleep(5);
    }
    CacheExpect(LabelsReadsRunning() == 0 && GetTickCount64() - waited < 200, L"a cancelled read ends in under 200 ms");
    LabelsFolderFor(big);  // Starts a new read; it is cancelled again at once.
    LabelsCancelAll();
    waited = GetTickCount64();
    while (LabelsReadsRunning() > 0 && GetTickCount64() - waited < 1000) {
        Sleep(5);
    }

    for (int i = 0; i < 1500; i++) {
        DeleteFileW((big + L"\\f" + std::to_wstring(i) + L".txt").c_str());
    }
    RemoveDirectoryW(big.c_str());
    DeleteFileW((tagged + kLabelsStream).c_str());
    DeleteFileW(tagged.c_str());
    DeleteFileW((dir + L"\\a.txt").c_str());
    RemoveDirectoryW(folder.c_str());
    RemoveDirectoryW(dir.c_str());

    if (g_cacheFailure.empty()) {
        Wh_Log(L"LABELS CACHE SELFTEST pass");
    } else {
        Wh_Log(L"LABELS CACHE SELFTEST FAIL %s", g_cacheFailure.c_str());
    }
}

}  // namespace

void LabelsCacheSelfTest() {
    StartModThread([]() { CacheSelfTestRun(); });
}

#endif  // EXTRAS_VERBOSE
