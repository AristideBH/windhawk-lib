// ==WindhawkMod==
// @id              probe-desktop-listview
// @name            Probe: desktop list view
// @description     Diagnostic only. Reports what the desktop icon view is and whether it accepts native list view groups.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: desktop list view

A diagnostic mod for the desktop grouping idea. It changes nothing unless you
turn on the active test.

**Report (always).** Logs the window chain of the desktop icon view, its
styles, its view mode, the item count, and whether group view is on.

**Active test (off by default).** Adds two temporary groups, assigns the icons
to them, turns group view on for a few seconds so you can look at the desktop,
then puts everything back: group view off, groups removed, icon positions
restored.

The active test can leave icons in a different place if the restore fails.
Positions are logged before the test so they can be compared afterwards. Take a
screenshot of the desktop first.

Send back the whole log, from `PROBE START` to `PROBE END`.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- activeTest: false
  $name: Run the active group test
  $description: Temporarily turns on group view on the desktop, then restores it. Off means report only.
- holdSeconds: 8
  $name: Seconds to keep group view on
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <commctrl.h>

#include <vector>

#ifndef LVM_GETVIEW
#define LVM_GETVIEW (LVM_FIRST + 143)
#endif
#ifndef LVM_INSERTGROUP
#define LVM_INSERTGROUP (LVM_FIRST + 145)
#endif
#ifndef LVM_REMOVEGROUP
#define LVM_REMOVEGROUP (LVM_FIRST + 150)
#endif
#ifndef LVM_GETGROUPCOUNT
#define LVM_GETGROUPCOUNT (LVM_FIRST + 152)
#endif
#ifndef LVM_ENABLEGROUPVIEW
#define LVM_ENABLEGROUPVIEW (LVM_FIRST + 157)
#endif
#ifndef LVM_ISGROUPVIEWENABLED
#define LVM_ISGROUPVIEWENABLED (LVM_FIRST + 175)
#endif

namespace {

constexpr int kGroupA = 9001;
constexpr int kGroupB = 9002;
constexpr DWORD kMsgTimeoutMs = 3000;

HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;

// Every message goes through a timeout so a busy or hung desktop can't block
// this thread, and through it the mod's unload, forever.
bool Send(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* out) {
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(hWnd, msg, wParam, lParam,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, kMsgTimeoutMs,
                             &result)) {
        Wh_Log(L"  message 0x%X timed out or failed, gle=%lu", msg,
               GetLastError());
        return false;
    }
    if (out) {
        *out = (LRESULT)result;
    }
    return true;
}

HWND FindListViewUnder(HWND top) {
    if (!top) {
        return nullptr;
    }
    HWND defView = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!defView) {
        return nullptr;
    }
    return FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
}

HWND FindDesktopListView() {
    HWND lv = FindListViewUnder(FindWindowW(L"Progman", nullptr));
    HWND worker = nullptr;
    while (!lv &&
           (worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr))) {
        lv = FindListViewUnder(worker);
    }
    return lv;
}

void LogWindowChain(HWND hWnd) {
    int depth = 0;
    for (HWND w = hWnd; w && depth < 8; w = GetAncestor(w, GA_PARENT)) {
        WCHAR cls[128] = L"";
        WCHAR title[128] = L"";
        GetClassNameW(w, cls, ARRAYSIZE(cls));
        GetWindowTextW(w, title, ARRAYSIZE(title));
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(w, &pid);
        Wh_Log(L"  chain[%d] hwnd=%p class=%s title=\"%s\" pid=%lu tid=%lu",
               depth, w, cls, title, pid, tid);
        depth++;
    }
}

