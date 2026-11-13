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

void LoadSettings() {
    g_enabled = Wh_GetIntSetting(L"columns.enabled") != 0;
    g_restore = Wh_GetIntSetting(L"columns.restoreOnOpen") != 0;
    g_sidebarRoot = Wh_GetIntSetting(L"columns.sidebarRoot") != 0;
    g_focusRect = Wh_GetIntSetting(L"columns.focusRect") != 0;
    g_compactScroll = Wh_GetIntSetting(L"columns.compactScrollbars") != 0;
    g_doubleClickUp = Wh_GetIntSetting(L"columns.doubleClickUp") != 0;
    g_headerKeyboard = Wh_GetIntSetting(L"columns.headerKeyboard") != 0;
    g_archives = Wh_GetIntSetting(L"columns.archives") != 0;
    g_remoteMarks = Wh_GetIntSetting(L"columns.remoteMarks") != 0;
    g_desktopRealOnly = Wh_GetIntSetting(L"columns.desktopRealOnly") != 0;
    {
        const wchar_t* mode = Wh_GetStringSetting(L"columns.background");
        g_backgroundMode = mode && wcscmp(mode, L"theme") == 0 ? 1
                           : mode && wcscmp(mode, L"transparent") == 0 ? 2
                                                                       : 0;
        if (mode) {
            Wh_FreeStringSetting(mode);
        }
    }
    g_styleVersion++;
    int minWidth = Wh_GetIntSetting(L"columns.minWidth");
    int maxWidth = Wh_GetIntSetting(L"columns.maxWidth");
    minWidth = minWidth < 80 ? 80 : (minWidth > 800 ? 800 : minWidth);
    maxWidth = maxWidth < minWidth ? minWidth : (maxWidth > 1200 ? 1200 : maxWidth);
    g_minWidth = minWidth;
    g_maxWidth = maxWidth;
    g_follow = Wh_GetIntSetting(L"columns.followActiveColumn") != 0;
    int maxItems = Wh_GetIntSetting(L"columns.maxItems");
    g_maxItems = maxItems < 50 ? 50 : (maxItems > 20000 ? 20000 : maxItems);
    g_debugLog = Wh_GetIntSetting(L"debugLog") != 0;
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
    Out(L"explorer-extras v0.9.3 start, compiled as %s, pid=%lu, columns=%d width=%d..%d follow=%d",
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
    // The worker first, so no result is queued after the windows are cleaned.
    StopWorker();
    CloseEverything();
}

void Wh_ModUninit() {
    UnregisterNotifyClass();
    Out(L"explorer-extras end");
}
