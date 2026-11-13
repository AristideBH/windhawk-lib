// ==WindhawkMod==
// @id              probe-tabs-virtual
// @name            Probe: new tabs and virtual items
// @description     Diagnostic only. Logs when Explorer creates tab windows, and what the Desktop's and Control Panel's items are made of.
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -lshell32 -lshlwapi -luuid -luser32 -ladvapi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Probe: new tabs and virtual items

Read-only. Two questions for Explorer Extras:

1. **New tabs.** What is the earliest reliable moment, and the hook, at which
   a new tab exists? The probe hooks `CreateWindowExW` and logs every window
   whose class looks like a tab, a view or a browser frame, with the thread,
   the parent and the time. A second thread watches each Explorer window and
   logs when the number of tabs changes, which gives the same moment without a
   hook.
2. **Virtual items.** The Desktop and Control Panel hold items that aren't
   real folders on disk. For each item the probe logs its name, parsing name,
   shell attributes, and whether it can be bound as a folder and enumerated.

Disable Explorer Extras first, enable this one, then:

1. Wait for `PROBE START` and `ITEMS DONE` in the log.
2. In a File Explorer window press the "+" button for a new tab, three times.
3. Close one tab. Open a new window with Win+E.
4. Open the Desktop in a tab. Click Control Panel and one other virtual item
   if the Desktop shows any.

Then send the log from `PROBE START` on. It is at
`Code\windhawk-lib\probes\logs\probe-tabs-virtual.log` and in Windhawk's log.
*/
// ==/WindhawkModReadme==

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <atomic>
#include <cstdarg>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
std::mutex g_logMutex;
wchar_t g_logPath[MAX_PATH] = L"";

void LogFile(const wchar_t* line) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logPath[0]) {
        return;
    }
    HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ |
                                                               FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t stamp[64];
    swprintf(stamp, 64, L"%02d:%02d:%02d.%03d [%lu] ", t.wHour, t.wMinute,
             t.wSecond, t.wMilliseconds, GetCurrentThreadId());
    std::wstring text = std::wstring(stamp) + line + L"\r\n";
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                    nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), utf8.data(),
                        bytes, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(file);
}

void Out(const wchar_t* format, ...) {
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    vswprintf(buffer, 1024, format, args);
    va_end(args);
    Wh_Log(L"%s", buffer);
    LogFile(buffer);
}

std::wstring ClassNameOf(HWND hwnd) {
    wchar_t name[256] = L"";
    GetClassNameW(hwnd, name, 256);
    return name;
}

// ---- 1. Window creation ----

