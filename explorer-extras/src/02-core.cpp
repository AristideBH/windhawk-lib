namespace {

std::atomic<bool> g_unloading{false};

// A log line that is only for a test build (build.py --debug defines EXTRAS_VERBOSE): lines that
// come with every click, selection or drag would bury the ones that matter.
#ifdef EXTRAS_VERBOSE
#define LogVerbose(...) Wh_Log(__VA_ARGS__)
#else
#define LogVerbose(...) ((void)0)
#endif

// The handle of the mod's own image. GetModuleHandleW(nullptr) is explorer.exe, which outlives the mod.
HINSTANCE ModHandle() {
    static HINSTANCE module = [] {
        HMODULE found = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&ModHandle, &found);
        return (HINSTANCE)found;
    }();
    return module;
}

// Every thread the mod starts is kept here and joined at unload: Windhawk frees the image right after
// the mod's uninit, so no thread may still be in mod code by then.
struct ModThread {
    std::thread thread;
    std::atomic<bool> done{false};
    std::atomic<DWORD> id{0};
};
std::mutex g_modThreadsMutex;
[[clang::no_destroy]] std::list<std::unique_ptr<ModThread>> g_modThreads;
bool g_modThreadsClosed = false;
// Signalled at unload: a long wait (an archive being made) ends and its job gives up.
HANDLE g_stopEvent = nullptr;

// False when the mod is already unloading: the job is not started.
template <typename Job>
bool StartModThread(Job job) {
    std::lock_guard<std::mutex> lock(g_modThreadsMutex);
    if (g_modThreadsClosed) {
        return false;
    }
    for (auto at = g_modThreads.begin(); at != g_modThreads.end();) {
        if ((*at)->done) {
            (*at)->thread.join();
            at = g_modThreads.erase(at);
        } else {
            ++at;
        }
    }
    auto entry = std::make_unique<ModThread>();
    ModThread* raw = entry.get();
    try {
        entry->thread = std::thread([raw, job = std::move(job)]() mutable {
            raw->id = GetCurrentThreadId();
            job();
            raw->done = true;
        });
    } catch (...) {
        return false;
    }
    g_modThreads.push_back(std::move(entry));
    return true;
}

// Waits for every thread of the mod. A thread stuck in a network call is told to give it up.
void JoinModThreads() {
    std::list<std::unique_ptr<ModThread>> all;
    {
        std::lock_guard<std::mutex> lock(g_modThreadsMutex);
        g_modThreadsClosed = true;
        all.swap(g_modThreads);
    }
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    for (auto& entry : all) {
        for (int waited = 0; !entry->done; waited++) {
            DWORD id = entry->id;
            if (id && waited > 5) {
                if (HANDLE thread = OpenThread(THREAD_TERMINATE, FALSE, id)) {
                    CancelSynchronousIo(thread);
                    CloseHandle(thread);
                }
            }
            if (waited == 200) {
                Wh_Log(L"a thread of the mod is still running after 10 s, waiting for it");
            }
            Sleep(50);
        }
        entry->thread.join();
    }
}

// A coroutine that is waiting for a WinRT call resumes later, in mod code. Each one counts itself,
// and the unload waits for the count to reach zero.
std::atomic<int> g_coroutines{0};
struct CoroutineGuard {
    CoroutineGuard() { g_coroutines++; }
    ~CoroutineGuard() { g_coroutines--; }
};

// A call of the shell that runs a message loop of its own (a menu command, a delete with its
// confirmation, Properties, a drag) can be on this thread's stack when the cleanup comes. The cleanup
// must not delete what those frames use: it waits until the count is back to zero.
thread_local int t_modalDepth = 0;
struct ModalScope {
    ModalScope() { t_modalDepth++; }
    ~ModalScope() { t_modalDepth--; }
};

std::atomic<bool> g_follow{true};
std::atomic<bool> g_autoFolderSize{false};  // A folder's size is counted when it is selected; off, a button counts it.
std::atomic<bool> g_detailsPane{true};     // The merged details pane.
std::atomic<bool> g_previewRedirect{true};  // The Preview pane shows the merged pane.
std::atomic<bool> g_panePreview{true};      // Documents and code get their real preview in the pane.
std::atomic<bool> g_paneAnim{true};         // The content of the pane slides and fades.
ULONGLONG g_paneLeaveUntil = 0;  // While the details pane slides out, the Columns panel stays behind it.
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_restore{true};
std::atomic<bool> g_sidebarRoot{true};   // Fixed: a click in the navigation pane makes its folder the first column.
std::atomic<bool> g_focusRect{true};
std::atomic<bool> g_doubleClickUp{false};
std::atomic<bool> g_lastEdge{true};       // Fixed: the line on the right of the last column shows (not an option any more).
std::atomic<bool> g_headerKeyboard{false};
std::atomic<bool> g_keepStale{true};
std::atomic<bool> g_foldersFirst{true};
std::atomic<bool> g_oddRows{false};
std::atomic<bool> g_previewImages{true};
std::atomic<bool> g_previewMedia{true};
std::atomic<int> g_previewMaxMb{60};   // The largest document that is previewed.
std::atomic<int> g_previewMaxMp{40};   // The largest picture that opens by itself.
std::atomic<int> g_paneVolume{100};    // The volume of the pane's player, in percent, kept between runs.
std::atomic<bool> g_paneMuted{false};
std::atomic<bool> g_staleAccent{true};  // The chevron of the folder you came from sits in an accent badge.
std::atomic<bool> g_autoPreview{true};  // The real preview opens by itself; otherwise a button opens it.
std::atomic<bool> g_harmonize{true};
std::atomic<bool> g_springLoad{true};     // Fixed: a folder held under a drag opens.
std::atomic<bool> g_rememberSort{true};
std::atomic<bool> g_rememberWidth{true};
std::atomic<bool> g_rememberLayout{true};
std::atomic<bool> g_remoteMarks{true};   // Fixed: the marks of cloud, network and device items.
std::atomic<bool> g_desktopRealOnly{false};
std::atomic<int> g_backgroundMode{0};  // 0 Explorer, 1 theme
std::atomic<int> g_styleVersion{0};
std::atomic<int> g_minWidth{160};
std::atomic<int> g_maxWidth{420};
std::atomic<int> g_maxItems{5000};
thread_local ULONGLONG g_lastWalk = 0;

