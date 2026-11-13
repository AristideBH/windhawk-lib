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
- [ ] Real file icons and thumbnails. Written in 0.3.5, not compiled yet.
- [ ] Native right-click menu. Written in 0.3.5, not compiled yet: the shell
      menu is copied into a XAML flyout, no modal loop.
- [ ] Polish pass: open in Columns without the flash of Explorer's own list.
      Fine for now, per Ari.
- [ ] A new tab opens in Columns. No creation hook for tabs is known yet.
- [x] Mouse horizontal scroll wheel scrolls the strip sideways across the
      columns, and does not scroll vertically inside a column. Tested in
      0.3.3: sideways glide, no vertical scroll. 0.3.2 marks the event
      handled on each column's row panel, 0.3.3 adds the glide.
- [x] The ordinary vertical wheel scrolls only the column under the pointer,
      and never moves the strip sideways. Tested in 0.3.4, handled on the
      strip.
- [x] Flash shortened: restore waits one tick instead of three. Tested in
      0.3.1, shorter.
- [ ] Focusing a Control Panel item, or another virtual shell item such as a
      link, in the Desktop folder. These aren't real folders on disk, so the
      column logic needs a way to open and focus them. Not looked at yet.

## 0.3.1

- [ ] Multiple selection of files and folders in a column, mirrored into
      Explorer's view
- [ ] Indicator on remote items: phone, network
- [ ] Quick options flyout on empty space in the columns
- [ ] Keyboard: type to jump to a name
- [ ] Rows built only when visible, carried over from 0.2

## 0.4

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
- [ ] New folder from selection, from the right-click menu and from the
      New button in the command bar
- [ ] Better archive extraction flow
- [ ] Toolbar customization
- [ ] Status bar and info bar customization
- [ ] Small tweaks

## Outside this mod

- [ ] Taskbar widget mods moved to the `Taskbar.View.dll` entry point
- [ ] Astur workspace indicator and layout toggle
- [ ] Desktop stacks, parked
