# Explorer Extras, handoff notes

For the next session, human or model. Written 2026-11-13.

## Read first

1. `ROADMAP.md` at the repo root: what is done and what is next.
2. `explorer-extras/PLAN.md`: decisions and architecture.
3. `explorer-extras/SPEC-0.2.md` and `SPEC-0.2.1.md`: how each part was
   meant to work, with what was unverified at the time.
4. The `windhawk-explorer-surfaces` skill: what the probes proved.

## State

- Current file: `explorer-extras/explorer-extras.wh.cpp`, version 0.5.1,
  compiled and tested by Ari on 2026-11-13. `ROADMAP.md` says what is done.
  The version history is in `git log`.
- Architecture in one paragraph. One WinUI panel per Explorer window, hosted
  as a `DesktopWindowXamlSource` over the file list, with a per-window
  `WindowState` used only on that window's UI thread. Folders and icons are
  read by one worker thread per window, results come back through the
  window's dispatcher queue and are dropped by generation or by a cancel flag.
  Each entry has a row shell at once and its content is built as the row comes
  near the screen. Explorer's own view stays under the panel and follows the
  active column, so the toolbar, panes and status bar act on the selection.
- Hooks: `FileExplorerExtensions.dll` command bar handlers (pointer press,
  `OnDpiChanged` for window creation, `InvokeMenuItem` for toolbar commands),
  `CreateWindowExW` for new tabs, `TranslateAcceleratorW` while the rename box
  has the keyboard, and `IShellBrowser::BrowseObject` (vtable slot 11) for the
  navigation trace and the sidebar click. See `PLAN.md` and the skill.
- Settings: enabled, minimum and maximum width, follow active column,
  restore on open, maximum items (default 5000), sidebar sets the first
  column, keyboard on headers (off), double-click up (off), focus outline, compact scrollbars, panel background (a test
  setting), debug log.
- Things that cost time and are worth knowing:
  - The unexplained jumps of Explorer's view were navigation pane clicks and
    the shell's own "paste" verb. A paste now goes through the folder's drop
    target and the guard is gone.
  - Pidls made from a path are rooted under This PC, the columns' pidls under
    the Desktop. They never compare equal, so compare paths for files and
    folders on disk.
  - A column's stale state is lifted by working in it, see `LiftStale`.
  - Never release XAML objects after their window is destroyed, see
    `AbandonWindow`.
- Parked, don't chase without a new idea: the transparent background mode and
  a blurred panel background (see the roadmap).
- `Columns Demo` on Ari's Desktop is a test folder, rebuilt by
  `explorer-extras/make-columns-demo.ps1`.
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
- The whole file, WinUI part included, can be syntax-checked with Windhawk's
  own clang, see the last section. Linking is still Windhawk's job.
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

## Compile check without the Windhawk editor

Windhawk's own clang is at `C:\Program Files\Windhawk\Compiler\bin\clang++.exe`.
A syntax-only pass over the whole mod finds the compile errors in seconds:

```
clang++ -x c++ -std=c++23 -target x86_64-w64-mingw32 -DUNICODE -D_UNICODE \
  -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 \
  -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD '-DWH_MOD_ID=L"explorer-extras"' \
  '-DWH_MOD_VERSION=L"X.Y.Z"' -include windhawk_api.h -Wno-unused-parameter \
  -Wno-missing-field-initializers -Wno-cast-function-type-mismatch -fsyntax-only \
  -I"C:/Program Files/Windhawk/Compiler/include" explorer-extras/explorer-extras.wh.cpp
```
Run it before every handover. It does not link, so missing `-l` flags still
show up only in Windhawk.
