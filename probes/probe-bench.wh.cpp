// ==WindhawkMod==
// @id              probe-bench
// @name            Probe bench: column host, folder data, DirectUI paint
// @description     Diagnostic only. Three File Explorer tests in one mod: a WinUI 3 panel over the file list, reading the folder through the shell, and counting DirectUI paint calls.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe bench

Three tests in one mod, so one compile covers them all.

**1. Column host.** On a click in the command bar, the row with New, Sort and
View, the probe lays a WinUI 3 panel of its own over the file list and keeps it
in step when the window is resized. The panel has three columns, a Count button
and a Close button.

**2. Folder data.** When the panel opens, the probe asks the window's shell
browser for the current folder and its first items, and shows them in the first
column. It reads names only.

**3. DirectUI paint.** The probe hooks two functions that `dui70.dll` exports,
`Element::PaintBackground` and `Element::PaintContent`, and counts which
element classes go through them. This tells whether a mark can be drawn on a
file list item without symbol files.

Nothing in Explorer is changed. The panel goes away with its Close button, with
the setting, or when the mod is disabled.

The log goes to `probes\logs\probe-bench.log` in the repo. The probe also reads
`probes\control.txt` once a second. Lines such as `host=0`, `paint=0` or
`dump=1` in that file switch a test off or ask for a fresh paint summary,
without a trip through Windhawk's settings.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- showPanel: true
  $name: Show the test panel
  $description: Off removes the panel and makes the host test log only.
- countPaint: true
  $name: Count DirectUI paint calls
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

#undef GetCurrentTime

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cwchar>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef PROBE_NO_WINRT
#include <windhawk_utils.h>

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.h>

namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
namespace mud = winrt::Microsoft::UI::Dispatching;
#endif

namespace {

std::atomic<bool> g_showPanel{true};
std::atomic<bool> g_unloading{false};
std::atomic<int> g_hookCalls{0};
std::atomic<bool> g_paintOn{true};
std::atomic<bool> g_dumpNow{false};

// Every log line also goes to a file in the repo, so the log can be read from
// disk: %USERPROFILE%\Code\windhawk-lib\probes\logs\probe-bench.log
std::mutex g_outMutex;
wchar_t g_outPath[MAX_PATH] = L"";

void OutInit() {
    wchar_t profile[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return;
    }
    std::wstring dir = std::wstring(profile) + L"\\Code\\windhawk-lib\\probes\\logs";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\probe-bench.log";
    if (path.size() < MAX_PATH) {
        wcscpy_s(g_outPath, path.c_str());
        // Start each run with an empty file.
        HANDLE file = CreateFileW(g_outPath, GENERIC_WRITE, FILE_SHARE_READ,
                                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }
}

void Out(const wchar_t* format, ...) {
    wchar_t text[1200];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, ARRAYSIZE(text), _TRUNCATE, format, args);
    va_end(args);
    Wh_Log(L"%s", text);

    if (!g_outPath[0]) {
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t line[1300];
    int chars = _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE,
                             L"%02d:%02d:%02d.%03d %s\r\n", now.wHour,
                             now.wMinute, now.wSecond, now.wMilliseconds, text);
    if (chars < 0) {
        chars = (int)wcslen(line);
    }
    char utf8[4000];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, chars, utf8,
                                    sizeof(utf8), nullptr, nullptr);
    if (bytes <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_outMutex);
    HANDLE file = CreateFileW(g_outPath, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, (DWORD)bytes, &written, nullptr);
        CloseHandle(file);
    }
}


std::wstring ClassNameOf(HWND hwnd) {
    wchar_t name[128] = L"";
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    return name;
}

struct FindContext {
    const wchar_t* className = nullptr;
    HWND found = nullptr;
};

