# Explorer Extras, handoff notes

For the next session, human or model. Written 2026-11-13.

## Read first

1. `ROADMAP.md` at the repo root: what is done and what is next.
2. `explorer-extras/PLAN.md`: decisions and architecture.
3. `explorer-extras/SPEC-0.2.md` and `SPEC-0.2.1.md`: how each part was
   meant to work, with what was unverified at the time.
4. The `windhawk-explorer-surfaces` skill: what the probes proved.

## State

- Current file: `explorer-extras/explorer-extras.wh.cpp`, version 0.3.15, not
  compiled yet. 0.3.5 adds icons and thumbnails (`LoadIcons`, `ApplyIcons`)
  and the right-click flyout (`OnRowRightTapped`) on top of 0.3.4, written
  without a compiler: expect compile errors, read `compiler_errors.log`.
  Last tested version: 0.3.4.
- 0.3.1 to 0.3.4, tested by Ari on 2026-11-13: restore waits one tick
  (shorter flash), the horizontal wheel glides across the columns and never
  scrolls a column vertically, the vertical wheel scrolls only the hovered
  column. The wheel handlers are in `ShowPanel` (root and strip) and in
  `AddColumn` (a handler on each list's row panel, added when the list
  loads). A scroll viewer inside each list sees the wheel first, so a
  handler on the list itself is too late.
- Compiled and tested by Ari up to 0.3.0 on 2026-11-13. Confirmed in 0.3.0:
  the Columns entry is in the View menu when a window opens, new windows
  open in Columns when it was the last layout chosen, also after an Explorer
  restart, choosing another layout turns that off, no crash on closing a
  window with columns on. Confirmed in 0.2.5: minimum and maximum width on
  drag, arrow keys between columns, sort inside groups.
- 0.3.0: hook on `CommandBarControl_Wave1::OnDpiChanged` gives the window's
  `XamlRoot`, a 50 ms timer finds the View flyout and inserts the entry, and
  the flag `lastLayoutColumns` in Windhawk's per-mod storage records the
  last layout. Found with `probes/probe-window-created.wh.cpp`, see the
  skill for the facts and the two crashes on the way.
- Open in 0.3.0: Explorer's own list shows for about 270 ms before the
  columns, because the panel waits for three ticks with a visible file
  list. A new tab in a window without columns doesn't open in Columns.
  Changing layout from a keyboard shortcut doesn't clear the flag. These
  three are untested ideas, not bugs seen in a log.
- 0.2.3: order inside groups, maximum width on drag, highlight stays on the
  current column, Left, Right and Enter move between columns.
- 0.2.4: fix for the crash when an Explorer window closes with the panel on.
  The panel's objects are let go without release, see `AbandonWindow`.
- 0.2.5: a drag also stops at the minimum width.
- Todo from Ari: focusing a Control Panel item or another virtual shell item
  in the Desktop folder, see the roadmap.
- `Columns Demo` on Ari's Desktop is a test folder: 90 items, 6 levels,
  several file types, 5 pngs, a 40-file folder, long names.
- Rest of 0.3 in the roadmap: real icons and thumbnails, then the native
  right-click menu. The menu needs its own probe first, since a modal menu
  inside a XAML handler crashed Explorer before, see the core skill.
- Not done from 0.2: rows built only when visible. Every row is still built
  up front.
- Open: group names from the fallback are English. Explorer's own
  categorizer returns no category for these folders,
  `GetCategoryForSCID` gives `S_FALSE`.
- `explorer-columns/` holds 0.1 and can be deleted.
- The editor's compile errors are not always in `compiler_errors.log`. The
  Windhawk clang at `C:\Program Files\Windhawk\Compilerin\clang++.exe`
  does a syntax check of the editor file for ARM64 with `-fsyntax-only`,
  `-target aarch64-w64-mingw32`, the flags in `compile_flags.txt` and
  `-DWH_MOD_ID=L"explorer-extras"`. It found the 0.3.0 error.

## How the work loop runs

- The mod's debug log is `probes/logs/explorer-extras.log`. It is kept
  across restarts and each line has a thread id. Read it before guessing.
  That folder is in `.gitignore`: it holds the user's file names.
- Windhawk's window runs elevated. A session driving the screen can't
  click in it. Ari clicks Edit and Compile.
- With the computer linked and `C:\ProgramData\Windhawk\EditorWorkspace`
  writable for Ari's account, the source can be written straight into
  `EditorWorkspace\mod.wh.cpp` while the mod is open in the editor. Check
  the file's `@id` first, so another mod isn't overwritten.
- Compile errors land in `EditorWorkspace\compiler_errors.log`.
- Compiled mods are in `C:\ProgramData\Windhawk\Engine\Mods\arm64`. The
  file name carries the version, which shows what Ari actually ran.
- Copying a changed file from a cloud session to the PC has sent a stale
  copy several times. Use a new staged file name for each version and
  compare checksums on the PC before writing to the editor.
- The non-WinUI part of the mod compiles outside Windhawk with
  `-DMOD_NO_WINRT`, using zig's mingw and stubs for the `Wh_` functions.
  The WinUI part can only be compiled by Windhawk.
- Test in a new Explorer tab opened with "+", and close it afterwards.
  Never navigate Ari's own tabs.
- The PowerToys command palette can count as the front window and block
  clicks. Ask for click access to it, don't work around it.

## Lessons from 0.3

- Never treat `this` of a command bar method as a XAML object.
- A coroutine moves its by-value arguments out of the caller's slot, so read
  them before the original runs.
- Look for a hook with a probe that hooks little and logs the full symbols,
  and raise the risk one step per run.

## Lessons from 0.2

- Never close or release XAML objects after their window is destroyed.
- A mod that logs into a folder it watches reloads forever. Own files are
  ignored and content changes don't trigger reloads.
- `MapColumnToSCID` has gaps. Don't stop at the first failure.
- When grouping is on, the group's property is also the first sort column.
- Two interfaces of one XAML object don't compare equal. Convert to the
  same type before comparing.
