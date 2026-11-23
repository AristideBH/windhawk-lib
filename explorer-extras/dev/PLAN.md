# Explorer Columns View, plan

## Done in 0.1, tested 2026-11-12 on ARM64, build 26300.9974

- Columns entry in the View menu, inserted on first open, checked state kept.
- WinUI 3 panel over the file list, one column per folder level, chevrons.
- Explorer's own view follows the active column: tab title, address bar, Back.
- Back trims the columns to the matching folder.
- Choosing another layout removes the panel.
- Compact mode row height, double-click opens a file.

## Known gaps in 0.1

- Sort and group from the Sort menu don't reach the columns.
- Selecting a file doesn't select it in Explorer's own view, so the preview
  pane, the details pane and the command bar don't see it.
- The status bar count stays on the first folder.
- Generic glyphs instead of real file icons.
- Fixed column width, end ellipsis.
- State is per window and lost on restart.
- Folders load on the UI thread.

## 0.2, next compile

1. Sort and group. Read sort columns and group-by from the starting folder's
   view, apply to every column, re-apply when the Sort menu changes them.
   Group headers through the shell's own categorizer, so names are localized.
   Unverified API path, log everything.
2. Selection mirrored into Explorer's own view, for the native preview pane,
   details pane, status bar and command bar.
3. Column width: fit to the longest name up to a maximum, middle ellipsis,
   drag to resize, double-click the separator to refit.
4. Settings grouped in sections, see architecture.

## 0.3

- Real file icons and thumbnails.
- Keyboard: left and right between columns, type to jump.
- Native right-click menu.
- Background loading for slow folders.

## 0.4

- Drag and drop between columns.
- Scroll animation to the newest column.
- Per-folder memory and a "columns everywhere" option.
- Entry present at window creation, not on first menu open.
- English strings moved to a table for translation.

## Decisions, 2026-11-12

- The mod is named Explorer Extras, id `explorer-extras`, from 0.2 on.
- One mod with a section per feature, see architecture.
- Settings window, laid out like Windows Settings, when the second feature
  lands. Until then, Windhawk's settings page with nested sections.
- Settings entry goes in the command bar's "..." menu under Options. Untested.
- No compact-rows option of the mod's own. Explorer has it under View, Show,
  Compact view, and the columns follow it.
- A feature ships off by default while unfinished, and may default to on once
  complete.
- Off means gone: no menu entry, no hook work.
- One-time tip on the Columns entry, never shown again after dismissal.

## Feature backlog beyond columns

- [x] Labels, macOS style: built in 0.69 to 0.75 (design and plan in `docs/superpowers/`, `docs/labels.md`). Later: drag onto a sidebar tag, an index and a cross-folder view, tag presets.
- New folder from selection.
- Better archive extraction flow.
- Toolbar customization.
- Status bar and info bar customization.
- Small tweaks.

## Architecture: one Explorer mod with sections

Features beyond columns: the details pane and labels are built; planned: archive
extraction flow, toolbar customization, small tweaks.

- One mod, Explorer Extras. Each feature has its own settings
  section with an `enabled` switch, using Windhawk's nested settings.
- One shared core: hooks into FileExplorerExtensions.dll, the per-window state,
  shell helpers, logging.
- Source kept as one file per feature under `explorer-extras/dev/src/`, joined into a
  single `.wh.cpp` by a small script, since Windhawk compiles one file.
- Cost: a crash in one feature takes the others down. Each feature guards its
  entry points and can be switched off without a recompile.

## 0.3.5 notes, unverified live

- Icons: the worker reads 32 px `IShellItemImageFactory` images for the first
  400 items after the list is delivered, in batches of 16, matched by column
  generation, shown in a 20 px Image over the glyph placeholder.
- Right-click: `IContextMenu::QueryContextMenu` into a hidden HMENU, copied
  into a `MenuFlyout`, run with `InvokeCommand`. No TrackPopupMenu, because of
  the modal-loop crash class. Send to and Open with submenus may be empty
  (`HandleMenuMsg` is not called). Next retest: right-click a file and a
  folder, try Copy, Properties and Delete.

