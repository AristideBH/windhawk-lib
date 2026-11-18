---
name: windhawk-mod-development
description: Core rules for writing, reviewing or debugging any Windhawk mod (.wh.cpp) in this repo. Covers the metadata and settings blocks, threading, event and hook lifetime, shutdown safety, crash triage, the compile and test loop, and PLAN.md conventions. Read this before touching any .wh.cpp file, before proposing a fix for a crash or freeze, and before adding a setting, a thread, a global WinRT object or an event handler. The other windhawk-* skills assume this one is loaded.
---

# Windhawk mod development

This skill holds the rules every mod in this repo follows. Most of them come from incidents logged in the mods' PLAN.md files. Each rule names the incident that produced it so you can read the full story.

Sister skills, loaded on top of this one:

- `windhawk-recon` before writing code against a process you haven't inspected.
- `windhawk-taskbar-xaml` for anything injected into the Windows 11 taskbar.
- `windhawk-ui-kit` for settings schemas, settings windows, popups and the native look.
- `windhawk-explorer-surfaces` for File Explorer windows and the desktop.

## How work goes wrong here

Seven bug classes account for most of the incident log. Check a change against each one before you hand it over.

| Class | Typical symptom | Section |
| --- | --- | --- |
| Wrong thread | Freeze with an empty log, timer or hotkey that never fires | Threading |
| Callback outlives the DLL | Explorer crash, faulting module "unknown", offset 0 | Event and hook lifetime |
| Re-entrancy | Crash on right-click, runaway log, stack overflow | Re-entrancy |
| Settings not parsed | Every setting reads 0 or empty, "no settings available" | Settings |
| Guessed UI structure | Element never appears, handler never fires | `windhawk-recon` |
| Architecture mismatch | Byte pattern doesn't match, feature silently off | Architecture |
| Load order between mods | Works in one Explorer session and not the next | `windhawk-taskbar-xaml` |

## Metadata block

```cpp
// ==WindhawkMod==
// @id              my-mod-id
// @name            My Mod
// @description     One sentence
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32
// @license         MIT
// ==/WindhawkMod==
```

- `@id` allows only `0-9`, `a-z` and hyphens.
- `@version` must increase with every published version. Bump it on every build you hand over, and log it from `Wh_ModInit`. In the Snipaste session a test ran old code because the editor source was never replaced. The version line in the log is how you catch that.
- The readme block can only embed images from `i.imgur.com` and `raw.githubusercontent.com`.
- Every block closes with a slash: `// ==/WindhawkMod==`, `// ==/WindhawkModReadme==`, `// ==/WindhawkModSettings==`. See Settings for what a missing slash costs.

## Architecture

The wiki defines `@architecture x86-64` like this. On an ARM64 device, a mod that only targets processes from a fixed list is compiled as ARM64. The list is StartMenuExperienceHost.exe, SearchHost.exe, explorer.exe, ShellExperienceHost.exe, ShellHost.exe, dwm.exe, notepad.exe and regedit.exe. Any other target gets both amd64 and arm64 builds.

Ari's main machine is ARM64, a Snapdragon laptop. Confirmed there on 2026-11-12: every explorer.exe probe mod logged that it was compiled as arm64, as the wiki says. taskbar-widget-stack/PLAN.md Incident 3 says Windhawk always builds x64 on that machine. That note is out of date, and code written around it should be rechecked.

Rules:

- Never pick a code path with `#if defined(_M_X64)` when the path reads another module's machine code. The target function's architecture and the mod's architecture are separate facts. Try each known byte pattern at runtime.
- Log the compiled architecture once in `Wh_ModInit`.
- A mod that reads or writes another module's instructions needs an ARM64 pattern and an x64 pattern, or it is x64-only and the readme says so.
- Test-compile for both when you can. A syntax check with a cross-compiler catches most mistakes before Ari's machine does.

### Symbol lookups on this machine

Windhawk reads symbols from files it downloads from Microsoft's public symbol server. If Microsoft hasn't published the file for a module's exact build, the lookup returns nothing, fast and without an error.

On 2026-11-12 Ari's laptop ran 26H2, build 26300.9974, and the server answered 404 for every System32 module tried: `taskbar.dll`, `ExplorerFrame.dll`, `Windows.UI.FileExplorer.dll`, `shell32.dll` and `dui70.dll`. It answered 200 for the two modules that ship in app packages, `Taskbar.View.dll` and `FileExplorerExtensions.dll`, and lookup worked for both. Whether the module is a hybrid ARM64X binary made no difference.

The cause is the update of 2026-11-10, KB5132061, and it isn't limited to preview builds. Microsoft had not yet published symbol files for that update's System32 modules. Users on 24H2 and 25H2, build 26100.9974, reported the same failure in Windhawk issue 1193, and a per-build listing of `taskbar.dll` showed every build from that update as missing on the server while all earlier builds were present. Expect this to recur after any Windows update, for days at a time, and to clear without a change on the mod's side. Run `probes/probe-symbols.wh.cpp` again to see whether it has cleared.

Update 2026-11-13, from Ari: Microsoft published the symbol files for that
update's System32 modules later the same day, and every mod that had stopped
working came back. So the gap lasted about three days. The advice below still
holds for the next update.

What follows from that:

- A `HookSymbols` call into a System32 module can fail for days after a Windows update, on any machine. Every such hook needs a fallback and one clear log line.
- Prefer hooks in app-package modules when the same job can be done from either side. The Start button mod and the View menu probe both work this way.
- For System32 modules, look for exported functions first. They need no symbol file. `dui70.dll` exports many of its C++ methods by decorated name, and File Explorer Styler hooks one of them through `GetProcAddress`.
- The taskbar mods' `taskbar.dll` hooks are in doubt on that machine. Check their log before debugging anything else there.
- `probes/probe-symbols.wh.cpp` reports, per module, whether the server has the symbol file. Run it first when a symbol hook fails.

### Hooking C++ member functions

Confirmed on ARM64 on 2026-11-12, with about 25 hooks in `FileExplorerExtensions.dll`:

- The object comes first. Reference parameters are pointers. A C++/WinRT projected type is one pointer wide, so a hook can declare `winrt::hstring const&` or `IVector<T> const&` directly for a parameter of that type.
- A member function that returns any class type by value takes the result's address as a hidden second argument and returns that address. This includes empty types such as `winrt::fire_and_forget`. Declare it as `T* Hook(void* pThis, T* result, ...)` and return what the original returns.
- A constructor returns the object.
- Class-type parameters passed by value arrive as pointers to a temporary.
- Declare every argument the real function has, even when the hook only logs. A hook with too few lets its own code overwrite the registers the original needs.
- Copy symbol strings from a log, never by hand. Some end in a space.

## Settings

### What the block accepts

Windhawk 1.7.3 accepts `$name`, `$description` and `$options`. Types are boolean, integer, string, arrays of those, and nested groups. Nested keys join with dots, as in `recolor.gradient.lightenPercent`.

Windhawk 2.0 adds `$format`, `$float`, `$min`, `$max`, `$dynamicSelect`, `$showIf` and `$hideIf`. Version 1.7.3 rejects a block that contains any of them unmarked, and the mod won't install. Prefix each one with `#!` and exactly one space:

```yaml
- Opacity: "0.85"
  $name: Opacity
  #! $float: true
  #! $min: 0
  #! $max: 1
```

Once one marker appears in a block, mark every 2.0 annotation in that block. `$showIf` and `$hideIf` only hide a control. The stored value still reaches the mod.

Quote hex colors. An unquoted `#3399FF` starts a YAML comment.

### Silent failures

These all produced a mod that loaded and ran with wrong settings and no error.

- **Closing marker without the slash.** Windhawk treated the block as absent, and every `Wh_GetIntSetting` returned 0. Eleven incidents passed before anyone looked. See taskbar-widget-stack/PLAN.md Incident 12.
- **Invalid YAML.** An anonymous list of groups failed to parse and blocked every other setting in the block. See Incident 13.
- **Empty array default.** The default for any array setting is `[""]`. A default of `[]` fails schema validation.
- **Null check on a string setting.** `Wh_GetStringSetting` returns an empty string for a missing value, never null. A fallback written as `value ? value : fallback` never runs. Test `value && value[0]`.

Two habits prevent all four. After any edit to the block, open the mod's Settings tab in Windhawk and confirm the controls appear. And log the loaded values once at the end of `LoadSettings()` behind a debug flag.

### Reading arrays

```cpp
std::vector<std::wstring> GetStringArraySetting(const std::wstring& name) {
    std::vector<std::wstring> result;
    std::wstring format = name + L"[%d]";
    for (int i = 0;; i++) {
        auto* raw = Wh_GetStringSetting(format.c_str(), i);
        bool empty = !raw || !raw[0];
        std::wstring entry = raw ? raw : L"";
        if (raw) Wh_FreeStringSetting(raw);
        if (empty) break;
        result.push_back(std::move(entry));
    }
    return result;
}
```

Grouped arrays go one level deeper per field, as in `group[%d].fieldA`. Pick one required field per item as the loop's stop signal. Windows 11 Taskbar Styler does this with `controlStyles[%d].target`.

If a mod needs a list the user edits freely, such as accounts or widgets, the YAML block is the wrong tool. Build a settings window, see `windhawk-ui-kit`.

### Values the mod writes itself

A mod can't write its own settings page. For state it must persist, use Windhawk's per-mod storage: `Wh_GetIntValue`, `Wh_SetIntValue`, `Wh_GetStringValue`, `Wh_SetStringValue`, `Wh_GetBinaryValue`, `Wh_SetBinaryValue`, `Wh_DeleteValue`. It works with portable Windhawk installs. Check the exact signatures in `windhawk_api.h` before first use, since this repo has not called them yet.

taskbar-widget-stack still writes to a private key under `HKCU\Software\WindhawkMods`. Treat that as a debt to migrate, and don't copy it into new mods.

## Threading

`Wh_ModInit` does not run on the target's UI thread in the normal case. If Windhawk injects into a process that is already running, every lifecycle callback runs on the Windhawk engine thread. Only a mod loaded before the process starts gets the main thread.

Things that silently fail or freeze from the wrong thread, all hit in this repo:

- `SetTimer(nullptr, ...)` and `RegisterHotKey(nullptr, ...)`. The thread has no message pump, so nothing is ever delivered. Snipaste session.
- `CreateWindowExW` with a taskbar window as parent. The new window belongs to a thread that never pumps, and Explorer froze on its next `SendMessage`. Stack Incident 1.
- `DestroyWindow` and `SetWindowSubclass`. Both have thread affinity.
- Any XAML call. XAML objects belong to the thread that created them.