// First child of this class that is visible through all its parents.
BOOL CALLBACK FindVisibleChildProc(HWND hwnd, LPARAM lParam) {
    auto* context = (FindContext*)lParam;
    if (IsWindowVisible(hwnd) && ClassNameOf(hwnd) == context->className) {
        bool shown = true;
        for (HWND p = GetParent(hwnd); p; p = GetParent(p)) {
            if (!IsWindowVisible(p)) {
                shown = false;
                break;
            }
        }
        if (shown) {
            context->found = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

HWND FindVisibleChild(HWND parent, const wchar_t* className) {
    FindContext context;
    context.className = className;
    EnumChildWindows(parent, FindVisibleChildProc, (LPARAM)&context);
    return context.found;
}

HWND FindFileList(HWND topLevel) {
    return FindVisibleChild(topLevel, L"SHELLDLL_DefView");
}

BOOL CALLBACK FindCabinetOnThread(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd) && ClassNameOf(hwnd) == L"CabinetWClass") {
        ((FindContext*)lParam)->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

// The Explorer window the click happened in.
HWND FindTopLevel() {
    HWND foreground = GetForegroundWindow();
    if (foreground && ClassNameOf(foreground) == L"CabinetWClass") {
        return foreground;
    }
    FindContext context;
    EnumThreadWindows(GetCurrentThreadId(), FindCabinetOnThread,
                      (LPARAM)&context);
    return context.found;
}

// The file list's rectangle in the top-level window's client coordinates.
bool FileListRect(HWND topLevel, HWND fileList, RECT* rect) {
    if (!GetWindowRect(fileList, rect)) {
        return false;
    }
    MapWindowPoints(nullptr, topLevel, (POINT*)rect, 2);
    return rect->right > rect->left && rect->bottom > rect->top;
}

// ---------------------------------------------------------------------------
// Test 2: folder data through the shell browser
// ---------------------------------------------------------------------------

struct FolderInfo {
    bool ok = false;
    std::wstring path;
    int count = 0;
    std::vector<std::wstring> names;
};

// Asks a window for its shell browser. The pointer is not AddRef'd.
IShellBrowser* BrowserOf(HWND hwnd) {
    DWORD_PTR result = 0;
    // WM_USER + 7 is the long-standing "get IShellBrowser" message.
    if (!SendMessageTimeoutW(hwnd, WM_USER + 7, 0, 0,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result)) {
        return nullptr;
    }
    return (IShellBrowser*)result;
}

FolderInfo ReadFolder(HWND topLevel) {
    FolderInfo info;
    DWORD thread = GetCurrentThreadId();
    HWND tab = FindVisibleChild(topLevel, L"ShellTabWindowClass");
    DWORD tabThread = tab ? GetWindowThreadProcessId(tab, nullptr) : 0;
    DWORD topThread = GetWindowThreadProcessId(topLevel, nullptr);
    Out(L"FOLDER: tab window=%p on thread %lu, top-level on thread %lu, this thread %lu",
        tab, tabThread, topThread, thread);

    // A shell browser belongs to the thread of its window. Calling it from
    // another thread is unsafe, so the probe only tries windows of this
    // thread.
    IShellBrowser* browser = nullptr;
    const wchar_t* source = L"none";
    if (tab && tabThread == thread) {
        browser = BrowserOf(tab);
        source = L"tab window";
    }
    if (!browser && topThread == thread) {
        browser = BrowserOf(topLevel);
        source = L"top-level window";
    }
    if (!browser) {
        Out(L"FOLDER: no shell browser reachable from this thread. tab same thread=%d, top-level same thread=%d",
            tabThread == thread ? 1 : 0, topThread == thread ? 1 : 0);
        return info;
    }
    Out(L"FOLDER: shell browser=%p from the %s", browser, source);

    IShellView* view = nullptr;
    HRESULT hr = browser->QueryActiveShellView(&view);
    if (FAILED(hr) || !view) {
        Out(L"FOLDER: QueryActiveShellView failed, hr=0x%08lX", hr);
        return info;
    }
    IFolderView* folderView = nullptr;
    hr = view->QueryInterface(IID_PPV_ARGS(&folderView));
    view->Release();
    if (FAILED(hr) || !folderView) {
        Out(L"FOLDER: no IFolderView, hr=0x%08lX", hr);
        return info;
    }

    IPersistFolder2* persist = nullptr;
    hr = folderView->GetFolder(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(hr) && persist) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(persist->GetCurFolder(&pidl)) && pidl) {
            PWSTR name = nullptr;
            if (SUCCEEDED(SHGetNameFromIDList(
                    pidl, SIGDN_DESKTOPABSOLUTEEDITING, &name)) &&
                name) {
                info.path = name;
                CoTaskMemFree(name);
            }
            CoTaskMemFree(pidl);
        }
        persist->Release();
    } else {
        Out(L"FOLDER: GetFolder(IPersistFolder2) failed, hr=0x%08lX", hr);
    }

    IShellFolder* folder = nullptr;
    hr = folderView->GetFolder(IID_PPV_ARGS(&folder));
    if (SUCCEEDED(hr) && folder) {
        int count = 0;
        if (SUCCEEDED(folderView->ItemCount(SVGIO_ALLVIEW, &count))) {
            info.count = count;
        }
        for (int i = 0; i < count && i < 12; i++) {
            PITEMID_CHILD child = nullptr;
            if (FAILED(folderView->Item(i, &child)) || !child) {
                continue;
            }
            STRRET text = {};
            wchar_t buffer[MAX_PATH] = L"";
            if (SUCCEEDED(folder->GetDisplayNameOf(child, SHGDN_NORMAL,
                                                   &text)) &&
                SUCCEEDED(StrRetToBufW(&text, child, buffer, MAX_PATH))) {
                info.names.push_back(buffer);
            }
            CoTaskMemFree(child);
        }
        folder->Release();
        info.ok = true;
    } else {
        Out(L"FOLDER: GetFolder(IShellFolder) failed, hr=0x%08lX", hr);
    }
    folderView->Release();

    Out(L"FOLDER READ: path=\"%s\", %d items, first %d names follow",
        info.path.c_str(), info.count, (int)info.names.size());
    for (size_t i = 0; i < info.names.size(); i++) {
        Out(L"  item[%d] %s", (int)i, info.names[i].c_str());
    }
    return info;
}

// ---------------------------------------------------------------------------
// Test 3: DirectUI paint calls, through dui70.dll's exported functions
// ---------------------------------------------------------------------------

struct PaintStat {
    long background = 0;
    long content = 0;
    RECT sample{};
    std::wstring name;
    std::wstring module;
    DWORD_PTR vtableRva = 0;
};

std::mutex g_paintMutex;
[[clang::no_destroy]] std::unordered_map<void*, PaintStat> g_paintStats;
std::atomic<long> g_paintCalls{0};
int g_classInfoSlot = -1;
int g_nameSlot = -1;

// Finds an export whose decorated name starts with `prefix`.
void* FindExport(HMODULE module, const char* prefix, std::string* fullName) {
    auto base = (BYTE*)module;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || !dir.Size) {
        return nullptr;
    }
    auto exports = (IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    auto names = (DWORD*)(base + exports->AddressOfNames);
    size_t prefixLength = strlen(prefix);
    for (DWORD i = 0; i < exports->NumberOfNames; i++) {
        const char* name = (const char*)(base + names[i]);
        if (strncmp(name, prefix, prefixLength) == 0) {
            if (fullName) {
                *fullName = name;
            }
            return (void*)GetProcAddress(module, name);
        }
    }
    return nullptr;
}

// Index of `function` in an exported vtable, or -1.
int FindSlot(void* vtable, void* function, const wchar_t* label) {
    if (!vtable || !function) {
        Out(L"PAINT: %s: vtable=%p function=%p, can't search", label, vtable,
            function);
        return -1;
    }
    void** slots = (void**)vtable;
    for (int i = 0; i < 120; i++) {
        if (IsBadReadPtr(&slots[i], sizeof(void*))) {
            break;
        }
        if (slots[i] == function) {
            Out(L"PAINT: %s is vtable slot %d", label, i);
            return i;
        }
    }
    Out(L"PAINT: %s not found in the first 120 vtable slots", label);
    return -1;
}

// The element's class name, asked through its own vtable. Empty when the
// slots weren't found.
std::wstring ElementClassName(void* element) {
    if (g_classInfoSlot < 0 || g_nameSlot < 0) {
        return L"";
    }
    void** vtable = *(void***)element;
    using GetClassInfo_t = void* (*)(void*);
    void* classInfo = ((GetClassInfo_t)vtable[g_classInfoSlot])(element);
    if (!classInfo || IsBadReadPtr(classInfo, sizeof(void*))) {
        return L"";
    }
    void** infoVtable = *(void***)classInfo;
    using GetName_t = const wchar_t* (*)(void*);
    const wchar_t* name = ((GetName_t)infoVtable[g_nameSlot])(classInfo);
    if (!name || IsBadStringPtrW(name, 200)) {
        return L"";
    }
    return name;
}

void RecordPaint(void* element, bool background, const RECT* rect) {
    if (!g_paintOn || !element) {
        return;
    }
    g_paintCalls++;
    void* vtable = *(void**)element;
    std::lock_guard<std::mutex> lock(g_paintMutex);
    auto it = g_paintStats.find(vtable);
    if (it == g_paintStats.end()) {
        if (g_paintStats.size() >= 300) {
            return;
        }
        PaintStat stat;
        stat.name = ElementClassName(element);
        HMODULE owner = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)vtable, &owner) &&
            owner) {
            wchar_t path[MAX_PATH] = L"";
            GetModuleFileNameW(owner, path, MAX_PATH);
            stat.module = PathFindFileNameW(path);
            stat.vtableRva = (DWORD_PTR)vtable - (DWORD_PTR)owner;
        }
        it = g_paintStats.emplace(vtable, std::move(stat)).first;
    }
    if (background) {
        it->second.background++;
    } else {
        it->second.content++;
    }
    if (rect && !IsBadReadPtr(rect, sizeof(RECT))) {
        it->second.sample = *rect;
    }
}