## Probe results, 2026-11-13 (`probes/probe-tabs-virtual.wh.cpp`)

New tabs:
- A "+" tab creates, on the window's own UI thread, `SHELLDLL_DefView`, then
  `DUIViewWndClassName`, then the `ShellTabWindowClass` itself, parent
  `CabinetWClass`. A hook on `CreateWindowExW` sees the tab window only after
  the call returns, with its children already there. A new window shows the
  same sequence and then `CabinetWClass`. So a post-call hook on class
  `ShellTabWindowClass` is a creation hook for tabs, on the same thread as
  the command bar, where `StateForThisThread` works.
- `SHELLDLL_DefView` is also created again on navigation inside a tab, so it
  is not a tab-creation signal.

Desktop and Control Panel items, root of the namespace (27 items):
- Virtual folders bind and enumerate: This PC, Recycle Bin, Control Panel
  (`::{26EE0668-...}`), Libraries, Linux, Gallery, Network, Home. They are
  `FOLDER` and not `FILESYSTEM`, so `IsFolderItem` is true for them.
- The Desktop's own "Control Panel" entry (`::{5399E694-...}`) and about 15
  Control Panel applets (Mouse, Keyboard, Region, ...) are neither `FOLDER`
  nor `STREAM` and don't bind. They are launchers: the column shows them as
  files, and they should be opened with the shell rather than navigated into.
- Recycle Bin items have the original path as their display name and a
  `$Recycle.Bin` path as parsing name.

## Navigation pane clicks and the unexplained jumps, 2026-11-13 (0.3.18 to 0.3.21 trace)

- `IShellBrowser::BrowseObject` is hooked through the vtable (slot 11, in
  `explorerframe.dll`). With the debug log on, each call is logged with its
  call stack.
- Navigation pane clicks (OneDrive, Columns Demo, tested at 11:27 in
  0.3.21) arrive as `flags=0x80001` (same browser, activate without focus),
  from `SHELL32+0x329730 < SHELL32+0xB10CC < dui70 < DUser < atlthunk <
  user32 < explorerframe`. No Windhawk mod is in the stack.
- The unexplained jumps (to "Bureau" 150 ms after a paste, to "Columns Demo"
  a few seconds after turning Columns on) have the same flags and the same
  stack. So they are the navigation pane navigating, not another mod. The
  likely trigger is the pane refreshing its tree after a file operation or a
  navigation by this mod. Not proven.
- Consequence for the "sidebar sets the first column" feature: a pane
  navigation can be recognised in the BrowseObject hook by flags with
  `SBSP_ACTIVATE_NOFOCUS`, not made by this mod, plus a recent mouse button
  press over the pane. The pane's tree items (This PC and below) still need
  to be told apart from the flat items. A pure-input check can't separate a
  pane click from the jump after a paste, so the paste guard stays until the
  cause is known.

## Ideas from reading DarkionAvey/windhawk-gpu-accelerated-explorer-view (GPL-2.0, ideas only)

Read on 2026-11-13, the first 200 KB of a 2.3 MB source and its README.
- Workers: per-tab private thread pools, each worker with its own COM
  apartment, interfaces never leaving it. Workers return plain pixels and the
  UI thread uploads them. Requests carry a cancel flag, and a memory budget
  caps the image cache. Ours has one worker shared by all windows: a slow
  folder blocks every window's icons. Split per window, and make icon jobs
  cancellable when a column goes away.
- Details arrive after names: names first, expensive properties later as
  "enrichment". Matches how our icons arrive. Use the same for tooltip
  details.
- Stale results are dropped by generation. We do that already.
- File changes: a burst is coalesced (first and last change time) before the
  diff runs, and the folder listing is diffed instead of replaced.
  Planned for 0.5 with item-level change events.
