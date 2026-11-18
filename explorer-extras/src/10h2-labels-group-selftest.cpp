// The self-test of the Tags values served to Explorer's Sort by and Group by (debug build only): "LABELS GROUP SELFTEST pass"
// or "LABELS GROUP SELFTEST FAIL <what>".
#ifdef EXTRAS_VERBOSE

void LabelsGroupSelfTest() {
    std::wstring failure;
    auto expect = [&](bool ok, const wchar_t* what) {
        if (!ok && failure.empty()) {
            failure = what;
        }
    };
    using V = std::vector<std::wstring>;
    expect(LabelsKeywordValues({L"holiday"}, {L"Red", L"Urgent"}) == V({L"holiday", L"Red", L"Urgent"}),
           L"the keywords the file has come first, then the labels");
    expect(LabelsKeywordValues({L"red"}, {L"Red", L"Blue"}) == V({L"red", L"Blue"}),
           L"a label that is already a keyword (any case) is not added twice");
    expect(LabelsKeywordValues({}, {L"Red"}) == V({L"Red"}), L"a file with no keywords gets the labels");
    expect(LabelsKeywordValues({L"a", L"b"}, {}) == V({L"a", L"b"}), L"no labels leave the keywords as they are");
    expect(LabelsKeywordValues({}, {}).empty(), L"nothing and nothing is nothing");
    expect(LabelsKeywordValues({}, {L"Red", L"red", L"Red"}) == V({L"Red"}), L"the same label twice is served once");
    expect(LabelsKeywordValues({}, {L"", L"Blue"}) == V({L"Blue"}), L"an empty name is dropped");
    // The groups of Columns: one per label, an item in the group of each of its labels, the items with none last.
    std::vector<V> entries = {{L"Red"}, {L"Red", L"Urgent"}, {}, {L"Blue"}};
    auto plan = LabelsPlanGroups(entries, true, L"None");
    expect(plan.names == V({L"Blue", L"Red", L"Urgent", L"None"}), L"groups are the labels in order, then the group of the untagged");
    expect(plan.members.size() == 4 && plan.members[1] == std::vector<size_t>({0, 1}) && plan.members[2] == std::vector<size_t>({1}) &&
               plan.members[3] == std::vector<size_t>({2}) && plan.members[0] == std::vector<size_t>({3}),
           L"an item is in each group of its labels");
    auto descending = LabelsPlanGroups(entries, false, L"None");
    expect(descending.names == V({L"Urgent", L"Red", L"Blue", L"None"}), L"descending reverses the labels, the untagged stay last");
    auto merged = LabelsPlanGroups({{L"red"}, {L"Red"}}, true, L"None");
    expect(merged.names == V({L"red"}) && merged.members[0] == std::vector<size_t>({0, 1}), L"labels that differ by case are one group");
    expect(LabelsPlanGroups({{}, {}}, true, L"None").names == V({L"None"}), L"all untagged is a single group");
    if (failure.empty()) {
        Wh_Log(L"LABELS GROUP SELFTEST pass");
    } else {
        Wh_Log(L"LABELS GROUP SELFTEST FAIL %s", failure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
