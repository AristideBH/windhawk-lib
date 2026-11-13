#ifndef MOD_NO_WINRT

// ---------------------------------------------------------------------------
// The panel and the menu entry. One WindowState per Explorer window, used only
// on that window's UI thread.
// ---------------------------------------------------------------------------

using Inspectable = winrt::Windows::Foundation::IInspectable;
using ItemsVector =
    winrt::Windows::Foundation::Collections::IVector<muxc::MenuFlyoutItemBase>;

void LogHresult(const wchar_t* where) {
    HRESULT hr = winrt::to_hresult();
    Out(L"  %s threw hr=0x%08lX", where, hr);
}

struct IconBits;

struct Column {
    Pidl folder;
    std::vector<Entry> entries;
    std::vector<int> rowToEntry;         // Entry index per row, -1 for a header.
    std::vector<muxc::TextBlock> texts;  // Name text per entry.
    std::vector<muxc::Grid> iconHosts;   // Icon cell per entry.
    std::vector<muxc::FontIcon> chevrons;  // Chevron per entry, folders only.
    int hintEntry = -1;  // The entry whose chevron is shown stronger.
    int hoverHeader = -1;  // The group header under the pointer.
    int focusHeader = -1;  // The group header the keyboard is on.
    winrt::event_token moveToken{};
    winrt::event_token pressToken{};
    mux::Input::PointerEventHandler stopScrollHandler{nullptr};  // A press stops a scroll in flight.
    winrt::event_token releaseToken{};
    winrt::event_token captureLostToken{};
    winrt::event_token exitToken{};
    bool filled = false;  // Has shown a listing at least once.
    std::shared_ptr<std::atomic<bool>> cancel;  // Set when this column's load is not wanted.
    int scrollStyle = -1;  // The scrollbar style these rows were given.
    double gutterRight = 10;  // The strip kept free on the right, in px.
    std::vector<char> tipBuilt;  // Per entry: the detail tooltip exists.
    // Rows are built when they come near the screen. Every entry has a row
    // shell from the start; its content is made later.
    std::vector<int> entryToRow;   // The row of each entry.
    std::vector<char> iconAsked;   // Per entry: an icon was asked for beyond the first batch.
    std::vector<size_t> iconWanted;  // Entries built since, waiting to be asked for.
    std::vector<char> built;       // Per entry: the row has its content.
    std::map<size_t, std::shared_ptr<IconBits>> pendingIcons;  // Icons that came before their row.
    muxc::ScrollViewer viewer{nullptr};  // The list's own scroller, watched for scrolling.
    winrt::event_token viewToken{};
    double rowHeight = 32;
    int animGroup = -1;        // A group that is opening or closing,
    bool animCollapse = false; // which way,
    ULONGLONG animDoneAt = 0;  // and when the tick finishes it.
    ULONGLONG selectionChangedAt = 0;  // When the selection last changed.
    // Groups: the group of each row (a header has its own), the group titles,
    // the chevron of each header row, and the titles collapsed by the user.
    std::vector<int> rowGroup;
    std::vector<std::wstring> groupTitles;
    std::vector<muxc::FontIcon> headerIcons;
    std::vector<std::wstring> collapsed;
    Pidl selectedChild;                 // Restored after a reload.
    double restoreScroll = -1;          // A scroll offset to put back once the rows are in.
    unsigned generation = 0;
    bool userSized = false;
    double charWidth = 7;
    muxc::StackPanel cell{nullptr};
    muxc::ListView list{nullptr};
    muxc::Border grip{nullptr};
    winrt::event_token selectionToken{};
    winrt::event_token doubleTapToken{};
    winrt::event_token keyToken{};
    winrt::event_token loadedToken{};
    winrt::event_token panelWheelToken{};
    mux::UIElement panel{nullptr};  // The list's row panel, for the wheel.
    // Vertical wheel turns are scrolled by this mod, so a press can end a glide.
    ULONGLONG lastWheelAt = 0;
    bool wheelSuppress = false;  // After a press: wheel turns are dropped until a gap.
    double wheelTarget = 0;
    winrt::event_token gripPressedToken{};
    winrt::event_token gripMovedToken{};
    winrt::event_token gripReleasedToken{};
    winrt::event_token gripLostToken{};
    winrt::event_token gripDoubleToken{};
    winrt::event_token gripEnteredToken{};
    winrt::event_token gripExitedToken{};
    winrt::event_token rightTapToken{};
    winrt::event_token tapToken{};
    muxc::Border pill{nullptr};
    ULONGLONG hoverSince = 0;
    ULONG notifyId = 0;
};

struct SiblingHook {
    muxc::MenuFlyoutItem item{nullptr};
    winrt::event_token token{};
};

