# Explorer Extras 0.2, build spec

Written 2026-11-12 for whoever builds 0.2, human or model. Start from
`explorer-columns.wh.cpp` 0.1, which compiles and passed its test. Read
`PLAN.md` and the `windhawk-explorer-surfaces` skill first.

Items marked UNVERIFIED have not been run. Log their results and expect to
adjust.

## 0. Rename

- `@id explorer-extras`, `@name Explorer Extras`, version 0.2, file
  `explorer-extras/explorer-extras.wh.cpp`. Log file `explorer-extras.log`.
- Settings nested under one section, with Windhawk's nested syntax:
  `columns: enabled, maxWidth, minWidth, followActiveColumn, maxItems`,
  and top-level `debugLog`. No compact option: read Explorer's own switch.
- The user disables "Explorer Columns View" 0.1 before compiling.

## 1. Sort and group

Goal: the sort and group settings of the folder where Columns was turned on
apply to every column, and a change in the Sort menu re-applies them.

Window state gains a `SortSpec`: a vector of `SORTCOLUMN` and a group key
with its direction.

Reading, from Explorer's own view through `IFolderView2`:
- `GetSortColumnCount`, `GetSortColumns`.
- `GetGroupBy(&key, &ascending)`. An empty key means no grouping.

Sorting a column's items, UNVERIFIED:
- Bind the folder as `IShellFolder2`. Find the column index whose
  `MapColumnToSCID(i, &key)` equals the sort key. Stop at the first failure.
- Compare with `CompareIDs(index, a, b)`, flip for descending. Apply the
  sort columns in order as tie-breakers.
- 0.1 forces folders first. Check whether `CompareIDs` on the name column
  already does that, and drop the forced rule if so.
- If no column maps to the key, fall back to the name column and log it.

Group headers, UNVERIFIED:
- `folder->CreateViewObject(hwnd, IID_ICategoryProvider)`.
- `GetCategoryForSCID(&key, &guid)`, then `CreateCategory(&guid,
  IID_ICategorizer)`.
- `ICategorizer::GetCategory(count, children, ids)` gives one id per item.
  `GetCategoryInfo(id, &info)` gives the localized name. `CompareCategory`
  orders the groups.
- Render each group as a disabled, non-selectable `ListViewItem` with small
  semi-bold text, followed by its items. Keep a row-to-entry index map,
  since rows and entries no longer line up.
- No provider or a failure: show the column ungrouped and log it.

Keeping descendants in step, decided 2026-11-12:
- The mod never writes sort or group settings onto Explorer's view. Each
  folder keeps its own saved view.
- The columns sort and group themselves from the window's `SortSpec`.
- The spec is read once when Columns is turned on, from the starting folder.
- It is read again after the user picks something in the Sort menu. Hook
  `CommandBarManager::InvokeMenuItem`, already resolved in the View menu
  probe, and queue one read after it returns. No polling.
- Accepted limit: when Explorer's view sits in a descendant folder, the Sort
  menu's check marks show that folder's saved state, which can differ from
  the columns until the user picks something.
- Later setting, not in 0.2: each column keeps its own folder's sort and
  group. Keep the spec per column in the data model now, all pointing at
  the window's spec, so that setting is a small change.

Checked 2026-11-12: `IFolderView2`, `SORTCOLUMN`, `IShellFolder2`,
`ICategoryProvider`, `ICategorizer` and `CATEGORY_INFO` all exist in the
mingw headers and a call sequence compiles for ARM64. Runtime behaviour is
still UNVERIFIED.

## 2. Selection mirrored into Explorer's view

Goal: the native preview pane, details pane, status bar and command bar act
on the file selected in a column.

- State gains `pendingSelect`, a child pidl plus its folder.
- File selected: if Explorer's view already shows the file's folder, call
  `IShellView::SelectItem(child, SVSI_SELECT | SVSI_DESELECTOTHERS |
  SVSI_ENSUREVISIBLE | SVSI_NOTAKEFOCUS)` now. Otherwise navigate and set
  `pendingSelect`. The tick applies it when the expected folder is reached.
- Folder selected: Explorer's view goes inside it, nothing selected. Same as
  0.1.
