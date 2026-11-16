// ---- Change notifications: a hidden window per Explorer window receives
// the shell's "this folder changed" messages for every open column. ----

const UINT kNotifyMessage = WM_APP + 71;
const wchar_t kNotifyClass[] = L"ExplorerExtrasNotify";
std::atomic<bool> g_notifyClassRegistered{false};
std::mutex g_notifyClassMutex;

LRESULT CALLBACK NotifyProc(HWND hwnd, UINT message, WPARAM wParam,
                            LPARAM lParam) {
    if (message == kNotifyMessage) {
        // The window's user data points at the time the columns should be
        // read again. Several changes in a row push it back.
        auto* due = (ULONGLONG*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (due) {
            *due = GetTickCount64() + 200;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateNotifyWindow(ULONGLONG* due) {
    HINSTANCE instance = ModHandle();
    {
        // Two windows opening together must not create one before the class exists.
        std::lock_guard<std::mutex> lock(g_notifyClassMutex);
        if (!g_notifyClassRegistered) {
            WNDCLASSW windowClass = {};
            windowClass.lpfnWndProc = NotifyProc;
            windowClass.hInstance = instance;
            windowClass.lpszClassName = kNotifyClass;
            // "Already exists" can only be a class an earlier load left behind, still pointing at its code.
            if (RegisterClassW(&windowClass)) {
                g_notifyClassRegistered = true;
            } else {
                Wh_Log(L"notify window: the class could not be registered, gle=%lu", GetLastError());
                return nullptr;
            }
        }
    }
    HWND hwnd = CreateWindowExW(0, kNotifyClass, L"", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, instance, nullptr);
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)due);
    }
    return hwnd;
}

void UnregisterNotifyClass() {
    std::lock_guard<std::mutex> lock(g_notifyClassMutex);
    if (g_notifyClassRegistered) {
        if (UnregisterClassW(kNotifyClass, ModHandle())) {
            g_notifyClassRegistered = false;
        } else {
            Wh_Log(L"notify window: the class is still in use, gle=%lu", GetLastError());
        }
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
    ModalScope modal;
    bool ok = ShellExecuteExW(&info);
    Wh_Log(L"OpenItem %s: %s, gle=%lu, hInstApp=%llu", byPath ? L"by path" : L"by pidl",
        ok ? L"ok" : L"failed", ok ? 0 : GetLastError(), (unsigned long long)(ULONG_PTR)info.hInstApp);
}

