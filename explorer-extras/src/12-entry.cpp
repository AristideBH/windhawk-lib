#endif

// Explorer can start this mod before it has loaded the module the mod hooks.
// In that case, wait for the module and hook it when it arrives.
std::atomic<bool> g_extensionsHooked{false};

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR name, HANDLE file, DWORD flags) {
    HMODULE module = LoadLibraryExW_Original(name, file, flags);
    const DWORD dataOnly = LOAD_LIBRARY_AS_DATAFILE |
                           LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE |
                           LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    if (module && !(flags & dataOnly) && !g_extensionsHooked && !g_unloading) {
        HMODULE extensions = GetModuleHandleW(L"FileExplorerExtensions.dll");
        if (extensions && !g_extensionsHooked.exchange(true)) {
            Wh_Log(L"FileExplorerExtensions.dll has loaded, hooking it now");
            HookExtensions(extensions);
            Wh_ApplyHookOperations();
            if (g_hooked) {
                Wh_Log(L"ready. Open the View menu of a File Explorer window.");
            }
        }
    }
    return module;
}

void LoadSettings() {
    g_enabled = SettingBool(L"enabled");
    g_restore = SettingBool(L"restoreOnOpen");
    g_sidebarRoot = SettingBool(L"sidebarRoot");
    g_focusRect = SettingBool(L"focusRect");
    g_doubleClickUp = SettingBool(L"doubleClickUp");
    g_lastEdge = SettingBool(L"lastEdge");
    g_headerKeyboard = SettingBool(L"headerKeyboard");
    g_keepStale = SettingBool(L"keepStale");
    g_harmonize = SettingBool(L"harmonizeWidths");
    g_foldersFirst = SettingBool(L"foldersFirst");
    g_oddRows = SettingBool(L"oddRows");
    g_previewImages = SettingBool(L"previewImages");
    g_previewMedia = SettingBool(L"previewMedia");
    g_previewMaxMb = SettingInt(L"previewMaxMb");
    g_previewMaxMp = SettingInt(L"previewMaxMp");
    g_paneVolume = Wh_GetIntValue(L"ui.paneVolume", 100);
    g_paneMuted = Wh_GetIntValue(L"ui.paneMuted", 0) != 0;
    g_staleAccent = SettingBool(L"staleAccent");
    g_autoPreview = SettingBool(L"autoPreview");
    g_springLoad = SettingBool(L"springLoad");
    g_rememberSort = SettingBool(L"rememberSort");
    g_rememberWidth = SettingBool(L"rememberWidth");
    g_rememberLayout = SettingBool(L"rememberLayout");
    g_remoteMarks = SettingBool(L"remoteMarks");
    g_desktopRealOnly = SettingBool(L"desktopRealOnly");
    {
        std::wstring mode = SettingText(L"background");
        g_backgroundMode = mode == L"theme" ? 1 : 0;
    }
    g_styleVersion++;
    int minWidth = SettingInt(L"minWidth");
    int maxWidth = std::max(SettingInt(L"maxWidth"), minWidth);
    g_minWidth = minWidth;
    g_maxWidth = maxWidth;
    g_follow = true;
    g_maxItems = SettingInt(L"maxItems");
    g_detailsPane = SettingBool(L"detailsPane");
    g_previewRedirect = SettingBool(L"previewRedirect");
    g_paneAnim = SettingBool(L"paneAnimation");
    g_panePreview = SettingBool(L"livePreview");
    {
        int height = Wh_GetIntValue(L"ui.paneHeight", 250);
        g_panePreviewHeight = height < 90 ? 90 : height > 520 ? 520 : height;
    }
}

}  // namespace

BOOL Wh_ModInit() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    LoadSettings();
#if defined(_M_ARM64) || defined(__aarch64__)
    const wchar_t* arch = L"arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif
    Wh_Log(L"explorer-extras v0.65.0 start, compiled as %s, pid=%lu, columns=%d width=%d..%d follow=%d",
        arch, GetCurrentProcessId(), g_enabled ? 1 : 0, g_minWidth.load(),
        g_maxWidth.load(), g_follow ? 1 : 0);

    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
        auto loadLibrary = kernelBase
            ? (decltype(&LoadLibraryExW))GetProcAddress(kernelBase, "LoadLibraryExW")
            : nullptr;
        BOOL waiting = loadLibrary &&
            WindhawkUtils::SetFunctionHook(loadLibrary, LoadLibraryExW_Hook, &LoadLibraryExW_Original);
        Wh_Log(L"FileExplorerExtensions.dll isn't loaded yet, waiting for it: %d",
            waiting ? 1 : 0);
        return TRUE;
    }
    g_extensionsHooked = true;
    HookExtensions(module);
    if (g_hooked) {
        Wh_Log(L"ready. Open the View menu of a File Explorer window.");
    } else {
        Wh_Log(L"no hook could be installed. The mod does nothing.");
    }
    return TRUE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    Wh_Log(L"settings changed: columns=%d width=%d..%d follow=%d maxItems=%d",
        g_enabled ? 1 : 0, g_minWidth.load(), g_maxWidth.load(),
        g_follow ? 1 : 0, g_maxItems.load());
    if (!g_enabled) {
        // Off means gone: the Columns entry and the panel go from every window. The details pane stays.
        if (g_detailsPane) {
            ColumnsOffEverywhere();
        } else {
            CloseEverything();
        }
    }
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    // Every thread of the mod ends first, so no result is queued after the windows are cleaned.
    JoinModThreads();
    StopWorker();
    CloseEverything();
}

void Wh_ModUninit() {
    CloseSettingsWindowAtUnload();
    UnregisterNotifyClass();
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    Wh_Log(L"explorer-extras end");
}
