// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Beta. Additions for File Explorer, one switch per area: a Columns layout (Miller columns) and a details and preview pane for PDF, text, Markdown, images, video and sound.
// @version         0.72.4
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

