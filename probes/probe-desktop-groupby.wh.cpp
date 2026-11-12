// ==WindhawkMod==
// @id              probe-desktop-groupby
// @name            Probe: desktop group by
// @description     Diagnostic only. Tests whether the desktop accepts Explorer's own "Group by", through the shell's folder view interface.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -luuid -lshell32 -lshlwapi
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: desktop group by

Second desktop probe. The first one showed the desktop icon view is a virtual
list: Explorer supplies every item on demand, so a mod can't assign items to
list view groups from outside. This probe asks Explorer itself to group the
desktop, the way "Group by" works in a folder window.

**Report (always).** Logs the desktop's current grouping, folder flags, view
mode, and the name and position of every icon as Explorer reports them.

**Active test (off by default).** Sets "Group by: Type" on the desktop for a few
seconds, then restores the previous grouping and puts every icon back through
the shell's own positioning call.

**Restore first-test layout (off by default).** Only if the first probe moved
your icons: puts 8 icons back in two columns of four, the layout logged before
that test.

Take a screenshot of the desktop before the active test. Send back the whole
log, from `PROBE START` to `PROBE END`, and say what the desktop looked like
during the hold.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- activeTest: false
  $name: Run the active "Group by Type" test
  $description: Temporarily groups the desktop by type, then restores it. Off means report only.
- holdSeconds: 8
  $name: Seconds to keep grouping on
- restoreFirstTestLayout: false
  $name: Restore the layout logged by the first probe
  $description: Only if the first probe moved your icons. Needs exactly 8 icons on the desktop.
*/
// ==/WindhawkModSettings==

#include <initguid.h>

#include <windows.h>

#include <commctrl.h>
#include <exdisp.h>
#include <propkey.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <vector>

#ifndef LVM_GETGROUPCOUNT
#define LVM_GETGROUPCOUNT (LVM_FIRST + 152)
#endif
#ifndef LVM_ISGROUPVIEWENABLED
#define LVM_ISGROUPVIEWENABLED (LVM_FIRST + 175)
#endif

namespace {

// SID_STopLevelBrowser, spelled out so the build doesn't depend on which
// header declares it.
const GUID kSidTopLevelBrowser = {
    0x4C96BE40, 0x915C, 0x11CF, {0x99, 0xD3, 0x00, 0xAA, 0x00, 0x4A, 0xE8, 0x37}};

HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;

template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p) {
            p->Release();
        }
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct SavedItem {
    PITEMID_CHILD pidl = nullptr;
    POINT pos = {};
};

void FreeItems(std::vector<SavedItem>& items) {
    for (auto& item : items) {
        if (item.pidl) {
            CoTaskMemFree(item.pidl);
            item.pidl = nullptr;
        }
    }
    items.clear();
}

HRESULT GetDesktopFolderView(IFolderView2** out) {
    *out = nullptr;

    Com<IShellWindows> shellWindows;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&shellWindows));
    Wh_Log(L"  CoCreateInstance(ShellWindows) hr=0x%08lX", hr);
    if (FAILED(hr)) {
        return hr;
    }

    VARIANT loc;
    VariantInit(&loc);
    loc.vt = VT_I4;
    loc.lVal = CSIDL_DESKTOP;
    VARIANT empty;
    VariantInit(&empty);
    long hwnd = 0;
    Com<IDispatch> dispatch;
    hr = shellWindows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd,
                                    SWFO_NEEDDISPATCH, &dispatch);
    Wh_Log(L"  FindWindowSW(desktop) hr=0x%08lX hwnd=0x%lX", hr, hwnd);
    if (hr != S_OK || !dispatch) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    Com<IServiceProvider> provider;
    hr = dispatch->QueryInterface(IID_PPV_ARGS(&provider));
    if (FAILED(hr)) {
        Wh_Log(L"  IServiceProvider hr=0x%08lX", hr);
        return hr;
    }

    Com<IShellBrowser> browser;
    hr = provider->QueryService(kSidTopLevelBrowser, IID_PPV_ARGS(&browser));
    Wh_Log(L"  QueryService(STopLevelBrowser) hr=0x%08lX", hr);
    if (FAILED(hr)) {
        return hr;
    }

    Com<IShellView> view;
    hr = browser->QueryActiveShellView(&view);
    Wh_Log(L"  QueryActiveShellView hr=0x%08lX", hr);
    if (FAILED(hr)) {
        return hr;
    }

    hr = view->QueryInterface(IID_PPV_ARGS(out));
    Wh_Log(L"  QueryInterface(IFolderView2) hr=0x%08lX", hr);
    return hr;
}

void LogGroupBy(IFolderView2* view, const wchar_t* label) {
    PROPERTYKEY key = {};
    BOOL ascending = FALSE;
    HRESULT hr = view->GetGroupBy(&key, &ascending);
    WCHAR guid[64] = L"";
    StringFromGUID2(key.fmtid, guid, ARRAYSIZE(guid));
    Wh_Log(L"  %s: GetGroupBy hr=0x%08lX key=%s pid=%lu ascending=%d", label,
           hr, guid, key.pid, ascending);
}

