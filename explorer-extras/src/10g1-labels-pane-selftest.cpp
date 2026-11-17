// Labels: the self-test of what the pane does with the tags of a selection (debug build only). One log line:
// "LABELS PANE SELFTEST pass" or "LABELS PANE SELFTEST FAIL <what>".
#ifdef EXTRAS_VERBOSE

namespace {

std::wstring g_paneLabelsFailure;

void PaneLabelsExpect(bool ok, const wchar_t* what) {
    if (!ok && g_paneLabelsFailure.empty()) {
        g_paneLabelsFailure = what;
    }
}

}  // namespace

void LabelsPaneSelfTest() {
    LabelTag red{1, L""}, urgent{2, L"Urgent"}, urgentLower{2, L"urgent"};

    // Merging the tags of several items: a tag is solid when every item has it.
    auto merged = LabelsMerge({{red, urgent}, {red}, {red, urgentLower}});
    PaneLabelsExpect(merged.tags.size() == 2, L"merging gives each tag once, names matched without case");
    if (merged.tags.size() == 2) {
        PaneLabelsExpect(merged.tags[0].first.colour == 1 && merged.tags[0].second, L"a tag on every item is solid");
        PaneLabelsExpect(merged.tags[1].first.colour == 2 && !merged.tags[1].second, L"a tag on some of the items is mixed");
    }
    PaneLabelsExpect(LabelsMerge({}).tags.empty(), L"nothing selected, nothing merged");
    PaneLabelsExpect(LabelsMerge({{urgent}}).tags.size() == 1 && LabelsMerge({{urgent}}).tags[0].second, L"one item: its tags are solid");

    // The look of a tag as a badge.
    PaneLabelsExpect(LabelsPillFill(1, false) == L"#59D13438", L"a badge is the colour at about a third of full strength");
    PaneLabelsExpect(LabelsPillFill(0, true) == L"#59A19F9D", L"the dark variant is used in the dark theme");
    PaneLabelsExpect(LabelsXmlEscape(L"A&B<\"'>") == L"A&amp;B&lt;&quot;&apos;&gt;", L"names are escaped for the markup");

    // Adding and removing in one list.
    std::vector<LabelTag> tags = {red};
    PaneLabelsExpect(LabelsAddTag(&tags, red) && tags.size() == 1, L"adding a tag that is there changes nothing");
    PaneLabelsExpect(LabelsAddTag(&tags, urgent) && tags.size() == 2, L"adding a new tag");
    LabelsRemoveTag(&tags, urgentLower);
    PaneLabelsExpect(tags.size() == 1 && tags[0].colour == 1, L"removing matches the name without case");
    std::vector<LabelTag> full;
    for (int i = 0; i < 16; i++) {
        full.push_back(LabelTag{(uint8_t)(i % 8), std::to_wstring(i)});
    }
    PaneLabelsExpect(!LabelsAddTag(&full, LabelTag{1, L"one more"}) && full.size() == 16, L"a 17th tag is refused");

    // Applying to files.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring dir = std::wstring(temp) + L"ee-labels-pane-" + std::to_wstring(GetCurrentProcessId());
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
    auto result = LabelsApplyToSelection(files, red, true);
    PaneLabelsExpect(result.done == 3 && result.worst == LabelsWrite::Ok, L"a tag added to three files");
    std::vector<LabelTag> read;
    PaneLabelsExpect(LabelsReadFile(files[1], &read) && read.size() == 1 && read[0].colour == 1, L"the tag is on the file");
    result = LabelsApplyToSelection(files, red, true);
    PaneLabelsExpect(result.done == 0 && result.worst == LabelsWrite::Ok, L"adding it again writes nothing");
    SetFileAttributesW(files[2].c_str(), FILE_ATTRIBUTE_READONLY);
    result = LabelsApplyToSelection(files, urgent, true);
    PaneLabelsExpect(result.done == 2 && result.worst == LabelsWrite::ReadOnly && result.failedName == L"f2.txt",
                     L"a read-only file is reported by name and the others are tagged");
    SetFileAttributesW(files[2].c_str(), FILE_ATTRIBUTE_NORMAL);
    result = LabelsApplyToSelection(files, red, false);
    PaneLabelsExpect(result.done == 3 && LabelsReadFile(files[0], &read) && read.size() == 1 && read[0].colour == 2,
                     L"removing a tag keeps the others");
    LabelsApplyToSelection(files, red, true);
    result = LabelsClearSelection(files);
    PaneLabelsExpect(result.done == 3 && LabelsReadFile(files[0], &read) && read.empty() && LabelsReadFile(files[2], &read) && read.empty(),
                     L"clearing the tags of a selection");
    result = LabelsClearSelection(files);
    PaneLabelsExpect(result.done == 0, L"clearing again writes nothing");
    std::vector<LabelTag> sixteen;
    for (int i = 0; i < 16; i++) {
        sixteen.push_back(LabelTag{(uint8_t)(i % 8), std::to_wstring(i)});
    }
    LabelsWriteFile(files[0], sixteen);
    result = LabelsApplyToSelection({files[0]}, LabelTag{1, L"extra"}, true);
    PaneLabelsExpect(result.full && result.done == 0, L"a file with 16 tags is reported as full");
    for (auto const& path : files) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW((path + kLabelsStream).c_str());
        DeleteFileW(path.c_str());
    }
    RemoveDirectoryW(dir.c_str());

    if (g_paneLabelsFailure.empty()) {
        Wh_Log(L"LABELS PANE SELFTEST pass");
    } else {
        Wh_Log(L"LABELS PANE SELFTEST FAIL %s", g_paneLabelsFailure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