using PaintContent_t = void (WINAPI*)(void* pThis, HDC dc, const RECT* rect);
PaintContent_t PaintContent_Original;
void WINAPI PaintContent_Hook(void* pThis, HDC dc, const RECT* rect) {
    RecordPaint(pThis, false, rect);
    PaintContent_Original(pThis, dc, rect);
}

// The four rectangles are references, which arrive as pointers.
using PaintBackground_t = void (WINAPI*)(void* pThis, HDC dc, void* value,
                                         const RECT* a, const RECT* b,
                                         const RECT* c, const RECT* d);
PaintBackground_t PaintBackground_Original;
void WINAPI PaintBackground_Hook(void* pThis, HDC dc, void* value,
                                 const RECT* a, const RECT* b, const RECT* c,
                                 const RECT* d) {
    RecordPaint(pThis, true, a);
    PaintBackground_Original(pThis, dc, value, a, b, c, d);
}

void SetupPaintTest() {
    HMODULE dui = GetModuleHandleW(L"dui70.dll");
    if (!dui) {
        Out(L"PAINT: dui70.dll isn't loaded in this process");
        return;
    }
    std::string name;
    void* elementVtable = (void*)GetProcAddress(dui, "??_7Element@DirectUI@@6B@");
    void* getClassInfo =
        FindExport(dui, "?GetClassInfoW@Element@DirectUI@@", &name);
    Out(L"PAINT: Element vtable=%p, Element::GetClassInfoW=%p (%S)",
        elementVtable, getClassInfo, name.c_str());
    g_classInfoSlot =
        FindSlot(elementVtable, getClassInfo, L"Element::GetClassInfoW");

    name.clear();
    void* infoVtable =
        (void*)GetProcAddress(dui, "??_7ClassInfoBase@DirectUI@@6B@");
    void* getName = FindExport(dui, "?GetName@ClassInfoBase@DirectUI@@", &name);
    Out(L"PAINT: ClassInfoBase vtable=%p, ClassInfoBase::GetName=%p (%S)",
        infoVtable, getName, name.c_str());
    g_nameSlot = FindSlot(infoVtable, getName, L"ClassInfoBase::GetName");

    name.clear();
    void* paintContent =
        FindExport(dui, "?PaintContent@Element@DirectUI@@", &name);
    Out(L"PAINT: PaintContent=%p (%S)", paintContent, name.c_str());
    name.clear();
    void* paintBackground =
        FindExport(dui, "?PaintBackground@Element@DirectUI@@", &name);
    Out(L"PAINT: PaintBackground=%p (%S)", paintBackground, name.c_str());

    BOOL hookedContent = paintContent &&
        Wh_SetFunctionHook(paintContent, (void*)PaintContent_Hook,
                           (void**)&PaintContent_Original);
    BOOL hookedBackground = paintBackground &&
        Wh_SetFunctionHook(paintBackground, (void*)PaintBackground_Hook,
                           (void**)&PaintBackground_Original);
    Out(L"PAINT: hooks set, content=%d background=%d", hookedContent ? 1 : 0,
        hookedBackground ? 1 : 0);
}