HWND FindDesktopListView() {
    HWND top = FindWindowW(L"Progman", nullptr);
    HWND defView =
        top ? FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)
            : nullptr;
    return defView
               ? FindWindowExW(defView, nullptr, L"SysListView32", nullptr)
               : nullptr;
}

void LogListViewGroupState(const wchar_t* label) {
    HWND lv = FindDesktopListView();
    if (!lv) {
        Wh_Log(L"  %s: list view not found", label);
        return;
    }
    DWORD_PTR enabled = 0, groups = 0, items = 0;
    SendMessageTimeoutW(lv, LVM_ISGROUPVIEWENABLED, 0, 0, SMTO_ABORTIFHUNG,
                        3000, &enabled);
    SendMessageTimeoutW(lv, LVM_GETGROUPCOUNT, 0, 0, SMTO_ABORTIFHUNG, 3000,
                        &groups);
    SendMessageTimeoutW(lv, LVM_GETITEMCOUNT, 0, 0, SMTO_ABORTIFHUNG, 3000,
                        &items);
    Wh_Log(L"  %s: list view group view=%d groups=%d items=%d", label,
           (int)enabled, (int)groups, (int)items);
}

// Reads every icon's id and position through the shell. `log` also prints each
// one with its display name.
bool ReadItems(IFolderView2* view, std::vector<SavedItem>& items, bool log) {
    int count = 0;
    HRESULT hr = view->ItemCount(SVGIO_ALLVIEW, &count);
    if (FAILED(hr) || count < 0 || count > 2000) {
        Wh_Log(L"  ItemCount hr=0x%08lX count=%d", hr, count);
        return false;
    }

    Com<IShellFolder> folder;
    view->GetFolder(IID_PPV_ARGS(&folder));

    for (int i = 0; i < count; i++) {
        SavedItem item;
        hr = view->Item(i, &item.pidl);
        if (FAILED(hr) || !item.pidl) {
            Wh_Log(L"  Item(%d) hr=0x%08lX", i, hr);
            FreeItems(items);
            return false;
        }
        HRESULT hrPos = view->GetItemPosition(item.pidl, &item.pos);
        if (log) {
            WCHAR name[128] = L"?";
            STRRET str = {};
            if (folder && SUCCEEDED(folder->GetDisplayNameOf(
                              item.pidl, SHGDN_NORMAL, &str))) {
                StrRetToBufW(&str, item.pidl, name, ARRAYSIZE(name));
            }
            Wh_Log(L"  item %d pos=(%ld,%ld) hr=0x%08lX name=\"%s\"", i,
                   item.pos.x, item.pos.y, hrPos, name);
        }
        items.push_back(item);
    }
    return true;
}

// Moves the given icons with the shell's own call and reports how many ended
// up where asked.
void PositionItems(IFolderView2* view, std::vector<SavedItem>& items) {
    if (items.empty()) {
        return;
    }
    std::vector<PCUITEMID_CHILD> pidls;
    std::vector<POINT> points;
    for (auto& item : items) {
        pidls.push_back(item.pidl);
        points.push_back(item.pos);
    }
    HRESULT hr = view->SelectAndPositionItems((UINT)pidls.size(), pidls.data(),
                                              points.data(), SVSI_POSITIONITEM);
    Wh_Log(L"  SelectAndPositionItems hr=0x%08lX", hr);

    int same = 0, differ = 0;
    for (auto& item : items) {
        POINT now = {};
        if (SUCCEEDED(view->GetItemPosition(item.pidl, &now)) &&
            now.x == item.pos.x && now.y == item.pos.y) {
            same++;
        } else {
            differ++;
            Wh_Log(L"  NOT restored: now (%ld,%ld), wanted (%ld,%ld)", now.x,
                   now.y, item.pos.x, item.pos.y);
        }
    }
    Wh_Log(L"  positions: %d match, %d differ", same, differ);
}

void RestoreFirstTestLayout(IFolderView2* view) {
    // The eight positions the first probe logged before its test, by index.
    static const POINT kLogged[8] = {
        {39, 2},  {39, 177},  {39, 352},  {39, 527},
        {201, 2}, {201, 177}, {201, 352}, {201, 527},
    };
    Wh_Log(L"RESTORE FIRST-TEST LAYOUT START");
    std::vector<SavedItem> items;
    if (!ReadItems(view, items, false)) {
        Wh_Log(L"RESTORE ABORTED: can't read the items");
        return;
    }
    if (items.size() != ARRAYSIZE(kLogged)) {
        Wh_Log(L"RESTORE ABORTED: %d icons on the desktop, the logged layout has 8",
               (int)items.size());
        FreeItems(items);
        return;
    }
    for (size_t i = 0; i < items.size(); i++) {
        items[i].pos = kLogged[i];
    }
    PositionItems(view, items);
    FreeItems(items);
    Wh_Log(L"RESTORE FIRST-TEST LAYOUT END");
}

