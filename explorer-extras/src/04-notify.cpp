// ---- Change notifications: a hidden window per Explorer window receives
// the shell's "this folder changed" messages for every open column. ----

const UINT kNotifyMessage = WM_APP + 71;
const wchar_t kNotifyClass[] = L"ExplorerExtrasNotify";
std::atomic<bool> g_notifyClassRegistered{false};

LRESULT CALLBACK NotifyProc(HWND hwnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
    if (message == kNotifyMessage) {
        // The window's user data points at the time the columns should be
        // read again. Several changes in a row push it back.
        PIDLIST_ABSOLUTE* pidls = nullptr;
        LONG event = 0;
        bool ownLog = false;
        HANDLE lock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam,
                                                &pidls, &event);
        if (lock) {
            // The mod's own log file changes each time the mod logs. Reacting
            // to it would reload, log, and reload again without end.
            wchar_t path[MAX_PATH] = L"";
            if (pidls && pidls[0] && SHGetPathFromIDListW(pidls[0], path) &&
                g_outPath[0] && lstrcmpiW(path, g_outPath) == 0) {
                ownLog = true;
            }
            SHChangeNotification_Unlock(lock);
        }
        auto* due = (ULONGLONG*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (due && !ownLog) {
            *due = GetTickCount64() + 200;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateNotifyWindow(ULONGLONG* due) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!g_notifyClassRegistered.exchange(true)) {
        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = NotifyProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kNotifyClass;
        RegisterClassW(&windowClass);
    }
    HWND hwnd = CreateWindowExW(0, kNotifyClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, instance, nullptr);
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)due);
    }
    return hwnd;
}

void UnregisterNotifyClass() {
    if (g_notifyClassRegistered.exchange(false)) {
        UnregisterClassW(kNotifyClass, GetModuleHandleW(nullptr));
    }
}

ULONG WatchFolder(HWND notifyWindow, PCIDLIST_ABSOLUTE folder) {
    if (!notifyWindow) {
        return 0;
    }
    SHChangeNotifyEntry entry = {folder, FALSE};
    return SHChangeNotifyRegister(
        notifyWindow,
        SHCNRF_ShellLevel | SHCNRF_InterruptLevel | SHCNRF_NewDelivery,
        SHCNE_CREATE | SHCNE_DELETE | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER |
            SHCNE_MKDIR | SHCNE_RMDIR | SHCNE_UPDATEDIR,
        kNotifyMessage, 1, &entry);
}

std::wstring PathOf(PCIDLIST_ABSOLUTE pidl) {
    PWSTR name = nullptr;
    std::wstring result;
    if (pidl && SUCCEEDED(SHGetNameFromIDList(
                    pidl, SIGDN_DESKTOPABSOLUTEEDITING, &name)) &&
        name) {
        result = name;
        CoTaskMemFree(name);
    }
    return result;
}

void OpenItem(HWND owner, PCIDLIST_ABSOLUTE pidl) {
    // A file on disk is opened by its path: an item of a synced folder such as
    // OneDrive has a pidl that ShellExecute handles badly (it does nothing).
    wchar_t path[MAX_PATH] = L"";
    bool byPath = SHGetPathFromIDListW(pidl, path) && path[0];
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.hwnd = owner;
    info.nShow = SW_SHOWNORMAL;
    if (byPath) {
        info.fMask = SEE_MASK_DEFAULT;
        info.lpFile = path;
    } else {
        info.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
        info.lpIDList = (void*)pidl;
    }
    bool ok = ShellExecuteExW(&info);
    Out(L"OpenItem %s: %s, gle=%lu, hInstApp=%llu", byPath ? L"by path" : L"by pidl",
        ok ? L"ok" : L"failed", ok ? 0 : GetLastError(), (unsigned long long)(ULONG_PTR)info.hInstApp);
}