- Check that the preview pane appears beside the panel. The panel covers
  only the `SHELLDLL_DefView` rectangle, and the pane is a sibling, so it
  should. UNVERIFIED.
- Seen in the 0.1 test: the status bar kept the first folder's count, and
  the address bar showed only the folder name without parent crumbs after a
  column navigation. Recheck both after this change.

## 3. Column width

- Fit: measure the longest names with a detached `TextBlock` and
  `Measure`. Measure only the 20 longest by character count. Width is that
  plus icon, chevron and padding, clamped to `minWidth` and `maxWidth`,
  defaults 160 and 420.
- Middle ellipsis: WinUI has none. When a name is wider than its cell, cut
  the middle and keep the start, the last six characters and the extension.
  Keep the full name in a tooltip. Recompute when the column's width changes.
- Resize: a 6 px transparent `Border` at each column's right edge with
  pointer pressed, moved and released handlers and pointer capture. Dragging
  sets the list's width. Double-tap refits.
- Resize cursor through `UIElement.ProtectedCursor` with an
  `InputSystemCursor`. UNVERIFIED, skip it if it costs a compile round.
- A manually resized column keeps its width until it is closed.

## 4. Performance, the rules for 0.2

0.1 is simple and slow in four places. Fix them in this order.

1. Rows are built for every item up front. Switch to virtualized rows: set
   `ItemsSource` to a vector of boxed row indices and build each row's
   content in `ContainerContentChanging`, reusing the container's existing
   content when it has one. Only visible rows cost anything. This also makes
   re-sorting cheap, since only the index vector changes.
2. Folders are read on the UI thread. Read them on one worker thread per
   window, initialised as an STA. Pidls are plain memory and can cross
   threads. Shell folder objects can't, so the worker binds its own from the
   pidl. Post the result back through the window's `DispatcherQueue`. Give
   every request a generation number and drop results that arrive stale.
3. The tick calls the shell four times a second. Navigation replaces the
   `SHELLDLL_DefView` window, seen in the bench log, so compare that window
   handle first and only ask the shell for the current folder when it
   changed. Keep the tick for the rectangle, which is plain Win32.
4. Every folder click makes Explorer navigate, which makes Explorer read the
   same folder a second time. Debounce it: navigate Explorer's view 350 ms
   after the last selection change, so a run of arrow keys or quick clicks
   costs one navigation.

Smaller points:
- Classify folders without a call per item: enumerate once with
  `SHCONTF_FOLDERS` and once with `SHCONTF_NONFOLDERS`. UNVERIFIED for zip
  files, which 0.1 treats as files. Keep 0.1's attribute check as fallback.
- Group ids come from one `ICategorizer::GetCategory` call for the whole
  column, never one call per item.
- Width measuring touches at most 20 names per column.
- Sorting 1500 items costs about 16000 `CompareIDs` calls. That is fine on
  the worker thread. Don't cache property values to avoid it.
- Hidden files: follow Explorer's own "show hidden" setting with
  `SHCONTF_INCLUDEHIDDEN`. 0.1 never shows them.
- Icons, for 0.3: system image list index through `SHGetFileInfo` with a
  pidl, one cached image source per index, thumbnails loaded on the worker.

## 5. Carry over from 0.1 unchanged

View menu entry and its sibling handlers, panel hosting, the tick, Back
handling, `CloseEverything`, file logging.

## Test checklist

Run in a new tab opened with "+", and close it afterwards. Never navigate
the user's own tabs.

1. View menu shows Columns once. Choose it.
2. Sort by date in the Sort menu: every column re-sorts, and no folder's
   saved view is changed by the mod.
3. Group by type: headers appear in every column, in French.
4. Open two folder levels: descendants use the same sort and group.
5. Select a file: preview pane shows it, status bar and command bar react.
6. Long file name shows a middle ellipsis and a tooltip.
7. Drag a column edge, then double-click it.
8. Back, then choose Details. Close the tab.
9. Disable the mod: entry and panel are gone, Explorer keeps running.
10. Open a folder with more than 1000 items: the column appears without
    freezing the window, and scrolling stays smooth.
