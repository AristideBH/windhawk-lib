// Sidebar Favorites: the self-test of pinning. Debug build only (EXTRAS_VERBOSE). It pins a folder of its own in the temp
// folder to Quick access through the shell, expects it in the list, unpins it and expects it gone, then logs one line:
// "FAVORITES SELFTEST pass", or "FAVORITES SELFTEST FAIL <what>". tools/smoke-test-explorer-extras.ps1 reads that line.
// It touches the user's real Quick access for a moment, so one Explorer process runs it at a time (a named mutex); the others
// log nothing under that name.
#ifdef EXTRAS_VERBOSE

namespace {

bool FavoritesTestHas(std::wstring const& path) {
    for (auto const& listed : FavoritesList()) {
        if (_wcsicmp(listed.c_str(), path.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

bool FavoritesTestWait(std::wstring const& path, bool present) {
    for (int i = 0; i < 20; i++) {
        if (FavoritesTestHas(path) == present) {
            return true;
        }
        Sleep(250);
    }
    return false;
}

void FavoritesSelfTestRun() {
    HANDLE once = CreateMutexW(nullptr, FALSE, L"Local\\ExplorerExtrasFavoritesSelfTest");
    if (!once || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (once) {
            CloseHandle(once);
        }
        Wh_Log(L"favorites pin test skipped: another process has it");
        return;
    }
    bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    for (auto const& listed : FavoritesList()) {
        if (listed.find(L"\\extras-fav-test-") != std::wstring::npos) {
            FavoritesUnpin(listed);  // Left by a test run that was cut short.
        }
    }
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring folder = std::wstring(temp) + L"extras-fav-test-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(folder.c_str(), nullptr);

    std::wstring failure;
    if (!FavoritesPin(folder)) {
        failure = L"pinning a folder did not work";
    } else if (!FavoritesTestWait(folder, true)) {
        failure = L"a pinned folder is not in the list";
    } else if (!FavoritesUnpin(folder)) {
        failure = L"unpinning a folder did not work";
    } else if (!FavoritesTestWait(folder, false)) {
        failure = L"an unpinned folder is still in the list";
    }
    if (!FavoritesTestHas(folder)) {
        // nothing left to undo
    } else {
        FavoritesUnpin(folder);  // Do not leave the test folder in the user's Quick access.
    }
    RemoveDirectoryW(folder.c_str());
    if (failure.empty()) {
        Wh_Log(L"FAVORITES SELFTEST pass");
    } else {
        Wh_Log(L"FAVORITES SELFTEST FAIL %s", failure.c_str());
    }
    if (com) {
        CoUninitialize();
    }
    ReleaseMutex(once);
    CloseHandle(once);
}

}  // namespace

void FavoritesSelfTest() {
    StartModThread([]() { FavoritesSelfTestRun(); });
}

#endif  // EXTRAS_VERBOSE
