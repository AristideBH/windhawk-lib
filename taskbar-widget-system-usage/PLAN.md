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

## Incident 3: registration never happened - both mods loaded standalone and overlapped (2026-09-17)

**Result**: both mods compile and run, but the bars appeared as a
separate, overlapping element instead of inside `taskbar-widget-stack`'s
pane, and the widget never showed up in that mod's settings window. No
log lines from either the "Registered with taskbar-widget-stack" or
"WidgetStack_RegisterWidget failed" branches added in Incident 2 - so the
registration path wasn't being attempted at all going by its own logging,
which was the actual diagnostic signal here even without a captured log
file.

**Root cause**: a load-order race, invisible from either mod's code in
isolation. Both mods start their own independent retry-inject thread
around Explorer startup, each polling every 500ms with no ordering
guarantee between them. `taskbar-widget-stack` only publishes its
`SetPropW` registration functions *after* its own XAML injection
succeeds - so on the very first (and often only) attempt,
`taskbar-widget-system-usage`'s `GetPropW(tray, kRegisterWidgetPropName)`
came back null (the host hadn't gotten there yet), which is exactly why
no "falling back to standalone" log appeared either: that log only fires
when a registration function *was* found but the call itself failed, not
when the property lookup itself came back empty. `TryRegisterOrInject`
then silently fell through to `InjectSystemUsageGrid`, which succeeded
immediately - and because `RetryInjectThreadProc` stopped the whole retry
loop as soon as *any* path succeeded, this mod never looked for the host
again afterward, even once `taskbar-widget-stack` finished its own
injection moments later and published its properties.

**Fix**: `TryRegisterOrInject` now checks for the host's registration
property on every call, not just when nothing is active yet - if the
host becomes available while this mod is already running standalone, it
tears down the standalone element (`RemoveSystemUsageGrid`) and switches
to registering with the host instead. `RetryInjectThreadProc` now only
stops the loop early once `g_remoteRegistered` is actually true; if only
the standalone fallback succeeded, it keeps polling for the rest of its
~5-minute retry budget so a `taskbar-widget-stack` that appears seconds
later still gets picked up.

**A second, related bug found and fixed while making that change**: the
standalone injection path tracked whether the taskbar's window subclass
(`TaskbarWindowSubclassProc`, needed to catch `WM_NCDESTROY`/
`WM_DISPLAYCHANGE`) was installed via a field inside `UiState`
(`g_ui.windowSubclassed`). Since switching from standalone to registered
mode resets `g_ui` entirely (`g_ui = {}`), that flag would silently go
back to `false` even though the subclass itself was still attached to
the taskbar HWND - and `Wh_ModBeforeUninit`'s subclass-removal code was
only reachable from the standalone teardown branch, so once registered,
it would never run. Left uncaught, this mod's DLL would stay subclassed
onto Explorer's taskbar window after being disabled/unloaded - exactly
the "XAML-hosting HWND referencing this DLL's code alive across a
Windhawk reload" failure mode this file's own comments already flag as
something to avoid. Fixed by moving that tracking to file-scope globals
(`g_windowSubclassed`/`g_subclassedHwnd`) that survive a `g_ui` reset,
and removing the subclass unconditionally in `Wh_ModBeforeUninit`
whenever it's set, regardless of which mode (standalone or registered)
ends up active by the time the mod unloads.

**Still a known limitation**: if the retry loop's ~5-minute budget runs
out before `taskbar-widget-stack` ever becomes available (e.g. the user
enables it well after Explorer has already started), this mod stays on
its standalone fallback for the rest of that Explorer session - it won't
pick up the host until the next Explorer/mod reload. Not fixed this
round; a longer-lived low-frequency background poll would close this gap
but adds complexity for a corner case, so left as documented behavior
for now.

**Next retest**: same two scenarios as Incident 2, now expecting actual
registration to succeed even under Explorer-startup timing (not just in
principle) - confirm the bars end up inside the stack's pane, confirm the
widget shows in the stack's settings window, and confirm no duplicate/
overlapping element remains on the taskbar.

## Incident 4: registration confirmed working; fixed height mismatch inside the stack (2026-09-17)

**Result**: Incident 3's fix worked - registration now succeeds live
(screenshot confirmed: CPU/RAM bars rendering inside
`taskbar-widget-stack`'s pane, Media Player placeholder correctly wider).
One visual regression from before extraction: this widget's own pane no
longer fills the full stack height, throwing off dot/pane alignment for
the widget(s) after it.

**Root cause**: `taskbar-widget-stack.wh.cpp`'s `PlaceholderWidget::Create()`
wraps its content in a `Border` with `border.Height(host.paneHeight)` -
every widget's root element must be exactly `paneHeight` tall, since
`widgetsPanel` stacks all widgets' root elements vertically and slides
between them with a `CompositeTransform`; each widget occupies exactly
one `paneHeight`-tall slot in that stack for the slide to land on slot
boundaries. `SystemUsage_Create` (Incident 2) built its `StackPanel root`
with no explicit height at all - it sized to its own content (1-3 rows,
shorter than `paneHeight`), so it under-filled its slot.

**Fix**: wrap the bars in a `Border` sized to `host->paneHeight`, same as
`PlaceholderWidget`, and center `root` vertically within it. Added a
`remoteContainer` field to `UiState` for this (the standalone path is
unaffected - it doesn't use the multi-widget slot mechanism, so its
`root.Width(130)`-based sizing stays as-is). `SystemUsage_Destroy` now
removes `remoteContainer` from `remoteParentPanel`, not `root` directly.

**Next retest**: confirm the bars now occupy the full pane height like
the placeholders, and that switching to/from another widget in the stack
(dots, scroll, drag) snaps cleanly with no partial-pane artifacts.

## Incident 5: layout settings exposed - spacing, sizes, show/hide, colors (2026-09-17)

**Request**: expose real configurability for the bars - spacing/gaps,
text size, show/hide per element type, min/max width, bar thickness,
colors. A "grill me" round beforehand settled the open design questions:
one shared color for all three bars rather than per-metric (with a
"threshold" mode - blue/normal below the warning percentage, then
warning/critical colors above two configurable thresholds - as an
alternative to a single fixed color, not per-metric colors); real min/
max width with the layout adapting to content, not just one fixed
adjustable number; show/hide toggles global across all three metrics
(not per-metric-per-element); and native Windhawk settings only, no
private settings-window store (this mod has never had one, unlike
taskbar-widget-stack).

**What changed**: 15 new settings under a `layout` group -
`showLabel`/`showBar`/`showPercent` (global toggles), `fontSize`,
`barThickness`, `barWidth`, `rowSpacing`, `labelGap`, `percentGap`,
`minWidth`/`maxWidth`, and 5 color-related settings (`colorMode`:
accent/custom/threshold, `customColor`, `thresholdWarnPercent`/
`thresholdCriticalPercent`, `thresholdWarnColor`/`thresholdCriticalColor`).
All native `Wh_Get*Setting` reads in `LoadSettings()`, same as this mod's
existing `showCpu`/`showRam`/`showGpu` - no new persistence layer needed,
since `Wh_ModSettingsChanged()` already does a full remove-then-inject
(standalone) or unregister-then-reregister (registered) on *any* settings
change, unconditionally - unlike `taskbar-widget-stack.wh.cpp`'s
`layout.position`, which needed special-casing there because that file's
`ApplyStackWidth` only re-reads *some* settings live.

**Colors**: new `TryParseHexColor`/`GetAccentColor`/`ResolveBaseColor`
helpers. `colorMode: accent` (default) reads the live Windows accent
color via `UISettings.GetColorValue(UIColorType::Accent)` (this mod's
first use of `Windows.UI.ViewManagement`); `custom` parses a `#RRGGBB`/
`#AARRGGBB` hex setting instead; `threshold` keeps the resolved
accent-or-custom color as the "normal" tier but swaps the fill Brush's
color live (in `ApplyBar`, every tick) to a warning or critical hex color
once the reported percentage crosses the two configurable thresholds.
Colors are resolved once per `LoadSettings()` call (accent lookup + hex
parsing), not per-tick - `ApplyBar` just picks between three already-
resolved `Color` values.

**Width**: `BuildRow` no longer builds a fixed 130px row - the label and
percentage columns are now `Auto` (sized to their own text at the
configured font size), and only the bar track itself keeps a determinate
pixel width (`layout.barWidth`) - which is what the fill/empty Star-split
inside the track (Incident 1's original bug) actually needs to compute
against, not `root`'s own sizing. `root` itself sets `MinWidth`/`MaxWidth`
from the new settings instead of an explicit `Width()`, in both the
standalone path (`InjectSystemUsageGrid`) and the registered path
(`SystemUsage_Create`). The registered path still has to hand the host a
single concrete desired-width number (the ABI contract, and
`taskbar-widget-stack.wh.cpp`'s own layout, can't understand "auto with
bounds") - gets it by calling `root.UpdateLayout()` then reading
`root.ActualWidth()` right after attaching to the host's panel, which
already reflects the `MinWidth`/`MaxWidth` clamp automatically; no manual
`std::clamp` needed on top of that.

**Refactor**: the three separate `*FillCol`/`*EmptyCol`/`*PercentText`
field triplets in `UiState` (one triplet per metric) collapsed into a
single `RowRefs` struct (`fillCol`/`emptyCol`/`percentText`/`fillBrush`)
and three `RowRefs` members (`cpuRow`/`ramRow`/`gpuRow`) - `BuildRow`/
`ApplyBar` take one `RowRefs&` instead of three separate reference
parameters.

**Genuinely unverified**: none of this has been compiled or tested live
yet - same caveat as every other round in this file. Specific risks:
whether `UISettings.GetColorValue` actually resolves the live accent
color correctly from inside this hosting context (falls back to the old
hardcoded blue on any exception, so a failure here degrades rather than
crashes); whether the `Auto`-column label/percent sizing plus `MinWidth`/
`MaxWidth` on `root` renders as expected rather than clipping content
when `maxWidth` is set too low for the configured font size; whether
`root.ActualWidth()` right after `UpdateLayout()` in `SystemUsage_Create`
reliably reflects the final size (this repo's own established caution
around `ActualWidth`/layout timing, e.g. `taskbar-widget-stack.wh.cpp`'s
Incident 22, is about calling `TransformToVisual` from risky/reentrant
contexts like input handlers - `Create()` isn't one of those, but the
measurement itself is new here regardless).

**Next retest**: try each new setting individually (font size, bar
width/thickness, gaps, min/max width, each color mode including
threshold at a couple of different CPU loads) in both standalone and
registered mode; confirm hiding all of label/bar/percent for a metric
still leaves a sane (not zero-width, not overlapping) row; confirm an
invalid hex value (typo, wrong length) falls back cleanly instead of
crashing or rendering a garbled color.
