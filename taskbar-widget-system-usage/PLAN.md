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

## Integration with `taskbar-widget-stack`

Implemented (Incident 2, below) - see `taskbar-widget-stack/PLAN.md`'s
Incident 35 for the host side and the full design rationale (shared
between both files' comments). Short version: `taskbar-widget-stack`
publishes two function pointers as window properties on `Shell_TrayWnd`;
this mod looks them up during its own retry-inject loop and, if found,
calls the register one with a small `extern "C"` struct of its own
`Create`/`Tick`/`Destroy`/etc. callbacks instead of self-injecting.
Falls back to the original standalone injection if the host isn't found.

## Incident 1: first compile/install confirmed - bars rendered at 0 width (2026-09-17)

**Result**: this mod's own injection/removal glue (the untested part
per the note above) worked on the first try - it compiles, injects, and
the bars appear and update. One visual bug: every bar rendered at 0
width (label and percent text visible, no visible fill/track).

**Root cause**: the bar track's fill/empty split is done with two
`Star`-weighted `ColumnDefinition`s (`fillCol`/`emptyCol`), which only
works when some ancestor in the tree has a *determinate* width for
those Star columns to proportion against. Inside `taskbar-widget-stack`,
that determinate width came from the host - `ApplyStackWidth` set an
explicit `Width` on the equivalent top-level element, sized to fit the
widest enabled widget (capped at `layout.maxWidth`). Extracting the
widget kept the bar-building code verbatim but dropped that part of the
host's job - `root` here had no explicit `Width` (just
`HorizontalAlignment::Left` + a `Margin`), so it sized `Auto` to
content, and a `Star` column inside an `Auto`-sized ancestor has no
space to proportion against - it collapses to 0. The user's own guess
("probablement un bug dû à la gestion de la largeur par le stack") was
exactly right.

**Fix**: `root.Width(130)` - a fixed pixel width, matching the desired
width this widget used to report to `taskbar-widget-stack`'s host
before extraction. Simple and sufficient for a single, self-contained
element with no sibling widgets to share space with, unlike the
original multi-widget-stack context this sizing logic was designed for.

**Next retest**: confirm all enabled bars now render with a real fill
proportional to their reported percentage, not just label/percent text
with an invisible track.

## Incident 2: cross-mod registration with `taskbar-widget-stack` implemented (2026-09-17)

**Request**: "on continue avec l'intégration réelle" - build the real
integration decided above, not just relocate the widget.

**What changed**: added a byte-for-byte duplicate of
`taskbar-widget-stack.wh.cpp`'s new `WidgetStackHostAbiV1`/
`WidgetStackWidgetAbiV1` structs and typedefs, plus the two property-name
constants they're discovered through. Six new free functions
(`SystemUsage_Create`/`Tick`/`OnSettingsChanged`/`Destroy`/`GetId`/
`GetDisplayName`) implement the ABI's callback slots, reusing the exact
same `BuildRow`/`ApplyBar`/`Sample*`/`InitPdh`/`StartTimer` helpers as the
standalone path - the only real differences are *where* the root element
attaches (the host's `parentPanelAbi`, re-wrapped via
`winrt::copy_from_abi`, instead of the taskbar's own `RootGrid`) and that
it reports a desired width (`kDesiredWidth = 130.0`, matching the
standalone path's fixed `root.Width(130)`) rather than setting one, per
the host's `IWidget::Create` contract.

`RetryInjectThreadProc`'s loop now calls a new `TryRegisterOrInject`
first: `GetPropW(tray, kRegisterWidgetPropName)` to look for the host's
registration function, call it with this mod's ABI struct if found, and
only fall through to the original `InjectSystemUsageGrid` if the property
isn't there (host not installed/enabled/injected yet). `context` sent to
the host is a stable non-null sentinel (`kWidgetContext`, the address of
a static tag variable) so the host's `dynamic_cast` + pointer-match
lookup in `WidgetStack_UnregisterWidget` has something real to compare
against.

`Wh_ModSettingsChanged` and `Wh_ModBeforeUninit` both branch on whether
registration succeeded: if so, they call the host's unregister function
(captured at registration time, alongside the register function pointer)
instead of the standalone `RemoveSystemUsageGrid`/teardown path - settings
changes specifically unregister-then-reregister (mirroring the host's own
documented Destroy-then-Create contract) since there's no separate
"renegotiate width" callback in the ABI.

**Known limitation, not handled this round**: if `taskbar-widget-stack` is
disabled *after* this mod has already registered with it, this mod has no
way to find out - its retry-inject thread already exited on success, and
the stored host function pointers would point into an unloaded DLL. Not
fixed here; would need either a liveness check or a host-side notification
back to registered widgets before this mod would fall back to standalone
mid-session.

**Genuinely unverified**: this file has not been compiled since these
changes - same caveat as `taskbar-widget-stack.wh.cpp`'s Incident 35 (the
`__declspec(dllexport)`/`extern "C"` mechanism this depends on, and
whether a `Panel` pointer really survives the ABI round trip, are reasoned
through but not confirmed live in either file yet).

**Next retest**: two scenarios - (1) both mods installed and enabled:
confirm these bars appear *inside* `taskbar-widget-stack`'s pane
(participating in its dots/snap-scroll/right-click menu) instead of as a
separate taskbar element, and that changing a `show*`/`refreshSeconds`
setting here rebuilds correctly in place; (2) `taskbar-widget-stack` not
installed or disabled: confirm this mod still falls back to its own
standalone injection exactly as before this round.
