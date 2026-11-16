# Explorer Extras roadmap

Checklist of features and settings by version. Tick a box when the item is
compiled and tested by Ari. Details live in `explorer-extras/PLAN.md` and
the `SPEC` files next to it. State on 2026-11-13: version 0.5.1, tested.

## 0.1, done and tested 2026-11-12

- [x] Columns entry in the View menu
- [x] One column per folder level, chevron on folders
- [x] Explorer's view follows the active column: tab title, address bar, Back
- [x] Back trims the columns
- [x] Choosing another layout removes the panel
- [x] Row height follows Explorer's compact view
- [x] Double-click opens a file
- [x] Settings: column width, follow active column, maximum items, debug log

## 0.2

- [x] Rename to Explorer Extras, settings grouped under a Columns section
- [x] Sort from the starting folder applied to every column
- [x] Group headers, from the mod's own ranges, in English for now
- [x] Sort menu changes re-sort every column, with nothing written to folders
- [x] Selection mirrored into Explorer's view: preview pane, details pane,
      status bar, command bar (tested from 0.3.8, multiple items too)
- [x] Column width fitted to the longest name, with minimum and maximum
- [x] Middle ellipsis on long names, full name in a tooltip
- [x] Drag to resize a column, double-click to refit
- [x] Rows built only when visible (done in 0.5.0 and 0.5.1)
- [x] Folders read on a background thread
- [x] Explorer's view follows with a 350 ms delay
- [x] Hidden files follow Explorer's setting
- [x] Settings: enabled, minimum width, maximum width, follow active column,
      maximum items, debug log

## 0.2.1, fixes from the 0.2 test, see `explorer-extras/SPEC-0.2.1.md`

- [x] Crash with a second Explorer window found and fixed
- [x] Log kept across restarts, with thread ids
- [x] Columns entry present after Explorer restarts
- [x] Columns update after copy, rename, delete and Refresh
- [x] Back, Up and outside navigation keep the column path
- [x] Active column marked
- [x] Keyboard: left, right and Enter between columns
- [x] Grouping works. The address bar focus could not be reproduced
- [x] Resize cursor and hover handle on column separators
- [x] Compact view applies, hidden files and compact view update live

## 0.3, done and tested

- [x] Columns present when a window opens, and remembered across restarts
      (hook on `CommandBarControl_Wave1::OnDpiChanged`, see the skill)
