// ==WindhawkMod==
// @id              explorer-extras
// @name            Explorer Extras
// @description     Additions for File Explorer, one switch per feature: Columns (Miller columns), a details and preview pane (PDF, text, Markdown, images, video, sound), colour labels (tags), and a toolbar with tighter gaps, a resizable search box and round corners.
// @version         1.0.21
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

**Extra features for File Explorer on Windows 11, one switch per feature.**

![Explorer Extras: the Columns layout, the details pane and colour labels in File Explorer](https://raw.githubusercontent.com/AristideBH/windhawk-lib/main/explorer-extras/assets/captures/hero.png)

A 40-second tour is at the top of the project page: https://github.com/AristideBH/windhawk-lib/tree/main/explorer-extras#readme

Explorer Extras adds four independent features to File Explorer. Each has its own section and its own off switch on this page: turn on what you like, and the rest stays out of the way.

## Columns

Browse folders the way you do on a Mac: one column per folder level, side by side. Open the **View** menu of a File Explorer window and choose **Columns**.

- **It feels like Explorer.** Sorting, grouping, icons, thumbnails and the right-click menu all come from Windows itself. The menu is tidied up: quick actions on top, "Show more options" for the full list.
- **Keyboard, selection and undo.** Arrow keys, type to jump, F2 (also for several items at once), Delete, copy and paste, a rubber band, and Ctrl+Z to take back a delete, a rename or a new folder.
- **Drag and drop, and touch.** Between columns, into the navigation pane, to other windows and apps, and back. Tap, hold, drag and select with a finger.
- **Tabs and memory.** Each tab keeps its own columns, and each folder remembers its sort, its grouping, its column widths and whether you showed it in Columns.
- **Archives.** Zip, 7z, rar and tar files open like folders.

## Details pane

One pane that replaces Explorer's Details and Preview panes, in Columns and in every view. Press **Alt+P**, or use the View menu or the toolbar button, to show or hide it.

- **Real previews.** PDF, text and code, Markdown, images (zoom, pan, animated GIF), video and sound.
- **Details by type.** A folder's size and contents are counted when you press **Calculate**, or by themselves if you turn that on.
- **Several files at once.** A fanned stack of thumbnails.
- **You stay in control.** Each kind of preview can be switched off, and has a size limit.

## Labels

Colour labels, like the tags of macOS, on files and folders.

- **Colours and names.** Seven colours and a neutral gray, an optional name, up to 16 on an item. Give each colour a meaning in the settings, such as "Red = Urgent".
- **Seen everywhere.** A dot after the name, in every view of Explorer's list and in Columns, in step with the light or dark theme.
- **Easy to set.** In the **Tags** section of the details pane, or from the right-click menu: a **Tags** entry with the colours, **Add tag** and **Clear tags**. The last eight names you typed are listed, so you type each one once.
- **Sort and group.** Explorer's own **Group by** and **Sort by** use them: choose **Tags** (**Étiquettes** in French; under **More** for Sort by) and each label becomes a group. An item with two labels is in both groups.
- **They follow the file.** A label is stored in the file itself, so it survives copying or moving on NTFS drives, and the file's dates do not change. See "Good to know" below.

## Toolbar

Tidy the top of the window. Each part is a setting, and one switch gives Explorer's own look back.

- **Navigation buttons.** Back, Forward, Up and Refresh sit closer together, and the address bar sits closer to them.
- **Command bar.** New, Cut, Copy, Paste and the rest sit closer together.
- **Search box.** Drag the edge between the address bar and the search box to resize it, with the mouse, a finger or a pen. A double click or double tap goes back to the width in the settings.
- **Round corners.** A corner radius for the address bar and one for the search box, up to fully round.

## Good to know

- **Labels stay in your files.** Labels are off by default, because they are the one part of the mod that changes files: turn on **Show labels** to use them. They are written into the files themselves (an NTFS stream), so they stay there after the mod is disabled or uninstalled, and also on a network share that supports named streams. To take them off, use **Clear tags** (in the pane or the right-click menu) while the mod is still running.
- **Removing the mod** leaves Explorer as it was, apart from those labels.
- **Works with QuickLook.** Explorer's own view follows the active column, so [QuickLook](https://github.com/QL-Win/QuickLook) previews the selected item as it does anywhere else.
- **Tested** daily on Windows 11 24H2 (build 26100) on ARM64, in French and English. x86-64 was tested up to version 0.55.
- **Built with AI assistance** (Claude Code) under the author's direction, and tested by hand. It has not had an independent code audit, so please report anything odd.

Bug reports and ideas: https://github.com/AristideBH/windhawk-lib/issues (the issue forms ask for the mod's version and the log).

If you like it, you can support the work on Ko-fi: https://ko-fi.com/aristidebh
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
@@SETTINGS_BLOCK@@
*/
// ==/WindhawkModSettings==