void ActiveTest(IFolderView2* view, int holdSeconds) {
    Wh_Log(L"ACTIVE TEST START");

    std::vector<SavedItem> saved;
    if (!ReadItems(view, saved, false)) {
        Wh_Log(L"ACTIVE TEST ABORTED: can't read the items");
        return;
    }
    PROPERTYKEY savedKey = {};
    BOOL savedAscending = TRUE;
    HRESULT hr = view->GetGroupBy(&savedKey, &savedAscending);
    if (FAILED(hr)) {
        Wh_Log(L"ACTIVE TEST ABORTED: GetGroupBy hr=0x%08lX", hr);
        FreeItems(saved);
        return;
    }

    hr = view->SetGroupBy(PKEY_ItemTypeText, TRUE);
    Wh_Log(L"  SetGroupBy(ItemTypeText) hr=0x%08lX", hr);
    LogGroupBy(view, L"after set");
    LogListViewGroupState(L"after set");
    Wh_Log(L"  LOOK AT THE DESKTOP NOW: holding for %d seconds", holdSeconds);

    WaitForSingleObject(g_stopEvent, (DWORD)holdSeconds * 1000);

    hr = view->SetGroupBy(savedKey, savedAscending);
    Wh_Log(L"  restore SetGroupBy hr=0x%08lX", hr);
    LogGroupBy(view, L"after restore");
    LogListViewGroupState(L"after restore");
    PositionItems(view, saved);

    FreeItems(saved);
    Wh_Log(L"ACTIVE TEST END");
}

void RunProbe() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    DWORD pid = 0;
    if (progman) {
        GetWindowThreadProcessId(progman, &pid);
    }
    if (pid != GetCurrentProcessId()) {
        Wh_Log(L"The desktop belongs to pid %lu, not this process. Nothing to do here.",
               pid);
        return;
    }

    Com<IFolderView2> view;
    if (FAILED(GetDesktopFolderView(&view)) || !view) {
        Wh_Log(L"Can't reach the desktop's folder view.");
        return;
    }

    LogGroupBy(view.p, L"current");
    DWORD flags = 0;
    HRESULT hr = view->GetCurrentFolderFlags(&flags);
    Wh_Log(L"  GetCurrentFolderFlags hr=0x%08lX flags=0x%08lX autoArrange=%d snapToGrid=%d",
           hr, flags, (flags & FWF_AUTOARRANGE) ? 1 : 0,
           (flags & FWF_SNAPTOGRID) ? 1 : 0);
    FOLDERVIEWMODE mode = FVM_AUTO;
    int iconSize = 0;
    hr = view->GetViewModeAndIconSize(&mode, &iconSize);
    Wh_Log(L"  GetViewModeAndIconSize hr=0x%08lX mode=%d iconSize=%d", hr,
           (int)mode, iconSize);
    LogListViewGroupState(L"current");

    std::vector<SavedItem> items;
    ReadItems(view.p, items, true);
    FreeItems(items);

    if (Wh_GetIntSetting(L"restoreFirstTestLayout")) {
        RestoreFirstTestLayout(view.p);
    }

    if (Wh_GetIntSetting(L"activeTest")) {
        int hold = Wh_GetIntSetting(L"holdSeconds");
        if (hold < 2) hold = 2;
        if (hold > 30) hold = 30;
        ActiveTest(view.p, hold);
    } else {
        Wh_Log(L"Active test is off. Report only.");
    }
}

DWORD WINAPI ProbeThread(LPVOID) {
    if (WaitForSingleObject(g_stopEvent, 1500) == WAIT_OBJECT_0) {
        return 0;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    const wchar_t* arch = L"arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif

    Wh_Log(L"PROBE START probe-desktop-groupby v0.1 compiled as %s, pid=%lu",
           arch, GetCurrentProcessId());

    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Wh_Log(L"  CoInitializeEx hr=0x%08lX", hrInit);
    if (SUCCEEDED(hrInit)) {
        // Braces so every COM object is released before CoUninitialize.
        { RunProbe(); }
        CoUninitialize();
    }

    Wh_Log(L"PROBE END");
    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return g_stopEvent != nullptr;
}

void Wh_ModAfterInit() {
    g_thread = CreateThread(nullptr, 0, ProbeThread, nullptr, 0, nullptr);
}

// A changed setting reloads the mod, which runs the probe again.
BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    *bReload = TRUE;
    return TRUE;
}

void Wh_ModBeforeUninit() {
    // Ends the hold early. The thread restores the desktop before it exits.
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

void Wh_ModUninit() {
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}