void DumpPaintStats(const wchar_t* why) {
    struct Row {
        void* vtable;
        PaintStat stat;
    };
    std::vector<Row> rows;
    {
        std::lock_guard<std::mutex> lock(g_paintMutex);
        for (auto& entry : g_paintStats) {
            rows.push_back({entry.first, entry.second});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.stat.background + a.stat.content >
               b.stat.background + b.stat.content;
    });
    Out(L"PAINT SUMMARY (%s): %ld calls, %d element classes", why,
        g_paintCalls.load(), (int)rows.size());
    int shown = 0;
    for (auto& row : rows) {
        if (shown++ >= 40) {
            break;
        }
        Out(L"  %-28s %s+0x%IX background=%ld content=%ld last rect %ldx%ld",
            row.stat.name.empty() ? L"(no name)" : row.stat.name.c_str(),
            row.stat.module.c_str(), row.stat.vtableRva, row.stat.background,
            row.stat.content, row.stat.sample.right - row.stat.sample.left,
            row.stat.sample.bottom - row.stat.sample.top);
    }
}

// ---------------------------------------------------------------------------
// Control file and worker thread
// ---------------------------------------------------------------------------

void ClosePanelFromAnyThread(const wchar_t* why);

HANDLE g_worker = nullptr;
HANDLE g_workerStop = nullptr;

