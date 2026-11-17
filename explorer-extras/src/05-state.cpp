#ifndef MOD_NO_WINRT

// ---------------------------------------------------------------------------
// The panel and the menu entry. One WindowState per Explorer window, used only
// on that window's UI thread.
// ---------------------------------------------------------------------------

using Inspectable = winrt::Windows::Foundation::IInspectable;

// Every handler the mod gives to an element is kept here as a revoker, one list per UI thread. XAML
// frees an element's delegates late, on turns of its own, and their code is in the mod: so at unload
// every handler is taken off first, while the mod is still mapped (Explorer Command Bar does the same).
struct KeptHandler {
    winrt::weak_ref<Inspectable> element;
    std::shared_ptr<void> revoker;
};
// No destructor at thread exit: it would run after the mod is gone.
[[clang::no_destroy]] thread_local std::vector<KeptHandler> t_handlers;
thread_local unsigned t_handlersSince = 0;

// The UI threads that gave the mod a handler, with their queues. A window that was open before the mod loaded has no
// WindowState, so nothing else would revoke what the menu hook gave to its elements: at unload each thread is asked.
struct KeepThread {
    DWORD id = 0;
    mud::DispatcherQueue queue{nullptr};
};
std::mutex g_keepThreadsMutex;
[[clang::no_destroy]] std::vector<KeepThread> g_keepThreads;
thread_local bool t_keepRegistered = false;

// Records this thread and its queue, once: at unload it is asked to release what it holds.
void RegisterKeepThread() {
    if (t_keepRegistered) {
        return;
    }
    t_keepRegistered = true;
    try {
        auto queue = mud::DispatcherQueue::GetForCurrentThread();
        if (queue) {
            std::lock_guard<std::mutex> lock(g_keepThreadsMutex);
            KeepThread entry;
            entry.id = GetCurrentThreadId();
            entry.queue = queue;
            g_keepThreads.push_back(std::move(entry));
        }
    } catch (...) {
    }
}

template <typename Element, typename Revoker>
void Keep(Element const& element, Revoker&& revoker) {
    RegisterKeepThread();
    // The handlers of elements that are gone are dropped now and then, so the list stays short.
    if (++t_handlersSince >= 256) {
        t_handlersSince = 0;
        t_handlers.erase(std::remove_if(t_handlers.begin(), t_handlers.end(),
                                        [](KeptHandler const& kept) { return !kept.element.get(); }),
                         t_handlers.end());
    }
    KeptHandler kept;
    kept.element = winrt::make_weak(element.template as<Inspectable>());
    kept.revoker = std::make_shared<std::decay_t<Revoker>>(std::move(revoker));
    t_handlers.push_back(std::move(kept));
}

// A routed-event handler that also hears what was handled already, kept the same way.
template <typename Element>
void AddRouted(Element const& element, mux::RoutedEvent const& routed, Inspectable const& handler) {
    element.AddHandler(routed, handler, true);
    auto weak = winrt::make_weak(element);
    KeptHandler kept;
    kept.element = winrt::make_weak(element.template as<Inspectable>());
    kept.revoker = std::shared_ptr<void>(new int(0), [weak, routed, handler](int* unused) {
        delete unused;
        if (auto alive = weak.get()) {
            try {
                alive.RemoveHandler(routed, handler);
            } catch (...) {
            }
        }
    });
    t_handlers.push_back(std::move(kept));
}

// Takes every handler of this thread off its element.
void RevokeAllHandlers() {
    auto all = std::move(t_handlers);
    t_handlers.clear();
    for (size_t i = all.size(); i-- > 0;) {
        try {
            all[i].revoker.reset();
        } catch (...) {
        }
    }
}
// Every UI thread that kept a handler takes its handlers off, on its own thread: the ones with a window state were done
// by their window's cleanup, the others (a window open before the mod loaded) are done here. Called at unload.
void LabelsReleaseThreadObjects();  // 10g2: the fonts and flyouts of labels that a thread holds