void Report(HWND lv) {
    LONG_PTR style = GetWindowLongPtrW(lv, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(lv, GWL_EXSTYLE);
    Wh_Log(L"  style=0x%08IX exStyle=0x%08IX", (size_t)style, (size_t)exStyle);
    Wh_Log(L"  LVS type bits=%d (0 icon, 1 report, 2 small icon, 3 list)",
           (int)(style & LVS_TYPEMASK));
    Wh_Log(L"  LVS_AUTOARRANGE=%d LVS_OWNERDATA=%d LVS_OWNERDRAWFIXED=%d",
           (style & LVS_AUTOARRANGE) ? 1 : 0, (style & LVS_OWNERDATA) ? 1 : 0,
           (style & LVS_OWNERDRAWFIXED) ? 1 : 0);

    LRESULT r = 0;
    if (Send(lv, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0, &r)) {
        Wh_Log(L"  list view extended style=0x%08IX", (size_t)r);
    }
    if (Send(lv, LVM_GETVIEW, 0, 0, &r)) {
        Wh_Log(L"  LVM_GETVIEW=%d (0 icon, 1 details, 2 small icon, 3 list, 4 tile)",
               (int)r);
    }
    if (Send(lv, LVM_GETITEMCOUNT, 0, 0, &r)) {
        Wh_Log(L"  item count=%d", (int)r);
    }
    if (Send(lv, LVM_ISGROUPVIEWENABLED, 0, 0, &r)) {
        Wh_Log(L"  group view enabled=%d", (int)r);
    }
    if (Send(lv, LVM_GETGROUPCOUNT, 0, 0, &r)) {
        Wh_Log(L"  group count=%d", (int)r);
    }
}

bool GetItemGroup(HWND lv, int index, int* groupId) {
    LVITEMW item = {};
    item.mask = LVIF_GROUPID;
    item.iItem = index;
    LRESULT r = 0;
    if (!Send(lv, LVM_GETITEMW, 0, (LPARAM)&item, &r) || !r) {
        return false;
    }
    *groupId = item.iGroupId;
    return true;
}

bool SetItemGroup(HWND lv, int index, int groupId) {
    LVITEMW item = {};
    item.mask = LVIF_GROUPID;
    item.iItem = index;
    item.iGroupId = groupId;
    LRESULT r = 0;
    return Send(lv, LVM_SETITEMW, 0, (LPARAM)&item, &r) && r;
}

int InsertGroup(HWND lv, int groupId, const wchar_t* header) {
    LVGROUP group = {};
    group.cbSize = sizeof(group);
    group.mask = LVGF_HEADER | LVGF_GROUPID;
    group.pszHeader = const_cast<LPWSTR>(header);
    group.iGroupId = groupId;
    LRESULT r = -1;
    if (!Send(lv, LVM_INSERTGROUP, (WPARAM)-1, (LPARAM)&group, &r)) {
        return -1;
    }
    return (int)r;
}

void ActiveTest(HWND lv, int holdSeconds) {
    Wh_Log(L"ACTIVE TEST START");

    LRESULT r = 0;
    if (!Send(lv, LVM_GETITEMCOUNT, 0, 0, &r)) {
        Wh_Log(L"ACTIVE TEST ABORTED: can't read the item count");
        return;
    }
    int count = (int)r;
    if (count <= 0 || count > 2000) {
        Wh_Log(L"ACTIVE TEST ABORTED: item count %d is outside 1..2000", count);
        return;
    }

    // Save everything this test changes.
    std::vector<POINT> positions(count);
    std::vector<int> groupIds(count, I_GROUPIDNONE);
    for (int i = 0; i < count; i++) {
        LRESULT ok = 0;
        if (!Send(lv, LVM_GETITEMPOSITION, i, (LPARAM)&positions[i], &ok) ||
            !ok) {
            Wh_Log(L"ACTIVE TEST ABORTED: can't read position of item %d", i);
            return;
        }
        GetItemGroup(lv, i, &groupIds[i]);
        Wh_Log(L"  saved item %d pos=(%ld,%ld) group=%d", i, positions[i].x,
               positions[i].y, groupIds[i]);
    }
    LRESULT wasEnabled = 0;
    Send(lv, LVM_ISGROUPVIEWENABLED, 0, 0, &wasEnabled);

    // The change under test.
    int idxA = InsertGroup(lv, kGroupA, L"Windhawk probe group A");
    int idxB = InsertGroup(lv, kGroupB, L"Windhawk probe group B");
    Wh_Log(L"  LVM_INSERTGROUP A -> %d, B -> %d (-1 means refused)", idxA,
           idxB);

    int assigned = 0;
    for (int i = 0; i < count; i++) {
        if (SetItemGroup(lv, i, i < (count + 1) / 2 ? kGroupA : kGroupB)) {
            assigned++;
        }
    }
    Wh_Log(L"  assigned a group to %d of %d items", assigned, count);

    LRESULT enableResult = -1;
    Send(lv, LVM_ENABLEGROUPVIEW, TRUE, 0, &enableResult);
    LRESULT nowEnabled = 0;
    Send(lv, LVM_ISGROUPVIEWENABLED, 0, 0, &nowEnabled);
    LRESULT groupCount = 0;
    Send(lv, LVM_GETGROUPCOUNT, 0, 0, &groupCount);
    Wh_Log(L"  LVM_ENABLEGROUPVIEW -> %d (1 changed, 0 already set, -1 failed)",
           (int)enableResult);
    Wh_Log(L"  after enable: group view enabled=%d group count=%d",
           (int)nowEnabled, (int)groupCount);
    Wh_Log(L"  LOOK AT THE DESKTOP NOW: holding for %d seconds", holdSeconds);

    WaitForSingleObject(g_stopEvent, (DWORD)holdSeconds * 1000);

    // Put everything back, in reverse order.
    Send(lv, LVM_ENABLEGROUPVIEW, wasEnabled ? TRUE : FALSE, 0, nullptr);
    for (int i = 0; i < count; i++) {
        SetItemGroup(lv, i, groupIds[i]);
    }
    LRESULT removedA = -1, removedB = -1;
    Send(lv, LVM_REMOVEGROUP, kGroupA, 0, &removedA);
    Send(lv, LVM_REMOVEGROUP, kGroupB, 0, &removedB);

    int restored = 0, moved = 0;
    for (int i = 0; i < count; i++) {
        Send(lv, LVM_SETITEMPOSITION32, i, (LPARAM)&positions[i], nullptr);
        POINT now = {};
        LRESULT ok = 0;
        if (Send(lv, LVM_GETITEMPOSITION, i, (LPARAM)&now, &ok) && ok) {
            if (now.x == positions[i].x && now.y == positions[i].y) {
                restored++;
            } else {
                moved++;
                Wh_Log(L"  item %d NOT restored: now (%ld,%ld), was (%ld,%ld)",
                       i, now.x, now.y, positions[i].x, positions[i].y);
            }
        }
    }
    Send(lv, LVM_ISGROUPVIEWENABLED, 0, 0, &nowEnabled);
    Wh_Log(L"  restore: group view enabled=%d, removed A=%d B=%d",
           (int)nowEnabled, (int)removedA, (int)removedB);
    Wh_Log(L"  restore: %d of %d positions match, %d differ", restored, count,
           moved);
    Wh_Log(L"ACTIVE TEST END");
}

DWORD WINAPI ProbeThread(LPVOID) {
    // Give Explorer a moment if the mod loaded at process start.
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

    Wh_Log(L"PROBE START probe-desktop-listview v0.1 compiled as %s, pid=%lu",
           arch, GetCurrentProcessId());

    HWND lv = FindDesktopListView();
    if (!lv) {
        Wh_Log(L"No SysListView32 found under Progman or any WorkerW.");
        Wh_Log(L"PROBE END");
        return 0;
    }

    LogWindowChain(lv);

    DWORD pid = 0;
    GetWindowThreadProcessId(lv, &pid);
    if (pid != GetCurrentProcessId()) {
        Wh_Log(L"The desktop belongs to pid %lu, not this process. Nothing to do here.",
               pid);
        Wh_Log(L"PROBE END");
        return 0;
    }

    Report(lv);

    if (Wh_GetIntSetting(L"activeTest")) {
        int hold = Wh_GetIntSetting(L"holdSeconds");
        if (hold < 2) hold = 2;
        if (hold > 30) hold = 30;
        ActiveTest(lv, hold);
    } else {
        Wh_Log(L"Active test is off. Report only.");
    }

    Wh_Log(L"PROBE END");
    return 0;
}

}  // namespace

BOOL Wh_ModInit() {
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        return FALSE;
    }
    return TRUE;
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
    // Ends the hold early. The thread then restores the desktop before it
    // exits, and every message it sends has a timeout.
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