// The shell's context menu for one item, shown as a XAML flyout. A native
// popup would run a modal loop inside a XAML handler, which crashed Explorer
// before, so the shell's items are copied into a MenuFlyout instead.
struct ContextMenuState {
    IContextMenu* menu = nullptr;
    HMENU hmenu = nullptr;
    UINT idFirst = 1;
    HWND owner = nullptr;
    Column* column = nullptr;  // The column and items the menu is for.
    std::vector<size_t> entries;
    bool insideArchive = false;  // The items are inside an archive that is being browsed.
    muxc::MenuFlyout flyout{nullptr};
    winrt::event_token closedToken{};
    std::vector<std::pair<muxc::MenuFlyoutItem, winrt::event_token>> hooks;
};

// What the user did in the columns that Ctrl+Z can take back. Explorer's own
// undo is out of reach from here, so deletes and renames are kept by the mod.
struct UndoEntry {
    bool deletion = false;
    std::vector<std::wstring> paths;  // Deleted paths, to restore.
    std::wstring renamedFrom;         // A rename: the old path
    std::wstring renamedTo;           // and the new one.
    bool created = false;             // A new folder, whose path is in renamedTo.
    std::vector<std::pair<std::wstring, std::wstring>> moves;  // Items moved into it, old and new path.
    std::vector<std::pair<std::wstring, std::wstring>> renames;  // Several renames at once, old and new.
};

// An item being renamed in place.
struct RenameState {
    Column* column = nullptr;
    size_t entry = 0;
    muxc::Grid grid{nullptr};
    muxc::TextBlock text{nullptr};
    muxc::TextBox box{nullptr};
    std::vector<size_t> multi;  // Several items renamed together: all their entries.
    winrt::event_token keyToken{};
    winrt::event_token lostToken{};
    ULONGLONG startedAt = 0;
    bool done = false;
};

struct Worker;

// The columns of a tab that is not showing, kept to put back when it is.
struct TabColumnSnapshot {
    double scroll = 0;  // The column's vertical scroll offset.
    Pidl folder;
    Pidl selected;
    double width = 0;
    bool userSized = false;
};
struct TabSnapshot {
    HWND tab = nullptr;
    std::vector<TabColumnSnapshot> columns;
    int active = 0;
};

struct WindowState {
    Worker* worker = nullptr;  // This window's folder and icon reader.
    DWORD thread = 0;
    std::unique_ptr<ContextMenuState> context;
    std::unique_ptr<RenameState> rename;
    std::vector<UndoEntry> undo;
    std::vector<UndoEntry> redo;
    // After an extraction: the names to select in a column once it has loaded.
    std::vector<std::wstring> selectNames;
    Column* selectNamesColumn = nullptr;
    // Open in a new tab: the folder to show, until when to wait, and the tab
    // Explorer made for it.
    Pidl openTabFolder;
    ULONGLONG openTabUntil = 0;
    HWND newTab = nullptr;
    int styleVersion = -1;  // The row style the rows were last given.
    winrt::Windows::UI::Color pillColor{};    // The accent shades the panel was last drawn with.
    winrt::Windows::UI::Color accentColor{};
    bool colorsSet = false;
    ULONGLONG scrollQuietUntil = 0;  // A scroll of the strip is under way: no refocusing meanwhile.
    ULONGLONG cursorFixUntil = 0;  // While Explorer navigates, a busy cursor over the panel is put right.
    bool shownDark = false;  // The theme the panel background was made for.
    int shownMode = 0;       // And the background mode.
    // The native lists made invisible, in transparent mode, and whether this mod
    // gave each the layered style. A tab can hold more than one view window
    // after layouts change, so every visible one is cleared.
    std::vector<std::pair<HWND, bool>> cleared;
    // A click on the name of the item that is already selected starts a
    // rename once the double-click time has passed.
    // After a command of Explorer's own toolbar: read the columns again, and
    // watch for Explorer opening its own rename box, which the panel hides.
    ULONGLONG postActionAt = 0;
    ULONGLONG postActionAt2 = 0;
    ULONGLONG watchEditUntil = 0;
    DWORD clipSeen = 0;
    ULONGLONG slowRenameAt = 0;
    ULONGLONG slowRenameBlockUntil = 0;
    Column* slowRenameColumn = nullptr;
    size_t slowRenameEntry = 0;
    HWND topLevel = nullptr;
    mud::DispatcherQueue queue{nullptr};
    // Columns per tab: the tab that is showing, and the stacks of the others.
    HWND currentTab = nullptr;
    std::vector<TabSnapshot> tabSnapshots;
    // Which tabs show Columns. The panel is one per window, so it is hidden
    // while a tab that is not in Columns is showing.
    std::vector<HWND> columnsTabs;
    std::vector<HWND> seenTabs;
    bool softHidden = false;

