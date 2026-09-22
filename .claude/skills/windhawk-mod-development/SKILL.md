---
name: windhawk-mod-development
description: Use when writing or reviewing Windhawk mod code (.wh.cpp files) in this repo - settings schema, array-setting conventions, process-shutdown safety, hooking, debugging, and injection-target constraints
---

# Windhawk Mod Development

Reference for developing Windhawk mods in this repo (`taskbar-widget-*`, `windows-11-*`). Compiled
against the Windhawk wiki (https://github.com/ramensoftware/windhawk/wiki), the local SDK header
(`C:\Program Files\Windhawk\Compiler\include\windhawk_utils.h` / `windhawk_api.h`), and real
published mod source (Windows 11 Taskbar Styler) verified directly rather than assumed.

## Settings schema

### Array-of-strings and array-of-groups

```yaml
- flatArraySetting: [""]
  $name: Flat array
- groupedArraySetting:
  - - fieldA: ""
      $name: Field A
    - fieldB: [""]
      $name: Field B (itself an array)
  $name: Grouped array
```

**The default for any array setting (flat or grouped) must be `[""]` (one empty-string
placeholder), never `[]`.** An empty-array default fails Windhawk's settings schema validation
("Failed to parse settings: instance[N].SectionName is not any of [subschema...]") — hit and fixed
in this repo's own `taskbar-widget-weather` `styleConstants`/`styleAliases` settings.

### Reading array settings in C++

`Wh_GetStringSetting`/`Wh_GetIntSetting` are variadic/printf-style (confirmed in
`windhawk_api.h`) and **officially documented to return an empty string on a missing value, never
null** ("If the value doesn't exist or in case of an error, an empty string is returned"). Array
iteration therefore terminates on the first **empty string**, not a null pointer — verified against
Taskbar Styler's own real source (`Wh_GetStringSetting(L"styleConstants[%d]", i)`, loop breaks when
`!*result`):

```cpp
std::vector<std::wstring> GetStringArraySetting(const std::wstring& settingName) {
    std::vector<std::wstring> result;
    std::wstring format = settingName + L"[%d]";
    for (int i = 0;; i++) {
        auto* raw = Wh_GetStringSetting(format.c_str(), i);
        bool empty = !raw || !raw[0];
        std::wstring entry = raw ? raw : L"";
        if (raw) {
            Wh_FreeStringSetting(raw);
        }
        if (empty) {
            break;
        }
        result.push_back(std::move(entry));
    }
    return result;
}
```

Grouped arrays go one level deeper per field: `groupName[%d].fieldA`, `groupName[%d].fieldB[%d]`.
Pick ONE required field per group item as the outer-loop termination sentinel (mirrors
`controlStyles[%d].target`'s role in Taskbar Styler's real source) — an empty value there means "no
more items," independent of whether other fields in that item are populated.

**Gotcha this repo already has a latent instance of** (in `taskbar-widget-weather.wh.cpp`'s own
`GetStringSetting` helper, not yet fixed): a helper like `value ? value : fallback` is dead code for
the fallback branch, since `Wh_GetStringSetting` never returns null — an unset string setting
returns `""`, not your intended fallback string, unless the setting's own YAML default already
covers it (which it normally does, since Windhawk pre-populates defaults at install time). Prefer
checking for an empty string explicitly if you need a non-empty fallback to actually take effect:
`std::wstring result = (value && value[0]) ? value : fallback;`.

### Windhawk 2.0-only annotations (need a `#!` marker for 1.7.3 compatibility)

