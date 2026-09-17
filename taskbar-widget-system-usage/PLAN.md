# Taskbar System Usage - PLAN

## Context

This mod is an extraction (2026-09-17) of `SystemUsageWidget`, a CPU/RAM/GPU
usage-bars widget originally built and confirmed working *inside*
[`taskbar-widget-stack`](../taskbar-widget-stack/), against that mod's
in-process `IWidget` SDK (see `taskbar-widget-stack/PLAN.md`'s Incidents
30-32 for the widget's own development history: bar sizing/spacing
tuning, the CPU-row-clipping bug and its fix).

**Why extract it**: the user wants this to become a genuinely separate,
independently-installable mod, eventually integrated with
`taskbar-widget-stack` through a real API - not by compiling the widget's
code directly into that mod's own `.wh.cpp`, which is how the in-process
`IWidget` SDK necessarily works today (single DLL, single address space).

## What moved, what stayed

**Moved out of `taskbar-widget-stack.wh.cpp` entirely** (not duplicated):
the `SystemUsageWidget` class, its registration in
`InitPlaceholderWidgets`, the PDH includes/`-lpdh` compiler option. Not a
copy - `taskbar-widget-stack` no longer has any system-usage content;
`PaneHeight()` there was reverted from 76 back to 32 (the bump existed
only to fit this widget's three bar rows).

**What this new mod is**: a full standalone Windhawk mod, not just the
widget's rendering code. It duplicates the "Taskbar XAML Access" section
(`TryGetTaskbarElementAbi`, `GetTaskbarXamlRoot`, `FindChildByName`,
`FindTaskbarRootGrid`, `HookTaskbarDllSymbols`) and the
`RunFromWindowThread` marshaling utility from `taskbar-widget-stack.wh.cpp`
(which itself ported the XAML-access part, with attribution, from
`taskbar-ai-quota.wh.cpp`) - each Windhawk mod is its own DLL with its own
address space, so there's no way to share that code at build time the way
two files in the same mod can; every taskbar-XAML-injecting mod in this
repo carries its own copy. The injection/removal lifecycle
(`InjectSystemUsageGrid`/`RemoveSystemUsageGrid`/`RetryInjectThreadProc`/
`StartRetryInject`/`TrayUI_StartTaskbar_Hook`) is a simplified version of
`taskbar-widget-stack`'s own - no dots, no slider/snap animation, no
drag/wheel navigation, no settings window, no right-click menu, since
none of that applies to a single fixed set of bars with nothing to
navigate between.

**Settings**: real Windhawk settings now (`Wh_GetIntSetting`/
`Wh_ModSettingsChanged`), not the private-registry-store workaround
`taskbar-widget-stack` uses for its own settings window. That workaround
existed specifically because a Windhawk mod can't write back to its own
`==WindhawkModSettings==` values from code - as a plain standalone mod
with no custom settings window of its own, this one doesn't need to
write anything; the user edits `showCpu`/`showRam`/`showGpu`/
`refreshSeconds` through Windhawk's own settings UI like any other mod,
and `Wh_ModSettingsChanged()` reacts normally. Since which bars exist is
baked in at injection time (unlike `taskbar-widget-stack`'s widgets,
which read `g_settings` live per-tick), changing a `show*` setting here
triggers a live remove-and-reinject rather than just reloading a flag.

## Not yet done / not yet verified

- **This exact file has never been compiled.** The XAML-access and
  marshaling sections are verbatim ports of code already confirmed
  working live (in `taskbar-widget-stack`), but the injection/removal/
  settings-reload glue around them is new, written for this simplified
  single-mod shape - expect at least one live-test-and-fix round, the
  same pattern nearly everything in `taskbar-widget-stack` went through.
- **No cross-mod integration yet.** Installing this mod alongside
  `taskbar-widget-stack` will very likely fight over the same taskbar
  position - both currently anchor flush left against the taskbar's
  `RootGrid` with no coordination between them. Not fixed yet; the real
  fix is integration (below), not a position tweak.

## Planned integration with `taskbar-widget-stack`

Decided (2026-09-17, "grill me" round): **exported functions +
`GetProcAddress`** is the first mechanism to prototype, over a custom
Windows-message-based protocol - both mods are separate DLLs but injected
into the *same* `explorer.exe` process, so this is an in-process (not
cross-process) integration problem despite being cross-DLL. Rough shape,
not yet designed in detail or implemented:

1. `taskbar-widget-stack` exports a registration function (e.g.
   `extern "C" __declspec(dllexport) bool WidgetStack_RegisterWidget(...)`)
   taking something resembling the existing in-process `IWidget` contract
   - a set of function pointers (`Create`/`Tick`/`Destroy`/etc.) rather
   than a C++ vtable, since C++ ABI isn't stable across separately
   compiled DLLs the way a plain C function-pointer struct is.
2. This mod (or any other widget-shaped mod) finds `taskbar-widget-stack`'s
   loaded module via `GetModuleHandleW`/`FindWindowW`-style discovery,
   resolves that export via `GetProcAddress`, and calls it once loaded,
   handing over its own function pointers.
3. Open questions not yet resolved: load-order (what if this mod's
   `Wh_ModInit` runs before `taskbar-widget-stack`'s, or the other mod
   isn't installed/enabled at all - this mod needs to keep working
   standalone either way, per its current design), whether XAML
   `UIElement`/`Panel` handles can safely cross the DLL boundary as raw
   WinRT interface pointers (COM objects are generally
   location-transparent within a process, but this needs to be confirmed
   live, not assumed), and how a widget's settings would work once a
   second mod's private-store pattern is in the mix.

This section will get its own "Incident" log once integration work
actually starts, matching `taskbar-widget-stack/PLAN.md`'s convention.
