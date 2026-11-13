// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Optional additions to File Explorer. First one: a Columns layout in the View menu, Miller columns drawn with Explorer's own WinUI 3 controls.
// @version         0.9.7
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid -ladvapi32 -lgdi32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Explorer Extras

Optional additions to File Explorer. Each one has its own settings section
and can be switched off, in which case it leaves no trace.

## Columns

Adds a **Columns** entry to File Explorer's View menu, next to Details and
List. It shows the folder as Miller columns: click a folder and its content
opens in the next column.

The columns are WinUI 3 controls hosted in the Explorer window, so they use
Explorer's own styles. The mod needs no symbol file from Microsoft for
System32 modules, so it keeps working right after a Windows update.

In 0.2:

- Sort and group come from the folder where you turned Columns on, and apply
  to every column. Pick something in the Sort menu and every column follows.
  The mod never changes a folder's saved view.
- Group headers use Explorer's own names.
- Selecting a file selects it in Explorer's own view too, so the preview
  pane, the details pane, the status bar and the command bar act on it.
- Columns fit their longest name, between a minimum and a maximum width. Long
  names are cut in the middle. Drag a column's right edge to resize it,
  double-click the edge to refit.
- Folders load on a background thread.
- Hidden files and compact view follow Explorer's own settings.

In 0.3 the Columns entry is in the View menu as soon as a window opens, with
no click on the command bar needed. When Columns was the last layout you
chose, new windows open in it, also after Explorer restarts. Turn this off
with "Open new windows in Columns".

Also in 0.3: real file icons and thumbnails, read on a background thread (a
glyph shows until each icon arrives), and a right-click menu that shows the
shell's own commands for the item as a flyout.

Also in 0.3: select several items with Ctrl and Shift, and type a few
letters to jump to a name.

Not yet: drag and drop.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- columns:
  - enabled: true
    $name: Enabled
    $description: Adds the Columns entry to the View menu. Off removes it.
  - restoreOnOpen: true
    $name: Open new windows in Columns
    $description: When the last layout you chose was Columns, a new Explorer window opens in Columns too. Choosing any other layout in the View menu turns this off.
  - desktopRealOnly: false
    $name: Desktop column shows only real items
    $description: Hides the virtual items of the Desktop column (This PC, Recycle Bin, Control Panel, Libraries and so on) and keeps the files, folders and shortcuts that are on the Desktop.
  - remoteMarks: true
    $name: Mark cloud, network and device items
    $description: A small glyph at the end of a row for a cloud file that is not downloaded, an item on a network location, or an item on a phone or camera.
  - archives: false
    $name: Browse archives in columns
    $description: A zip, 7z, rar or tar file opens as a folder in the next column, read by the shell's own archive support. Only on local fixed drives, and not above 512 MB. Archives that Windows cannot open as folders stay files.
  - headerKeyboard: false
    $name: Keyboard on group headers
    $description: Up from the first item of a group goes to its header, and Left, Right and Enter collapse and open it. Off, the arrow keys only move between items.
  - doubleClickUp: false
    $name: Double-click on empty space goes up
    $description: A double-click on the empty space of a column goes up one folder, as in the "Explorer double-click up" mod. In the first column the parent folder is added in front.
  - compactScrollbars: false
    $name: Compact scrollbars
    $description: Keeps the scrollbars of the columns thin, also when the pointer is over them. They can still be dragged and clicked.
  - focusRect: true
    $name: Show the focus outline on the active item
    $description: The outline around the item under the keyboard cursor while you navigate with the arrow keys. Off hides it, the selection highlight stays.
  - background: explorer
    $name: Panel background
    $description: Explorer takes the colour of Explorer's own file list. Theme uses the standard dark or light colour. Transparent makes Explorer's own list invisible, not removed, and lets the window's own backdrop show through the panel. Test mode.
    $options:
    - explorer: Explorer's file list colour
    - theme: Theme colour
    - transparent: Transparent (test)
  - sidebarRoot: false
    $name: Sidebar sets the first column
    $description: A click on Home, Gallery, OneDrive or a pinned folder in the navigation pane makes that folder the first column. The expandable tree (This PC, drives, Network) keeps following the columns as before.
  - minWidth: 160
    $name: Minimum column width
  - maxWidth: 420
    $name: Maximum column width
    $description: Columns fit their longest name up to this width. Longer names are cut in the middle.
  - followActiveColumn: true
    $name: Explorer follows the active column
    $description: Explorer's own file list navigates to the folder of the column you are in, so the address bar, Back, the preview pane and the status bar stay in step.
  - maxItems: 5000
    $name: Maximum items per column
  $name: Columns
- debugLog: true
  $name: Write a debug log
  $description: Writes to Code\windhawk-lib\probes\logs\explorer-extras.log in your user folder, if that folder exists.
*/
// ==/WindhawkModSettings==

