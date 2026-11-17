// Labels: the self-test of where a dot goes (debug build only). One log line: "LABELS PLACE SELFTEST pass" or
// "LABELS PLACE SELFTEST FAIL <what>". The geometry is the one recorded from the real list in the surfaces skill.
#ifdef EXTRAS_VERBOSE

namespace {

std::wstring g_placeFailure;

void PlaceExpect(bool ok, const wchar_t* what) {
    if (!ok && g_placeFailure.empty()) {
        g_placeFailure = what;
    }
}

}  // namespace

void LabelsPlaceSelfTest() {
    PlaceExpect(LabelsBlockWidth(1, false, 8) == 8, L"one dot is one side wide");
    PlaceExpect(LabelsBlockWidth(3, false, 8) == 20, L"three stacked dots, each 6 px after the one before");
    PlaceExpect(LabelsBlockWidth(4, true, 8) == 35, L"four stacked dots, then a 4 px gap and a 5 px plus");
    PlaceExpect(LabelsDotStep(8) == 6 && LabelsDotStep(16) == 12, L"the step is 3/4 of a side");
    PlaceExpect(LabelsMoreWidth(8) == 5 && LabelsMoreWidth(16) == 10, L"the plus is small: 5/8 of a side, at least 5 px");

    LabelTag red{1, L""}, green{4, L"x"}, blue{6, L""};
    bool more = true;
    auto colours = LabelsDotColours({red, red, green}, &more);
    PlaceExpect(colours == std::vector<int>({1, 4}) && !more, L"equal colours make one dot");
    std::vector<LabelTag> many;
    for (uint8_t c = 0; c < 6; c++) {
        many.push_back(LabelTag{c, L""});
    }
    colours = LabelsDotColours(many, &more);
    PlaceExpect(colours.size() == 4 && more, L"more than four colours show four dots and a plus");
    PlaceExpect(LabelsDotColours({blue}, &more) == std::vector<int>({6}), L"a single colour");

    LabelsPlaceInput in = {};
    in.side = 8;
    in.blockWidth = 8;
    in.lineHeight = 16;

    // List: the 28 px gap in front of the icon, centred on the row.
    in.view = LabelsView::List;
    in.row = {0, 0, 165, 22};
    LabelsPlace place = LabelsPlaceFor(in);
    PlaceExpect(place.x == 7 && place.centreY == 11 && !place.corner, L"List: centred in the gap before the icon");

    // Details: right end of the name cell, unless the name nearly fills it.
    in.view = LabelsView::Details;
    in.row = {0, 0, 594, 19};
    in.cell = {22, 0, 252, 19};
    in.textLeft = 36;
    in.textRight = 80;
    place = LabelsPlaceFor(in);
    PlaceExpect(place.x == 238 && place.centreY == 9 && !place.corner, L"Details: right-aligned in the name cell");
    in.textRight = 240;
    place = LabelsPlaceFor(in);
    PlaceExpect(place.x == 238 && !place.corner, L"Details: a long name does not move the dot");

    // Tiles: after the last detail line, or at its start when it is empty.
    in.view = LabelsView::Tiles;
    in.cell = {55, 16, 246, 32};
    in.textLeft = 55;
    in.textRight = 100;
    place = LabelsPlaceFor(in);
    PlaceExpect(place.x == 106 && place.centreY == 26 && !place.corner, L"Tiles: after the detail text, a little below the middle of the line");
    in.textRight = in.textLeft - 1;
    PlaceExpect(LabelsPlaceFor(in).x == 55, L"Tiles: at the start of an empty line");

    // Content: the start of the second line of the text block.
    in.view = LabelsView::Content;
    in.cell = {66, 0, 911, 49};
    place = LabelsPlaceFor(in);
    PlaceExpect(place.x == 66 && place.centreY == 24, L"Content: start of the line below the name");
    in.cell = {121, 27, 258, 43};
    in.textLeft = 123;
    in.textRight = 255;
    place = LabelsPlaceFor(in);
    PlaceExpect(place.x == 261 && place.centreY == 37, L"Content: after the type text when there is one");
    in.textLeft = 0;
    in.textRight = -1;

    // The lines of a Tiles text block.
    PlaceExpect(LabelsLines(64, 32) == 2 && LabelsLines(96, 32) == 3 && LabelsLines(32, 32) == 1, L"lines in a text block");
    PlaceExpect(LabelsIsLastLine(420, 424, 32) && !LabelsIsLastLine(388, 424, 32) && !LabelsIsLastLine(356, 424, 32),
                L"only the bottom line is the last");

    // Icons: after the last line of the name, on a new line below when that line has no room.
    in.view = LabelsView::Icons;
    in.cell = {0, 98, 97, 114};  // The last line of the label.
    in.textLeft = 28;
    in.textRight = 69;
    in.iconLeft = 4;
    in.iconBottom = 98;
    place = LabelsPlaceFor(in);
    PlaceExpect(place.x == 75 && place.centreY == 106 && !place.corner, L"Icons: after the text of the last line");
    in.textLeft = 2;
    in.textRight = 95;
    place = LabelsPlaceFor(in);
    PlaceExpect(place.x == 44 && place.centreY == 122 && !place.corner, L"Icons: a full last line wraps the dot onto a new line, centred");

    if (g_placeFailure.empty()) {
        Wh_Log(L"LABELS PLACE SELFTEST pass");
    } else {
        Wh_Log(L"LABELS PLACE SELFTEST FAIL %s", g_placeFailure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
