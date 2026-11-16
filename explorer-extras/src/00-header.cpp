// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     A Columns layout for File Explorer: Miller columns in the View menu, with drag and drop, a restyled context menu, touch support and per-folder memory.
// @version         0.52.2
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid -ladvapi32 -lgdi32 -lcomctl32 -lpropsys
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Explorer Extras

Optional additions to File Explorer. Each one has its own settings page and can be switched off, in which case it leaves no trace. Open the settings window from **Extras** at the bottom of the toolbar's ... menu, or from the View menu.

## Columns

Adds a **Columns** entry to File Explorer's View menu, next to Details and List. The folder is shown as Miller columns: click a folder and its content opens in the next column. The columns are WinUI 3 controls hosted in the Explorer window, so they use Explorer's own styles. The mod needs no symbol file from Microsoft for System32 modules, so it keeps working right after a Windows update.

- **Native behaviour.** Explorer's own view follows the active column, so the address bar, tab title, toolbar, details pane, preview pane and status bar act on the selection. Sort and group, file icons, thumbnails and cloud marks come from the shell.
- **Context menu.** A restyled menu: a row of quick actions, the usual commands, and "Show more options" for the full shell menu. Submenus open on hover. Archives get Extract and Compress entries.
- **Keyboard and selection.** Arrow keys, type to jump, Enter, Backspace, F2 (also for several items), Delete, Ctrl+C, X, V, Z, Y, Ctrl+Shift+N for a new folder, and a rubber band to select. Ctrl+Z takes back a delete, a rename or a new folder.
- **Drag and drop.** Between columns, to the navigation pane, other windows, tabs, the desktop and other apps, and into the columns from them. Hold items on a folder to open it. Ctrl copies, Shift moves, Alt makes a shortcut.
- **Touch.** Tap, press and hold, drag and drop, and a rubber band by touch.
- **Tabs.** Each tab keeps its own columns. Middle click on a folder opens it in a new tab.
- **Memory.** Each folder remembers its sort, group, column widths and whether it is shown in Columns.
- **Widths.** Drag a border to resize a column, double-click to fit the names, Shift and double-click to give every column the same width.
- **Archives.** Zip, 7z, rar and tar files open as folders in the next column.
- **Settings window.** Every setting applies at once. It has a sidebar of pages that turns into an icon rail in a narrow window.

Tested on Windows 11 24H2 on ARM64. The x86-64 build compiles but has not been run. Needs Windhawk to download the symbols of FileExplorerExtensions.dll.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
@@SETTINGS_BLOCK@@
*/
// ==/WindhawkModSettings==

