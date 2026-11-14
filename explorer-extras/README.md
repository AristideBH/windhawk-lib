# Explorer Extras

A [Windhawk](https://windhawk.net) mod for File Explorer on Windows 11. It adds a **Columns** layout to the View menu: Miller columns, one column per folder level, drawn with Explorer's own WinUI 3 controls.

Status: version 0.13.6, in daily use by its author, not yet published to the Windhawk catalog. Tested on Windows 11 build 26100 (24H2), ARM64, French. x86-64 is built but untested.

## What it does

- **Columns layout.** Choose Columns in Explorer's View menu. Each folder opens in the next column, with a chevron on folders. Explorer's own view follows the active column, so the address bar, tab title, toolbar, details pane and status bar act on the selection.
- **Native feel.** Sort and group come from Explorer's view. File icons, thumbnails and the context menu come from the shell. Right-click opens a restyled menu: a row of quick actions on top, the usual commands below, and "Show more options" for everything else.
- **Keyboard.** Arrow keys, type to jump, Enter, Backspace, F2 (also for several items at once), Delete, Ctrl+C, X, V, Z, Y, Ctrl+Shift+N for a new folder, Ctrl+Alt+N for a new folder holding the selection.
- **Selection.** Drag a rectangle, Ctrl and Shift click, collapsible group headers.
- **Tabs.** Each tab keeps its own columns. A tab in another layout is left alone. Middle click on a folder, or "Open in new tab" in the menu, opens it in a new tab.
- **Per folder memory.** Each column remembers its sort and group, its width, and whether the folder is shown in Columns. All three can be switched off.
- **Drag and drop.** Drag rows onto a folder or into another column, out to the navigation pane, another Explorer window (any view), the desktop or another application, or drop files in from them. Ctrl copies, Shift moves, Alt makes a shortcut, Escape cancels. The shell does the work, so conflict and progress dialogs are Explorer's. A label with the icons, the name or count, and what a drop would do follows the pointer, the target row or column gets an accent frame, and columns scroll near their edges while dragging.
- **Widths.** Drag a border to resize a column, double-click it to fit the names, Shift and double-click to give every column the average fitted width. A small handle follows the pointer along the border.
- **Look.** Selected neighbouring rows share one background, the chevron of the folder that leads to dimmed columns sits on a small badge, and the View button wears a Columns icon while Columns is on.
- **Archives (optional).** Zip, 7z, rar and tar files open as folders in the next column, with Extract here and Extract selected here.
- **Undo.** Ctrl+Z takes back a delete, a rename, a new folder, or a group of renames.
- **Marks.** Cloud files that are not downloaded, always-kept and downloaded cloud files, network items and phones get a small glyph.

Not done yet: dragging out of an archive column, toolbar Select all, Invert and Select none, translations. See [`../ROADMAP.md`](../ROADMAP.md). `SHOWCASE-TESTS.md` describes a rich test folder built by `make-columns-showcase.ps1`.

## Install

1. Install [Windhawk](https://windhawk.net).
2. In Windhawk choose **Create a new mod**, replace the code with the contents of `explorer-extras.wh.cpp`, and compile it (Ctrl+B).
3. Enable the mod. Open the View menu of a File Explorer window and choose **Columns**.

The mod hooks `FileExplorerExtensions.dll`. Windhawk must be able to download Microsoft's symbols for it, which has worked on every build tested. If symbols are missing right after a Windows update, the mod waits and logs it.

## Settings

All under `columns` in the mod's settings.

| Setting | Default | What it does |
| --- | --- | --- |
| `enabled` | on | The Columns layout. |
| `minWidth`, `maxWidth` | 210, 380 | Column width range, in pixels. |
| `followActiveColumn` | on | Explorer's view follows the active column. |
| `restoreOnOpen` | on | Windows and tabs open in Columns when it was the last layout. |
| `maxItems` | 5000 | Most rows shown per folder. |
| `sidebarRoot` | on | A click in the navigation pane starts the columns over there. |
| `focusRect`, `compactScrollbars` | on | Look of the keyboard focus and the scrollbars. |
| `doubleClickUp`, `headerKeyboard` | off | Optional behaviours. |
| `archives` | off | Browse archives in columns. |
| `desktopRealOnly` | off | Hide This PC, Recycle Bin and the other virtual items in the Desktop column. |
| `remoteMarks` | on | The cloud, network and phone glyphs. |
| `rememberSort`, `rememberWidth`, `rememberLayout` | on | The per folder memory. |
| `springLoad` | on | Hold dragged items on a folder row for about 0.7 s and it opens in the next column. |
| `harmonizeWidths` | on | Shift and double-click a border: every column gets the average fitted width. |
| `keepStale` | on | Columns to the right of a chosen file stay, dimmed. Off: they close. |
| `dragOut` | on | Start a real drag when rows leave the panel, to drop them on the desktop, in another window or app. |
| `debugLog` | on | Write a log file for debugging. |

## Build from source

The mod is one file for Windhawk but is kept in parts under `src/`, joined by a script:

```
python explorer-extras/build.py            # writes explorer-extras.wh.cpp
python explorer-extras/build.py --check    # exits 1 if the generated file is out of date
```

Edit the parts, never the generated file. A syntax-only check with Windhawk's own clang is described in [`HANDOFF.md`](HANDOFF.md).

## More

- [`HANDOFF.md`](HANDOFF.md): how the work is run, the architecture, and what costs time.
- [`PLAN.md`](PLAN.md): findings from the probes and the decisions behind the design.
- [`../ROADMAP.md`](../ROADMAP.md): what is done, tested and open.

## License

MIT.
