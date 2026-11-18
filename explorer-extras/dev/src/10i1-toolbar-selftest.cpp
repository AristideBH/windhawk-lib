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
    auto first = ToolbarMarginFor(original, 0, 4, 0, 0);
    auto second = ToolbarMarginFor(original, 1, 4, 0, 0);
    auto last = ToolbarMarginFor(original, 3, 4, 0, 0);
    expect(first.Left == 0 && first.Right == 0 && first.Top == 8 && first.Bottom == 0, L"gap 0: the first button has no margin");
    expect(second.Left == 0 && second.Right == 0, L"gap 0: a middle button has no margin");
    expect(last.Left == 0 && last.Right == 0, L"gap 0: the last button has no margin");
    auto spaced = ToolbarMarginFor(original, 1, 4, 2, 2);
    expect(spaced.Left == 0 && spaced.Right == 2, L"the gap is the right margin of every button but the last");
    expect(ToolbarMarginFor(original, 0, 4, 2, 5).Left == 2, L"the first button's left margin is the gap");
    expect(ToolbarMarginFor(original, 3, 4, 2, 5).Right == 5, L"the last button's right margin is the gap to the address bar");
    expect(ToolbarMarginFor(original, 0, 4, 16, 16).Left == 8, L"the left end never grows past Explorer's own margin");
    expect(ToolbarMarginFor(original, 3, 4, 2, 12).Right == 12, L"the gap to the address bar can be wider than Explorer's");
    auto single = ToolbarMarginFor(original, 0, 1, 7, 3);
    expect(single.Left == 7 && single.Right == 3, L"a lone button has the gap on the left and the address bar gap on the right");
    // The command bar: Explorer's AppBarButtons have 4 or 2 on each side; the gap is shared by the two sides of a neighbour pair.
    auto cmd = ToolbarCommandMargin(mux::Thickness{4, 0, 4, 0}, 2);
    expect(cmd.Left == 1 && cmd.Right == 1 && cmd.Top == 0 && cmd.Bottom == 0, L"command bar: a gap of 2 is 1 on each side");
    expect(ToolbarCommandMargin(mux::Thickness{2, 0, 2, 0}, 16).Left == 2, L"command bar: never more than Explorer's own margin");
    expect(ToolbarCommandMargin(mux::Thickness{4, 0, 4, 0}, 0).Right == 0, L"command bar: a gap of 0 touches");
    // The search box's width: wanted, kept between 160 and what leaves the address bar 240 beside the navigation buttons.
    expect(ToolbarSearchWidth(300, 1100, 148) == 300, L"search width: a width inside the limits is kept");
    expect(ToolbarSearchWidth(50, 1100, 148) == 160, L"search width: not narrower than 160");
    expect(ToolbarSearchWidth(5000, 1100, 148) == 712, L"search width: not wider than the grid minus buttons minus 240");
    expect(ToolbarSearchWidth(300, 300, 148) == 160, L"search width: a tiny window gets the minimum, never a negative width");
    // The corner radius of the address bar and the search box: the setting when the area is on, Explorer's own when it is off.
    expect(ToolbarRadius(4, 12, true) == 12, L"radius: the setting is used when the area is on");
    expect(ToolbarRadius(4, 12, false) == 4, L"radius: Explorer's own comes back when the area is off");
    expect(ToolbarRadius(4, 99, true) == 16 && ToolbarRadius(4, -3, true) == 0, L"radius: kept between 0 and 16");
    if (failure.empty()) {
        Wh_Log(L"TOOLBAR SELFTEST pass");
    } else {
        Wh_Log(L"TOOLBAR SELFTEST FAIL %s", failure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
