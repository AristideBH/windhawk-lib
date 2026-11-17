// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Beta. Additions for File Explorer, one switch per area: Columns (Miller columns), a details and preview pane (PDF, text, Markdown, images, video, sound), colour labels (tags), and a toolbar with tighter gaps, a resizable search box and round corners.
// @version         0.77.21
// @author          AristideBH
// @github          https://github.com/AristideBH
// @donateUrl       https://ko-fi.com/aristidebh
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid -ladvapi32 -lgdi32 -lcomctl32 -lpropsys -lmsimg32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Explorer Extras

![Columns, the View menu and the details pane in File Explorer](https://raw.githubusercontent.com/AristideBH/windhawk-lib/3422bd28aa32f126030284b602fe06e1ddfceceb/explorer-extras/assets/captures/hero.png)

Additions for File Explorer. The mod has several areas, each with its own section and switch on this page, so you use only what you like.

> **Beta, and vibe-coded.** This mod is in public beta. It is built largely with AI assistance and tested by hand by its author, without an independent code audit. Use it at your own judgment, and please report anything odd.

## Columns

A **Columns** entry in File Explorer's View menu: Miller columns, one column per folder level, drawn with Explorer's own WinUI 3 controls. Explorer's own view follows the active column, so the address bar, toolbar, panes and status bar act on the selection.

- **Native feel.** Sort, group, icons, thumbnails and the context menu come from the shell. The context menu is restyled: quick actions on top, "Show more options" for the full menu.
- **Keyboard, selection, undo.** Arrow keys, type to jump, F2 (also for several items), Delete, copy and paste, a rubber band, and Ctrl+Z to take back a delete, a rename or a new folder.
- **Drag and drop and touch.** Between columns, to the navigation pane, other windows and apps, and in from them. Tap, hold, drag and a rubber band by touch.
- **Tabs, widths and memory.** Each tab keeps its own columns. Each folder remembers its sort, group, column widths and whether it was shown in Columns.
- **Archives.** Zip, 7z, rar and tar files open as folders.

## Details pane

One pane in place of Explorer's Details and Preview panes, in Columns and in every view.

- Previews of PDF, text and code, Markdown, images (zoom, pan, animated GIF), video and sound.
- The details of what is selected, by type. A folder's size and contents are counted when you press **Calculate**, or by themselves if you turn that on in the settings.
- A fanned stack of thumbnails when several items are selected.
- Alt+P, the View menu and the toolbar button show and hide it.
- Each kind of preview can be switched off, and has a size limit, in the settings (Limits).

Works with QuickLook.

## Labels

Colour labels, like the tags of macOS, on files and folders. A label is a coloured dot with an optional name, from a palette of seven colours and a neutral gray, and an item can have up to 16.

- **Where they show.** As a dot after the name, stacked when there are several, in every view of Explorer's own list (Details, List, Tiles, Content and the icon sizes) and in Columns. They follow the light and dark theme.
- **How you set them.** In the **Tags** section of the details pane (badges that wrap, and an **Add a tag** link), and in the right-click menu, both in Explorer's own list and in Columns: a **Tags** entry with the colours, **Add tag** and **Clear tags**.
- **Where they live.** In a named stream of the file itself, so they follow it when it is copied or moved on an NTFS drive. They need NTFS: the menu entry and the pane say so when a drive cannot keep them. The file's dates are not changed.
- **Your own meanings.** Each colour has a name you can edit on Windhawk's page (Labels, Colour names), such as "Red = Urgent".
- Switch them off with **Show labels**.

## Toolbar

The top of the window, with a setting for each part (group **Toolbar**, switch **Change the toolbar**):

- **Navigation buttons.** Back, Forward, Up and Refresh sit closer together (gap 4 pixels, 0 to 16; Explorer's is 16), and so does the address bar after them (4 pixels, 0 to 16; Explorer's is 8). The address bar takes the room that frees.
- **Command bar.** The gap between New, Cut, Copy, Paste and the rest (4 pixels, 0 to 8; Explorer's is 8).
- **Search box.** Drag the edge between the address bar and the search box, with the mouse, a finger or a pen; a double click or double tap goes back to the width in the settings. A width you drag is kept until you change the setting.
- **Corners.** The corner radius of the address bar and of the search box (0 to 16; 16 makes the ends round).

Switch the area off and Explorer's own spacing and corners come back.

A short video tour: https://github.com/user-attachments/assets/14dddb22-8866-4082-aede-a6f5ed995bea

Tested daily on Windows 11 24H2 on ARM64, in French and English; x86-64 was tested up to 0.55. Needs Windhawk to download the symbols of FileExplorerExtensions.dll.

Bug reports and ideas: https://github.com/AristideBH/windhawk-lib/issues (the issue forms ask for the mod's version and the log).

If you like it, you can support the work on Ko-fi: https://ko-fi.com/aristidebh
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
@@SETTINGS_BLOCK@@
*/
// ==/WindhawkModSettings==

