# Explorer Columns View, plan

## Done in 0.1, tested 2026-11-12 on ARM64, build 26300.9974

- Columns entry in the View menu, inserted on first open, checked state kept.
- WinUI 3 panel over the file list, one column per folder level, chevrons.
- Explorer's own view follows the active column: tab title, address bar, Back.
- Back trims the columns to the matching folder.
- Choosing another layout removes the panel.
- Compact mode row height, double-click opens a file.

## Known gaps in 0.1

- Sort and group from the Sort menu don't reach the columns.
- Selecting a file doesn't select it in Explorer's own view, so the preview
  pane, the details pane and the command bar don't see it.
- The status bar count stays on the first folder.
- Generic glyphs instead of real file icons.
- Fixed column width, end ellipsis.
- State is per window and lost on restart.
- Folders load on the UI thread.

## 0.2, next compile

1. Sort and group. Read sort columns and group-by from the starting folder's
   view, apply to every column, re-apply when the Sort menu changes them.
   Group headers through the shell's own categorizer, so names are localized.
   Unverified API path, log everything.
2. Selection mirrored into Explorer's own view, for the native preview pane,
   details pane, status bar and command bar.
3. Column width: fit to the longest name up to a maximum, middle ellipsis,
   drag to resize, double-click the separator to refit.
4. Settings grouped in sections, see architecture.

## 0.3

- Real file icons and thumbnails.
- Keyboard: left and right between columns, type to jump.
- Native right-click menu.
- Background loading for slow folders.

## 0.4

- Drag and drop between columns.
- Scroll animation to the newest column.
- Per-folder memory and a "columns everywhere" option.
- Entry present at window creation, not on first menu open.
- English strings moved to a table for translation.

## Decisions, 2026-11-12

- The mod is named Explorer Extras, id `explorer-extras`, from 0.2 on.
- One mod with a section per feature, see architecture.
- Settings window, laid out like Windows Settings, when the second feature
  lands. Until then, Windhawk's settings page with nested sections.
- Settings entry goes in the command bar's "..." menu under Options. Untested.
- No compact-rows option of the mod's own. Explorer has it under View, Show,
  Compact view, and the columns follow it.
- A feature ships off by default while unfinished, and may default to on once
  complete.
- Off means gone: no menu entry, no hook work.
- One-time tip on the Columns entry, never shown again after dismissal.

## Feature backlog beyond columns

- Labels, macOS style.
- New folder from selection.
- Better archive extraction flow.
- Toolbar customization.
- Status bar and info bar customization.
- Small tweaks.

## Architecture: one Explorer mod with sections

Planned features beyond columns: labels, new folder from selection, archive
extraction flow, toolbar customization, small tweaks.

- One mod, Explorer Extras. Each feature has its own settings
  section with an `enabled` switch, using Windhawk's nested settings.
- One shared core: hooks into FileExplorerExtensions.dll, the per-window state,
  shell helpers, logging.
- Source kept as one file per feature under `explorer-extras/src/`, joined into a
  single `.wh.cpp` by a small script, since Windhawk compiles one file.
- Cost: a crash in one feature takes the others down. Each feature guards its
  entry points and can be switched off without a recompile.

## 0.3.5 notes, unverified live

- Icons: the worker reads 32 px `IShellItemImageFactory` images for the first
  400 items after the list is delivered, in batches of 16, matched by column
  generation, shown in a 20 px Image over the glyph placeholder.
- Right-click: `IContextMenu::QueryContextMenu` into a hidden HMENU, copied
  into a `MenuFlyout`, run with `InvokeCommand`. No TrackPopupMenu, because of
  the modal-loop crash class. Send to and Open with submenus may be empty
  (`HandleMenuMsg` is not called). Next retest: right-click a file and a
  folder, try Copy, Properties and Delete.

## Probe results, 2026-11-13 (`probes/probe-tabs-virtual.wh.cpp`)

New tabs:
- A "+" tab creates, on the window's own UI thread, `SHELLDLL_DefView`, then
  `DUIViewWndClassName`, then the `ShellTabWindowClass` itself, parent
  `CabinetWClass`. A hook on `CreateWindowExW` sees the tab window only after
  the call returns, with its children already there. A new window shows the
  same sequence and then `CabinetWClass`. So a post-call hook on class
  `ShellTabWindowClass` is a creation hook for tabs, on the same thread as
  the command bar, where `StateForThisThread` works.
- `SHELLDLL_DefView` is also created again on navigation inside a tab, so it
  is not a tab-creation signal.

Desktop and Control Panel items, root of the namespace (27 items):
- Virtual folders bind and enumerate: This PC, Recycle Bin, Control Panel
  (`::{26EE0668-...}`), Libraries, Linux, Gallery, Network, Home. They are
  `FOLDER` and not `FILESYSTEM`, so `IsFolderItem` is true for them.
- The Desktop's own "Control Panel" entry (`::{5399E694-...}`) and about 15
  Control Panel applets (Mouse, Keyboard, Region, ...) are neither `FOLDER`
  nor `STREAM` and don't bind. They are launchers: the column shows them as
  files, and they should be opened with the shell rather than navigated into.
- Recycle Bin items have the original path as their display name and a
  `$Recycle.Bin` path as parsing name.