    // View menu entry.
    ItemsVector menuItems{nullptr};
    muxc::RadioMenuFlyoutItem menuItem{nullptr};
    winrt::event_token menuClickToken{};
    std::vector<SiblingHook> siblings;

    // Entry at window creation, and restoring the last layout.
    mux::XamlRoot xamlRoot{nullptr};
    mud::DispatcherQueueTimer entryTimer{nullptr};
    winrt::event_token entryToken{};
    bool entryStopped = false;
    int entryTries = 0;
    int restoreSettle = 0;
    HWND armTab = nullptr;  // A new tab to wait for before restoring Columns.
    std::wstring renameName;      // A folder just made, to put in edit mode
    Column* renameColumn = nullptr;  // once its column has loaded.

    // Panel.
    bool on = false;
    mux::Hosting::DesktopWindowXamlSource source{nullptr};
    winrt::Microsoft::UI::Content::DesktopChildSiteBridge bridge{nullptr};
    mud::DispatcherQueueTimer timer{nullptr};
    winrt::event_token tickToken{};
    muxc::ScrollViewer scroller{nullptr};
    muxc::Grid rootGrid{nullptr};
    Inspectable wheelHandler{nullptr};  // The boxed handler, for removal.
    Inspectable stripWheelHandler{nullptr};
    double wheelTarget = 0;             // Where the wheel is scrolling to.
    ULONGLONG wheelAt = 0;              // Tick of the last wheel event.
    muxc::StackPanel strip{nullptr};
    // A rebuild keeps the old columns on screen until the new first column has
    // loaded, then swaps them in one step, so nothing blanks in between.
    size_t swapOld = 0;
    unsigned swapGeneration = 0;
    ULONGLONG swapDeadline = 0;
    // Selection by a rectangle, drawn on the layer over the strip.
    struct Band {
        bool pending = false;  // Pressed on empty space, not yet moved far.
        bool active = false;   // Dragging.
        bool additive = false; // Ctrl was down: keep what was selected.
        Column* column = nullptr;
        winrt::Windows::Foundation::Point start{};  // List coordinates.
        double startOffset = 0;                     // Scroll offset at the press.
        std::vector<double> top, bottom;            // Row extents, content coordinates.
        std::vector<int> base;                      // Rows selected at the start.
        int first = -1, last = -1;                  // The rows the rectangle covers.
    } band;
    muxc::Border bandBox{nullptr};
    // The pill that marks the active column, on a layer over the strip.
    muxc::Canvas overlay{nullptr};
    muxc::Border pill{nullptr};
    double pillLeft = -1;      // Where the pill is, or is going.
    ULONGLONG pillUntil = 0;   // Until when its animation runs.
    std::vector<std::unique_ptr<Column>> columns;
    RECT lastRect{};
    bool building = false;
    unsigned nextGeneration = 0;
    unsigned ticks = 0;

    // Sort and group, shared by every column.
    SortSpec spec;
    SortSpec specBeforeMenu;
    ULONGLONG specCheckAt = 0;

    // Folder tracking.
    HWND lastFileList = nullptr;
    Pidl lastSeen;   // Explorer's own folder when last read.
    Pidl expected;   // A folder this mod asked Explorer to open.
    Pidl pendingNav; // A folder to open once the user stops moving.
    ULONGLONG navDue = 0;
    Pidl selectFolder;  // A file to select in Explorer's view,
    Pidl selectChild;   // once it shows that folder.
    std::vector<Pidl> selectExtra;  // More items to add to that selection.
    // Cut items, dimmed until the clipboard changes.
    Pidl cutFolder;
    std::wstring cutFolderPath;  // The same, as a path. Pidls built from a path
                                 // are rooted elsewhere and never compare equal.
    std::vector<std::wstring> cutPaths;
    DWORD cutSeq = 0;
    // The column to keep keyboard focus in, for a moment after a key, since
    // Explorer's own view takes focus when it navigates.
    Column* focusColumn = nullptr;
    ULONGLONG focusUntil = 0;
    std::wstring typed;     // Type to jump: what has been typed,
    ULONGLONG typedAt = 0;  // and when the last key came.
    int selectTries = 0;

    // Change notifications and live settings.
    HWND notifyWindow = nullptr;
    ULONGLONG reloadDue = 0;
    ULONGLONG lastReload = 0;
    bool shownHidden = false;
    bool shownCompact = false;

    // The column Explorer's own view is in.
    int active = -1;

    // Column edge being dragged.
    int dragColumn = -1;
    double dragStartX = 0;
    double dragStartWidth = 0;
};

std::mutex g_statesMutex;
[[clang::no_destroy]] std::vector<std::unique_ptr<WindowState>> g_states;

