// Sidebar Locations: the self-test of the pure parts (grouping of the drives, the entry that contains a folder, the saved
// visibility). Debug build only (EXTRAS_VERBOSE); runs once when the mod loads and writes one line to the log:
// "LOCATIONS SELFTEST pass", or "LOCATIONS SELFTEST FAIL <what>". tools/smoke-test-explorer-extras.ps1 reads that line.
#ifdef EXTRAS_VERBOSE

void SidebarLocationsSelfTest() {
    std::wstring failure;
    auto expect = [&](bool ok, const wchar_t* what) {
        if (!ok && failure.empty()) {
            failure = what;
        }
    };

    // The pure function, not the user's setting (a user who chose "List drives without a group" would fail the test).
    expect(!LocationsGroupedFor(0, false) && !LocationsGroupedFor(1, false) && !LocationsGroupedFor(2, false), L"fewer than 3 drives stay flat");
    expect(LocationsGroupedFor(3, false) && LocationsGroupedFor(8, false), L"3 or more drives are grouped");
    expect(!LocationsGroupedFor(3, true) && !LocationsGroupedFor(8, true), L"the flat-only setting keeps every drive flat");

    std::vector<LocationPath> entries = {
        {L"drive.C", L"C:\\"},
        {L"documents", L"C:\\Users\\me\\Documents"},
        {L"home", L""},  // no folder on disk: never contains anything
    };
    expect(SidebarEntryIn(entries, L"C:\\Users\\me\\Documents\\Taxes\\2025") == L"documents", L"the closest entry wins over the drive");
    expect(SidebarEntryIn(entries, L"C:\\Users\\me\\Documents") == L"documents", L"the folder itself is its entry");
    expect(SidebarEntryIn(entries, L"c:\\users\\ME\\documents\\x") == L"documents", L"paths compare without case");
    expect(SidebarEntryIn(entries, L"C:\\Temp") == L"drive.C", L"a folder with no closer entry falls to its drive");
    expect(SidebarEntryIn(entries, L"C:\\Users\\me\\DocumentsOld") == L"drive.C", L"a folder that only starts with the same letters is not inside");
    expect(SidebarEntryIn(entries, L"C:\\") == L"drive.C", L"the drive root is the drive");
    expect(SidebarEntryIn(entries, L"Z:\\x").empty(), L"a folder on no listed drive has no entry");
    expect(SidebarEntryIn(entries, L"").empty(), L"no path, no entry");
    std::reverse(entries.begin(), entries.end());
    expect(SidebarEntryIn(entries, L"C:\\Users\\me\\Documents\\a") == L"documents", L"the order of the entries does not matter");

    const wchar_t* key = L"selftest.location";
    expect(LocationVisible(key), L"an entry is visible until hidden");
    LocationSetVisible(key, false);
    expect(!LocationVisible(key), L"hiding is saved");
    LocationSetVisible(key, true);
    expect(LocationVisible(key), L"showing again is saved");
    Wh_DeleteValue((std::wstring(L"sidebar.loc.") + key).c_str());

    if (failure.empty()) {
        Wh_Log(L"LOCATIONS SELFTEST pass");
    } else {
        Wh_Log(L"LOCATIONS SELFTEST FAIL %s", failure.c_str());
    }
}

#endif  // EXTRAS_VERBOSE