- Selection: captured on the owning thread, checked against item identities
  before it is applied, and a failed import blocks file commands instead of
  acting on a wrong selection. Worth copying for Delete and Cut.
- Re-entrancy: counters on every hook and callback, drained at unload;
  scoped flags stop a nested message from starting a second transaction;
  each key press is consumed once. Our guards are per feature, a common
  scoped-depth helper would be cleaner.
- An optimistic destination: the view shows the target folder before
  Explorer has built its own view. The same idea can remove the first-column
  flash when a window opens in Columns.

## Findings from 0.7 to 0.13, 2026-11-13 and 14

Tabs and Columns per tab (0.8):
- The visible `ShellTabWindowClass` identifies a tab. The tick notices a
  switch, saves the old tab's snapshot (folders, selected child, widths, active
  column, vertical scroll of each column) and restores the new tab's, or starts
  from Explorer's folder. A tab seen for the first time takes the layout chosen
  last. The panel is hidden (`bridge.Hide()`) while a tab that is not in
  Columns shows. A tab made after the panel is a window above it and covers it,
  so the panel is raised again whenever a window is above it.
- Pending navigation, selection mirroring and the focus column belong to the
  tab that left, and are dropped at a switch.

Per-folder memory (0.11):
- One string value `folderMemory` in the mod's storage: lines of
  `path TAB sort TAB width TAB columns`, 150 folders, lowercased path key. Sort
  and group are saved as `{guid},pid,direction;...` and the group key.
  Explorer's own per-folder sort is authoritative for the active column: when a
  column becomes active, the sort Explorer shows for that folder is read and
  taken. Columns per folder: choosing another layout marks the folder 0,
  choosing Columns marks it 1 (also when the panel is built from scratch, a bug
  fixed in 0.11.1). Going to a folder marked 0 turns Columns off for the tab,
  going to one marked 1 from a tab not in Columns turns it on.

Menus (0.9):
- The item and folder menus are custom `Flyout`s with a header row of icon
  buttons with labels and a list of rows, not `MenuFlyout`s: a `CommandBarFlyout`
  cannot draw two-tone icons. The shell's entries come from
  `IContextMenu::QueryContextMenu`, are classified by verb, and everything that
  is not listed is under "Show more options" (a `MenuFlyout` filled by
  `FillFlyoutItems` with a skip list).
- The two-tone icons are the SVG files Windows ships in the File Explorer
  package (path in the roadmap), loaded with
  `SvgImageSource(Uri("file:///C:/Windows/SystemApps/..."))`. Light grey plus a
  fixed blue (`#4CC2FF` in dark theme). They work from Explorer.
- Verbs seen: `cut`, `copy`, `rename`, `delete`, `openas`, `properties`,
  `pintohomefile`, `copyaspath`, `link`, `Windows.ModernShare`, `Windows.Share`
  (a submenu), `PreviousVersions`. Compress to and the other new Windows 11
  entries are not in `QueryContextMenu`.

Drag and drop (0.10, 0.12, 0.13):
- Between columns the mod follows the pointer itself (a label in the overlay
  canvas, a tint on the target row or column) and gives the dragged items to
  the target folder's `IDropTarget` (`DragEnter`, `DragOver`, `Drop`), so the
  shell does the work and shows its own dialogs. The items' `IDataObject` comes
  from `IShellFolder::GetUIObjectOf(IID_IDataObject)`. Keys: Ctrl copy, Shift
  move, Alt link; the shell chooses a move on the same drive and a copy to
  another.
- A move done by the target returns effect 0. Copy returns 1.
- A row marks its own press handled, so the drag starts from the handler
  registered with `handledEventsToo`.