// The state of the Explorer window that owns the current thread.
WindowState* StateForThisThread(bool create) {
    DWORD thread = GetCurrentThreadId();
    std::lock_guard<std::mutex> lock(g_statesMutex);
    for (auto& state : g_states) {
        if (state->thread == thread) {
            return state.get();
        }
    }
    if (!create) {
        return nullptr;
    }
    auto state = std::make_unique<WindowState>();
    state->thread = thread;
    g_states.push_back(std::move(state));
    return g_states.back().get();
}

mux::Media::SolidColorBrush Brush(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return mux::Media::SolidColorBrush(winrt::Windows::UI::Color{a, r, g, b});
}

// Transparent mode: Explorer's own file list stays, but is drawn fully
// transparent, so nothing shows through the panel except the window's backdrop.
// Undone whenever the panel goes away.
BOOL CALLBACK CollectViewsProc(HWND hwnd, LPARAM lParam) {
    wchar_t name[32];
    if (GetClassNameW(hwnd, name, ARRAYSIZE(name)) &&
        wcscmp(name, L"SHELLDLL_DefView") == 0 && IsWindowVisible(hwnd)) {
        ((std::vector<HWND>*)lParam)->push_back(hwnd);
    }
    return TRUE;
}

void ClearOne(WindowState* state, HWND list) {
    LONG_PTR ex = GetWindowLongPtrW(list, GWL_EXSTYLE);
    bool added = !(ex & WS_EX_LAYERED);
    if (added) {
        SetWindowLongPtrW(list, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    }
    BOOL ok = SetLayeredWindowAttributes(list, 0, 0, LWA_ALPHA);
    DWORD error = ok ? 0 : GetLastError();
    bool known = false;
    for (auto& entry : state->cleared) {
        if (entry.first == list) {
            entry.second = entry.second || added;
            known = true;
        }
    }
    if (!known) {
        state->cleared.emplace_back(list, added);
    }
    Out(L"the native file list is made transparent (window %p, %s, result %d, error %lu)",
        list, ClassNameOf(list).c_str(), ok ? 1 : 0, error);
}

// Every visible view window of this window is made transparent, and one that
// something else made visible again is cleared again.
void ClearNativeLists(WindowState* state) {
    std::vector<HWND> views;
    EnumChildWindows(state->topLevel, CollectViewsProc, (LPARAM)&views);
    for (HWND view : views) {
        bool known = false;
        for (auto& entry : state->cleared) {
            known = known || entry.first == view;
        }
        if (!known) {
            ClearOne(state, view);
            continue;
        }
        // The style or the alpha can be put back by Explorer, or by a mod that
        // fades the view in when the layout changes.
        BYTE alpha = 255;
        DWORD flags = 0;
        bool intact = (GetWindowLongPtrW(view, GWL_EXSTYLE) & WS_EX_LAYERED) &&
                      GetLayeredWindowAttributes(view, nullptr, &alpha, &flags) &&
                      (flags & LWA_ALPHA) && alpha == 0;
        if (!intact) {
            Out(L"the native list %p was made visible again by something else (alpha %d), clearing it again",
                view, (int)alpha);
            ClearOne(state, view);
        }
    }
    // Windows that are gone are forgotten.
    for (size_t i = 0; i < state->cleared.size();) {
        if (!IsWindow(state->cleared[i].first)) {
            state->cleared.erase(state->cleared.begin() + i);
        } else {
            i++;
        }
    }
}

void RestoreNativeList(WindowState* state) {
    auto cleared = std::move(state->cleared);
    state->cleared.clear();
    for (auto& entry : cleared) {
        HWND list = entry.first;
        if (!list || !IsWindow(list)) {
            continue;
        }
        SetLayeredWindowAttributes(list, 0, 255, LWA_ALPHA);
        if (entry.second) {
            LONG_PTR ex = GetWindowLongPtrW(list, GWL_EXSTYLE);
            SetWindowLongPtrW(list, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
        }
        RedrawWindow(list, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    if (!cleared.empty()) {
        Out(L"the native file list is visible again");
    }
}

// The panel's background, as the setting says.
mux::Media::SolidColorBrush PanelBackground(HWND fileList, bool dark) {
    int mode = g_backgroundMode;
    if (mode == 2) {
        Out(L"panel background: transparent (test)");
        return Brush(0, 0, 0, 0);
    }
    COLORREF sampled = mode == 0 ? SampleListBackground(fileList) : CLR_INVALID;
    Out(L"panel background: %s",
        mode == 1            ? L"the theme colour, by setting"
        : sampled == CLR_INVALID ? L"not readable, using the theme colour"
                                 : L"taken from Explorer's file list");
    if (sampled != CLR_INVALID) {
        return Brush(255, GetRValue(sampled), GetGValue(sampled), GetBValue(sampled));
    }
    return dark ? Brush(255, 25, 25, 25) : Brush(255, 255, 255, 255);
}

