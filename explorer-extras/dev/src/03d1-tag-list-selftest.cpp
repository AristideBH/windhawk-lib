// Sidebar tag list: the self-test of the list operations and its text. It only exists in the debug build
// (EXTRAS_VERBOSE), runs once when the mod loads and writes one line to the log: "TAGS SELFTEST pass", or
// "TAGS SELFTEST FAIL <what>". tools/smoke-test-explorer-extras.ps1 reads that line.
#ifdef EXTRAS_VERBOSE

namespace {

std::wstring g_tagsFailure;

void TagsExpect(bool ok, const wchar_t* what) {
    if (!ok && g_tagsFailure.empty()) {
        g_tagsFailure = what;
    }
}

std::vector<std::wstring> TagsNames(std::vector<TagDef> const& list) {
    std::vector<std::wstring> names;
    for (auto const& tag : list) {
        names.push_back(tag.name);
    }
    return names;
}

}  // namespace

void TagListSelfTest() {
    std::vector<TagDef> list;

    TagsExpect(TagListAddTo(list, L"Urgent", 1), L"a new tag is added");
    TagsExpect(!TagListAddTo(list, L"urgent", 2), L"the same name in another case is refused");
    TagsExpect(!TagListAddTo(list, L"  ", 2), L"a blank name is refused");
    TagsExpect(!TagListAddTo(list, L"a\tb", 2), L"a name with a tab is refused");
    TagsExpect(!TagListAddTo(list, std::wstring(41, L'x'), 2), L"a 41 character name is refused");
    TagsExpect(TagListAddTo(list, std::wstring(40, L'y'), 3), L"a 40 character name is accepted");
    TagsExpect(TagListAddTo(list, L"Home", 2), L"a second tag is added");
    TagsExpect(list.size() == 3, L"three tags are in the list");

    TagsExpect(TagNameValid(L"Todo"), L"a plain name is valid");
    TagsExpect(!TagNameValid(L"line\nbreak"), L"a name with a line feed is invalid");
    TagsExpect(!TagNameValid(L""), L"an empty name is invalid");

    // Rename keeps the colour and the other fields; it refuses a name that another tag has.
    TagsExpect(TagListRenameIn(list, L"Urgent", L"Important"), L"a tag is renamed");
    TagsExpect(list[0].name == L"Important" && list[0].colour == 1, L"rename keeps the place and the colour");
    TagsExpect(!TagListRenameIn(list, L"Important", L"home"), L"a rename to another tag's name is refused");
    TagsExpect(TagListRenameIn(list, L"Important", L"IMPORTANT"), L"a rename that only changes the case is accepted");
    TagsExpect(!TagListRenameIn(list, L"Nothing", L"X"), L"a rename of an unknown tag is refused");

    TagListRecolorIn(list, L"important", 5);
    TagsExpect(list[0].colour == 5, L"recolor changes the colour (the name is found in any case)");

    // Hide, remove.
    TagListSetHiddenIn(list, L"Home", true);
    TagsExpect(list[2].hidden, L"a tag is hidden");
    TagsExpect(TagListRemoveFrom(list, L"HOME") && list.size() == 2, L"a tag is removed");
    TagsExpect(!TagListRemoveFrom(list, L"Home"), L"removing a missing tag says so");

    // Ensure adds an unknown tag and leaves a known one alone.
    TagsExpect(TagListEnsureIn(list, L"Found", 4) && list.size() == 3, L"ensure adds an unknown tag");
    TagsExpect(!TagListEnsureIn(list, L"found", 6) && list.size() == 3 && list[2].colour == 4,
               L"ensure leaves a known tag (and its colour) alone");
    TagsExpect(TagListEnsureIn(list, L"a\tb", 4) && list.size() == 4 && list[3].name == L"a\tb",
               L"ensure keeps a name found on disk even when the list refuses to create it");

    // Recent: visible tags by last use, newest first; never used ones after, by name.
    std::vector<TagDef> recent;
    TagListAddTo(recent, L"Beta", 1);
    TagListAddTo(recent, L"Alpha", 2);
    TagListAddTo(recent, L"Gamma", 3);
    TagListAddTo(recent, L"Delta", 4);
    TagListTouchIn(recent, L"Beta", 100);
    TagListTouchIn(recent, L"Gamma", 300);
    TagListTouchIn(recent, L"Delta", 200);
    TagListSetHiddenIn(recent, L"Delta", true);
    auto top = TagListRecentOf(recent, 3);
    TagsExpect(TagsNames(top) == std::vector<std::wstring>({L"Gamma", L"Beta", L"Alpha"}),
               L"recent skips hidden tags, newest first, unused after");
    TagsExpect(TagListRecentOf(recent, 1).size() == 1 && TagListRecentOf(recent, 1)[0].name == L"Gamma",
               L"recent honours the count");
    TagsExpect(TagListRecentOf(recent, 0).empty(), L"a count of zero gives nothing");

    // The text round trip keeps every field, a tab and a backslash in a name, and refuses what is not ours.
    std::vector<TagDef> text;
    TagListEnsureIn(text, L"x\ty\\z", 7);
    TagListAddTo(text, L"Plain", 0);
    TagListSetHiddenIn(text, L"Plain", true);
    TagListTouchIn(text, L"Plain", 123456789012345ULL);
    std::vector<TagDef> back;
    TagsExpect(TagListParse(TagListSerialize(text), &back), L"the text round trip parses");
    TagsExpect(back.size() == 2 && back[0].name == L"x\ty\\z" && back[0].colour == 7 && back[1].hidden &&
                   back[1].lastUsed == 123456789012345ULL,
               L"the round trip keeps every field");
    TagsExpect(!TagListParse(L"v9\n1\t0\t0\tA", &back), L"a newer version is refused");
    TagsExpect(!TagListParse(L"1\t0\t0\tA", &back), L"no version line is refused");
    TagsExpect(!TagListParse(L"v1\n9\t0\t0\tA", &back), L"a colour out of range is refused");
    TagsExpect(TagListParse(L"v1\n", &back) && back.empty(), L"a version line alone is an empty list");

    // The old recent-tags store is taken into the list once: oldest first, so the newest ends up most recent; a tag the
    // list has keeps its colour and its later last-use time.
    std::vector<TagDef> imported;
    TagListAddTo(imported, L"Known", 6);
    TagListTouchIn(imported, L"Known", 10000);
    std::vector<LabelTag> legacy = {{3, L"Later"}, {1, L"known"}, {2, L"Urgent"}};  // newest first
    TagListImportInto(imported, legacy, 5000);
    TagsExpect(imported.size() == 3, L"the import adds the two tags the list did not have");
    TagsExpect(imported[0].name == L"Known" && imported[0].colour == 6 && imported[0].lastUsed == 10000,
               L"a known tag keeps its colour and its later last-use time");
    TagsExpect(TagsNames(TagListRecentOf(imported, 3)) == std::vector<std::wstring>({L"Known", L"Later", L"Urgent"}),
               L"the newest old tag is the most recent after the import");
    TagListImportInto(imported, legacy, 5000);
    TagsExpect(imported.size() == 3, L"importing twice adds nothing");

    // The "Add a tag" flyout reads the list as label tags.
    auto asLabels = TagListAsLabels(TagListRecentOf(imported, 2));
    TagsExpect(asLabels.size() == 2 && asLabels[0].name == L"Known" && asLabels[0].colour == 6 &&
                   asLabels[1].name == L"Later" && asLabels[1].colour == 3,
               L"the list is read as label tags, in recent order");

    if (g_tagsFailure.empty()) {
        Wh_Log(L"TAGS SELFTEST pass");
    } else {
        Wh_Log(L"TAGS SELFTEST FAIL %s", g_tagsFailure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
