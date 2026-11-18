// The self-test of the recent tags (debug build only): "LABELS RECENT SELFTEST pass" or "LABELS RECENT SELFTEST FAIL <what>".
#ifdef EXTRAS_VERBOSE

void LabelsRecentSelfTest() {
    std::wstring failure;
    auto expect = [&](bool ok, const wchar_t* what) {
        if (!ok && failure.empty()) {
            failure = what;
        }
    };
    using L = std::vector<LabelTag>;
    auto names = [](L const& list) {
        std::wstring text;
        for (auto const& tag : list) {
            text += std::to_wstring(tag.colour) + L":" + tag.name + L";";
        }
        return text;
    };
    L list = LabelsRecentPush({}, LabelTag{1, L"Urgent"});
    expect(names(list) == L"1:Urgent;", L"a named tag is remembered");
    list = LabelsRecentPush(list, LabelTag{3, L"Later"});
    expect(names(list) == L"3:Later;1:Urgent;", L"the newest is first");
    list = LabelsRecentPush(list, LabelTag{1, L"urgent"});
    expect(names(list) == L"1:urgent;3:Later;", L"a tag used again moves to the front, once, whatever the case");
    list = LabelsRecentPush(list, LabelTag{2, L"urgent"});
    expect(names(list) == L"2:urgent;1:urgent;3:Later;", L"the same name in another colour is another tag");
    list = LabelsRecentPush(list, LabelTag{4, L""});
    expect(list.size() == 3, L"a tag with no name is not remembered");
    L many;
    for (int i = 0; i < 12; i++) {
        many = LabelsRecentPush(many, LabelTag{0, L"t" + std::to_wstring(i)});
    }
    expect(many.size() == kLabelsRecentMax && many.front().name == L"t11" && many.back().name == L"t4",
           L"only the newest few are kept");
    if (failure.empty()) {
        Wh_Log(L"LABELS RECENT SELFTEST pass");
    } else {
        Wh_Log(L"LABELS RECENT SELFTEST FAIL %s", failure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
