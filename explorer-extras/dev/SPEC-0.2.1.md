# Explorer Extras 0.2.1, fix list

From Ari's test of 0.2 on 2026-11-13. Each item has what was seen, the likely
cause and the fix. Causes marked GUESS were not confirmed in a log.

Build order: 1 and 2 first, they decide whether the rest can be debugged.

## 1. Crash after opening a second Explorer window

- Seen: Explorer crashed shortly after a second window was opened.
- Cause: unknown. The log of that session is gone, see item 2.
- Fix: none yet. After item 2, reproduce with two windows and read the log.
  Suspects, all GUESS: the second window's XAML source created while the
  first window's tick runs, `FindTopLevel` picking the other window when
  the foreground window belongs to another thread, a result from the worker
  applied to the wrong window's column.
- Add to every log line the thread id, so two windows can be told apart.

## 2. The log is erased at each start

- Seen: after the crash, the new session's log replaced the old one.
- Cause: `OutInit` truncates the file.
- Fix: append, write a session header with date and pid, and trim the file
  to its last 2000 lines at start.

## 3. Columns entry missing after Explorer restarts

- Seen: after the crash and restart, no Columns entry until the mod was
  restarted.
- Cause: `Wh_ModInit` ran before `FileExplorerExtensions.dll` was loaded, so
  no hook was installed. The log line for that case exists in the code.
- Fix: when the module is missing, hook `LoadLibraryExW` and install the
  symbol hooks when that module loads, then `Wh_ApplyHookOperations`. This
  is the pattern the core skill describes for late-loaded modules.

## 4. Copy, rename and delete don't update the columns, Refresh does nothing

- Cause: columns are read once. Nothing listens for changes.
- Fix: `SHChangeNotifyRegister` on the window's UI thread with a
  message-only window, one registration per open column folder,
  non-recursive. On create, delete, rename, mkdir, rmdir, update: reload
  that column after a 300 ms pause, keeping its selection.
- Refresh: the address bar's Refresh acts on Explorer's own view. Reload
  every column when the file list window is replaced while the folder stays
  the same. GUESS that Refresh replaces it. If not, find the Refresh
  command in `InvokeMenuItem` or accept F5 only.

## 5. Back, Up and outside navigation collapse the columns

- Seen: Back left a single column. Up needs proper handling.
- Cause: `OnExplorerFolderChanged` trims the columns or rebuilds from one
  folder.
- Fix, one rule: the columns always show the path from the root column down
  to Explorer's current folder.
  - Current folder is one of the columns: keep every column, move the
    active column to it. Don't trim.
  - Current folder is under the root column: open the chain of columns
    along the path, selecting each step. Use `ILFindChild` or walk parents.
  - Current folder is the parent of the root column: insert it as a new
    first column with the old root selected. This is Up.
  - Anything else: rebuild with the current folder as root.
- Active column: mark it visibly. A 2 px accent line on top of the column
  or a slightly lighter background. Follow the system accent colour.

## 6. Grouping

- Seen: groups never show. Picking a group in the Sort menu also moves
  focus to the address bar and opens it.
- Cause, from the log: `group: no categorizer for this property,
  hr=0x80004002` on every column. That line covers two calls, so which one
  fails is unknown.
- Fix: log `GetCategoryForSCID` and `CreateCategory` separately, with the
  property key. Try `CanCategorizeOnSCID` first. If the folder's provider
  can't do it, fall back to grouping by the property's display string read
  through `IShellFolder2::GetDetailsEx`, which gives plain groups without
  Explorer's date ranges.
- Address bar focus: GUESS that Explorer applies the group to its own
  covered view and moves focus as part of that. Check whether it happens
  with the mod's `InvokeMenuItem` hook disabled.

## 7. Resize handle

- Seen: resizing works, but the cursor doesn't change and the separator is
  plain.
- Fix: a thin 1 px line at rest. On hover, show the resize cursor and a
  short rounded handle centred on the line, in the style of the Community
  Toolkit sizers, reference
  https://github.com/CommunityToolkit/Labs-Windows/issues/101. Read that
  issue before styling.
- Cursor: `InputSystemCursor::Create(InputSystemCursorShape::SizeWestEast)`
  set through the element's `ProtectedCursor`. UNVERIFIED from C++/WinRT
  without a subclass.

## 8. Compact view and hidden files

- Seen: compact view changed nothing, even after leaving and re-entering
  Columns. Hidden files only updated after changing view.
- Cause for compact, GUESS: the registry value read is wrong or the row
  height is overridden by the list item's own minimum. Log the value read
  and the height applied.
- Fix for live update: both switches live in the View menu's Show submenu
  and go through `InvokeMenuItem`. After any menu command, re-read both and
  reload every column if either changed.

## 9. Remote items

- Wanted: an indicator on folders and files that live on a phone or a
  network share.
- Fix, for 0.3 with real icons: read `SFGAO_ISSLOW` and the folder's
  parsing name, show a small overlay glyph. Column headers are a second
  place for it.
