# Explorer Extras roadmap

Checklist of features and settings by version. Tick a box when the item is
compiled and tested. Details live in `explorer-columns/PLAN.md` and
`explorer-columns/SPEC-0.2.md`.

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
- [ ] Selection mirrored into Explorer's view: preview pane, details pane,
      status bar, command bar
- [x] Column width fitted to the longest name, with minimum and maximum
- [x] Middle ellipsis on long names, full name in a tooltip
- [x] Drag to resize a column, double-click to refit
- [ ] Rows built only when visible
- [x] Folders read on a background thread
- [x] Explorer's view follows with a 350 ms delay
- [x] Hidden files follow Explorer's setting
- [ ] Settings: enabled, minimum width, maximum width, follow active column,
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

## 0.3

- [x] Columns present when a window opens, without a click on the command bar.
      Tested 2026-11-13 in 0.3.0. Hook: `OnDpiChanged`, see the skill.
- [x] Columns remembered when Explorer or a window is reopened. Tested in
      0.3.0, with a short flash of Explorer's own list before the columns
      appear, about 270 ms.
- [x] Real file icons and thumbnails. Tested in 0.3.7. The Documents folder
      icon still shows small, maybe the icon cache, look again later.
- [x] Right-click menu works: shell commands in a XAML flyout, Copy, Cut,
      Rename in place, Properties, Delete. Tested in 0.3.7. Paste and the
      native look are left for later: visual pass on the flyout, and check
      what Microsoft changed in the context menu.
- [ ] Polish pass: open in Columns without the flash of Explorer's own list.
      Fine for now, per Ari.
- [ ] A new tab opens in Columns. Written in 0.3.14, not tested: a hook on
      the tab window's creation, found with the probe, waits until the new tab
      is the visible one.
- [x] Mouse horizontal scroll wheel scrolls the strip sideways across the
      columns, and does not scroll vertically inside a column. Tested in
      0.3.3: sideways glide, no vertical scroll. 0.3.2 marks the event
      handled on each column's row panel, 0.3.3 adds the glide.
- [x] The ordinary vertical wheel scrolls only the column under the pointer,
      and never moves the strip sideways. Tested in 0.3.4, handled on the
      strip.
- [x] Flash shortened: restore waits one tick instead of three. Tested in
      0.3.1, shorter.
- [x] Control Panel in columns. Tested in 0.3.15: one Control Panel entry on
      the Desktop, the columns browse into its categories and subfolders, and
      Explorer's own view no longer follows into it (it swapped the window for
      a Control Panel window). Launchers (applets) show a gear and open with
      Enter, that part is not tested yet.
- [ ] Focusing a Control Panel item, or another virtual shell item such as a
      link, in the Desktop folder. These aren't real folders on disk, so the
      column logic needs a way to open and focus them. Not looked at yet.

## 0.3 polish, done in 0.3.5 to 0.3.27, tested

- [x] Icons, thumbnails, right-click menu, rename in place, Copy, Cut, Paste,
      Delete, F2, multiple selection, type to jump, background menu with
      Paste and New folder, new tabs, Control Panel, stale columns with a
      stronger chevron, active column pill with a gentle glide, sidebar
      sets the first column.

## 0.4

- [ ] Keyboard shortcuts in a column: Delete, Ctrl+C, Ctrl+X, Ctrl+V, F2
      rename, Ctrl+A, Alt+Enter for properties. Copy, Cut and Rename already
      exist from the right-click menu, Paste does not.
- [ ] (0.3.16, not tested: Paste, New folder with rename, Refresh,
      Properties.) Right-click on a column's empty space: flyout with Paste and the other
      background options (New folder, Refresh, Properties, sort and group).
      This is the quick options flyout below. The shell's background menu
      may return `E_NOTIMPL` for Paste, as Copy did, then paste goes through
      the folder's drop target with the clipboard's data object.
- [ ] Keep the columns of a folder while focus moves to a file in the same
      column, until a different folder is chosen in that column or a parent.
      Keep the focused column smoothly in view, no jump.
- [x] Tested in 0.4.4 to 0.4.8: collapsible group headers, animated, like Explorer's: a chevron on the header,
      a click hides or shows the group's rows. Collapsed groups are kept per
      column across reloads. Collapsed rows must not be reachable by Shift
      range selection, type to jump or the arrow keys. Keyboard collapse is
      not planned for the first version, headers are not focusable.
- [x] Tested in 0.4.5 to 0.4.8: rename by a slow second click on the name of the
      selected item, like Explorer. Panel background taken from Explorer's own
      file list. Group headers: hover tint, turning chevron.
- [ ] PARKED, Ari's call 2026-11-13: transparent background mode. It works
      after a restart, but after a layout switch the native view still shows
      through even though every visible view window is made transparent
      (0.4.18). Something else draws it, or the layered style does not reach
      the DirectUI content. Don't chase it again without a new idea.
- [x] Compact scrollbars, with a free strip beside the bar for the selection
      rectangle (0.4.12 to 0.4.18).
- [ ] Panel background that blurs like Explorer's own (Mica). Not reachable
      with the WinUI headers of this Windhawk build: no MicaBackdrop, no
      SystemBackdrop on islands. Revisit with a newer Windows App SDK, or by
      hiding the native list. A test setting exists since 0.4.7.
