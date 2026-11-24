// Sidebar, list of a tag's files: the self-test of its pure parts (debug build only): the rows made from what the index lists, and
// how they sort. One log line: "TAG RESULTS SELFTEST pass" or "TAG RESULTS SELFTEST FAIL <what>".
#ifdef EXTRAS_VERBOSE

void TagResultsSelfTest() {
    std::wstring failure;
    auto expect = [&failure](bool ok, const wchar_t* what) {
        if (!ok && failure.empty()) {
            failure = what;
        }
    };
    auto file = [](const wchar_t* path, ULONGLONG size, ULONGLONG time) {
        IndexedFile item;
        item.path = path;
        item.size = size;
        ULARGE_INTEGER value;
        value.QuadPart = time;
        item.modified.dwLowDateTime = value.LowPart;
        item.modified.dwHighDateTime = value.HighPart;
        return item;
    };
    auto names = [](std::vector<TagResultRow> const& rows) {
        std::wstring text;
        for (auto const& row : rows) {
            text += row.name + L";";
        }
        return text;
    };
    std::vector<IndexedFile> files = {file(L"C:\\a\\one.txt", 300, 20), file(L"C:\\b\\two.txt", 100, 30),
                                      file(L"C:\\a\\three.txt", 200, 10), file(L"C:\\root.txt", 50, 40)};
    auto rows = TagResultsRows(files);
    expect(rows.size() == 4, L"a row for each file");
    expect(rows[0].name == L"one.txt" && rows[0].folder == L"C:\\a", L"the name and the Location are told apart");
    expect(rows[3].folder == L"C:\\", L"a file at the root of a drive keeps its slash in Location");
    TagResultsSort(rows, TagResultSort::Size, true);
    expect(names(rows) == L"root.txt;two.txt;three.txt;one.txt;", L"sorted by size, smallest first");
    TagResultsSort(rows, TagResultSort::Size, false);
    expect(names(rows) == L"one.txt;three.txt;two.txt;root.txt;", L"sorted by size, largest first");
    TagResultsSort(rows, TagResultSort::Name, true);
    expect(names(rows) == L"one.txt;root.txt;three.txt;two.txt;", L"sorted by name");
    TagResultsSort(rows, TagResultSort::Modified, true);
    expect(names(rows) == L"three.txt;one.txt;two.txt;root.txt;", L"sorted by date, oldest first");
    TagResultsSort(rows, TagResultSort::Location, true);
    expect(names(rows) == L"root.txt;one.txt;three.txt;two.txt;", L"sorted by Location, ties by path");
    expect(TagResultsRows({}).empty(), L"no files, no rows");
    if (failure.empty()) {
        Wh_Log(L"TAG RESULTS SELFTEST pass");
    } else {
        Wh_Log(L"TAG RESULTS SELFTEST FAIL %s", failure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
