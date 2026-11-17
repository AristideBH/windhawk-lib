# Explorer Extras, changelog

Versions are the `@version` of the mod. The newest is first. Dates are when the version was tested by the author.

## 0.77 (2026-11-17)

### Added
- **Toolbar**, a new area: the Back, Forward, Up and Refresh buttons in front of the address bar sit closer together. Explorer gave each of them a margin of 8 on every side but the bottom, so two buttons were 16 apart; the mod sets the margin of each button, and the address bar, which takes what is left, grows. A **Toolbar** group on Windhawk's page: **Tighten the navigation buttons** (on) and **Gap between the navigation buttons** (4 pixels by default, 0 to 16; the gap is also the space at the two ends). Switching it off, or unloading the mod, puts Explorer's margins back.
- The space between Refresh and the address bar is the gap too: the command bar's spare column of 6 pixels after the buttons is taken away, so it was 17 pixels and is now about 7.

### Changed
- New defaults, from the author's own settings: double-click on empty space goes up one column (on), only real Desktop items (on), no focus outline (off), the theme's colour as the panel background (instead of Explorer's), no accent on stale columns (off), alternate row shading (on).

## 0.76.2 (2026-11-17)

### Fixed, the smaller findings of the Labels review
- Writing a tag no longer cuts the stream before the new text is written, so a write that fails cannot leave an item with no tags, and a hidden or system item can be tagged again.
- A file that is in use, or that you have no access to, is no longer reported as having tags "this version cannot read": the pane and the menus say it is in use or read-only.
- Hidden items are read for labels only when Explorer shows hidden items, so a hidden file can no longer take the dot away from a visible one with the same displayed name.
- On a drive that cannot keep labels, the Tags entry of both right-click menus is greyed, with "needs an NTFS drive" in its name, instead of missing.
- The fonts and flyouts that labels keep on each window thread are released at unload.

## 0.76 (2026-11-17)

### Fixed, from a review of the whole Labels work
- The handlers that the right-click menu gave to Explorer's elements are taken off at unload in every window, also in one that was open before the mod loaded (such a window has no state of its own, so nothing took them off before). This could have crashed Explorer after the mod was disabled or updated.
- A folder is read again when you show it, and when it has been on show for 20 seconds, so a rename, a file tagged by another program or a file copied in shows its dot. The read of a folder you leave is cancelled. Before, a folder read once kept its dots until Explorer restarted.
- Tagging one of two items that show the same name (hidden extensions) no longer puts the dot on both, and a newly tagged item no longer has its dot on top of the type text in Content.
- The pane says when an item has no dot because another item shows the same name.
- A tag name such as `{wip}` or with unusual characters can no longer stop the pane from showing a file.
- Tags from the right-click menus (Explorer's and Columns') now report a read-only or locked file, and refresh the pane. Many files are written on a thread of the mod, not by the window, and a big selection is not read every time the pane or the menu is built.
- After **Show labels** is switched off, the dots go at once and the Tags entry of a menu that was already built is taken out.

## 0.75 (2026-11-17)

### Added
- **Labels**, a new area: colour labels, like the tags of macOS, on files and folders. A label is a coloured dot with an optional name, from seven colours and a neutral gray, up to 16 on an item.
- The dots show after the name in every view of Explorer's own list (Details, List, Tiles, Content, and the icon sizes) and in Columns, stacked when there are several and with a small plus when there are more than four. They follow the light and dark theme.
- A **Tags** section in the details pane: the labels of the selection as badges that wrap, an **Add a tag** link (the colours and an optional name), and a menu on each badge to remove it. With several items selected, a label on only some of them is dimmed, and its menu can add it to all.
- A **Tags** entry in the right-click menu, in Explorer's own list and in Columns: the colours as checkable items, **Add tag** and **Clear tags**.
- Labels are kept in a named stream of the file itself, so they follow it on NTFS when it is copied or moved. The file's dates are not changed. A drive that is not NTFS, a read-only file or a file in use is told to you, not guessed at.
- A **Labels** group on Windhawk's page: **Show labels**, and a name for each colour ("Red = Urgent").
- Window threads that the mod never saw being created (a window open before the mod loaded) are found by class, so labels work in them too.
- 0.75.1 and 0.75.2: in Content and Tiles the dot starts at the text edge of the name, and the Type line of a Content row is measured with the size of its own font.
- 0.74.x: the hook on Explorer's right-click menu; 0.73: Columns; 0.72: the pane; 0.70 to 0.71: the dots on the list.

## 0.68 (2026-11-17)

### Changed
- A new setting, **Count folder sizes by itself** (Details pane, Pane), off by default. A folder's size, contents and types are no longer counted when it is selected, which used some CPU on a slow computer: the pane shows a button, **Get folder size**, and counts when you press it. On, it counts by itself as before.
- 0.68.2: the readme in Windhawk and the README say that a folder's size is counted when you press Calculate, or by itself if the setting is on.
- 0.68.1: the folder size button is a compact **Calculate** button in the row "Size and content", the name on the left and the button on the right like the other details.
- The wheel hook is gone. A preview scrolls with the wheel without it, so the mod no longer puts a global mouse hook in Explorer.

## 0.67 (2026-11-17)

### Changed
- Archives open as folders when Windows hides their extension (the default: `backup.zip` is shown as `backup`). The real name decides, not the name shown.
- Renaming keeps the extension that is really there, shown or hidden, and a name with dots (`v1.2 notes`) is no longer cut at its last dot. This holds for one item and for several.
- 0.67.1: with a grouping on, Invert selection and Select none from the toolbar leave no group header selected.
- Taking Explorer's selection after a toolbar command, such as Invert selection, is linear: a big folder no longer makes the window wait.

## 0.66 (2026-11-16)

### Changed
- The mod's own settings window is gone: the settings are on the mod's page in Windhawk, in groups (Columns with its Behaviour, Content, Layout and Memory; Details pane with its Pane and Previews; and a new Limits group). The switch of each area is the first setting of its group. How it was built is kept in `docs/settings-window-notes.md`.
- Four options are fixed in the code, at the values they had: the marks of cloud, network and device items, a folder held under a drag opening, no line on the last column, and a click in the navigation pane making its folder the first column (this one now on for everybody).
- A change on Windhawk's page applies at once, and the columns are fitted and read again when it needs it.
- The settings keys changed with the groups, so every setting returns to its default once.
- The "Extras" entry of the toolbar's ... menu is gone, since it only opened the window.
- 0.66.4: the details pane's button in the toolbar no longer shows a ticked state: no accent colour, and the icon stays as Explorer draws it (the one-colour icon is gone with it). After this update, if the mod's settings page looks wrong, use Windhawk's reset to defaults once: the settings were renamed in 0.66.
- 0.66.3, from the third review: the calls that can show a dialog (paste, a drop, a rename in place) also hold the cleanup back until they return; the timer that waits for a dialog is held, so it ticks; the first change of a setting rebuilds the columns too; icons are asked for at the scale of the window's monitor; two leftover fields of the settings window are gone.
- 0.66.2: the macro for debug-only log lines is no longer named like a Windhawk symbol (`Wh_LogV` is now `LogVerbose`), which the catalog check flagged.
- 0.66.1: the texts of the settings, the readme in Windhawk and the documents are brought up to date (the minimum width and the item limit had no description).

## 0.65 (2026-11-16)

### Changed
- Type-to-jump uses the character the key makes on your keyboard layout (accented letters, digits that need Shift on AZERTY).
- A folder or file made from the toolbar's New menu is renamed in the column once it is listed, as the one just made, not the item that was selected.
- Icons are asked for at the size of the screen (they were 32 px and soft at high DPI).
- The log of a normal build is quiet: the lines that come with every click, selection or drag are only in a debug build.

## 0.64 (2026-11-16)

From the second catalog review.

### Changed
- The cleanup at unload no longer runs inside a dialog or a drag the mod started (a Delete confirmation, Properties, a paste, a drag out): it waits until that ends. The touch-drag operations that are still waiting are cancelled, and waited for without a time limit.
- The mouse-wheel hook for out-of-process previews belongs to one window thread, and is on only while the pointer is over the preview.
- A context menu no longer keeps its submenus and buttons alive until the window closes.
- Switching Columns off keeps the details pane; before, the pane went with it.
- The notify window's class counts any registration failure as a failure.
- "Open in new tab" presses the tab strip's add button, as a click would. 0.60 had changed it to the folder's `opennewtab` verb, which opened a new window.

## 0.63 (2026-11-16)

Smaller fixes, from the Windhawk catalog review.

### Changed
- OneDrive files of every sync state get their mark (the reparse tag mask kept only one of sixteen).
- A `.ts` file is a video only if it is an MPEG transport stream; TypeScript files are shown as text.
- The folder size is not counted for a drive's root or a network share, where the count ran for its whole time limit.
- The columns' width limits are read together, so a change made at the wrong moment cannot reverse them. The pane's preview height is shared safely between windows.
- System libraries are loaded from System32 only. Windhawk's own helpers replace hand-made hook and string-setting code. Settings that were always on are gone, and so are a few unused functions and a log helper that grew without end.

## 0.62 (2026-11-16)

### Changed
- A context menu, a drag or a rename in a column is closed or cancelled when that column's listing changes under it (a download finishing in the folder, for example), or when the column is removed. It kept row numbers, so Compress, Move or Rename could act on other files. The pointers to a removed column are cleared too.

## 0.61 (2026-11-16)

### Changed
- The timers of the panel and of the details pane no longer tick every 60 ms in every window. A window you are working in keeps the quick tick; a window left alone ticks every 120 to 250 ms, and one in the background every 400 ms. Anything pending, a drag, a load, an animation or a playing sound, keeps the quick tick.

## 0.60 (2026-11-16)

Hooks and input, from the Windhawk catalog review.

### Changed
- Alt+P is taken in Explorer's accelerator hook, on the window's own thread. The mod no longer installs a system-wide keyboard hook for each window, which every keystroke on the computer waited on.
- The mod sends no keys. The details pane is shown and hidden through Explorer's own Details control, as a click would. "Open in new tab" uses the folder's own verb instead of Ctrl+T, which went to whatever window had the focus.
- The preview handler is asked to take the focus itself. The mod no longer attaches Explorer's input to another process's.

## 0.59 (2026-11-16)

### Changed
- A closed Explorer window now releases what the mod put in it: the panel, the details pane (its player and preview handler included), the menu entries and the window's state. It was left behind before, a little more with each window closed. The mod watches the window's own destruction for this. 0.59.1 also stops a song or video playing in a window that was open before the mod loaded and then closed, and makes the unload wait for a touch drag that is still getting its files ready.

## 0.58 (2026-11-16)

### Changed
- Every handler the mod gives to a XAML element is now kept as a revoker and taken off at unload, before anything is closed. XAML freed those handlers late, after the mod was gone, which crashed Explorer just after a disable. The handlers of removed elements are dropped from the list as it grows. 0.58.1 and 0.58.2 cover the routed handlers (the settings page, the slider, the list) and the last few events.
- Thread-local objects no longer run code when a thread ends after the mod is unloaded.

## 0.57 (2026-11-16)

Unload safety, from the Windhawk catalog review. 0.57.1 also revokes the click handlers left on Explorer's own toolbar and View menu. 0.57.2 runs the window's queue before the mod is unloaded, so XAML finishes freeing what the mod closed (it crashed Explorer just after a disable). 0.57.3 also does it after the settings window is released, and waits a set time, not only for the marker. 0.57.4 bounds the loop, which could spin while a window kept posting messages.

### Changed
- Every thread the mod starts is kept and waited for when the mod unloads: the folder size, the preview reader, the details reader, compress and the "switch off" cleanup. None is left running.
- An archive being made stops when the mod unloads, and the partial file is removed.
- Reading a folder stops when the mod unloads, and a slow network folder is told to give up.
- Unloading waits for each window's cleanup as long as it takes, instead of giving up after 4 seconds.
- The settings window is released properly at unload, instead of being left behind.

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