// ---------------------------------------------------------------------------
// Windows and settings
// ---------------------------------------------------------------------------

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

// The same, from a cache kept by the caller: a window that is still there, visible and a child of `parent` is the answer, so a
// timer tick does not walk the whole window tree each time. Another tab coming to the front hides this one, and the walk is done.
HWND FindVisibleChildCached(HWND* cache, HWND parent, const wchar_t* className);

HWND FindVisibleChild(HWND parent, const wchar_t* className) {
    FindContext context;
    context.className = className;
    EnumChildWindows(parent, FindVisibleChildProc, (LPARAM)&context);
    return context.found;
}

HWND FindVisibleChildCached(HWND* cache, HWND parent, const wchar_t* className) {
    if (*cache && IsWindow(*cache) && IsWindowVisible(*cache) && IsChild(parent, *cache)) {
        return *cache;
    }
    *cache = FindVisibleChild(parent, className);
    return *cache;
}

BOOL CALLBACK FindCabinetOnThread(HWND hwnd, LPARAM lParam) {
    if (IsWindowVisible(hwnd) && ClassNameOf(hwnd) == L"CabinetWClass") {
        ((FindContext*)lParam)->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

// The Explorer window that belongs to this UI thread.
HWND FindTopLevel() {
    DWORD thread = GetCurrentThreadId();
    HWND foreground = GetForegroundWindow();
    if (foreground && ClassNameOf(foreground) == L"CabinetWClass" &&
        GetWindowThreadProcessId(foreground, nullptr) == thread) {
        return foreground;
    }
    FindContext context;
    EnumThreadWindows(thread, FindCabinetOnThread, (LPARAM)&context);
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

DWORD ReadExplorerDword(const wchar_t* subKey, const wchar_t* value,
                        DWORD fallback) {
    DWORD data = 0, size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, value, RRF_RT_REG_DWORD,
                     nullptr, &data, &size) == ERROR_SUCCESS) {
        return data;
    }
    return fallback;
}

// Explorer's own "Compact view" switch.
bool IsCompactMode() {
    return ReadExplorerDword(
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
               L"UseCompactMode", 0) != 0;
}

// The colour of the empty part of Explorer's own file list, read from the
// native view that sits under the panel. Explorer's colours can be changed by
// other mods, so the panel takes whatever the list really shows. CLR_INVALID if
// it can't be read.
COLORREF SampleListBackground(HWND fileList) {
    RECT client;
    if (!fileList || !GetClientRect(fileList, &client)) {
        return CLR_INVALID;
    }
    int width = client.right, height = client.bottom;
    if (width < 80 || height < 80 || width > 8000 || height > 8000) {
        return CLR_INVALID;
    }
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ old = SelectObject(memory, bitmap);
    COLORREF result = CLR_INVALID;
    if (PrintWindow(fileList, memory, 0x2 /* PW_RENDERFULLCONTENT */)) {
        // A row of points along the bottom edge, where a list is usually
        // empty. The colour seen most often wins.
        COLORREF seen[7];
        int count[7] = {};
        int kinds = 0;
        for (int i = 0; i < 7; i++) {
            COLORREF c = GetPixel(memory, width * (10 + i * 13) / 100, height - 4);
            if (c == CLR_INVALID) {
                continue;
            }
            int at = 0;
            while (at < kinds && seen[at] != c) {
                at++;
            }
            if (at == kinds) {
                seen[kinds++] = c;
            }
            count[at]++;
        }
        int best = -1;
        for (int i = 0; i < kinds; i++) {
            if (best < 0 || count[i] > count[best]) {
                best = i;
            }
        }
        if (best >= 0 && seen[best] != RGB(0, 0, 0)) {
            result = seen[best];
        }
    }
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return result;
}

bool IsDarkMode() {
    return ReadExplorerDword(
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", 1) == 0;
}

// ---------------------------------------------------------------------------
// Shell data. Everything here runs on the Explorer window's UI thread.
// ---------------------------------------------------------------------------

struct PidlFree {
    void operator()(ITEMIDLIST* pidl) const {
        if (pidl) {
            CoTaskMemFree(pidl);
        }
    }
};
using Pidl = std::unique_ptr<ITEMIDLIST, PidlFree>;

struct Entry {
    Pidl child;
    std::wstring name;
    bool folder = false;
    // Neither a folder nor a file: a Control Panel applet or the Desktop's
    // Control Panel entry. It can't be opened as a column, only run.
    bool launcher = false;
    // A zip or the like, browsed as a folder when the setting is on. It is a
    // folder to the columns (entry.folder is true) and a file to everything else.
    bool archive = false;
    // Where the item really is: 0 here, 1 a cloud file not on this disk,
    // 2 on a network location, 3 on a device such as a phone, 4 a cloud file
    // kept on this device, 5 a cloud file that is downloaded.
    int remote = 0;
    // A shortcut to a folder: it opens as that folder, in the next column.
    Pidl linkTarget;
};
