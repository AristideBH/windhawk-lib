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
| `0xC0000409` | A fail-fast, often a stack overflow guard or Control Flow Guard. Look for recursion first. |

The Start button PLAN.md calls `0x20474343` a Control Flow Guard failure. That reading is wrong, though the fix it led to was right.

**Stop after one wrong guess.** The right-click crash took two confident, reasoned, wrong fixes before logging located it in one round. If a fix based on reading the code doesn't change the symptom, add a `Wh_Log` line at each step of the path and get a log before writing anything else. See stack Incident 22.

A crash can also hide other evidence. "No log line appeared" proves nothing if the process died before the line was written.

## The build and test loop

Ari compiles and tests on his own Windows machine. You usually can't run the mod.

1. Hand over the full file, never a fragment to merge by hand.
2. He replaces the editor source, compiles with Ctrl+B and checks the compiler output.
3. He enables the mod, reproduces, and pastes back the full log.

Rules for that loop:

- Mark every fix you couldn't run as "unverified live" in PLAN.md, and remove the mark only when a test confirms it.
- List the exact steps and the expected log lines for the next retest.
- Expect link errors on first build of new code. Missing `-l` flags are common, such as `-lcomctl32` for subclassing and `-ladvapi32` for the registry.
- C++/WinRT methods with deduced return types need the full header, for example `winrt/Windows.Foundation.Collections.h` for `IVector::Append`. The forward-declaring headers compile until the first call.
- `WindhawkUtils`' subclass procedure takes five parameters. It has no `dwRefData`.
- `UIElementCollection` has no `Remove(value)`. Use `IndexOf` then `RemoveAt`.
- Put `#undef GetCurrentTime` after `<windows.h>` and before any WinUI 3 header. `winbase.h` defines it as a macro and the WinUI animation headers declare a method with that name.
- Remove diagnostic logging once the bug is confirmed fixed, and say so in PLAN.md.

For a mod that crash-loops a process which restarts itself, the wiki has a snippet that makes the mod refuse to load at process startup. Use it while developing.

Debugging with a debugger: install CodeLLDB in the Windhawk editor, add `--optimize=0 --debug` to `@compilerOptions`, and attach by process id. Without a debugger: turn on "Detailed debug logs" in the mod's Advanced tab.

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
- Windhawk refuses to inject into a fixed list of critical processes, and into games by default. `svchost.exe`, `werfault.exe` and `winlogon.exe` can only be targeted by exact name.

## Sources

- Windhawk wiki: https://github.com/ramensoftware/windhawk/wiki
- This repo's PLAN.md files, read on 2026-11-12
- Windows 11 Taskbar Styler source, for the array-setting convention