- [x] A new tab opens in Columns (hook on the tab window's creation)
- [x] Real file icons and thumbnails, read on a worker thread
- [x] Right-click menu on an item: the shell's commands in a XAML flyout,
      Copy, Cut, Paste, Rename in place, Delete, Folder properties
- [x] Right-click on empty space: Paste, New folder with rename, Refresh,
      Properties ("Personalize" on the Desktop)
- [x] Keyboard: Delete, Shift+Delete, Ctrl+C, Ctrl+X, Ctrl+V, F2, Ctrl+A,
      Alt+Enter, Ctrl+Z (deletes and renames), type to jump
- [x] Multiple selection with Ctrl and Shift, mirrored into Explorer's view
- [x] Selection by a rectangle, from empty space, a header or the right strip
- [x] Horizontal wheel glides across the columns, vertical wheel scrolls only
      the column under the pointer
- [x] Control Panel in columns, launchers open with Enter
- [x] Stale columns (a file chosen after a folder) stay, dimmed, with a
      stronger chevron, and come back when the folder is chosen again
- [x] Active column marked by an accent pill that glides
- [x] Sidebar sets the first column (setting, off by default)
- [x] Panel background taken from Explorer's own file list
- [x] Slow second click on a selected name starts a rename
- [x] Toolbar commands refresh the columns and show a rename and a cut
- [x] Group headers collapse and open, animated, with hover
- [x] Detail tooltips (type, size, dates, dimensions, tags and more)
- [x] Compact scrollbars (setting) with a free strip beside the bar
- [x] Focus outline on the keyboard item (setting)
- [x] A reader thread per window, cancelled loads, a limit that cuts after the
      sort (default 5000)
- [x] Rows built only when visible, icons for every built row

## Where we stopped, 2026-11-14 (version 0.15.0, since passed: see the plan from 0.18.5)

Done and tested by Ari: everything in the plan table through 0.12.3 (columns
per tab, menus, per-folder memory with three toggles, drag between columns,
Ctrl copy into the same folder). 0.13.x, drag out of the panel: works to the
desktop (a plain drag moves, Ctrl copies, Ctrl may be let go up to 400 ms
before the button). Everything below is open.

Open, most useful first:

1. Drag out: done and tested in 0.14.4. Cause of everything that failed before: the
   shell's drag loop gets no pointer moves from the panel's input (a XAML island),
   so a target only heard of the release point. A helper thread now posts the
   moves to the loop (`MoveNudger`), and the navigation pane, other windows (any
   view), the desktop and other apps hover and answer natively. The mod's own
   finishing of a drop on the nav pane (a name walk to a folder) stays as a
   fallback for a tree that gives no answer. Never call a drop target's
   `DragEnter`/`DragLeave` by hand: it crashed Explorer twice (explorerframe.dll).
2. `dragOut` is off by default after the first test hung Explorer. The hang
   was not reproduced (it came right after the mod was replaced while a window
   was in Columns). When it is switched on and works for a while, make it
   default on.
3. Drops coming in from other windows and apps, and edge auto-scroll: built and tested
   in 0.15.0. The panel registers its own OLE drop target on its window and
   hands the drop to the folder under the pointer (row or column), through that
   folder's own drop target, so the shell does the move, copy or link. The target is
   lit like in the drag of rows. A column scrolls near its top or bottom edge and the
   strip near its left or right edge, for inbound drags and for the drag of rows.
4. Still open while dragging: dragging out of an archive column is refused; the
   dragged label of the drag of rows between columns is weak (count badge,
   translucent), see 1b.
5. The folder (background) menu and the item menu build their rows with
   similar code that should be shared; their visuals get a dedicated pass
   (spacing, icon sizes, the Windows icon files in
   `C:\Windows\SystemApps\MicrosoftWindows.Client.FileExp_cw5n1h2txyewy\
   FileExplorerExtensions\Assets\images\contrast-standard\theme-dark|light`).
6. Parked: a click should stop a scroll that is still gliding (tried four
   ways in 0.7.12 to 0.7.15, none worked with a mouse click).
7. Small: focusing virtual shell items on the Desktop (Ari never described what
   fails), a syncing state for OneDrive marks, MTP/phone detection is a guess,
   the remote marks log lines.
8. Toward 1.0: strings in a table for translation, remove the test settings
   (transparent and Mica background), the README, the pull request to
   windhawk-mods, update the skills one last time.
9. Ideas not started: Paste in the item menu (Ari said no need), labels,
   toolbar customization, Home page overhaul, details columns auto-fit, GPU
   Explorer view compatibility (Ari tests that himself).

Branches: work happens on `claude/version-0-3-continuation-1f97fb`;
`wip/drag-0.12` is an old side branch already merged and can be deleted.

## Plan from 0.18.5, drawn up 2026-11-14

State: 0.18.6 is committed and tested by Ari (the drag label, drop frame,
keepStale, view button icon, stale badge, QuickLook speed, harmonize widths,
border handle, connected selection). 0.18.7 was the housekeeping.

| Version | Content |
| --- | --- |
| 0.18.7 | Done: 0.16 to 0.18 committed, README, HANDOFF, PLAN and the skills updated (drag loop findings, MoveNudger, bridge drop target, ToolTip surface, AppBarButton icon, presenter border). |
| 0.19 | Selection polish: one accent bar per run of selected rows, toolbar Select all, Invert selection and Select none, Space preview left to QuickLook. |
| 0.20 | Done and tested: drag out of an archive column (always a copy, the label says Extract to), spring-loaded folders (hold dragged items on a folder row for 0.7 s, setting `springLoad`). Not built: drops onto a tab or a breadcrumb (to be tried with the native targets first), drag image outside the panel, autoscroll tuning. |
| 0.21 | Done and tested in 0.21.2: OneDrive syncing mark (storage provider transfer progress), phone detection (Pixel 8 over MTP, Phone Link's CrossDevice folder), one log line per folder for the marks, one attribute call per item, a timing line for slow folders, and no more Copy/Move/Cancel popup on a drop coming in (the button of the drag is kept through the release). The Documents icon was fixed on the way. Left: focusing virtual Desktop items (Ari never described what fails). |
| 0.22 | Done and tested in 0.22.0: icon requests newest first and for the rows on screen, folders ahead of icons, a cache-only icon pass before the full one, timing lines (`fill:` from 40 ms, `load:` from 500 ms). Measured on the Huge folder (1500 files), Photos Unsorted and a phone's Camera folder (972 items): no fill or load reached the thresholds, so diffing a folder on change is not needed now. |
| 0.23 | Menus. 0.23.0 done: one `MenuBuilder` and `MenuStyle` shared by the item and folder menus. Next: the visual pass from Ari's mockups, see `explorer-extras/MENU-MOCKUP-NOTES.md` for what to send (waiting for them). |
| 0.24 to 0.28 | Settings shell (0.24 to 0.27 done and tested: schema, the window, live apply and Reset, the Extras entry in the toolbar's ... menu, Home actions, tip and what's new; 0.28 done: upcoming pages behind the showUpcoming flag and the window and menu strings in the table; the settings shell is complete), spec in `explorer-extras/SPEC-settings-shell.md` (draft, 2026-11-14): schema and generated Windhawk block, the window (Mica, sidebar, Home and Columns pages), live apply and reset, the "..." menu entry, status and tips, upcoming pages behind a flag. Done before touch and release prep. |
| 0.29 to 0.32 | Touch, done and tested in 0.32.1 (Ari's call 2026-11-14: required before 1.0). Hold and lift opens the flyout; hold then move starts the system's own drag (`StartDragAsync`, files plus a shell item list) with our label as its picture; drops inside the panel go through XAML drag events, to the desktop and other windows and onto the sidebar through the system; a finger on the empty space below a column draws a selection rectangle. Also in 0.30 to 0.32: the setting `lastEdge` (a border after the last column), the Columns entry on a narrow window (the View button in the "..." overflow), Desktop and Home as coming-soon pages, the first sidebar page renamed Explorer Extras. What the device taught is in "Touch plan" below. Left: a touch drop on a sidebar folder shows Explorer's Copy/Move popup (the key state of the system's drag has no button; not fixable from the mod). |
| 0.33 to 0.34 | Done and tested in 0.34.1. The file flyout rebuilt from Ari's Figma (320 px, quick row of 40 px icon buttons with Properties pinned right, rows on a 34 px pitch, groups: Open, Open in new tab, Open with / Extract, Compress to, New folder from selection, Copy as path, the format's main action / More options). Open shows the icon of the app that opens the file, Open with lists the apps (the shell fills it on WM_INITMENUPOPUP), Compress to ZIP, 7z and TAR through Windows' tar.exe, Extract here, to a named folder, all. It behaves as a context menu: submenus open on hover beside the row (transient, so another replaces them), a click outside closes it, the menu avoids the clicked row and the quick row goes under the list when it opens upwards. 0.34: surgical column refresh, a reload patches the rows (removed, new, kept) instead of rebuilding them; grouped, cut-short and mostly changed columns still rebuild. Left: the folder and empty-space flyouts have the new look but not their own mockups (grouped columns were patched in 0.35; app extensions rows dropped). |
| 0.35 | Done and tested in 0.35.5: grouped columns patch in place (a group header is a row, kept by its title), the empty-space flyout is one quick row (Paste, New folder, Refresh, Properties or Personalize pinned right), rows fade in and out in 100 ms (the default transitions are off), and the slide of the rows below is parked. The app extensions rows are dropped for good: the shell's menu data does not list them, so everything they would hold stays in More options. |
| 0.36 | Release-candidate cleanup, 2026-11-15 (Ari's answers): the settings window's sidebar is a rail of icons below 760 px wide, opened over the page by a menu button; the Transparent background test mode and its code are gone; the debug log is off by default; archives browsing is on by default; compactScrollbars is gone (always on); the showUpcoming flag and the placeholder pages are gone (a feature gets its page when it is built); the fade timer is stopped at teardown. 23 settings left. 0.36.2 to 0.36.7: option titles and descriptions shortened; follow-active-column, drag-out and archives are always on (20 settings); the rail and its overlay fixed (item minimum width, column span); the mod description and README rewritten, with placeholder captures in explorer-extras/assets/captures; x86-64 and ARM64 compile and link against Windhawk's symbols only. To do: reload and uninstall walk on the device, real captures, the showcase video. |
| 0.37 to 0.52 | The details pane, the first feature that is not Columns (spec and probe results in `explorer-extras/SPEC-details-pane.md`), 2026-11-15 and 16, tested by Ari through 0.52.2. One XAML pane over Explorer's own Details pane window, so it works in Columns and in every default view: thumbnail or icon, name and type, action row (Open, Copy path, Properties), the properties by type with a "More details" part, several items as a stack of thumbnails, folders with their count, nothing selected as the folder's summary. Selection changes crossfade (the preview slides in the direction of the move), the pane slides in and out. Previews drawn by the pane itself: PDF (Windows' PDF API), text and code, Markdown (headings, lists, quotes, code, bold, italic, links), images at full size with wheel, pinch and double-tap zoom and pan, video and sound with the pane's own thin controls (volume kept). Office types use the shell's preview handler in a native window (cannot scroll). Alt+P is swallowed by a keyboard hook and becomes Alt+Shift+P; the View menu's two pane entries are replaced by one, with the navigation pane entry moved up beside it; the toolbar's Details button is replaced by ours (one-colour icon while ticked). Also in this stretch: Columns settings (folders first, alternate row shading, accent chevron badge with an animated entry and exit), rename by click only on the active column, a rename is cancelled by a click elsewhere, the settings window opens on its first section without a title or icon and has an About card, a sidebar click gives the keyboard to the first column. 28 settings. Dropped: the full-window image viewer. Left: Office previews in XAML, drag from the pane, folder size on demand, labels and tags. |
| 0.9x, release candidates | Remaining: x64 and arm64 builds, uninstall and reload safety, README with captures, the showcase video. |
| 1.0 | The pull request to windhawk-mods, skills updated one last time. |
| later | Labels, toolbar customization, Home page overhaul, details columns auto-fit, GPU Explorer view compatibility, Space preview, and as other mods: sidebar overhaul (Ari has one), preview and details pane, status bar options. |

## Plan for the next versions, drawn up 2026-11-13

Order is by value for daily use, then risk. Low priority, by Ari: Home page
overhaul, details columns auto-fit, GPU Explorer view compatibility. They go
last and are not scheduled.

| Version | Content | Effort | Risk |
| --- | --- | --- | --- |
| 0.6.x | Done: everyday keys, redo, live accent, multi-rename, undo of a new folder, columns of a deleted folder pruned. | | |
| 0.7.0 | Done and tested: new folder with selection (flyout item and Ctrl+Alt+N, one Ctrl+Z takes it back) and open in a new tab (flyout item and middle-click). | small | low. The new tab is made with Ctrl+T sent to the window, then shown its folder once the tab window exists. |
| 0.7.1 to 0.7.6 | Done and tested: archives in columns (setting, off by default): browsed as folders, Extract here, Extract selected here from inside an archive, the extracted items are selected and centred, a spinner while an archive is read. Inside an archive the flyout offers only Copy and no Properties, Paste or New folder (Properties on an archive item crashed Explorer, 0xC0000409). | medium | low |
| 0.7.2 | Small tweaks: icon jobs for the rows on screen first, show only real Desktop items (setting), Documents icon, focusing a virtual shell item, remote item indicator. | small each | low |
| 0.7.3 | Done in 0.7.15: the source is split into `explorer-extras/src/` parts joined by `build.py` (the generated file is byte-identical to before). | medium | low |
| 0.8 | Done and tested in 0.8.4: columns per tab. Each tab keeps its own stack (folders, selection, widths, active column), put back when the tab is shown again. Columns is per tab: the panel hides while a tab in another layout shows, and a tab seen for the first time takes the layout chosen last. The panel is raised when a tab made later covers it. The vertical scroll of each column is kept too, and a tab with no saved columns clears the old ones first (this fixed the overlap with the previous tab's content). Tested in 0.8.4. | large | medium |
| 0.9 | Done in 0.9.7 (menus tested, visual tuning left for a dedicated pass): the item menu and the folder (background) menu are custom flyouts: an icon row on top with labels, the rest listed below, Windows' own two-tone icons read from the File Explorer package, Show more options for the shell's other entries. Known debt: the two menus build their rows with similar code that could be shared. | medium | medium |
| 0.10 | Done and tested in 0.12.3: drag rows to a folder row or to another column (Ctrl copies, also into the same folder, Shift moves, Alt links, Escape cancels), the drop goes through the folder's own drop target. Not done: dragging out to other windows and apps, auto-scroll while dragging, dragging out of an archive. | large | medium |
| 0.11 | Done and tested in 0.11.0: per-folder memory kept across restarts (the last 150 folders): each column has its own sort and group (a toolbar Sort or Group change applies to the active column, and the sort Explorer has for a folder is taken when the column becomes active), the width of a resized column, and whether a folder is shown in Columns (choosing another layout in a folder, or Columns in it, is remembered; navigating to a remembered folder switches the tab). Limits: the Columns-per-folder switch only works while some tab is in Columns, and each Explorer process keeps its own copy of the memory. | medium | low |
| 0.12 | Done and tested: drag and drop between columns (0.12.3), three toggles for the per-folder memory (`rememberSort`, `rememberWidth`, `rememberLayout`). | | |
| 0.13 | Drag out of the panel with the shell's drag, setting `dragOut` (off by default). Works onto the desktop for move and copy (0.13.6). Does not work onto the navigation pane. | | |
| 1.0 | Release: strings in a table for translation, settings cleaned of test options, README, the pull request to windhawk-mods. | medium | low |
| later | Home page overhaul, details columns auto-fit, GPU Explorer view compatibility, Labels, toolbar customization, (the odd/even row background in the Columns views was done in 0.42). | | |

Why this order: the keys and multi-rename are cheap and used all day. The
source split costs a day and makes every change after it easier to review.
Columns per tab is the biggest gap and the biggest change, so it comes after
the split. Drag and drop and the flyout restyle are polish on a working
whole. The three low priority items depend on things we don't control: a
DirectUI hook, another author's mod, a design call.

## What the touch work found, 2026-11-14 (0.28.1 to 0.32.1, on an ARM64 touch screen)

Read this before the plan below, which was drawn up before any of it was known.

- A finger on a row is `PointerDeviceType.Touch` with the left button flag and contact on. `Holding` Started comes ~0.5 s after the press, Completed on the lift, then `RightTapped` (the menu).
- At the first real move after a hold the system takes the finger from the row: `PointerCaptureLost`, the pointer id changes, and no move of that contact reaches the panel again. Locking every `ScrollViewer` above the row did not stop it. So nothing built on XAML pointer events, or on a shell drag loop fed by them, can follow a finger.
- `GetPointerInfo` fails (error 87) with XAML's pointer id. `WM_POINTER` reaches none of the 33 windows of the frame (subclassed to check). The cursor does not follow the finger during a drag. `GetAsyncKeyState(VK_LBUTTON)` stays up.
- What works: `UIElement.StartDragAsync(pointerPoint)` called from the move event that crosses the threshold, while the row still has the finger. The files are fetched during the hold (`StorageFile.GetFileFromPathAsync`), since the call must be synchronous in the event. The data carries the storage items and a `Shell IDList Array` provider (the shell's own targets read it), and `DragUI.SetContentFromSoftwareBitmap` takes a bitmap of our label drawn off screen with `RenderTargetBitmap` during the hold. Start it on the pressed row, not the list, or the picture is the whole list.
- The system's drag never reaches the panel's `IDropTarget`. Drops inside the panel come as XAML `DragEnter/DragOver/Drop` on the root grid (set `AllowDrop` for the length of the drag, answer `AcceptedOperation` Move, Copy with Ctrl; leave `RequestedOperation` unset or the drop never fires). The drop itself reuses the mouse drag's code.
- Rubber band: the scrollers are locked at the press on empty space and the capture is taken back when lost (it was not needed to start the rectangle, it works from the first move).
- The sidebar takes the drop (files on a folder, a folder between items to pin it), with Explorer's Copy/Move popup because the drop arrives with no mouse button in its key state.

## Touch plan (0.29), drawn up 2026-11-14, before the above

Goal: everything the mouse does for drag and drop works with a finger and a
pen, without breaking panning, scrolling and tapping. To do on Ari's touch
screen, with logs, since none of it can be judged from code.

1. Telling the pointers apart. `PointerRoutedEventArgs` gives `PointerDeviceType`
   (Mouse, Touch, Pen). A touch press must not start a drag by moving: that is a
   pan of the column. A drag starts after a press and hold (about 400 ms, no
   move beyond 8 px), with a small haptic-style cue (the row lifts: scale, shadow
   or accent frame), and only then does moving drag. A pen drags like a mouse
   with its barrel or tip contact and no hold; to check.
2. Panning and the hold. The column's scroll viewer pans by touch. While the
   hold timer runs a move cancels it and the pan proceeds; after the lift the
   scroll viewer must stop panning (capture the pointer, `CancelDirectManipulations`).
3. The context menu. A hold and release on a row opens the item menu today
   (right-tap). The lift must come first and the menu only when the finger is
   released without moving. Same for an empty space and the folder menu.
4. The drag out of the panel with the shell's loop. `SHDoDragDrop` ends when the
   mouse button is not down: for touch the button is never down, so
   `QueryContinueDrag` would end the drag at once. It has to read the contact
   state from the XAML pointer (a flag set by `PointerReleased` and
   `PointerCanceled`) and the `MoveNudger` has to post the finger's position,
   since the system cursor does not follow a finger (`GetCursorPos` may be stale).
   To verify on the device: what `WindowFromPoint` and `GetCursorPos` give during
   a touch drag.
5. The label and frame. The label goes above the finger, not below and right,
   since the hand hides it. The drop frame and the spring-loaded folder work
   the same (pointer position, dwell), with a longer dwell for a finger (1 s).
   Edge scroll zones wider for touch (72 px).
6. Drops coming in. Touch drags from other windows and apps arrive through the
   panel's `IDropTarget` like a mouse drag; check the hover and the helper image.
7. Targets. Rows are 32 px: Explorer's touch mode uses taller rows. Option to
   follow the "touch keyboard and touch mode" spacing of Explorer, or a setting
   for taller rows. Chevron and border grips need a wider touch target (the
   border grip is 9 px).
8. Also by touch: tap to select, double tap to open, tap on the empty strip
   to pick the column, Shift and Ctrl selection by the selection mode of the
   list (touch has no modifier keys: a check-box mode or a press and hold to
   enter multi-select), rename (slow second tap) must not fire while panning,
   Harmonize widths needs a gesture (two-finger tap on a border?).

Order: 1 to 3 (hold, pan, menu), then 4 and 5 (drag out and label) on the
device, then 6 to 8. Log the pointer type in the existing drag lines so the
first test says what the platform does.


## Ideas taken from three other mods, 2026-11-13, ideas only

- `luizbizzio/archive-preview` (Apache-2.0): browses archives in the preview
  pane, through the shell's own archive folders, with no extraction engine of
  its own, and Extract here, Extract to and Extract selected. Two uses for us.
  Columns can browse an archive as a folder the same way (0.6.2). And it
  already works with the columns: a selected archive is selected in Explorer's
  view, so the preview pane shows its content. Ari tests the two together.
  Its limits are ours too: local fixed drives, no password prompt, big
  archives are slow, a few thousand items per request.
- `krlvm/AccentColorizer-E11`: recolours Explorer's glyphs with the accent
  colour and follows an accent change while running. For us only the second
  half: the pill and the selection rectangle read the accent at start, and
  should follow it (0.6.0). Accent coloured chevrons and placeholder glyphs
  are a possible setting, low priority.
- `krlvm/DragDropNormalizer` (GPL-3.0, standalone): restores the translucent
  drag overlay of Windows 11 21H2, which 22H2 made solid white, by patching
  the overlay's style in memory. For 0.9: drag with the shell's own drag
  helper, so the overlay is Explorer's, and don't draw one by hand. An option
  to make the overlay translucent again is a later idea, written fresh, since
  the licence forbids copying.

## Open

- [x] Keyboard on group headers (0.5.2, a setting, off by default since
      0.5.16): Up from the first item of a group goes to its header, Left and
      Right collapse and open it, Enter toggles, Up and Down move on.
- [x] Tested in 0.6.3: rename several items at once (F2 or the toolbar, one name, numbered like Explorer). Ctrl+Z takes back a new folder or a group of renames.
- [ ] Focusing a virtual shell item, such as a link, in the Desktop folder
- [ ] Open in Columns without the flash of Explorer's own list. Fine for now.
- [ ] The Documents folder icon shows small. Maybe the icon cache.
- [ ] Indicator on remote items: phone, network
- [ ] Icons for rows only read for the first 400 items were fixed in 0.5.1,
      but icon jobs are not prioritised by what is on screen
- [ ] Compatibility with `DarkionAvey/windhawk-gpu-accelerated-explorer-view`
      (GPL-2.0, replaces the Details and Icons views with its own GPU
      overlay). Ari tests the two together first. If they conflict: detect
      its window property `WindhawkExplorerGpuOverlayOwnsShellView` on the
      native view, keep our panel above its overlay, and don't depend on a
      visible `SHELLDLL_DefView`. Never copy its code, only ideas.
- [ ] Ideas to take from that mod: middle-click opens a folder in a new tab,
      a drag preview with a count badge, a toggle back to the native view
      with a hotkey.
- [ ] Menu flyouts restyled to match Explorer's own, and Paste in the item
      menu. The native look needs a probe, since a modal menu inside a XAML
      handler crashed Explorer before.
- [ ] PARKED, Ari's call 2026-11-13: transparent background mode. It works
      after a restart, but after a layout switch the native view still shows
      through even though every visible view window is made transparent
      (0.4.18). Don't chase it again without a new idea.
- [ ] PARKED: a panel background that blurs like Explorer's (Mica). Not
      reachable with the WinUI headers of this Windhawk build.

## Found in a review of the whole session, 2026-11-13

Keyboard and everyday gaps:
- [x] Tested through 0.6.3 (Backspace, F5, Menu key still to confirm): F5 refreshes the columns, Escape clears the
      selection, Backspace goes to the parent column, the Menu key and
      Shift+F10 open the item's flyout, Ctrl+Shift+N makes a new folder,
      Ctrl+Shift+C copies the paths, Ctrl+Y redoes a delete or a rename
- [x] The pill and the selection rectangle follow a
      change of the system accent or theme
- [x] Middle-click opens a folder in a new tab (0.7.0)
- [ ] Space for a quick preview, if Explorer's preview pane can be driven

Structure and polish:
- [ ] Columns per tab: the panel is per window, so switching tabs rebuilds the
      columns from the new tab's folder. Each tab should keep its own columns.
- [ ] The pill's accent colour is read when Columns turns on, so it doesn't
      follow a live theme or accent change
- [ ] Icon jobs are not prioritised by what is on screen
- [ ] Texts are English only for now ("Paste", "New folder", "N more items
      not shown", group names from the fallback): the string table in 0.6
- [ ] The Documents icon, a virtual item in the Desktop folder, remote item
      indicator (all under Open above)
- [ ] The skills `windhawk-explorer-surfaces` and `windhawk-mod-development`
      should get what this session proved: the tab creation hook, the
      navigation pane click signature in `BrowseObject`, the path-versus-pidl
      rooting, per-window workers, and the local clang syntax check

## 0.6

- [ ] Drag and drop between columns
- [ ] Scroll animation to the newest column
- [ ] Per-folder memory and a "columns everywhere" setting
- [ ] Strings moved to a table for translation
- [ ] Setting: each column keeps its own folder's sort and group
- [ ] Item-level change events update single rows, and a folder is diffed
      instead of replaced

## With the second feature

- [ ] Settings window laid out like Windows Settings, in sync with
      Windhawk's page
- [ ] "Extras settings" entry in the "..." menu under Options
- [ ] One-time tip on the Columns entry
- [ ] Source split into one file per feature, joined by a script

## Feature backlog, order open

- [ ] Labels, macOS style
- [ ] Home page overhaul
- [ ] Option to show only real Desktop items, hiding the virtual ones (This
      PC, Recycle Bin, Control Panel, Libraries and so on). The toggle's form
      is open.
- [x] New folder from selection, from the right-click menu (tested in 0.7.0)
      and with a shortcut. The command bar's New button is still open. Design: make "New folder" like Ctrl+Shift+N, then move the
      selected items into it with the shell's own move, so conflicts and
      progress behave as in Explorer, then edit the name. The undo entry holds
      the moves and the folder. The command bar's New button is a later step.
- [ ] Toolbar selection actions in Columns: Select all, Invert selection and
      Select none (Ctrl+A already works), acting on the active column and
      mirrored into Explorer's view. Later, Ari's call 2026-11-14.
- [ ] Mod ideas to note, 2026-11-14, separate mods or later features:
      sidebar overhaul (Ari already has a mod for the sidebar, so look at it
      first and don't duplicate), preview and details pane overhaul, status
      bar options.
- [ ] Better archive extraction flow
- [ ] Toolbar customization
- [ ] Status bar and info bar customization
- [x] Double-click on empty space goes up one column, in Columns only (0.5.5
      to 0.5.8), a setting, off by default since 0.5.9. For the other layouts keep the
      Windhawk mod `explorer-double-click-up` enabled: it acts on Explorer's
      own list, which sits under the panel and never sees a click on it. Ari's
      call: not built in, too much for the gain.
- [ ] Details view columns fit their content (built-in
      `file-explorer-details-autofit-columns`)
- [ ] Small tweaks

## Outside this mod

- [ ] Taskbar widget mods moved to the `Taskbar.View.dll` entry point
- [ ] Astur workspace indicator and layout toggle
- [ ] Desktop stacks, parked

- [ ] Parked: a click should stop a long scroll that is still gliding. Tried in 0.7.12 to 0.7.15 (ChangeView to the same offset, a nudge, toggling scroll mode and inertia, scrolling the vertical wheel in the mod and dropping turns after a press). None worked with a mouse click.

- Settings window responsive: the sidebar collapses to icons on a narrow window (Ari, 0.29 session).
- Touch, still open: pen (not tried), a live "Drop in ..." line on the touch drag picture, taller rows or wider grips for touch, a gesture for Harmonize widths.

- Surgical refresh: done in 0.34 (see the table). Still to do: grouped columns (headers are rows too), and a count threshold test on a large folder.
- Parked: a slide of the rows below an added or removed row. Tried in 0.35.2 to 0.35.4: a translation animated by a Vector3Transition, then a compositor implicit animation of the rows' Offset. Neither moved the rows in Explorer's XAML (no error, no motion). The 100 ms fades of rows coming and going stay.