void RevokeKeptThreads() {
    std::vector<KeepThread> threads;
    {
        std::lock_guard<std::mutex> lock(g_keepThreadsMutex);
        threads.swap(g_keepThreads);
    }
    DWORD self = GetCurrentThreadId();
    for (auto& thread : threads) {
        if (thread.id == self) {
            RevokeAllHandlers();
            LabelsReleaseThreadObjects();
            continue;
        }
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!done) {
            continue;
        }
        bool queued = false;
        try {
            queued = thread.queue.TryEnqueue([done]() {
                RevokeAllHandlers();
                LabelsReleaseThreadObjects();
                SetEvent(done);
            });
        } catch (...) {
        }
        if (queued && WaitForSingleObject(done, 1500) == WAIT_OBJECT_0) {
            CloseHandle(done);  // After a time out the handle is left: the thread may still use it.
        } else if (!queued) {
            CloseHandle(done);
        }
    }
    threads.clear();
}

using ItemsVector =
    winrt::Windows::Foundation::Collections::IVector<muxc::MenuFlyoutItemBase>;

void LogHresult(const wchar_t* where) {
    HRESULT hr = winrt::to_hresult();
    Wh_Log(L"  %s threw hr=0x%08lX", where, hr);
}

struct IconBits;

struct Column {
    Pidl folder;
    std::vector<Entry> entries;
    std::vector<int> rowToEntry;         // Entry index per row, -1 for a header.
    std::vector<muxc::TextBlock> texts;  // Name text per entry.
    std::vector<muxc::Grid> iconHosts;   // Icon cell per entry.
    std::vector<muxc::FontIcon> chevrons;  // Chevron per entry, folders only.
    std::vector<muxc::Grid> labelHosts;    // Where the label dots of each entry go (labels).
    std::wstring labelPath;                // The folder's path, for the label cache.
    bool labelPathKnown = false;
    int hintEntry = -1;  // The entry whose chevron wears the badge.
    muxc::Border hintBadge{nullptr};  // The small rounded background behind that chevron.
    muxc::Border goneBadge{nullptr};  // One that has faded out and is taken off at the next change.
    std::vector<uint32_t> shapedRows;  // Rows whose corners and gaps were changed for a run of selected rows.
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
    SortSpec spec;                      // This folder's own sort and group,
    bool hasSpec = false;               // when it has one. Otherwise the window's.
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
    muxc::Primitives::FlyoutBase flyout{nullptr};
    winrt::event_token closedToken{};
    std::vector<std::pair<muxc::MenuFlyoutItem, winrt::event_token>> hooks;
    std::vector<std::pair<muxc::Button, winrt::event_token>> buttonHooks;
    // Behaviour of a context menu: the submenu that is open, the clock that lets a submenu
    // be replaced by another without the whole menu closing, and a watch for a click outside.
    muxc::MenuFlyout openSub{nullptr};
    ULONGLONG switchUntil = 0;
    mud::DispatcherQueueTimer watch{nullptr};
    mud::DispatcherQueueTimer hover{nullptr};  // The delay before a submenu opens; a timer nobody holds is dropped.
    bool wasDown = false;
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

// A drag of rows from one column to a folder, followed by the mod itself.
// What the drag label shows: the name or the count, and the icons of the first
// items. Kept apart from the columns, so the label can be shown again, when a
// drag that left the panel comes back to it.
struct GhostSpec {
    std::wstring name;
    size_t count = 0;
    std::vector<mux::Media::ImageSource> icons;  // Up to three, a null one when not loaded.
};

struct DragState {
    bool pending = false;  // Pressed on a row, the pointer has not moved far yet.
    bool active = false;   // Dragging.
    Column* column = nullptr;
    int pressedEntry = -1;
    std::vector<size_t> entries;
    winrt::Windows::Foundation::Point start{};  // In the source list's coordinates.
    Column* targetColumn = nullptr;
    int targetEntry = -1;  // The folder row under the pointer, or -1 for a column.
    bool targetOk = false;
    bool lastCtrl = false;  // Ctrl as it was when the target was last judged.
    bool fromArchive = false;  // The dragged items are inside an archive: a drop extracts them.
    int deviceType = 2;        // Of the press: 0 touch, 1 pen, 2 mouse, 3 touchpad.
    int movesLogged = 0;
    // Touch: a drag starts only after a hold. The hold arms it and locks the list's scrolling,
    // so the finger is not taken by a pan.
    bool armed = false;
    muxc::ListViewItem pressedRow{nullptr};  // Its snapshot is the picture of the system's drag.
    bool platformDrag = false;  // The system's own drag took the finger (StartDragAsync).
    std::optional<decltype(std::declval<mux::Input::PointerRoutedEventArgs>().GetCurrentPoint(std::declval<mux::UIElement>()))> touchPoint;
    int recaptures = 0;
    uint32_t pointerId = 0;
    mux::Input::Pointer touchPointer{nullptr};
    struct LockedScroller {
        muxc::ScrollViewer viewer{nullptr};
        muxc::ScrollMode v = muxc::ScrollMode::Auto;
        muxc::ScrollMode h = muxc::ScrollMode::Auto;
    };
    std::vector<LockedScroller> locked;  // Every scroller above the row: any of them can take the finger.
    muxc::Border frame{nullptr};  // The accent frame around the row or column a drop would go to.
    mux::FrameworkElement ghost{nullptr};
    muxc::TextBlock ghostText{nullptr};
    muxc::TextBlock ghostAction{nullptr};  // What a drop would do, under the name.
    muxc::FontIcon ghostIcon{nullptr};     // The pictogram before it.
    muxc::StackPanel ghostRow{nullptr};    // The pictogram and the line.
    std::wstring labelName;                // The first line of the label.
};

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
    int tickMs = 60;  // The interval of the panel's timer now (see TickIntervalMs).
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
    // A click on the name of the item that is already selected starts a
    // rename once the double-click time has passed.
    // After a command of Explorer's own toolbar: read the columns again, and
    // watch for Explorer opening its own rename box, which the panel hides.
    ULONGLONG postActionAt = 0;
    ULONGLONG syncSelAt = 0;  // When to take Explorer's selection after a toolbar command.
    ULONGLONG postActionAt2 = 0;
    ULONGLONG watchEditUntil = 0;
    DWORD clipSeen = 0;
    ULONGLONG slowRenameAt = 0;
    int activeAtPress = -1;  // The active column when the last click on a row began.
    ULONGLONG slowRenameBlockUntil = 0;
    Column* slowRenameColumn = nullptr;
    size_t slowRenameEntry = 0;
    HWND topLevel = nullptr;
    mud::DispatcherQueue queue{nullptr};
    // Columns per tab: the tab that is showing, and the stacks of the others.
    DragState drag;
    HWND currentTab = nullptr;
    ULONGLONG specSyncDue = 0;  // When to read the sort of the folder Explorer shows.
    std::wstring hiddenFolder;  // The folder a tab that is not in Columns showed last.
    std::vector<TabSnapshot> tabSnapshots;
    // Which tabs show Columns. The panel is one per window, so it is hidden
    // while a tab that is not in Columns is showing.
    std::vector<HWND> columnsTabs;
    std::vector<HWND> seenTabs;
    bool softHidden = false;

