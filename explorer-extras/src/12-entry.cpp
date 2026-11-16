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
            Out(L"FileExplorerExtensions.dll has loaded, hooking it now");
            HookExtensions(extensions);
            Wh_ApplyHookOperations();
            if (g_hooked) {
                StartWorker();
                Out(L"ready. Open the View menu of a File Explorer window.");
            }
        }
    }
    return module;
}

// The status bar switch of 0.54 is gone (Explorer has the option itself). If a test of it left
// Explorer's own "Show status bar" option off, it is given back, once.
void GiveBackStatusBar() {
    if (Wh_GetIntValue(L"ui.statusChanged", 0) == 0) {
        return;
    }
    DWORD original = (DWORD)Wh_GetIntValue(L"ui.statusOriginal", 1);
    Wh_SetIntValue(L"ui.statusChanged", 0);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", L"ShowStatusBar", REG_DWORD,
                    &original, sizeof(original));
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ShellState", SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG, 1500, &result);
}

void LoadSettings() {
    g_enabled = SettingBool(L"enabled");
    g_restore = SettingBool(L"restoreOnOpen");
    g_sidebarRoot = SettingBool(L"sidebarRoot");
    g_focusRect = SettingBool(L"focusRect");
    g_compactScroll = true;
    g_doubleClickUp = SettingBool(L"doubleClickUp");
    g_lastEdge = SettingBool(L"lastEdge");
    g_headerKeyboard = SettingBool(L"headerKeyboard");
    g_archives = true;
    g_dragOut = true;
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
    g_debugLog = SettingBool(L"debugLog");
    GiveBackStatusBar();
}

}  // namespace

BOOL Wh_ModInit() {
    LoadSettings();
    OutInit();
#if defined(_M_ARM64) || defined(__aarch64__)
    const wchar_t* arch = L"arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif
    Out(L"explorer-extras v0.54.9 start, compiled as %s, pid=%lu, columns=%d width=%d..%d follow=%d",
        arch, GetCurrentProcessId(), g_enabled ? 1 : 0, g_minWidth.load(),
        g_maxWidth.load(), g_follow ? 1 : 0);

    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
        void* loadLibrary = kernelBase
            ? (void*)GetProcAddress(kernelBase, "LoadLibraryExW")
            : nullptr;
        BOOL waiting = loadLibrary &&
            Wh_SetFunctionHook(loadLibrary, (void*)LoadLibraryExW_Hook,
                               (void**)&LoadLibraryExW_Original);
        Out(L"FileExplorerExtensions.dll isn't loaded yet, waiting for it: %d",
            waiting ? 1 : 0);
        return TRUE;
    }
    g_extensionsHooked = true;
    HookExtensions(module);
    if (g_hooked) {
        StartWorker();
        Out(L"ready. Open the View menu of a File Explorer window.");
    } else {
        Out(L"no hook could be installed. The mod does nothing.");
    }
    return TRUE;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    Out(L"settings changed: columns=%d width=%d..%d follow=%d maxItems=%d",
        g_enabled ? 1 : 0, g_minWidth.load(), g_maxWidth.load(),
        g_follow ? 1 : 0, g_maxItems.load());
    if (!g_enabled) {
        // Off means gone: take the entry and the panel out of every window.
        CloseEverything();
    }
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    for (int i = 0; i < 60 && (g_compressJobs > 0 || g_paneJobs > 0); i++) {
        Sleep(50);  // An archive being made by tar.exe, or details being read: the thread ends in this code.
    }
    // The worker first, so no result is queued after the windows are cleaned.
    StopWorker();
    CloseEverything();
}

void Wh_ModUninit() {
    CloseSettingsWindowAtUnload();
    UnregisterNotifyClass();
    Out(L"explorer-extras end");
}
