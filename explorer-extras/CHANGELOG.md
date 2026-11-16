# Explorer Extras, changelog

Versions are the `@version` of the mod. The newest is first. Dates are when the version was tested by the author.

## 0.56 (2026-11-16)

Fixes from the Windhawk catalog review.

### Changed
- The mod writes no log file and no icon files. The debug log setting and the "Open the log" action are gone; the mod logs through Windhawk.
- Folder memory is stored one value per folder, so it no longer loses everything past 32,767 characters or in portable Windhawk.
- Preview handlers run in their own process only. Undoing a new folder removes it only when it is empty. Compress ends its options with `--` and works at a drive root.
- The notify window class is registered with the mod's own module and its result is checked.
- A drag no longer sends tree-view messages to other programs. Probe and dead code removed.

## Beta, 0.55 (2026-11-16)

The first public beta. Explorer Extras is now more than the Columns layout: it has a details pane and a settings window, each its own area.

### Added
- **Details pane**, in Columns and every other view: a preview on top and the details below, in place of Explorer's Details and Preview panes. Previews of PDF, text and code, Markdown, images (full size, zoom, pan, animated GIF), video and sound. Several selected items show as a fanned stack. Folders show their size, contents and types, counted in the background.
- Alt+P, the View menu and the toolbar button all show and hide the pane. The View menu gets one pane entry, with the navigation pane entry beside it.
- Settings: previews on or off by kind, size limits, big images open on a button, previews that open on a button only.
- **Settings window** in English and French, following the display language. It opens on its first page and has an About card.
- Columns: folders first, alternate row shading, an accent badge on the path chevron (with an animated entrance and exit).
- Touch: a sideways drag from a column's empty space scrolls the columns.

### Works with
- QuickLook (the Space-bar previewer) in Columns, since Explorer's own view follows the selection.

### Tested
- On ARM64 and x86-64, and the mod was unloaded and removed without leaving a trace.

### Changed
- A slow second click renames only in the active column, and a rename is cancelled by a click anywhere else.
- A click in the navigation pane gives the keyboard to the first column, and Down then selects its first row.
- The README is shorter and lists areas; the Columns details moved to `docs/columns.md`.

### Fixed
- A second finger no longer leaves the columns' scrolling switched off.
- Several rough edges of the pane's animations, its resize, and its previews found while building it.

## 0.36 (2026-11-15), release candidate cleanup

- The settings window's sidebar is a rail of icons on a narrow window.
- Option texts shortened; the debug log is off by default; several always-on options no longer have a switch.

## 0.35 and earlier (2026-11-12 to 2026-11-14)

- 0.35: grouped columns patch in place; the empty-space flyout is one quick row; rows fade in and out.
- 0.33 to 0.34: the file flyout rebuilt from a Figma; context menu behaviour; surgical column refresh.
- 0.29 to 0.32: touch (hold and drag, drops, rubber band).
- 0.24 to 0.28: the settings window and its schema.
- 0.3 to 0.23: the Columns layout, drag and drop between columns, windows and apps, per folder memory, archives, undo.
