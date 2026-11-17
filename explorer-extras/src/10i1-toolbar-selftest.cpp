// The self-test of the toolbar's margins (debug build only): "TOOLBAR SELFTEST pass" or "TOOLBAR SELFTEST FAIL <what>".
#ifdef EXTRAS_VERBOSE

void ToolbarSelfTest() {
    std::wstring failure;
    auto expect = [&](bool ok, const wchar_t* what) {
        if (!ok && failure.empty()) {
            failure = what;
        }
    };
    // Explorer gives each of the four buttons the margin 8,8,8,0: a gap of 16 between two of them, and 8 at each end.
    // The ends take the gap too (never more than Explorer's own), so the address bar gets the room.
    const mux::Thickness original{8, 8, 8, 0};
    auto first = ToolbarMarginFor(original, 0, 4, 0);
    auto second = ToolbarMarginFor(original, 1, 4, 0);
    auto last = ToolbarMarginFor(original, 3, 4, 0);
    expect(first.Left == 0 && first.Right == 0 && first.Top == 8 && first.Bottom == 0, L"gap 0: the first button has no margin");
    expect(second.Left == 0 && second.Right == 0, L"gap 0: a middle button has no margin");
    expect(last.Left == 0 && last.Right == 0, L"gap 0: the last button has no margin");
    auto spaced = ToolbarMarginFor(original, 1, 4, 2);
    expect(spaced.Left == 0 && spaced.Right == 2, L"the gap is the right margin of every button but the last");
    expect(ToolbarMarginFor(original, 0, 4, 2).Left == 2 && ToolbarMarginFor(original, 3, 4, 2).Right == 2, L"the ends take the gap");
    expect(ToolbarMarginFor(original, 0, 4, 16).Left == 8 && ToolbarMarginFor(original, 3, 4, 16).Right == 8,
           L"the ends never grow past Explorer's own margin");
    auto single = ToolbarMarginFor(original, 0, 1, 7);
    expect(single.Left == 7 && single.Right == 7, L"a lone button has the gap on both sides");
    if (failure.empty()) {
        Wh_Log(L"TOOLBAR SELFTEST pass");
    } else {
        Wh_Log(L"TOOLBAR SELFTEST FAIL %s", failure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
