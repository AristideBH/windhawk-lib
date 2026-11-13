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

## Open

- [ ] (0.5.2, not tested) Keyboard collapse for group headers: Up from the
      first item of a group goes to its header, Left and Right collapse and
      open it, Enter toggles, Up and Down move on.
- [ ] Rename several items at once, like Explorer's own
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
- [ ] New folder from selection, from the right-click menu and from the
      New button in the command bar
- [ ] Better archive extraction flow
- [ ] Toolbar customization
- [ ] Status bar and info bar customization
- [ ] (0.5.5, not tested) Double-click on empty space goes up one folder, a
      setting, on by default (built-in `explorer-double-click-up`)
- [ ] Details view columns fit their content (built-in
      `file-explorer-details-autofit-columns`)
- [ ] Small tweaks

## Outside this mod

- [ ] Taskbar widget mods moved to the `Taskbar.View.dll` entry point
- [ ] Astur workspace indicator and layout toggle
- [ ] Desktop stacks, parked
