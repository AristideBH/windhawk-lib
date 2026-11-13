# Explorer Extras, handoff notes

For the next session, human or model. Written 2026-11-13.

## Read first

1. `ROADMAP.md` at the repo root: what is done and what is next.
2. `explorer-extras/PLAN.md`: decisions and architecture.
3. `explorer-extras/SPEC-0.2.md` and `SPEC-0.2.1.md`: how each part was
   meant to work, with what was unverified at the time.
4. The `windhawk-explorer-surfaces` skill: what the probes proved.

## State

- Current file: `explorer-extras/explorer-extras.wh.cpp`, version 0.2.5.
- Compiled and tested by Ari up to 0.2.5 on 2026-11-13. Confirmed: no crash
  when a window closes with columns on, minimum and maximum width on drag,
  arrow keys between columns, sort inside groups.
- Reported and not yet done: Columns is not remembered when Explorer is
  relaunched.
- 0.2.3: order inside groups, maximum width on drag, highlight stays on the
  current column, Left, Right and Enter move between columns.
- 0.2.4: fix for the crash when an Explorer window closes with the panel on.
  The panel's objects are let go without release, see `AbandonWindow`.
- 0.2.5: a drag also stops at the minimum width.
- Next block is 0.3 in the roadmap: real icons, right-click menu, multiple
  selection, remote indicator, quick options flyout.
- Not done from 0.2: rows built only when visible. Every row is still built
  up front.
- Open: group names from the fallback are English. Explorer's own
  categorizer returns no category for these folders,
  `GetCategoryForSCID` gives `S_FALSE`.
- `explorer-columns/` holds 0.1 and can be deleted.

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

## Lessons from 0.2

- Never close or release XAML objects after their window is destroyed.
- A mod that logs into a folder it watches reloads forever. Own files are
  ignored and content changes don't trigger reloads.
- `MapColumnToSCID` has gaps. Don't stop at the first failure.
- When grouping is on, the group's property is also the first sort column.
- Two interfaces of one XAML object don't compare equal. Convert to the
  same type before comparing.