- Dragging out: when the pointer leaves the panel's root grid, the panel's drag
  ends and `SHDoDragDrop` starts (after the event, through the dispatcher
  queue) with a data object from the same call and an own `IDropSource` that
  logs what the target offered and where the button went up. The key state the
  drag loop gives (`MK_CONTROL` and so on) is correct, but a key let go just
  before the button must still count, so the last time each was seen is kept
  and `Preferred DropEffect` is set in the data at the moment of release.
  Works onto the desktop (`Progman`, `SysListView32`) and for a move or a
  copy, and since 0.14.4 onto every target, see the next section. The Windows 11
  window has an invisible resize border of about 6 px: a release inside it is
  over the Explorer window, not the desktop.

Cloud and remote marks (0.7.8 to 0.7.11):
- OneDrive placeholder: `FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS`; downloaded
  cloud file: reparse tag `0x9000001A` (mask `0xF000FFFF`); always kept:
  `FILE_ATTRIBUTE_PINNED` (0x80000). A OneDrive image that cannot be opened is
  not the mod's doing (it fails from Explorer's own view too).

Not solved, parked:
- A click stopping a scroll that glides (mouse): the glide is probably a stream
  of wheel events. Tried ChangeView to the same offset, a nudge, toggling the
  scroll mode and inertia, scrolling the vertical wheel in the mod and dropping
  turns after a press.

## Findings from 0.14 to 0.18, 2026-11-14

The shell's drag loop and the XAML island (0.14):
- The panel's input is a XAML island that takes the mouse as pointer messages.
  A drag started from it with `SHDoDragDrop` gets no `WM_MOUSEMOVE`: the loop
  called `GiveFeedback` once, at the release, so a target only heard the
  release point. The desktop and most views take a drop from that alone; the
  navigation pane tree needs a hovered item and ignored it.
- Fix (0.14.4): `MoveNudger`, a thread that posts `WM_MOUSEMOVE` with the real
  position and key state to the drag loop's thread while it runs. Everything
  then hovers and answers: the navigation pane, any view of another Explorer
  window, the desktop, other applications.
- Before that, 0.14.0 to 0.14.3 finished a release over the navigation pane by
  hand: hit test the tree (`TVM_HITTEST`), walk the item's names to a folder
  (Home, then the known folders, the Desktop, the profile, This PC), and give the
  items to that folder's drop target. It stays as a fallback and runs only when
  the tree left the effect at the whole offer (7).
- Never call a drop target's `DragEnter` and `DragLeave` by hand from outside
  the drag: it crashed Explorer twice in `explorerframe.dll` (0.13.8).