bool Interesting(const std::wstring& name) {
    static const wchar_t* words[] = {L"ShellTab", L"Cabinet", L"DefView",
                                     L"DUIView", L"ExplorerBrowser",
                                     L"ShellView", L"TabHost", L"Tab",
                                     L"Microsoft.UI.Content"};
    for (auto word : words) {
        if (name.find(word) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int,
                                        int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
CreateWindowExW_t CreateWindowExW_Original;
HWND WINAPI CreateWindowExW_Hook(DWORD exStyle, LPCWSTR className,
                                 LPCWSTR title, DWORD style, int x, int y,
                                 int w, int h, HWND parent, HMENU menu,
                                 HINSTANCE instance, LPVOID param) {
    HWND hwnd = CreateWindowExW_Original(exStyle, className, title, style, x, y,
                                         w, h, parent, menu, instance, param);
    if (!g_stop && hwnd && className && !IS_INTRESOURCE(className)) {
        std::wstring name = className;
        if (Interesting(name)) {
            Out(L"CreateWindowExW class=\"%s\" hwnd=%p parent=%p (%s) style=0x%08lX",
                name.c_str(), hwnd, parent,
                parent ? ClassNameOf(parent).c_str() : L"none", style);
        }
    }
    return hwnd;
}

// ---- 1b. Tabs seen from outside: count the tab windows of every frame ----

struct CountContext {
    int tabs = 0;
    HWND visibleTab = nullptr;
};

BOOL CALLBACK CountTabs(HWND hwnd, LPARAM lParam) {
    auto* context = (CountContext*)lParam;
    if (ClassNameOf(hwnd) == L"ShellTabWindowClass") {
        context->tabs++;
        if (IsWindowVisible(hwnd)) {
            context->visibleTab = hwnd;
        }
    }
    return TRUE;
}

BOOL CALLBACK WatchFrame(HWND hwnd, LPARAM lParam) {
    auto* seen = (std::map<HWND, std::pair<int, HWND>>*)lParam;
    if (ClassNameOf(hwnd) != L"CabinetWClass") {
        return TRUE;
    }
    CountContext context;
    EnumChildWindows(hwnd, CountTabs, (LPARAM)&context);
    auto found = seen->find(hwnd);
    if (found == seen->end() || found->second.first != context.tabs ||
        found->second.second != context.visibleTab) {
        Out(L"frame %p: %d tab window(s), visible tab %p%s", hwnd, context.tabs,
            context.visibleTab, found == seen->end() ? L" (new frame)" : L"");
        (*seen)[hwnd] = {context.tabs, context.visibleTab};
    }
    return TRUE;
}

DWORD WINAPI WatchProc(LPVOID) {
    std::map<HWND, std::pair<int, HWND>> seen;
    while (!g_stop) {
        EnumWindows(WatchFrame, (LPARAM)&seen);
        for (auto it = seen.begin(); it != seen.end();) {
            if (!IsWindow(it->first)) {
                Out(L"frame %p gone", it->first);
                it = seen.erase(it);
            } else {
                ++it;
            }
        }
        Sleep(100);
    }
    return 0;
}

// ---- 2. What the Desktop's and Control Panel's items are ----

std::wstring NameOf(IShellFolder* folder, PCUITEMID_CHILD child, SHGDNF flags) {
    STRRET text = {};
    wchar_t buffer[MAX_PATH * 2] = L"";
    if (SUCCEEDED(folder->GetDisplayNameOf(child, flags, &text)) &&
        SUCCEEDED(StrRetToBufW(&text, child, buffer, ARRAYSIZE(buffer)))) {
        return buffer;
    }
    return L"(no name)";
}

void DescribeFolder(IShellFolder* folder, const wchar_t* label, int limit) {
    IEnumIDList* list = nullptr;
    HRESULT hr = folder->EnumObjects(
        nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
        &list);
    Out(L"-- %s: EnumObjects hr=0x%08lX", label, hr);
    if (hr != S_OK || !list) {
        return;
    }
    PITEMID_CHILD child = nullptr;
    int count = 0;
    while (list->Next(1, &child, nullptr) == S_OK && child) {
        if (count++ >= limit) {
            CoTaskMemFree(child);
            break;
        }
        SFGAOF attrs = SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM |
                       SFGAO_BROWSABLE | SFGAO_HASSUBFOLDER | SFGAO_LINK |
                       SFGAO_CANLINK | SFGAO_NONENUMERATED | SFGAO_STORAGE;
        PCUITEMID_CHILD one = child;
        folder->GetAttributesOf(1, &one, &attrs);
        std::wstring name = NameOf(folder, child, SHGDN_NORMAL);
        std::wstring parsing = NameOf(folder, child, SHGDN_FORPARSING);

        // Can it be opened as a folder, and does it have content?
        const wchar_t* bind = L"no";
        int inside = -1;
        IShellFolder* sub = nullptr;
        if (SUCCEEDED(folder->BindToObject(child, nullptr, IID_PPV_ARGS(&sub))) &&
            sub) {
            bind = L"yes";
            IEnumIDList* subList = nullptr;
            if (sub->EnumObjects(nullptr,
                                 SHCONTF_FOLDERS | SHCONTF_NONFOLDERS,
                                 &subList) == S_OK &&
                subList) {
                inside = 0;
                PITEMID_CHILD item = nullptr;
                while (subList->Next(1, &item, nullptr) == S_OK && item) {
                    CoTaskMemFree(item);
                    if (++inside >= 200) {
                        break;
                    }
                }
                subList->Release();
            }
            sub->Release();
        }
        Out(L"   \"%s\" | parse=\"%s\" | folder=%d stream=%d fs=%d browsable=%d "
            L"subfolders=%d link=%d storage=%d | binds=%s, %d inside",
            name.c_str(), parsing.c_str(), !!(attrs & SFGAO_FOLDER),
            !!(attrs & SFGAO_STREAM), !!(attrs & SFGAO_FILESYSTEM),
            !!(attrs & SFGAO_BROWSABLE), !!(attrs & SFGAO_HASSUBFOLDER),
            !!(attrs & SFGAO_LINK), !!(attrs & SFGAO_STORAGE), bind, inside);
        CoTaskMemFree(child);
        child = nullptr;
    }
    list->Release();
}

void DescribeParsingName(const wchar_t* parsing, const wchar_t* label,
                         int limit) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(parsing, nullptr, &pidl, 0, nullptr);
    if (FAILED(hr) || !pidl) {
        Out(L"-- %s: can't parse \"%s\", hr=0x%08lX", label, parsing, hr);
        return;
    }
    IShellFolder* folder = nullptr;
    hr = SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(&folder));
    if (SUCCEEDED(hr) && folder) {
        DescribeFolder(folder, label, limit);
        folder->Release();
    } else {
        Out(L"-- %s: can't bind, hr=0x%08lX", label, hr);
    }
    CoTaskMemFree(pidl);
}

DWORD WINAPI ItemsProc(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellFolder* desktop = nullptr;
    if (SUCCEEDED(SHGetDesktopFolder(&desktop)) && desktop) {
        DescribeFolder(desktop, L"Desktop (namespace root)", 80);
        desktop->Release();
    }
    // The user's Desktop folder, then Control Panel and its all-items view.
    DescribeParsingName(L"shell:Desktop", L"shell:Desktop", 80);
    DescribeParsingName(L"::{26EE0668-A00A-44D7-9371-7A37A92F4E94}",
                        L"Control Panel (all items)", 25);
    DescribeParsingName(L"shell:ControlPanelFolder", L"shell:ControlPanelFolder",
                        25);
    DescribeParsingName(L"shell:MyComputerFolder", L"This PC", 25);
    DescribeParsingName(L"shell:RecycleBinFolder", L"Recycle Bin", 10);
    Out(L"ITEMS DONE");
    CoUninitialize();
    return 0;
}

HANDLE g_watcher = nullptr;
HANDLE g_items = nullptr;

}  // namespace