    // View menu entry.
    ItemsVector menuItems{nullptr};
    muxc::RadioMenuFlyoutItem menuItem{nullptr};
    // The View button of the toolbar: while Columns shows, its icon is ours.
    muxc::Button viewButton{nullptr};
    mux::UIElement viewIconOriginal{nullptr};
    mux::UIElement viewIconOurs{nullptr};
    int viewIconWhere = 0;  // 1: the button's content, 2: a child of a panel, 3: the child of a border.
    bool viewIconShown = false;
    // While the keyboard is in use, rows do not show the hover of a pointer that
    // has not moved.
    bool kbHoverOff = false;
    POINT kbCursor{};
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
        bool touch = false;                         // A finger started it, on the empty space below the rows.
        int recaptures = 0;
        // A finger that moves mostly sideways from the empty space scrolls the columns instead of
        // drawing a rectangle.
        bool panning = false;
        double panStartX = 0, panStartOffset = 0, panLastX = 0, panVelocity = 0;  // Pixels, and pixels per millisecond.
        ULONGLONG panLastAt = 0;
        std::vector<DragState::LockedScroller> locked;  // Scrolling is off while a finger may draw a rectangle.
    } band;
    muxc::Border bandBox{nullptr};
    mud::DispatcherQueueTimer fadeTimer{nullptr};
    LabelsFilter labelsFilter;  // The tag this window's list is filtered by (labels).
    struct DetailsPane* details = nullptr;  // The merged details pane of this window, 10d.  // The pause between a fade out and the removal of rows.
    // The pill that marks the active column, on a layer over the strip.
    muxc::Canvas overlay{nullptr};
    // Drops coming in from other windows and applications, and the scroll that
    // follows a drag (inbound or of rows) near the edge of a column or the strip.
    IDropTarget* panelDrop = nullptr;
    GhostSpec outSpec;               // The label of a drag that left the panel,
    bool outDragActive = false;      // shown again when it comes back to it.
    bool outFromArchive = false;     // The drag that left the panel carries items of an archive.
    // A folder row held under dragged items opens after a pause.
    Column* springColumn = nullptr;
    int springEntry = -1;
    ULONGLONG springSince = 0;
    bool springDone = false;
    HWND panelDropWindow = nullptr;
    winrt::Windows::Foundation::Point autoPoint{};  // The pointer, host coordinates.
    bool autoOn = false;
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