- Drops coming in (0.15): the panel registers its own `IDropTarget` on the
  bridge window (`RegisterDragDrop`, after `RevokeDragDrop` of the island's), and
  hands the data object to the folder under the pointer through that folder's own
  drop target (`CreateViewObject(IDropTarget)`), so the shell decides the effect
  and does the work. A drag from elsewhere gets the shell's image through
  `IDropTargetHelper`. Our own drag coming back gets the label it had.
- Edge scroll: the tick scrolls a column near its top or bottom and the strip
  near its left or right, for both kinds of drag.

The drag label, the frame, the icon (0.16 to 0.18):
- The label is a `ToolTip` control in the overlay canvas: that gives the default
  tooltip surface with no stroke of its own. Icons are the row images' sources.
  It fades and scales in and out with a `Storyboard` on opacity and a
  `ScaleTransform`.
- The drop target is an accent framed border in the overlay, placed from
  `TransformToVisual`, re-placed on every move and scroll step.
- The View button of the command bar is an `AppBarButton`: `Icon` holds an
  `ImageIcon` inside a `Viewbox`. The Viewbox scales the icon to fill its box, so
  a replacement must have the same square box (16 x 16 here) or it comes out
  bigger; the drawing is then scaled and moved inside it. A `PathIcon` made with
  `XamlReader::Load` takes `Foreground='{ThemeResource TextFillColorSecondaryBrush}'`.
- A selected row's background is the first `Border` inside its
  `ListViewItemPresenter`, inset 4 px at the sides and 2.3 px above and below.
  Changing its `Margin` and `CornerRadius` joins a run of selected rows; the
  item's own `CornerRadius` has no effect on it.
- A row's hover under a resting pointer stays through keyboard navigation. The
  rows are made not hit-testable on the first key and given back when the cursor
  moves (polled in the tick) or the wheel turns.
- QuickLook (and other previewers) read the selection from Explorer's own view:
  a selection in the folder Explorer already shows is mirrored at once, others
  after 120 ms for files (350 ms for folders).

## Sidebar probes, 2026-11-23 (Windows 11 24H2 build 26300, ARM64, French)

Plan: `docs/superpowers/plans/2026-11-23-sidebar.md`, Task 1. Pins and scan are standalone PowerShell scripts, not Windhawk mods: they only need shell and file APIs, which a mod can call identically.

- **Pins (`probes/probe-sidebar-pins.ps1`), confirmed.** `Shell.Application` on `shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}` lists the pinned folders in the shell's order (here Bureau, Téléchargements, Documents, Code, Images, Showcase, the user folder). The folder verb `pintohome` pins, and `unpinfromhome` on the item in that namespace unpins; both took effect within 1.5 s. The store is `%APPDATA%\Microsoft\Windows\Recent\AutomaticDestinations\f01b4d95cf55d32a.automaticDestinations-ms`, and its last-write time changes on a pin: a `ReadDirectoryChangesW` watch on that folder (filter on that file name) is the way to notice pins made elsewhere. A new pin goes to the end of the list. Reordering was not tested; no API route is known, so Favorites follow the shell's order and Task 6 builds no reordering. The Windows verb names are localized ("Épingler à l'accès rapide"): use the verb ids `pintohome` and `unpinfromhome`, never the displayed names. A third-party shell extension (`HonorFileShareMenu`) logs noise while verbs are enumerated; harmless.
- **Scan (`probes/probe-sidebar-scan.ps1`), confirmed.** C: holds about 998,000 files in 123,000 folders. A single background-priority pass with the exclusions, skipping reparse points and recall-on-open files, finished in 90 s (about 11,000 files/s), found 146 files with the `:ExplorerExtras.Labels` stream, 5 access errors, 5,400 items skipped, and grew memory by nothing noticeable. Conclusion: a plain walk is fast enough for the first pass; the USN journal is not needed. Rescans at start-up can use the same walk with a last-write comparison of folders.
- **Animation (`probes/probe-sidebar-anim.wh.cpp`), confirmed for opacity.** In an island over the pane, an `OpacityTransition` on the rows and a chevron pair cross-faded shows intermediate frames (sampled brightness 32, 34, 40, 45 rising and 45, 42, 36, 33, 32 falling, about 150 ms each way) and not a jump. `TranslationTransition` was set with the fade but its motion was not measured here; the earlier details-pane finding says it animates, so keep the slide but do not depend on it. Collapsing the layout after the fade is a plain `Visibility` change.
- **Island over the native pane, partly confirmed.** A `DesktopWindowXamlSource` placed on the `NamespaceTreeControl` rectangle (263 x 1079 at client 2,136) hides the native tree completely and follows a 100 ms rectangle timer. Created from a `SetTimer` callback 1.5 s after a `CreateWindowExW` hook sees `ShellTabWindowClass`, on the window's own thread. Splitter drag, the View > Show > Navigation pane toggle, new tabs and a second window were checked by hand by Ari. Probe 0.1 stopped following with a second tab (it kept the first tab's hidden pane); 0.2 picks the pane that is visible through all its parents each tick, hides when none is, and works for two tabs, a second window and the splitter. Rule for the sidebar: follow the visible `NamespaceTreeControl`, never the first one found.
- **Hiding the native pane (probe 0.3 log, confirmed).** View, Show, Navigation pane off sets `NamespaceTreeControl` and its parent `CtrlNotifySink` invisible (`IsWindowVisible` 0) and leaves their geometry unchanged; on again they are visible at the same rectangle. The sidebar island must therefore be shown only while a visible `NamespaceTreeControl` exists in the window, tested every tick, and placed on that control rectangle.
