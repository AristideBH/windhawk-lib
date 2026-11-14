// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Optional additions to File Explorer. First one: a Columns layout in the View menu, Miller columns drawn with Explorer's own WinUI 3 controls.
// @version         0.34.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid -ladvapi32 -lgdi32 -lcomctl32
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
@@SETTINGS_BLOCK@@
*/
// ==/WindhawkModSettings==