void ReadControlFile(std::string* last) {
    wchar_t profile[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return;
    }
    std::wstring path =
        std::wstring(profile) + L"\\Code\\windhawk-lib\\probes\\control.txt";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    char buffer[300] = "";
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    buffer[read] = 0;
    if (*last == buffer) {
        return;
    }
    *last = buffer;
    Out(L"CONTROL: file changed");
    if (strstr(buffer, "paint=0")) {
        g_paintOn = false;
        Out(L"CONTROL: paint counting off");
    }
    if (strstr(buffer, "paint=1")) {
        g_paintOn = true;
        Out(L"CONTROL: paint counting on");
    }
    if (strstr(buffer, "host=0")) {
        g_showPanel = false;
        Out(L"CONTROL: panel off");
        ClosePanelFromAnyThread(L"control file");
    }
    if (strstr(buffer, "host=1")) {
        g_showPanel = true;
        Out(L"CONTROL: panel on, click the command bar to show it");
    }
    if (strstr(buffer, "dump=")) {
        g_dumpNow = true;
    }
}

DWORD WINAPI WorkerThread(LPVOID) {
    std::string lastControl;
    long lastCalls = 0;
    int automaticDumps = 0;
    int seconds = 0;
    while (WaitForSingleObject(g_workerStop, 1000) == WAIT_TIMEOUT) {
        seconds++;
        ReadControlFile(&lastControl);
        if (g_dumpNow.exchange(false)) {
            DumpPaintStats(L"asked through the control file");
            continue;
        }
        // A few automatic summaries, only while the counts still move.
        long calls = g_paintCalls.load();
        if (seconds % 10 == 0 && calls != lastCalls && automaticDumps < 6) {
            lastCalls = calls;
            automaticDumps++;
            DumpPaintStats(L"automatic");
        }
    }
    return 0;
}

#ifndef PROBE_NO_WINRT

using Inspectable = winrt::Windows::Foundation::IInspectable;

void LogHresult(const wchar_t* where) {
    HRESULT hr = winrt::to_hresult();
    Out(L"  %s threw hr=0x%08lX", where, hr);
}

// Everything the panel owns. Created, used and destroyed on one Explorer
// window's UI thread. Other threads only read g_panelThread and g_panelQueue.
struct Panel {
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token tickToken{};
    muxc::Button countButton{nullptr};
    winrt::event_token countToken{};
    muxc::Button closeButton{nullptr};
    winrt::event_token closeToken{};
    muxc::ListView lists[3]{{nullptr}, {nullptr}, {nullptr}};
    winrt::event_token listTokens[3]{};
    muxc::TextBlock status{nullptr};
    HWND topLevel = nullptr;
    HWND fileList = nullptr;
    RECT lastRect{};
    int clicks = 0;
    int moves = 0;
};

[[clang::no_destroy]] Panel g_panel;
std::atomic<DWORD> g_panelThread{0};
std::mutex g_queueMutex;
[[clang::no_destroy]] mud::DispatcherQueue g_panelQueue{nullptr};

void ClosePanelOnThisThread(const wchar_t* why) {
    if (g_panelThread != GetCurrentThreadId()) {
        return;
    }
    Out(L"PANEL CLOSING: %s. clicks=%d moves=%d", why, g_panel.clicks,
        g_panel.moves);
    try {
        if (g_panel.timer) {
            g_panel.timer.Stop();
            g_panel.timer.Tick(g_panel.tickToken);
        }
        if (g_panel.countButton) {
            g_panel.countButton.Click(g_panel.countToken);
        }
        if (g_panel.closeButton) {
            g_panel.closeButton.Click(g_panel.closeToken);
        }
        for (int i = 0; i < 3; i++) {
            if (g_panel.lists[i]) {
                g_panel.lists[i].SelectionChanged(g_panel.listTokens[i]);
            }
        }
        if (g_panel.source) {
            g_panel.source.Content(nullptr);
            g_panel.source.Close();
        }
    } catch (...) {
        LogHresult(L"closing the panel");
    }
    g_panel = Panel{};
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_panelQueue = nullptr;
    }
    g_panelThread = 0;
    Out(L"PANEL CLOSED");
}