bool OnDragMoved(WindowState* state, Column* column, muxc::ListView const& list,
                 mux::Input::PointerRoutedEventArgs const& args);
void OnDragHold(WindowState* state, muxc::ListView const& list);
void DragRefreshTarget(WindowState* state, winrt::Windows::Foundation::Point where);
void DragClearTarget(WindowState* state);
struct GhostSpec;
void ShowGhost(WindowState* state, GhostSpec const& spec);
GhostSpec SpecOf(Column* column, std::vector<size_t> const& entries);
void DragUpdateLabel(WindowState* state);
void RemoveGhost(WindowState* state);
bool TouchContact(uint32_t id, POINT* where);
void TouchTrack(WindowState* state, mux::Input::PointerRoutedEventArgs const& args, bool released);
void OnDragPressed(WindowState* state, muxc::ListView const& list, muxc::ListViewItem const& row,
                   mux::Input::PointerRoutedEventArgs const& args);
bool OnDragReleased(WindowState* state);
void CancelDrag(WindowState* state);
void StartDetails(WindowState* state);
void AbandonDetails(WindowState* state);
void WatchWindowClose(HWND top);  // 11-window: the window releases its objects when it is closed.
void ReleaseDetails(WindowState* state);

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

// How often a window's timer ticks. The window the person is working in gets the quick tick; a
// window left alone, or in the background, is looked at less often: every tick scans the frame.
int TickIntervalMs(HWND top, bool busy) {
    if (busy) {
        return 60;
    }
    LASTINPUTINFO input = {sizeof(input)};
    DWORD idle = GetLastInputInfo(&input) ? GetTickCount() - input.dwTime : 0;
    HWND front = GetForegroundWindow();
    bool ours = front && top && GetAncestor(front, GA_ROOT) == top;
    if (ours) {
        return idle < 2000 ? 60 : (idle < 8000 ? 120 : 250);
    }
    return idle < 2000 ? 120 : 400;
}

void RetimeTick(mud::DispatcherQueueTimer const& timer, int* current, int wanted) {
    if (timer && *current != wanted) {
        try {
            timer.Interval(std::chrono::milliseconds(wanted));
            *current = wanted;
        } catch (...) {
        }
    }
}

mux::Media::SolidColorBrush Brush(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return mux::Media::SolidColorBrush(winrt::Windows::UI::Color{a, r, g, b});
}

// The panel's background, as the setting says.
mux::Media::SolidColorBrush PanelBackground(HWND fileList, bool dark) {
    int mode = g_backgroundMode;
    COLORREF sampled = mode == 0 ? SampleListBackground(fileList) : CLR_INVALID;
    Wh_Log(L"panel background: %s",
        mode == 1            ? L"the theme colour, by setting"
        : sampled == CLR_INVALID ? L"not readable, using the theme colour"
                                 : L"taken from Explorer's file list");
    if (sampled != CLR_INVALID) {
        return Brush(255, GetRValue(sampled), GetGValue(sampled), GetBValue(sampled));
    }
    return dark ? Brush(255, 25, 25, 25) : Brush(255, 255, 255, 255);
}