`$format` (colorRgb/colorArgb/filePath/folderPath/fontFamily/hotkey/slider), `$float`, `$min`/`$max`,
`$dynamicSelect`, `$showIf`/`$hideIf` are Windhawk 2.0-only. Windhawk 1.7.3 (current stable) rejects
any settings block containing an unmarked one ("is not an allowed property" → mod won't install). To
stay compatible with both, prefix each 2.0-only annotation with `#!` (a comment in 1.7.3, parsed
specially by 2.0):

```yaml
- Opacity: "0.85"
  $name: Opacity
  #! $float: true
  #! $min: 0
  #! $max: 1
```

Rules: the marker is exactly `#!` + one space (`#!$float` with no space fails); once any marker
appears in a settings block, **every** new-annotation in that block must be marked — no mixing
marked and unmarked 2.0-only annotations in one block. `$showIf`/`$hideIf` are edit-time UI hints
only — a hidden setting still keeps its stored value and is still delivered to the mod, which must
handle it regardless of visibility.

## Process-shutdown safety (read before adding any global WinRT/COM object or thread)

**Two teardown paths differ fundamentally**, and this is the single most consequential Windhawk-
specific correctness concern for any mod with background threads or persistent WinRT state:

- **Normal mod unload** (disable/update/reload): `Wh_ModUninit` runs normally, other threads alive,
  no loader lock, subsystems intact.
- **Hard process exit** (Explorer restart, sign-out, reboot): Windows kills every other thread
  first, then delivers `DLL_PROCESS_DETACH` **without ever calling `Wh_ModUninit`**. Global/static
  C++ destructors still run — but under the loader lock, with other threads already dead (their
  locks/events orphaned) and subsystems (COM apartments, XAML core, DirectX) possibly already torn
  down, executing on the exiting thread rather than whichever thread actually owned the object.

**Three failure modes in that environment:** blocking (releasing an out-of-process COM proxy,
waiting on an event nobody will ever signal → hangs the whole sign-out/restart), crashing
(`std::thread`'s destructor calls `std::terminate()` if still joinable; calling into an already-torn-
down subsystem crashes), and thread-affinity violations (releasing XAML/STA-COM objects from the
wrong thread after the owning thread is already dead).

**Safe without any fix:** POD data, raw pointers/handles/tokens (no destructor),
`std::atomic<T>`, `std::mutex`/`SRWLOCK`, heap-only containers (`std::wstring`, `std::vector<int>`,
`std::map`), `std::unique_ptr<T>` where `~T()` only frees heap memory, `winrt::event_token`,
`winrt::weak_ref<T>`, raw `HANDLE`s from `CreateThread` (as long as `Wh_ModUninit` still signals,
waits, and `CloseHandle`s them on the normal-unload path).

**Needs the fix:** `std::thread` (unjoinable at hard exit), strong WinRT/COM references (a XAML
`Grid`/`Border`/`Flyout`, `MessageWebSocket`, `DataWriter`, `winrt::com_ptr<T>`), containers of the
above, any RAII type whose destructor calls `CoUninitialize`/`UnregisterClass`/`DestroyWindow`/
`UnhookWindowsHookEx`.

**Fix — two steps:**

```cpp
// 1. Suppress the automatic (hard-exit) destructor.
[[clang::no_destroy]] std::optional<std::thread> g_workerThread;
[[clang::no_destroy]] Grid g_someXamlGlobal{nullptr};
[[clang::no_destroy]] std::optional<std::vector<std::unique_ptr<SomeStruct>>> g_instances;

// 2. Release explicitly, fully, on the correct thread, in Wh_ModUninit
//    (the fix removes AUTOMATIC cleanup - skipping this step just leaks
//    the resource on every normal unload instead, which is worse).
void Wh_ModUninit() {
    g_stopWorker = true;
    SetEvent(g_wakeEvent);
    if (g_workerThread->joinable()) {
        g_workerThread->join();
    }
    g_workerThread.reset();

    // XAML objects: release on the UI thread (e.g. via a RunFromWindowThread-
    // style marshal), not directly from Wh_ModUninit if it runs elsewhere.
    g_someXamlGlobal = nullptr;
    g_instances.reset();
}
```

Choosing bare attribute vs. `std::optional<T>` wrapper: bare `[[clang::no_destroy]]` for
nullable/handle-like types (WinRT projected types, `com_ptr`, `unique_ptr`, `shared_ptr`, raw
handles) — release with `= nullptr`/`.reset()`. `std::optional<T>` for everything else (containers,
`std::thread`) — release with `.reset()`, since e.g. `vector::clear()` keeps the buffer allocated
(a partial leak) and a `std::thread` can't be "emptied" by assignment at all (move-assigning over a
joinable thread calls `std::terminate()`).

**Window classes registered with `RegisterClass` are never auto-unregistered on DLL unload** — their
`lpfnWndProc` then points at unmapped memory. Always `UnregisterClass` in `Wh_ModUninit`.

**Test the real shutdown path**, not `taskkill /f` (which skips destructors entirely and won't
reproduce the bug): graceful "Exit Explorer" (Ctrl+Shift+right-click taskbar), sign-out, or reboot.
Watch for a hanging sign-out, a crash dialog, or the shell failing to restart. Separately, test plain
mod unload (disable/re-enable repeatedly) and confirm handle/thread counts return to baseline in
Task Manager.

