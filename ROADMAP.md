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

- [ ] Rename to Explorer Extras, settings grouped under a Columns section
- [ ] Sort from the starting folder applied to every column
- [ ] Group headers with Explorer's own names
- [ ] Sort menu changes re-sort every column, with nothing written to folders
- [ ] Selection mirrored into Explorer's view: preview pane, details pane,
      status bar, command bar
- [ ] Column width fitted to the longest name, with minimum and maximum
- [ ] Middle ellipsis on long names, full name in a tooltip
- [ ] Drag to resize a column, double-click to refit
- [ ] Rows built only when visible
- [ ] Folders read on a background thread
- [ ] Explorer's view follows with a 350 ms delay
- [ ] Hidden files follow Explorer's setting
- [ ] Settings: enabled, minimum width, maximum width, follow active column,
      maximum items, debug log

## 0.3

- [ ] Real file icons and thumbnails
- [ ] Keyboard: left and right between columns, type to jump
- [ ] Native right-click menu
- [ ] Quick options flyout on empty space in the columns

## 0.4

- [ ] Drag and drop between columns
- [ ] Scroll animation to the newest column
- [ ] Per-folder memory and a "columns everywhere" setting
- [ ] Columns entry present when the window opens
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
- [ ] New folder from selection
- [ ] Better archive extraction flow
- [ ] Toolbar customization
- [ ] Status bar and info bar customization
- [ ] Small tweaks

## Outside this mod

- [ ] Taskbar widget mods moved to the `Taskbar.View.dll` entry point
- [ ] Astur workspace indicator and layout toggle
- [ ] Desktop stacks, parked