void OnTick() {
    if (g_panelThread != GetCurrentThreadId() || !g_panel.bridge) {
        return;
    }
    if (g_unloading || !g_showPanel) {
        ClosePanelOnThisThread(L"setting off or mod unloading");
        return;
    }
    try {
        if (!IsWindow(g_panel.topLevel)) {
            ClosePanelOnThisThread(L"the Explorer window is gone");
            return;
        }
        HWND fileList = FindFileList(g_panel.topLevel);
        if (fileList != g_panel.fileList) {
            Out(L"  file list window changed: %p -> %p", g_panel.fileList,
                fileList);
            g_panel.fileList = fileList;
        }
        if (!fileList) {
            return;
        }
        RECT rect;
        if (!FileListRect(g_panel.topLevel, fileList, &rect)) {
            return;
        }
        if (!EqualRect(&rect, &g_panel.lastRect)) {
            g_panel.lastRect = rect;
            g_panel.moves++;
            g_panel.bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
                rect.left, rect.top, rect.right - rect.left,
                rect.bottom - rect.top});
            if (g_panel.moves <= 20) {
                Out(L"  panel moved to %ld,%ld size %ldx%ld", rect.left,
                    rect.top, rect.right - rect.left, rect.bottom - rect.top);
            }
        }
    } catch (...) {
        LogHresult(L"OnTick");
    }
}

muxc::ListView MakeColumn(int index, const FolderInfo& folder) {
    muxc::ListView list;
    list.Width(220);
    list.SelectionMode(muxc::ListViewSelectionMode::Single);
    if (index == 0 && !folder.names.empty()) {
        for (auto& name : folder.names) {
            list.Items().Append(winrt::box_value(winrt::hstring{name}));
        }
    } else {
        for (int row = 1; row <= 8; row++) {
            wchar_t text[64];
            _snwprintf_s(text, ARRAYSIZE(text), _TRUNCATE,
                         L"Column %d, row %d", index + 1, row);
            list.Items().Append(winrt::box_value(winrt::hstring{text}));
        }
    }
    g_panel.lists[index] = list;
    g_panel.listTokens[index] = list.SelectionChanged(
        [index](Inspectable const&,
                muxc::SelectionChangedEventArgs const&) {
            if (g_panelThread != GetCurrentThreadId()) {
                return;
            }
            int selected = g_panel.lists[index]
                               ? g_panel.lists[index].SelectedIndex()
                               : -1;
            Out(L"  INPUT: column %d selected row %d", index + 1,
                selected + 1);
        });
    return list;
}

