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

## Plan for the next versions, drawn up 2026-11-13

Order is by value for daily use, then risk. Low priority, by Ari: Home page
overhaul, details columns auto-fit, GPU Explorer view compatibility. They go
last and are not scheduled.

| Version | Content | Effort | Risk |
| --- | --- | --- | --- |
| 0.6.x | Done: everyday keys, redo, live accent, multi-rename, undo of a new folder, columns of a deleted folder pruned. | | |
| 0.7.0 | New folder with selection (flyout item and Ctrl+Alt+N, one Ctrl+Z takes it back) and open in a new tab (flyout item and middle-click). Written, not tested. | small | low. The new tab is made with Ctrl+T sent to the window, then shown its folder once the tab window exists. |
| 0.7.1 | Archives in columns: a zip, 7z, rar or tar is browsed as a folder in the next column (setting, off by default), with Extract here and Extract to from the flyout, using the shell's own archive folders. | medium | medium: the shell reads big archives slowly and may write temporary files, local fixed drives only. Needs a limit and a loading state. |
| 0.7.2 | Small tweaks: icon jobs for the rows on screen first, show only real Desktop items (setting), Documents icon, focusing a virtual shell item, remote item indicator. | small each | low |
| 0.7.3 | Housekeeping: split the 8000 line source into files per area, joined by a script. Every later patch depends on it, so before 0.8. | medium | low |
| 0.8 | Columns per tab. The state is per window today. Each tab gets its own columns. Needs a model change, from `WindowState` to a per-tab state with the window's shared parts split out. | large | medium to high |
| 0.9 | Flyouts restyled to match Explorer, Paste in the item menu, the native look if a probe shows a safe way. | medium | medium |
| 0.10 | Drag and drop between columns and out to other windows, with the shell's drag helper. | large | medium |
| 0.11 | Per-folder memory and "columns everywhere", each column keeps its own sort and group. | medium | low |
| 1.0 | Release: strings in a table for translation, settings cleaned of test options, README, the pull request to windhawk-mods. | medium | low |
| later | Home page overhaul, details columns auto-fit, GPU Explorer view compatibility, Labels, toolbar customization. | | |

Why this order: the keys and multi-rename are cheap and used all day. The
source split costs a day and makes every change after it easier to review.
Columns per tab is the biggest gap and the biggest change, so it comes after
the split. Drag and drop and the flyout restyle are polish on a working
whole. The three low priority items depend on things we don't control: a
DirectUI hook, another author's mod, a design call.

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
- [ ] Middle-click opens a folder in a new tab
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
- [ ] New folder from selection, from the right-click menu and from the
      New button in the command bar. Scheduled in 0.6.2 for the flyout and a
      shortcut. Design: make "New folder" like Ctrl+Shift+N, then move the
      selected items into it with the shell's own move, so conflicts and
      progress behave as in Explorer, then edit the name. The undo entry holds
      the moves and the folder. The command bar's New button is a later step.
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