- [ ] (0.4.10, not tested) Selection by drawing a rectangle: from empty space, a group header or the 10 px gutter on the right of each column. Ctrl adds. The list scrolls near its edges.
- [ ] (0.4.13, not tested) A reader thread per window with cancelled loads; a
      column over the limit shows the first items in sort order, with the
      count of those not shown; transparent background mode makes the native
      list invisible; compact scrollbars without the stretched arrows.
- [ ] (0.5.0, not tested) Rows built only when visible: every entry has a row shell, its content is made as it comes near the screen. Default limit 5000. The item limit can go away later; icons are still read for the first 400 items only. Original note: so the item limit can go away. Today
      every row of a column is built up front: a column of several thousand
      items takes seconds on the UI thread.
- [ ] Rename several items at once, like Explorer's own.
- [x] Tested in 0.4.0 to 0.4.8: faster column refresh after the user's own actions (rename, new folder,
      delete, paste, move):
      1. reload the column right after the action, not on the folder's change
         notification, which waits up to 2 s;
      2. shorter debounce, about 250 ms, for the notifications that follow;
      3. optimistic rows: a rename updates the name in place, a new folder
         appears at once with its name box, a delete removes the rows at once
         and the reload puts them back if the delete did not happen.
      Changes from outside the mod keep the full reload until 0.5, where
      item-level change events can update single rows, together with rows
      built only when visible.
- [x] Tested in 0.3.22 to 0.3.27: setting "Sidebar sets the first column",
      off by default; flat entries only: Home, Gallery, OneDrive, known and
      Quick access folders. Rebuilds swap the new first column in without a
      blank. A click on an item of the navigation pane (Home, Gallery, pinned and
      cloud folders, not the expandable tree) makes that folder the first
      column. A setting, off by default. Needs a short recon first: how to
      tell a pane click from Explorer following a column, Back or Up. Today
      the column view is rebuilt with that folder when it is outside the
      current chain, and re-expands the chain when it is inside it.
- [ ] Selection by drawing a rectangle: click and drag from the empty space
      of a column. Also when the column is full to the bottom, so it needs a
      strip that is always empty, such as a gutter beside the rows or a
      margin under the last row.
- [x] Removed in 0.4.19, untested for a jump: the paste guard (0.3.13): after a paste, Explorer's jump to
      another folder is ignored for 3 s and the view is sent back. A
      workaround for a jump seen once, not reproduced since. Find the cause
      or delete the guard before release; a delay and a return trip is not
      acceptable as a final design.

- [ ] Multiple selection of files and folders in a column, mirrored into
      Explorer's view. Written in 0.3.8, not tested: Ctrl and Shift select,
      right-click, Copy and Cut act on all selected items.
- [ ] Indicator on remote items: phone, network
- [ ] Quick options flyout on empty space in the columns
- [ ] Keyboard: type to jump to a name. Written in 0.3.8, not tested.
- [ ] Rows built only when visible, carried over from 0.2

## 0.5

- [ ] Drag and drop between columns
- [ ] Scroll animation to the newest column
- [ ] Per-folder memory and a "columns everywhere" setting
- [ ] Strings moved to a table for translation
- [ ] Setting: each column keeps its own folder's sort and group

## With the second feature

- [ ] Settings window laid out like Windows Settings, in sync with
      Windhawk's page
- [ ] "Extras settings" entry in the "..." menu under Options
- [ ] One-time tip on the Columns entry
- [ ] Source split into one file per feature, joined by a script

## Feature backlog, order open

- [ ] Labels, macOS style
- [ ] Home page overhaul
- [ ] (0.4.9, not tested) Item tooltip shows details (type, size, dates, dimensions, tags), not
      the full name. The full name is still reachable another way, such as a
      longer hover or the status bar.
- [ ] Compatibility with `DarkionAvey/windhawk-gpu-accelerated-explorer-view`
      (GPL-2.0, replaces the Details and Icons views with its own GPU
      overlay). Ari tests the two together first. If they conflict: detect
      its window property `WindhawkExplorerGpuOverlayOwnsShellView` on the
      native view, keep our panel above its overlay, and don't depend on a
      visible `SHELLDLL_DefView`. Never copy its code, only ideas.
- [ ] Ideas to take from that mod: middle-click opens a folder in a new tab,
      marquee selection, a drag preview with a count badge, a toggle back to
      the native view with a hotkey.
- [ ] Option to show only real Desktop items, hiding the virtual ones (This
      PC, Recycle Bin, Control Panel, Libraries and so on). The toggle's form
      is open.
- [ ] New folder from selection, from the right-click menu and from the
      New button in the command bar
- [ ] Better archive extraction flow
- [ ] Toolbar customization
- [ ] Status bar and info bar customization
- [ ] Small tweaks
- [ ] Built-in versions of two Windhawk mods that are disabled for now:
      double-click on empty space goes up one folder
      (`explorer-double-click-up`), and details view columns fit their
      content (`file-explorer-details-autofit-columns`).

## Outside this mod

- [ ] Taskbar widget mods moved to the `Taskbar.View.dll` entry point
- [ ] Astur workspace indicator and layout toggle
- [ ] Desktop stacks, parked