Use a marshal helper to reach the owning thread. `RunFromWindowThread` in taskbar-widget-stack.wh.cpp is the reference copy. It installs a short-lived `WH_CALLWNDPROC` hook on the window's thread and sends a registered message. `WindhawkUtils::SetWindowSubclassFromAnyThread` does the same job for subclassing.

Some copies of the helper take a plain function pointer. Lambdas passed to those can't capture anything, and reading a local inside one is a compile error. See taskbar-widget-media-player/PLAN.md Incident 1.

For a target whose UI thread you can't reach by window, hook a function the UI thread calls all the time and do the work from there. In Qt apps that is `PeekMessageW`, since Qt never calls `GetMessageW`.

A mod that needs something that doesn't exist yet at init, such as `Shell_TrayWnd` after an Explorer restart, starts a worker thread that polls. It must not give up on the first miss.

## Event and hook lifetime

Windhawk unloads the mod's DLL on every recompile, disable and settings-driven reload. Anything in the host process that still holds a pointer into the DLL will crash the host when it fires.

- Store the `event_token` from every handler you register on an object the mod doesn't own. Revoke every token in `Wh_ModBeforeUninit` and on every path that drops the object.
- Never rely on a callback noticing an unload flag on its next run. `CompositionTarget::Rendering` stops firing when nothing redraws, so the callback never ran and never revoked itself. See windows-11-start-menu-button/PLAN.md, Crash containment.
- `RegisterClass` is never undone for you. Call `UnregisterClass` in `Wh_ModUninit`, or the class keeps a `lpfnWndProc` pointing into unmapped memory.
- `Wh_SetFunctionHook` can't be called after `Wh_ModBeforeUninit` returns. `Wh_RemoveFunctionHook` can't be called before `Wh_ModInit` returns. `Wh_ApplyHookOperations` is slow, so avoid it.

Handlers on short-lived elements the mod creates and discards are lower risk. The element dies with its handler.

## Re-entrancy

Three separate crashes came from calling back into a system that was already on the call stack.

- **A modal loop inside a XAML handler.** `TrackPopupMenu` pumps its own message loop. Calling it from `RightTapped`, even deferred through the dispatcher, crashed Explorer on a window that hosts a XAML island. The fix was to stop using it. Use a XAML `MenuFlyout`. See stack Incidents 10 and 20 to 23.
- **A generic hook that catches its own calls.** The native menu hook on `IVector::Append` recursed without bound on the first menu open. Guard generic hooks with a depth counter and a scope object. See stack Incident 59.
- **A teardown callback with side effects.** Two widget mods re-registered from inside `Destroy()`. The host calls `Destroy()` on every widget during every rebuild, so the two mods triggered each other until Explorer died. `Destroy()` frees things and nothing else. See taskbar-widget-system-usage/PLAN.md Incident 9.

Before adding a call inside any handler or callback, ask what that call can trigger and whether it leads back here.

## Exceptions and crash isolation

A C++ exception that escapes into a native callback kills the host. Wrap the whole body of every event handler, timer tick and hook in `try`/`catch`, log, and return.

`catch (...)` does not catch access violations. No file in this repo uses `__try`, so the "crash isolation" promised in the stack design only covers C++ exceptions. Nobody has yet compiled `__try`/`__except` under Windhawk's Clang. Test that before designing around it, and keep each guarded call in its own small function, because structured handling can't share a scope with objects that have destructors.

## Per-window state in a shell mod

Learned building explorer-extras, 0.1 to 0.7:

- One state object per window, touched only on that window's UI thread. One worker thread per window does the slow shell reads and returns results through the window's `DispatcherQueue`. A result carries a generation number or a cancel flag and is dropped when stale.
- A callback queued to the dispatcher must check that the window and the DLL are still alive (`g_unloading`, the state lookup for this thread, and that the object it captured is still in the state's list) before it touches anything.
- After the window is destroyed, never release its XAML objects. Abandon them.
- A pidl made from a path is rooted under This PC, one built from the namespace under the Desktop. They never compare equal with `ILIsEqual`, so compare paths for items on disk.
- A shell folder inside an archive has a path that looks like a normal one (`C:\x\a.zip\sub`), and `SHGetPathFromIDList` succeeds on it. To tell whether an item is inside an archive, walk the path upward until a part is a file, not a folder.
- The first column's name can be a library name (`Téléchargements`), not a path. Never find a parent column by comparing display names to a path.
- A verb or property sheet on an item inside an archive crashed Explorer. Offer Copy only there.
- A `$name` or `$description` in the settings block must not contain a colon followed by a space ("Off: all columns" broke the whole block, twice). Scan every description for `: ` before a handover.
- A mod of several thousand lines is kept in numbered parts and joined by a script into the one file Windhawk takes (`explorer-extras/dev/src/*.cpp`, `explorer-extras/dev/build.py`). Edit the parts, never the generated file, and check with `build.py --check`.
- Replacing a mod while an Explorer window has its panel can hang or crash that window (a callback of the old DLL), and the first start after a reload is slow. After a reload, test in a new window.
- When a drag, a menu or a dialog hangs the host, add a log line before each blocking call, so the next log shows the last step reached. Do not guess from a log that has nothing after the event.
- A shell target that did a move reports effect 0. Do not read it as a failure.
- A modal loop inside a XAML handler is the usual crash. Prefer a XAML flyout for menus.
- A call that returns success is not proof the user saw the result. `ShellExecuteEx` returned ok with `hInstApp=42` while no app opened, because the file was a cloud placeholder that the image app could not open. Log the result, then ask what the user saw before changing the call.

## Process shutdown

Two teardown paths behave differently.

- **Normal unload.** `Wh_ModUninit` runs, other threads are alive, nothing holds the loader lock.
- **Hard process exit.** Explorer restart, sign-out or reboot. Windows kills the other threads first and then runs global destructors under the loader lock. `Wh_ModUninit` is never called.

At hard exit a global destructor can block on a dead thread's lock, crash in `std::thread`'s destructor, or release a XAML object on the wrong thread.

Safe as plain globals: POD data, raw pointers and handles, `std::atomic`, `std::mutex`, heap-only containers, `winrt::event_token`, `winrt::weak_ref`.

Needs the fix: `std::thread`, any strong WinRT or COM reference such as a `Grid` or a `com_ptr`, containers of those, and any RAII type whose destructor calls `CoUninitialize`, `UnregisterClass`, `DestroyWindow` or `UnhookWindowsHookEx`.

```cpp
// 1. Suppress the automatic destructor.
[[clang::no_destroy]] std::optional<std::thread> g_worker;
[[clang::no_destroy]] Grid g_rootGrid{nullptr};

// 2. Release by hand, on the right thread, in Wh_ModUninit.
void Wh_ModUninit() {
    g_stop = true;
    SetEvent(g_wakeEvent);
    if (g_worker && g_worker->joinable()) g_worker->join();
    g_worker.reset();
    RunFromWindowThread(g_taskbarWnd, [] { g_rootGrid = nullptr; });
}
```

Use the bare attribute for nullable types and release with `= nullptr` or `.reset()`. Wrap everything else in `std::optional` and release with `.reset()`. Skipping step 2 turns a crash into a leak on every unload.

Repo state on 2026-11-12, by count of `no_destroy` in each file:

| Mod | Uses it | Note |
| --- | --- | --- |
| taskbar-widget-media-player | yes | |
| taskbar-widget-home-assistant | yes | |
| windows-11-start-menu-button | once | Audit its other globals |
| taskbar-widget-stack | no | Holds XAML globals in `g_ui`, highest risk |
| taskbar-widget-system-usage | no | |
| taskbar-widget-weather | no | |

Fix the mod you are touching and say so in its PLAN.md.

Test the real path. `taskkill /f` skips destructors and proves nothing. Use Exit Explorer from Ctrl+Shift+right-click on the taskbar, then a sign-out. Also disable and re-enable the mod several times and watch the handle and thread counts in Task Manager return to baseline.

## Hooking

- A hook with the wrong calling convention crashes. WinAPI hooks need `WINAPI`. Prefer `WindhawkUtils::SetFunctionHook`, which checks the signature at compile time.
- `WindhawkUtils::HookSymbols` hooks internal functions by symbol name and caches the lookup per binary version. The Windhawk Symbol Helper tool lists a module's symbols.
- A symbol hook that fails must leave the mod working without that feature, with one log line. Never return `FALSE` from `Wh_ModInit` for an optional feature.
- `WindhawkUtils::StringSetting` is a RAII wrapper that frees a string setting for you.

## Mod lifetime

`Wh_ModInit`, then hooks are applied, then `Wh_ModAfterInit`. While active, `Wh_ModSettingsChanged` fires on changes. Then `Wh_ModBeforeUninit`, hooks are removed, then `Wh_ModUninit`.

`Wh_ModSettingsChanged` has two forms: `void Wh_ModSettingsChanged(void)` and `BOOL Wh_ModSettingsChanged(BOOL* bReload)`. The second can return `FALSE` to unload or set `*bReload = TRUE` to force a reload.

Expose tuning values as live settings instead of constants. A settings change doesn't need a recompile, and each recompile is one more unload to survive.

## Mods that don't need injection

A mod that only shows a tray icon or runs a timer shouldn't live in explorer.exe. The wiki's "Mods as tools" page has a snippet that runs the mod in its own `windhawk.exe` process. Paste it at the end of the file, rename the callbacks to `WhTool_ModInit`, `WhTool_ModSettingsChanged` and `WhTool_ModUninit`, and set `@include windhawk.exe`.

Notes from reviewing a tray mod of that kind, the recycle-bin-tray pull request:

- The host window must be a hidden top-level window. A message-only window never receives `WM_SETTINGCHANGE` or `WM_THEMECHANGED`.
- The tool process may run at higher integrity than Explorer. Allow `TaskbarCreated` through with `ChangeWindowMessageFilterEx` or the icon won't come back after an Explorer restart.
- Debounce shell change notifications before an expensive query. The shell sends one event per item.
- With `NOTIFYICON_VERSION_4`, one click can arrive as two messages. Handle one.

## Crash triage

Ask for the Event Viewer entry, Windows Logs, Application, Event ID 1000. It gives the faulting module, the exception code and the offset.

| Signature | Usual meaning |
| --- | --- |
| `0xC0000005`, module "unknown", offset 0 | A call through a pointer into unloaded code. Look for a callback that outlived the DLL, or a nested loop under XAML. |
| `0x20474343`, module KERNELBASE.dll | A C++ exception thrown by Clang or GCC code that nothing caught. The bytes spell "GCC". Look for a handler without a `try`. |
| `0xC0000409` | A fail-fast, often a stack overflow guard or Control Flow Guard. Look for recursion first. In explorer-extras the same code and the same offset came back twice, from the shell's Properties verb on an item inside a browsed zip. An identical offset on a repeat means the first fix did not touch the cause: confirm from the log that the code path you restricted is the one that ran. |

The Start button PLAN.md calls `0x20474343` a Control Flow Guard failure. That reading is wrong, though the fix it led to was right.

**Stop after one wrong guess.** The right-click crash took two confident, reasoned, wrong fixes before logging located it in one round. If a fix based on reading the code doesn't change the symptom, add a `Wh_Log` line at each step of the path and get a log before writing anything else. See stack Incident 22.

A crash can also hide other evidence. "No log line appeared" proves nothing if the process died before the line was written.

## The build and test loop

Ari tests on his own Windows machine (ARM64 here, with Windhawk 2). You build, he enables the mod and uses it, you read what it wrote.

**Build.** `python explorer-extras/dev/build.py` joins the parts. `python explorer-extras/dev/build.py --debug <logfile>` writes `explorer-extras.debug.wh.cpp` instead (git-ignored): the same mod, and every `Wh_Log` line is also appended to that file, because Windhawk 2 keeps the log only in its live panel and you cannot read it. Use the debug build for every test, and the plain one for the commit.

**Check.** Run Windhawk's clang over the file and count the errors, so a failed check cannot look clean:

- The compiler is `C:/Program Files/Windhawk/Compiler/bin/clang++.exe`, not on PATH. Call the full path and count matches (`grep -c error`), or read the exit code.
- The flags are in `explorer-extras/dev/HANDOFF.md`, "Compile check without the Windhawk editor". Without `-include windhawk_api.h` and the `-D` flags you get dozens of false errors (`Wh_SetFunctionHook` undeclared).
- A script that patches a file from a Bash heredoc halves backslashes. Write the script with the Write tool, or build a backslash with `chr(92)`. Check the patched lines afterwards. Files here are CRLF: read, normalise to LF, edit, and write back with the original ending.

**Version.** Every build that goes to Ari bumps `@version` and the version in the start log line, test builds included. Ari tells builds apart by the version in the log, and a fix rebuilt under the same number looks like the old one.

**Deploy.** One command does the whole build and deploy, and you can run it yourself: `pwsh -NoProfile -File tools\deploy-explorer-extras.ps1 -Log <logfile>` joins the parts, makes the debug build, compiles both architectures and deploys. The deploy step opens one UAC prompt on Ari's screen, which he accepts. You cannot elevate by yourself, but you can start this and let him click. Say so before you run it. `-CompileOnly` needs no prompt. Two ways underneath, and the first is the default now:

1. `tools/windhawk-build/build.ps1` (Dan Rotaru's script, MIT, see its NOTICE.md) compiles with Windhawk's own clang, links both architectures, and deploys like the editor's Compile: it copies the DLL to `ProgramData\Windhawk\Engine\Mods\<arch>`, updates the mod's registry key and writes `LibraryFileName` last, which makes every injected process reload the mod. `-CompileOnly` checks the build and links, and needs no rights. A real deploy needs admin rights: Ari runs it once in an elevated PowerShell (`pwsh -NoProfile -File tools\windhawk-build\build.ps1 -Source explorer-extras\explorer-extras.debug.wh.cpp`). You cannot elevate. Do not install its scheduled task unless Ari asks. The Windhawk editor can stay closed.
2. Copy the generated file into the editor (`C:/ProgramData/Windhawk/EditorWorkspaces/1/mod.wh.cpp`). Windhawk 2 made the editor a separate window and the folder write-protected: Ari grants write access once with `icacls`. Compare with `cmp` and `grep @version`.

A mod reload does not rebuild what Explorer already built. To test code that runs when a window or its command bar is created, Ari must open a **new** Explorer window after the deploy. A log from a window that was open before the reload proves nothing about that code.

**Smoke test.** After a deploy run `pwsh -NoProfile -File tools\smoke-test-explorer-extras.ps1 -Log <logfile>`: it opens a window of its own, checks what the mod must place without a click (log lines, the toolbar toggle through UI Automation), closes it, and prints PASS or FAIL for six checks, about ten lines. It is a committed script, so it costs one command, not a rewrite. It does not replace Ari's checks of what he sees.

**Read.** After each test read the log file the debug build writes, then check `%LOCALAPPDATA%\CrashDumps` for a dump newer than the test. An Explorer crash on a disable shows in the log as a clean `explorer-extras end` followed by nothing: the crash came after the unload. See "Reading a crash dump".

Rules for the loop:

- Mark every fix you couldn't run as "unverified" in the notes, and remove the mark only when a test or a log line confirms it. A log with none of the lines the fix would write means the fix did not run (the test used another folder, or an old window), not that it passed.
- List the exact steps and the log lines to expect for the next test.
- Expect link errors on first build of new code. Missing `-l` flags are common, such as `-lcomctl32` for subclassing and `-ladvapi32` for the registry.
- C++/WinRT methods with deduced return types need the full header, for example `winrt/Windows.Foundation.Collections.h` for `IVector::Append`.
- `WindhawkUtils`' subclass procedure takes five parameters. It has no `dwRefData`.
- `UIElementCollection` has no `Remove(value)`. Use `IndexOf` then `RemoveAt`.
- Put `#undef GetCurrentTime` after `<windows.h>` and before any WinUI 3 header.
- `Wh_Log` is a macro in `windhawk_api.h`; to wrap it, call it by name inside the wrapper and `#undef` it before redefining.
- Remove diagnostic logging once the bug is confirmed fixed.

For a mod that crash-loops a process which restarts itself, the wiki has a snippet that makes the mod refuse to load at process startup. Use it while developing.

Debugging with a debugger: install CodeLLDB in the Windhawk editor, add `--optimize=0 --debug` to `@compilerOptions`, and attach by process id.

## Reading a crash dump

Windows keeps a minidump of every Explorer crash in `%LOCALAPPDATA%\CrashDumps`. There is no debugger here, but two scripts in `tools/crash-analysis/` are enough for the commonest mod crash:

1. `python -I tools/crash-analysis/dump.py <dump>` prints the exception, the faulting module and offset, the address a read or write failed at (`AVADDR`), and whether the mod's DLL was still loaded.
2. If the fault is in `Microsoft.UI.Xaml.dll` or another host DLL, and `AVADDR` lies where the mod's DLL used to be (an address in no loaded module), the host called into code that was unmapped. `python -I tools/crash-analysis/rd.py <the mod's DLL from Engine\Mods\<arch>> <low 16 bits of AVADDR>` names the C++ type there. Use the DLL of the **same build** as the crash: the offset moves with every change.

What the type tells you: `winrt::impl::delegate<...Handler, lambda in Function>` with slot 2 is `Release` of an event handler the mod gave to a XAML element. XAML freed it late, after the mod was gone. The function name is the place that registered it. The same low 16 bits on every dump of one build means one object; different bits between scenarios means more than one.

Same fault offset in the host DLL on many crashes does not mean the same bug in your code: it is the host reading a dead object. Decode each dump.

## Unloading cleanly (shell mods with XAML)

Windhawk calls `Wh_ModBeforeUninit`, then `Wh_ModUninit`, then `FreeLibrary` at once. Nothing of the mod may run, or be released, after that. What went wrong in Explorer Extras, in the order it was found:

- **Threads.** Every thread goes through one registry (`StartModThread`) and is joined at unload, never detached. Long jobs read a stop flag or a stop event, and `CancelSynchronousIo` frees one stuck on a network folder. A wait that gives up after a timeout leaves a thread in unmapped code. `fire_and_forget` coroutines count themselves, and the unload pumps messages until the count is zero.
- **Handlers.** A lambda given to an element is a heap object whose methods live in the mod. XAML releases it late, after `Close()`, on later turns of the queue. Keep every handler as a revoker (`winrt::auto_revoke`, `AddHandler` with the boxed handler kept for `RemoveHandler`), and take them all off first in each window's cleanup. A script that wraps every `x.Event([...])` finds them. Check that it also matches events not on your list, and `AddHandler`.
- **Elements you do not own** (Explorer's toolbar, menus) keep your handlers after you leave. Revoke those by token, and hold such elements by `weak_ref`.
- **Thread-locals** with a destructor run it at thread exit, after the mod is gone: `[[clang::no_destroy]]` and reset them in the cleanup.
- **Closed windows.** Subclass the top-level window and release everything in `WM_DESTROY`, while its children still exist. Polling `IsWindow` after the fact can only leak.
- **Draining.** After the cleanup, run the window's queue and message loop for a short, bounded time (`DrainUi`). Bound the inner `PeekMessage` loop by time too, or a thread that keeps posting messages never leaves it.
- **Classes and hooks.** Register window classes with the mod's own module (`GetModuleHandleExW` from an address in the mod), not `GetModuleHandleW(nullptr)`. Unregister after the windows are gone and check the result.
- **Per-window work belongs to events, not a timer in every window.** Where a timer stays, slow it for idle and background windows (`TickIntervalMs`).
- **Never inject keys or attach input** (`SendInput`, `AttachThreadInput`, a low-level keyboard hook): take the key in `TranslateAcceleratorW` and call the thing it stands for (Explorer's own control, through its automation peer: the Details toggle, the tab strip's add button). A verb of a context menu is not a safe substitute for a key: `opennewtab` opened a window.
- **The mod's own modal loops.** A call that shows a dialog or runs a drag (a menu command, paste, Delete and its confirmation, Properties, a drop, a rename in place, `SHDoDragDrop`) can be on the thread's stack when the unload comes. A per-thread counter (`ModalScope`) around each such call, and a cleanup that waits until it is zero, keep the state alive under those frames. The wait shows as "uninitializing" in Windhawk until the dialog is answered; the shell's confirmation does not pump the queue, so the cleanup only runs when it ends. The timer that does the waiting must be held by the mod: a timer nobody holds is dropped and never ticks.
- **Stale settings after a rename.** Renaming or regrouping settings leaves the old values in the registry and the new keys with no value, which `Wh_GetIntSetting` reads as 0. A user may need Windhawk's reset to defaults once, and a deploy script must write the defaults of the keys that are missing (the vendored `build.ps1` does).
- **Catalog validation.** The catalog's check flags a macro or symbol named like Windhawk's (`Wh_...`) and a setting text with a colon and a space. Name helpers differently (`LogVerbose`, not `Wh_LogV`).
- **Raw pointers to rows.** A menu, a drag or a rename that keeps a column pointer and row numbers must be closed when that column's listing changes or the column goes.

## Test checklist before calling a mod done

- Settings tab shows every control, and a change applies live.
- Disable and re-enable five times with no crash and no leftover UI.
- Exit Explorer and restart it. The mod comes back by itself.
- Sign out and back in. No hang.
- Light and dark theme, switched while running.
- 100% and 150% display scaling.
- Second monitor attached, if the mod draws on the taskbar.
- Another mod from this repo enabled at the same time.

## PLAN.md conventions

Every mod folder has a PLAN.md. It is the project's memory, and the only record future sessions get.

- **Context and decisions** at the top, with dates.
- **Confirmed facts** about the target, each with how it was confirmed.
- **Rejected approaches**, with the reason, so nobody retries them.
- **Incidents**, numbered, each with Symptom, Root cause, Fix and Next retest.
- **Open issues** that are known and not yet fixed.

Write "Root cause (theory)" when it is a theory. Several logged root causes were later proved wrong, and the label is what tells a reader which ones to trust. Keep the README's tested or untested note in step with PLAN.md.

## Shared code is copied by hand

Windhawk mods are single files. This repo therefore carries copies of the same code. When you change one, change all, and say which files you touched.

| Code | Reference copy | Other copies |
| --- | --- | --- |
| `RunFromWindowThread` | taskbar-widget-stack | all five other mods |
| `WidgetStackWidgetAbiV1` and `WidgetStackHostAbiV1` | taskbar-widget-stack | system-usage, media-player, weather, home-assistant |
| Taskbar XAML access, `TryGetTaskbarElementAbi` | taskbar-widget-stack | system-usage, weather |

## Publishing to windhawk-mods

Pull requests to ramensoftware/windhawk-mods go through an automated review first. Comment `/ai-review`, fix the findings, repeat, then comment `/ready-for-reviewer`. A pull request that skips the first step is sent back. Explain in a sentence any finding you leave unaddressed.

## Compiler and API facts

- Clang with mingw-w64, C++23. The effective flags are in `compile_flags.txt` in the editor.
- The API headers are `windhawk_api.h` and `windhawk_utils.h` under `C:\Program Files\Windhawk\Compiler\include`. Read them before using a function this repo hasn't used.
- Networking is limited to `Wh_GetUrlContent`, a synchronous HTTP fetch. For WebSockets use WinRT's `MessageWebSocket`.
- WinUI headers: `winrt/Microsoft.UI.Xaml.Markup.h` for `XamlReader::Load` (a `PathIcon` from path data), `FontWeights` is in `Microsoft::UI::Text`, not `Windows::UI::Text`. A `ToolTip` control works as a free standing surface in an overlay canvas.
- A `DispatcherQueueTimer` nobody holds is dropped before it ticks: keep it in a member (the hover delay of a submenu never fired until it was stored). A timer in a lambda that stops itself, as with the one in the hover handler, still needs a holder until the first tick.
- A popup with light dismiss blocks the pointer for everything under it. A hover menu needs a transient one.
- A logging build first, then the design: for touch, five builds of logs found what no reading of docs did (the finger replaced at the first move, error 87 on `GetPointerInfo`, no `WM_POINTER` on any window). When an input path is unknown, log the device type, flags, ids and positions at each hook point, ask for one short test, and read the result before choosing an approach.
- A touch or modal path that can hang needs an Escape (the shell's drag loop cancels on it) and a guard that gives scrolling and captures back at every exit.
- A `co_await` in a coroutine hands the UI thread back; a call that must happen inside an event (`StartDragAsync`) needs what it uses prepared before the event, not awaited in it.
- Patch scripts: write them with the Write tool, not a shell heredoc, and run them with Python; a heredoc turns `\n` and private-use glyphs into real characters. Search for a line with a private-use glyph by pattern, not by its text.
- PowerShell: an alias beats a function of the same name, so a function called `Dir` runs `Get-ChildItem`.
- Windhawk refuses to inject into a fixed list of critical processes, and into games by default. `svchost.exe`, `werfault.exe` and `winlogon.exe` can only be targeted by exact name.

## Sources

- Windhawk wiki: https://github.com/ramensoftware/windhawk/wiki
- This repo's PLAN.md files, read on 2026-11-12
- Windows 11 Taskbar Styler source, for the array-setting convention

## Lessons from Explorer Extras 0.36 to 0.54

- **A native crash leaves nothing in the log.** Log before a risky call, and keep calls into other components out of event handlers that are about to close something (a menu click, a flyout). Defer them to a timer tick on the window's own thread.
- **Scripted edits on Windows.** A Python script that rewrites a file in text mode writes CRLF, and a generator that looks for `\n` markers then fails. Normalise line endings after scripted edits, write the script with the file-writing tool rather than a shell heredoc (backslashes and quotes get mangled), and check each edit with an assertion that its anchor occurs once. A script that stops half way leaves earlier files changed: make each step safe to run again.
- **One-shot timers and threads at unload.** Hold every one-shot timer in a list that the teardown stops, and count background threads so `Wh_ModBeforeUninit` can wait for them. A callback that fires after the DLL is unloaded crashes the host.
- **Hooks.** A low-level hook belongs to the thread that installed it and runs in its message loop. Install it from each window's UI thread, check the foreground window inside it, keep it fast, and remove it at teardown.
- **Settings table in two languages.** `settings.schema.json` holds the English texts and an `fr` block; `build.py` writes both into the mod, and a `Str()` lookup picks the French table when Windows' display language is French. Windhawk's own settings page stays English.
- **Check Windows first.** A switch that edits a Windows setting persistently (the status bar) is a reason to look in Folder Options before building it.
- **Release hygiene.** `python explorer-extras/dev/build.py --check` before every commit, Windhawk's clang for syntax, a link check with the real library flags (`-lole32 -loleaut32 -lruntimeobject -lshell32 -lshlwapi -luuid -ladvapi32 -lgdi32 -lcomctl32 -lpropsys`), and a copy to the editor workspace.

## Lessons from the labels build (explorer-extras 0.69 to 0.75)

- **Self-tests inside the mod.** With no unit test runner, a pure part of the mod (a text format, a placement rule, a merge of tags) is tested by a function in a part that only exists in the debug build (`#ifdef EXTRAS_VERBOSE`), called when the mod loads, that writes one line, `LABELS ... SELFTEST pass` or `FAIL <what>`. `tools/smoke-test-explorer-extras.ps1` reads the last such line. Write the test first and see it fail: a test that names a function that does not exist fails to compile (the deploy script stops), a test of new behaviour fails at the next load with the `FAIL` line. A test that waits (a reader thread) runs on a mod thread, not in `Wh_ModInit`.
- **A deploy can silently not happen.** The elevated step may be cancelled, and the output then lacks the `Deployed local@... <version>` line. Check for that line, and that the log says `explorer-extras v<version> start` after it, before reading a test result. A result from the old build looks exactly like "nothing changed".
- **Windows open before the mod loaded** have no `WindowState`. A hook that finds its window by `StateForThisThread(false)` does nothing there: look the window up by class on the thread.
- **Write files for Explorer with PowerShell, not Bash.** A control file written from the Bash tool into `%LOCALAPPDATA%` was never seen by Explorer; one written by the PowerShell tool was.
- **Pasting Python into a heredoc mangles backslashes and quotes.** Write the script with the Write tool and run it.
- **Keep one probe per question** (`probes/`), compile and deploy it with `tools/windhawk-build/build.ps1 -Source ...`, and disable it in Windhawk when the answer is in.
- **Every handler needs an owner thread that will revoke it.** `Keep()` stores a revoker in a thread-local list, and the cleanup of a window runs it only for threads with a `WindowState`. A hook that puts a handler on an element of a window the mod never saw created (open before the mod loaded, or the desktop) must still be revoked at unload: `Keep()` records the thread and its `DispatcherQueue`, and `RevokeKeptThreads()` runs `RevokeAllHandlers()` on each, on its own thread, and waits a bounded time.
- **A cache that is only filled once is wrong.** Say when it is read again (navigation, an age), when a read is cancelled (the folder is left), and what the old data does meanwhile (it stays in use). Test those with a file changed behind the cache's back.
- **Ambiguity is a property of all the items, not of the tagged ones.** Count every name, so a later change cannot make two items look like one.
- **Data is not markup.** Names from files go into XAML only through properties set in code.
- **A review by a fresh agent finds what the author cannot see.** Run it before the first merge of a big feature: this one found a crash path in the first minute of reading.

## Lessons from the toolbar build (explorer-extras 0.77)

- **Tweaking Explorer's own XAML properties is a pattern.** Find the element from `xamlRoot.Content()` by `Name()` on the ticks of a new window (the tree is not complete at creation), keep a weak reference and the original value for each property, set yours, and restore the originals on setting-off and in the unload path of that thread (register the thread with `RegisterKeepThread`, restore after `RevokeAllHandlers`). Explorer rebuilds some elements later (the command bar's buttons change with the selection): re-apply on `LayoutUpdated` and only write when the value differs, and treat a value you did not set as Explorer's new original.
- **A setting equal to Explorer's own value looks like a bug.** A gap setting at 8 did nothing visible because 8 was Explorer's gap; a log line with the value read and the count of elements changed settled it in one run. Log the value and the effect on every settings change in new areas.
- **Setting values to prove "it works" in one round.** Ask Ari for an extreme value (0 or the maximum) when verifying a setting live.
- **A draggable handle in XAML.** A transparent Border added to the grid, with `ProtectedCursor` through `IUIElementProtected`, `CapturePointer` on press and the pointer position relative to the grid, works for mouse, touch and pen. Persist the dragged value with `Wh_SetIntValue`, together with the setting value it was dragged against, so a change of the setting wins over an old drag.
- **Know when to stop.** A compact height meant moving native windows in the frame (no symbols for ExplorerFrame.dll) and was dropped at Ari's word. Estimate the cost of the host side before building on the XAML side.