BOOL Wh_ModInit() {
    wchar_t profile[MAX_PATH] = L"";
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) {
        wchar_t dir[MAX_PATH];
        swprintf(dir, MAX_PATH, L"%s\\Code\\windhawk-lib\\probes\\logs", profile);
        if (GetFileAttributesW(dir) != INVALID_FILE_ATTRIBUTES) {
            swprintf(g_logPath, MAX_PATH, L"%s\\probe-tabs-virtual.log", dir);
        }
    }
    Out(L"PROBE START probe-tabs-virtual 0.1 pid=%lu", GetCurrentProcessId());

    if (!Wh_SetFunctionHook((void*)CreateWindowExW,
                            (void*)CreateWindowExW_Hook,
                            (void**)&CreateWindowExW_Original)) {
        Out(L"could not hook CreateWindowExW");
    }
    g_watcher = CreateThread(nullptr, 0, WatchProc, nullptr, 0, nullptr);
    g_items = CreateThread(nullptr, 0, ItemsProc, nullptr, 0, nullptr);
    return TRUE;
}

void Wh_ModBeforeUninit() {
    g_stop = true;
}

void Wh_ModUninit() {
    if (g_watcher) {
        WaitForSingleObject(g_watcher, 3000);
        CloseHandle(g_watcher);
    }
    if (g_items) {
        WaitForSingleObject(g_items, 5000);
        CloseHandle(g_items);
    }
    Out(L"PROBE END");
}