**This repo's current state**: `taskbar-widget-media-player` and `windows-11-start-menu-button`
already use `[[clang::no_destroy]]`; `taskbar-widget-weather` does not (a latent gap, out of scope
to retrofit incidentally — flag it explicitly if ever touching that file's global WinRT state).

## Hooking

- **Calling-convention mismatches are the most common crash cause.** WinAPI hook functions almost
  always need `WINAPI`: `HWND WINAPI FindWindowW_Hook(LPCWSTR lpClassName, LPCWSTR lpWindowName)`.
  Prefer `WindhawkUtils::SetFunctionHook` (strongly typed, from `windhawk_utils.h`) over raw
  `Wh_SetFunctionHook` — it catches signature/calling-convention mismatches at compile time instead
  of a runtime crash.
- `Wh_SetFunctionHook` can't be called after `Wh_ModBeforeUninit` returns. `Wh_RemoveFunctionHook`
  can't be called before `Wh_ModInit` returns or after `Wh_ModBeforeUninit` returns.
  `Wh_ApplyHookOperations` is "very slow, avoid using it if possible."
- `WindhawkUtils::HookSymbols` (symbol-name-based hooking with caching across calls, since symbol
  enumeration is slow) is the standard way to hook internal/undocumented functions found by symbol
  name rather than export name. The Windhawk Symbol Helper tool can list a module's available
  symbols.
- `WindhawkUtils::SetWindowSubclassFromAnyThread` exists because `SetWindowSubclass` itself has
  thread affinity (can only be called from the target window's own thread) — internally it's the
  same short-lived-`WH_CALLWNDPROC`-hook-plus-`SendMessage` pattern this repo's own
  `RunFromWindowThread` helpers already use for cross-thread UI marshaling.

## Mod lifetime and threading

Callback order: `Wh_ModInit` → (hooks applied) → `Wh_ModAfterInit` → [active: engine thread waits,
`Wh_ModSettingsChanged` fires on settings changes] → `Wh_ModBeforeUninit` → (hooks removed) →
`Wh_ModUninit`.

`Wh_ModSettingsChanged` has two accepted signatures: `void Wh_ModSettingsChanged(void)`, or
`BOOL Wh_ModSettingsChanged(BOOL* bReload)` (return `FALSE` to unload the mod instead, or set
`*bReload = TRUE` to force a full reload).

**Thread identity depends on how the mod was loaded**: if the mod is pre-loaded before the target
process starts, `Wh_ModInit`/`Wh_ModAfterInit` run on the target's own main thread. If the mod is
injected into an already-running process, **every** lifecycle event (including `Wh_ModInit`) runs on
the Windhawk Engine thread instead — never assume `Wh_ModInit` is running on the target's UI thread.

## Injection targets

Windhawk hard-excludes a fixed list of critical system processes (`csrss.exe`, `services.exe`,
`smss.exe`, `lsass.exe`, `dwm.exe`, `searchindexer.exe`, `spoolsv.exe`, `consent.exe`,
`logonui.exe`, and others). A separate "mods-excluded" list (`svchost.exe`, `werfault.exe`,
`winlogon.exe`) still allows exact-name targeting but rejects wildcard patterns. Games are broadly
excluded by default (anti-cheat intolerance of code injection) across ~30+ publisher/platform
folders. None of this affects `explorer.exe`-targeted taskbar mods directly, but worth knowing if a
future mod ever needs a different `@include` target.

## Debugging

1. In editing mode, install the CodeLLDB extension (Ctrl+P → `ext install vadimcn.vscode-lldb`).
2. Add `--optimize=0 --debug` to `@compilerOptions` — required for the debug build to actually be
   debuggable.
3. Configure `launch.json` with `"request": "attach"` (remove `args`/`cwd`, use `"pid":
   "${command:pickMyProcess}"` to pick a running process) or `"request": "launch"` (starts a fresh
   process under the debugger).
4. Ctrl+Shift+D opens Run and Debug; start from there.

For non-debugger troubleshooting: disable the mod → Advanced tab → "Detailed debug logs" →
re-enable → reproduce → inspect output. Missing debug symbols show as `HTTP_STATUS_NOT_FOUND` in the
log (can take a day or two to become available after a Windows update). Antivirus is a common cause
of injection failures — worth disabling to test. Persistent conflicts with another program can be
added to Settings → Advanced settings → More advanced settings → Process exclusion list.

## No documented WebSocket/networking API

Windhawk's own API surface has no WebSocket support — the only documented network-adjacent function
is `Wh_GetUrlContent`/`Wh_FreeUrlContent` (a simple synchronous HTTP fetch). Mods needing WebSockets
(e.g. `taskbar-widget-home-assistant`) use WinRT's own `winrt::Windows::Networking::Sockets::
MessageWebSocket` directly — a real, working WinRT API, just not something Windhawk's own SDK
wraps or documents.

## Compiler

Clang 20 (mingw-w64), C++23 mode. Full effective flags are visible in the editor via Ctrl+P →
`compile_flags.txt`. `@compilerOptions` passes raw flags straight to the compiler (e.g.
`-lole32 -loleaut32 -lruntimeobject -luser32`, this repo's standard link set for WinRT-heavy mods).

## Sources

- https://github.com/ramensoftware/windhawk/wiki (Creating a new mod, Development tips, Global
  objects and process shutdown, Debugging the mods, Mod lifetime, Injection targets and critical
  system processes, Troubleshooting)
- `C:\Program Files\Windhawk\Compiler\include\windhawk_utils.h` / `windhawk_api.h` (read directly,
  not just wiki-summarized)
- Windows 11 Taskbar Styler's published source (`raw.githubusercontent.com/ramensoftware/
  windhawk-mods/main/mods/windows-11-taskbar-styler.wh.cpp`) — used to verify the array-setting
  empty-string-termination convention against real, working code rather than assumption.