void CreatePanel() {
    if (g_panelThread != 0) {
        return;
    }
    DWORD thread = GetCurrentThreadId();
    HWND topLevel = FindTopLevel();
    if (!topLevel) {
        Out(L"PANEL: no Explorer window found for this click");
        return;
    }
    HWND fileList = FindFileList(topLevel);
    DWORD topThread = GetWindowThreadProcessId(topLevel, nullptr);
    DWORD listThread =
        fileList ? GetWindowThreadProcessId(fileList, nullptr) : 0;
    Out(L"PANEL: click thread=%lu, window=%p on thread %lu, file list=%p on thread %lu",
        thread, topLevel, topThread, fileList, listThread);
    if (!fileList) {
        Out(L"PANEL: no visible file list in this window");
        return;
    }
    RECT rect;
    if (!FileListRect(topLevel, fileList, &rect)) {
        Out(L"PANEL: the file list has no size");
        return;
    }
    Out(L"PANEL: file list at %ld,%ld size %ldx%ld, dpi=%u", rect.left,
        rect.top, rect.right - rect.left, rect.bottom - rect.top,
        GetDpiForWindow(topLevel));

    FolderInfo folder = ReadFolder(topLevel);

    g_panelThread = thread;
    const wchar_t* step = L"start";
    try {
        step = L"getting the thread's dispatcher queue";
        auto queue = mud::DispatcherQueue::GetForCurrentThread();
        if (!queue) {
            Out(L"PANEL: this thread has no WinUI dispatcher queue");
            g_panelThread = 0;
            return;
        }

        step = L"building the content";
        muxc::StackPanel columns;
        columns.Orientation(muxc::Orientation::Horizontal);
        columns.Spacing(1);
        for (int i = 0; i < 3; i++) {
            columns.Children().Append(MakeColumn(i, folder));
        }

        muxc::TextBlock status;
        status.Text(L"Windhawk test panel. Clicks: 0");
        status.VerticalAlignment(mux::VerticalAlignment::Center);
        g_panel.status = status;

        muxc::Button countButton;
        countButton.Content(winrt::box_value(L"Count"));
        g_panel.countButton = countButton;
        g_panel.countToken = countButton.Click(
            [](Inspectable const&, mux::RoutedEventArgs const&) {
                if (g_panelThread != GetCurrentThreadId()) {
                    return;
                }
                g_panel.clicks++;
                Out(L"  INPUT: Count clicked, total %d", g_panel.clicks);
                if (g_panel.status) {
                    wchar_t text[80];
                    _snwprintf_s(text, ARRAYSIZE(text), _TRUNCATE,
                                  L"Windhawk test panel. Clicks: %d",
                                  g_panel.clicks);
                    g_panel.status.Text(text);
                }
            });

        muxc::Button closeButton;
        closeButton.Content(winrt::box_value(L"Close"));
        g_panel.closeButton = closeButton;
        g_panel.closeToken = closeButton.Click(
            [](Inspectable const&, mux::RoutedEventArgs const&) {
                // Close after this handler returns, not from inside it.
                auto queue = mud::DispatcherQueue::GetForCurrentThread();
                if (queue) {
                    queue.TryEnqueue([]() {
                        g_showPanel = false;
                        ClosePanelOnThisThread(L"Close button");
                    });
                }
            });

        muxc::StackPanel bar;
        bar.Orientation(muxc::Orientation::Horizontal);
        bar.Spacing(12);
        bar.Margin(mux::Thickness{12, 8, 12, 8});
        bar.Children().Append(status);
        bar.Children().Append(countButton);
        bar.Children().Append(closeButton);

        muxc::StackPanel content;
        content.Orientation(muxc::Orientation::Vertical);
        content.Children().Append(bar);
        content.Children().Append(columns);

        muxc::Grid root;
        root.Background(mux::Media::SolidColorBrush(
            winrt::Windows::UI::Color{255, 32, 32, 32}));
        root.BorderBrush(mux::Media::SolidColorBrush(
            winrt::Windows::UI::Color{255, 255, 140, 0}));
        root.BorderThickness(mux::Thickness{2, 2, 2, 2});
        root.RequestedTheme(mux::ElementTheme::Dark);
        root.Children().Append(content);

        step = L"creating DesktopWindowXamlSource";
        mux::Hosting::DesktopWindowXamlSource source;
        g_panel.source = source;

        step = L"Initialize with the Explorer window";
        source.Initialize(winrt::Microsoft::UI::WindowId{(uint64_t)topLevel});

        step = L"setting the content";
        source.Content(root);

        step = L"getting the site bridge";
        auto bridge = source.SiteBridge();
        g_panel.bridge = bridge;

        step = L"MoveAndResize";
        bridge.MoveAndResize(winrt::Windows::Graphics::RectInt32{
            rect.left, rect.top, rect.right - rect.left,
            rect.bottom - rect.top});

        step = L"Show";
        bridge.Show();

        step = L"raising the panel's window";
        HWND bridgeWindow = (HWND)bridge.WindowId().Value;
        BOOL raised = SetWindowPos(bridgeWindow, HWND_TOP, 0, 0, 0, 0,
                                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Out(L"PANEL: bridge window=%p class=%s parent=%p raised=%d",
            bridgeWindow, ClassNameOf(bridgeWindow).c_str(),
            GetParent(bridgeWindow), raised);

        g_panel.topLevel = topLevel;
        g_panel.fileList = fileList;
        g_panel.lastRect = rect;

        step = L"starting the timer";
        auto timer = queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(250));
        g_panel.tickToken = timer.Tick(
            [](mud::DispatcherQueueTimer const&, Inspectable const&) {
                OnTick();
            });
        timer.Start();
        g_panel.timer = timer;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_panelQueue = queue;
        }
        Out(L"PANEL SHOWN. Click a row, click Count, resize the window, switch folder.");
    } catch (...) {
        Out(L"PANEL FAILED at step: %s", step);
        LogHresult(L"CreatePanel");
        ClosePanelOnThisThread(L"creation failed");
    }
}

using TwoArgs_t = void(WINAPI*)(void* pThis, Inspectable const& a,
                                Inspectable const& b);

void OnCommandBarClick(const wchar_t* name) {
    int call = ++g_hookCalls;
    if (call <= 5) {
        Out(L"> %s call %d, thread=%lu", name, call, GetCurrentThreadId());
    }
    if (g_unloading || !g_showPanel) {
        return;
    }
    CreatePanel();
}

TwoArgs_t PointerPressed_Original;
void WINAPI PointerPressed_Hook(void* pThis, Inspectable const& sender,
                                Inspectable const& args) {
    PointerPressed_Original(pThis, sender, args);
    OnCommandBarClick(L"CommandBarControl::PointerPressedHandler");
}

TwoArgs_t PointerPressedWave1_Original;
void WINAPI PointerPressedWave1_Hook(void* pThis, Inspectable const& sender,
                                     Inspectable const& args) {
    PointerPressedWave1_Original(pThis, sender, args);
    OnCommandBarClick(L"CommandBarControl_Wave1::PointerPressedHandler");
}

bool g_hooked = false;

void HookExtensions(HMODULE module) {
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &PointerPressed_Original,
         PointerPressed_Hook,
         true},
        {{LR"(public: void __cdecl winrt::FileExplorerExtensions::implementation::CommandBarControl_Wave1::PointerPressedHandler(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &))"},
         &PointerPressedWave1_Original,
         PointerPressedWave1_Hook,
         true},
    };
    bool ok = WindhawkUtils::HookSymbols(module, hooks, ARRAYSIZE(hooks));
    Out(L"HookSymbols returned %d, PointerPressedHandler=%p, Wave1=%p",
        ok ? 1 : 0, (void*)PointerPressed_Original,
        (void*)PointerPressedWave1_Original);
    g_hooked = PointerPressed_Original || PointerPressedWave1_Original;
}

// Closes the panel from a thread that doesn't own it, and waits.
void ClosePanelFromAnyThread(const wchar_t* why) {
    mud::DispatcherQueue queue{nullptr};
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        queue = g_panelQueue;
    }
    if (!queue) {
        return;
    }
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool queued = false;
    try {
        queued = queue.TryEnqueue([done, why]() {
            ClosePanelOnThisThread(why);
            if (done) {
                SetEvent(done);
            }
        });
    } catch (...) {
        LogHresult(L"queueing the panel close");
    }
    queue = nullptr;
    if (queued && done) {
        DWORD wait = WaitForSingleObject(done, 4000);
        Out(L"panel close %s", wait == WAIT_OBJECT_0 ? L"done" : L"TIMED OUT");
        if (wait == WAIT_OBJECT_0) {
            CloseHandle(done);
        }
    } else if (done) {
        CloseHandle(done);
    }
}

#else  // PROBE_NO_WINRT: stand-ins for a compile check without WinRT headers

bool g_hooked = false;
void HookExtensions(HMODULE) {}
void ClosePanelFromAnyThread(const wchar_t*) {}

#endif

}  // namespace

BOOL Wh_ModInit() {
#if defined(_M_ARM64) || defined(__aarch64__)
    const wchar_t* arch = L"arm64";
#elif defined(_M_X64) || defined(__x86_64__)
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif
    OutInit();
    Out(L"PROBE START probe-bench v0.1 compiled as %s, pid=%lu", arch,
        GetCurrentProcessId());

    g_showPanel = Wh_GetIntSetting(L"showPanel") != 0;
    g_paintOn = Wh_GetIntSetting(L"countPaint") != 0;
    Out(L"showPanel=%d countPaint=%d", g_showPanel ? 1 : 0, g_paintOn ? 1 : 0);

    SetupPaintTest();

    HMODULE module = GetModuleHandleW(L"FileExplorerExtensions.dll");
    if (!module) {
        Out(L"FileExplorerExtensions.dll isn't loaded yet. Open a File Explorer window, then disable and enable this mod.");
        return TRUE;
    }
    HookExtensions(module);
    return TRUE;
}

void Wh_ModAfterInit() {
    if (g_hooked) {
        Out(L"HOOKS READY. Click an empty spot of a File Explorer command bar.");
    }
    g_workerStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_worker = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
}

void Wh_ModSettingsChanged() {
    g_showPanel = Wh_GetIntSetting(L"showPanel") != 0;
    g_paintOn = Wh_GetIntSetting(L"countPaint") != 0;
    Out(L"showPanel=%d countPaint=%d", g_showPanel ? 1 : 0, g_paintOn ? 1 : 0);
    if (!g_showPanel) {
        ClosePanelFromAnyThread(L"setting turned off");
    }
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    g_paintOn = false;
    if (g_worker) {
        SetEvent(g_workerStop);
        WaitForSingleObject(g_worker, INFINITE);
        CloseHandle(g_worker);
        g_worker = nullptr;
    }
    ClosePanelFromAnyThread(L"mod unloading");
    DumpPaintStats(L"final");
}

void Wh_ModUninit() {
    Out(L"PROBE END");
}
