# taskbar-widget-media-player — PLAN

## Context

This mod is a fork of [Salyts' Taskbar Fluent Media
Player](https://github.com/Salyts/Taskbar-Fluent-Media-Player) (MIT
licensed - see this folder's `LICENSE`, which keeps Salyts' original
copyright notice as the license requires), created 2026-09-17 to
integrate it with [`taskbar-widget-stack`](../taskbar-widget-stack/)'s
cross-mod widget ABI, the same way
[`taskbar-widget-system-usage`](../taskbar-widget-system-usage/) already
does.

**Ground rule going into this** (explicit user request, "grill me"
round beforehand): touch the original mod's own code as little as
possible. A cross-mod registration genuinely can't be done with *zero*
changes - the ABI requires the widget-owning mod to actively call the
host's exported registration function, and the original mod has no way
to know that function exists without new code calling it - so this is a
**minimal fork**, not a from-scratch rewrite: every original function
(`BuildPlayerGrid`, `InjectPlayerGrid`, `RemovePlayerGrid`,
`ApplySettings`, the audio visualizer, the mini player flyout, SMTC
session handling, everything else) is untouched. The only edits are:

1. A new, self-contained "Cross-mod widget ABI" section added just
   before `BuildPlayerGrid` (struct/typedef layout + registration state
   + the six ABI callback functions + `TryRegisterOrApplySettings`).
2. Four existing call sites, each changed by the smallest amount that
   makes the branch work: `Wh_ModAfterInit`'s and
   `ApplySettingsWithRetry`'s `ApplySettings()` calls become
   `TryRegisterOrApplySettings(...)` (one-line swaps); `Wh_ModSettingsChanged`
   and `Wh_ModUninit`'s bodies gained an `if (registered) { ... } else { ...
   original code ... }` branch around their existing
   `RemovePlayerGrid()`/`InjectPlayerGrid()` calls.
3. `TrayUI_StartTaskbar_Hook` gained a few extra state resets
   (`g_mpRemoteRegistered` and friends) alongside the resets it already
   does for `g_playerGrid`/`g_injectionParent` - same reasoning, same
   spot, not a new code path.

## Design

Ported (with attribution) from `taskbar-widget-stack.wh.cpp`/
`taskbar-widget-system-usage.wh.cpp`'s own established pattern - same
ABI struct layout (`WidgetStackHostAbiV1`/`WidgetStackWidgetAbiV1`),
same discovery mechanism (`GetPropW` on `Shell_TrayWnd` for
`TaskbarWidgetStack_RegisterWidgetFn_v1`/
`..._UnregisterWidgetFn_v1`), same fallback/upgrade logic
(`TryRegisterOrApplySettings` mirrors `taskbar-widget-system-usage.wh.cpp`'s
`TryRegisterOrInject` almost line for line). An earlier version of this
mod also ported that file's Incident 8 host-disappearance recovery
(`g_mpExpectingHostDestroy`); this was reverted in Incident 2 below - see
that entry, and `taskbar-widget-system-usage`'s own Incident 9, for why.

**`MediaPlayer_Create`** calls the original `BuildPlayerGrid()`
unmodified to get the player's content `Grid`, wraps it in a `Border`
pinned to `host->paneHeight` (matching every other widget in the stack -
see `taskbar-widget-stack.wh.cpp`'s `PlaceholderWidget::Create`), and
appends that to `host->parentPanelAbi` instead of whatever
`ResolveInjectionTarget`'s position-based logic would have picked. It
then reuses `g_playerGrid` as the SAME global the original mod's own
`RefreshPlayerContents`/`UpdateVisibility`/timers/click-handlers already
key off of - deliberately, not just for convenience: it's what makes
those subsystems keep working transparently against whatever tree
`g_playerGrid` is actually parented into, standalone or registered,
without needing to touch any of them.

**`MediaPlayer_Destroy`** does NOT call the original `RemovePlayerGrid()`
- that function assumes `g_injectionParent` is a `Grid` it owns a real
`ColumnDefinition` in (tray-column or edge-position placement), which
isn't true for a widget hosted in the stack's `widgetsPanel` (a
`StackPanel`). Duplicates the handful of `RemovePlayerGrid`'s own
cleanup lines that are universally safe (closing an open mini player
flyout, clearing cached album art/title state) rather than editing that
function to special-case a non-`Grid` target - keeps `RemovePlayerGrid`,
and every standalone code path that calls it, untouched.

## Not yet done / genuinely unverified

- **Never compiled or tested at all.** Everything below is reasoned
  through, not confirmed live - expect at least one (probably several,
  given this mod's own scope) live-test-and-fix round, the same pattern
  every cross-mod integration in this repo has gone through.
- **Feature scope**: every one of the original mod's features (album
  art, playback controls, audio visualizer, mini player flyout,
  scrolling text, session switching) is left fully intact and should, in
  principle, keep working once registered - none were stripped down for
  the narrower pane. Whether they all actually *fit* and *look right* in
  a stack pane sized by `taskbar-widget-stack`'s own
  `layout.paneHeight`/`layout.maxWidth` settings (rather than the
  original mod's own `playerMinHeight`/`playerMaxHeight`/margin
  settings, which still apply on top and may need retuning) is unknown
  until tested live.
- **Taskbar-restart race, not specifically solved this round**: this
  mod and the host both independently hook `TrayUI::StartTaskbar` with
  no ordering guarantee between them. If the host is mid-rebuilding its
  own grid (which clears its registration properties, calls `Destroy()`
  on every widget it's holding, then re-injects and re-publishes the
  properties) at the exact moment this mod's own hook or
  `MediaPlayer_Destroy`'s host-disappearance recovery tries to
  re-register, it can land in the brief window where the host's
  properties are cleared but not yet republished, and fall back to
  standalone. Should self-correct on the next settings change or
  restart (same eventually-consistent behavior the other two mods
  already accept), not confirmed how long that actually takes live.
- **Multi-monitor**: this mod supports picking a specific monitor's
  taskbar (`g_settings.monitor`); the host is documented (its own
  PLAN.md's "Context") as single-monitor/primary-taskbar only.
  Registration is only attempted against whatever
  `FindCurrentProcessTaskbarWnd()` resolves to - if that's a secondary
  monitor's tray window, the host's properties won't be there (it only
  publishes on the primary `Shell_TrayWnd`), and this mod correctly
  falls back to its own standalone injection on that monitor. Not
  specially handled, just naturally falls back.

## Incident 1: first compile attempt - capture-less lambda used a local variable (2026-09-17)

**Symptom**: compile error - `variable 'hWnd' cannot be implicitly
captured in a lambda with no capture-default specified`, in
`Wh_ModSettingsChanged`'s `RunFromWindowThread` lambda.

**Root cause**: this mod's own `RunFromWindowThread(HWND, WindowThreadProc,
void*)` takes a plain function pointer (`using WindowThreadProc =
void(*)(void*);`), not a `std::function` - every lambda passed to it
must be capture-less to be convertible to that pointer type (matches
every *original* call site in this file, all `[](void*) { ... }` with
no captures, reading only globals). My edit to that one call site's
lambda body referenced the enclosing function's local `hWnd` parameter
instead of a global, which requires a capture and doesn't compile.

**Fix**: use the global `g_taskbarWnd` instead - it's set to that exact
same `hWnd` value on the line immediately before the
`RunFromWindowThread` call, so behavior is unchanged.

**Next retest**: recompile; if this was the only build error, move on to
the actual live-test checklist below.

## Incident 2: Explorer crash/restart when registered alongside a second remote widget (2026-09-17)

**Symptom** (live test, user report): enabling this mod alongside
`taskbar-widget-stack` and `taskbar-widget-system-usage` crashed/
restarted `explorer.exe` within about a second, with thousands of
alternating `SystemUsage_Destroy`/`MediaPlayer_Destroy`
"host-initiated, retrying registration or falling back to standalone"
log lines in between. Separately, before the crash, the widget was
observed sitting in its own standalone position rather than inside the
stack - likely just a downstream symptom of the crash loop, not
independently confirmed as a distinct bug.

**Root cause**: same root cause as
`taskbar-widget-system-usage/PLAN.md`'s Incident 9 - full explanation
there. In short: `taskbar-widget-stack.wh.cpp`'s `RebuildStackContents()`
destroys and recreates every registered widget on every rebuild,
including a widget's own very first registration (it's pushed onto the
list, then the very next rebuild call tears it straight back down before
its `Create()` has run once). This mod's `MediaPlayer_Destroy` treated
every `Destroy()` call it didn't itself expect as "the host disappeared"
and reacted by calling `TryRegisterOrApplySettings(g_taskbarWnd)`
*synchronously, inline, directly from inside `Destroy()`* - unlike
`taskbar-widget-system-usage`'s equivalent (which spawns a thread), this
created immediate, unbounded C++ call-stack recursion: `Destroy` →
re-register → the host's `RebuildStackContents` → `Destroy` on the
newly-duplicated entry → re-register → ... This is almost certainly why
Explorer crashed within about a second here specifically, faster/harder
than the same underlying bug in `taskbar-widget-system-usage` alone.

**Fix**: fully reverted the auto-recovery block ported in from Incident
8 of `taskbar-widget-system-usage`. `MediaPlayer_Destroy` no longer
checks `g_mpExpectingHostDestroy` or calls
`TryRegisterOrApplySettings` - it only tears down UI/cache state now.
Removed the now-dead `g_mpExpectingHostDestroy` global and its two
set-sites (`Wh_ModSettingsChanged`, `Wh_ModUninit`). This knowingly
reintroduces the "no automatic recovery if the host disappears
mid-session" gap as an accepted limitation, matching
`taskbar-widget-system-usage`'s own Incident 9 reversion - the "fix"
for that gap was actively dangerous once two remote widgets shared a
host.

**Next retest**: enable this mod alongside `taskbar-widget-stack` and
`taskbar-widget-system-usage` together and confirm Explorer no longer
crashes/restarts and all three widgets register into the stack cleanly.
Separately re-check whether the widget now correctly shows up inside
the stack (rather than its own standalone position) once the crash loop
is gone - if it still doesn't, that's a distinct bug to diagnose fresh.

## Incident 3: registration with the host flaky, needs Explorer restart / mod toggling (2026-09-18)

**Symptom** (user report, local live testing): whether this mod ends up
registered into `taskbar-widget-stack`'s pane or falls back to its own
standalone placement varies from one Explorer session to the next, with
no code change in between - getting it to register sometimes needs an
Explorer restart, sometimes toggling the mod off/on in Windhawk, and
it's hard to reproduce on demand.

**Root cause**: `TryRegisterOrApplySettings` (the `GetPropW` discovery
on `Shell_TrayWnd` documented at its call site) only ever ran as a
single, one-shot attempt - once from `Wh_ModAfterInit`, once from
`Wh_ModSettingsChanged`, and once from `TrayUI_StartTaskbar_Hook` (via
`ApplySettingsWithRetry`, which only retries waiting for
`SystemTray.SystemTrayFrame` to exist in the XAML tree, not for the
host's property to be published). Windhawk gives no load-order
guarantee between mods, so whether `taskbar-widget-stack` has already
run far enough to call `SetPropW(hWnd, kRegisterWidgetPropName, ...)`
by the exact moment one of those three call sites runs is essentially a
coin flip each session. Landing on the wrong side of that flip meant
falling back to standalone permanently, since nothing re-tried
afterward. `taskbar-widget-system-usage.wh.cpp` hit the identical race
(its own Incident 3) and already solved it with a background retry
thread (`RetryInjectThreadProc`/`StartRetryInject`) - that fix was never
ported over when this mod was forked in.

**Fix**: ported the same pattern over, prefixed `mp` to avoid clashing
with this file's existing globals - `g_mpRetryThread`, `g_mpRetryEvent`,
`g_mpRetryThreadMutex`, `g_mpRetryStopRequested`,
`MPRetryRegisterThreadProc` (polls `FindWindowW(Shell_TrayWnd)` +
`TryRegisterOrApplySettings` every 500ms, up to 600 attempts/~5
minutes, stopping early once registered), and `StartMPRetryRegister`.
Wired in: `Wh_ModInit` creates the event; `Wh_ModAfterInit` starts the
thread right after its existing one-shot attempt as a safety net;
`TrayUI_StartTaskbar_Hook` restarts it for each new taskbar instance
(Explorer restarts get a fresh polling window); `Wh_ModUninit` signals
stop and joins the thread before tearing anything else down.

**Next retest**: enable this mod alongside `taskbar-widget-stack`
across several consecutive Explorer restarts (and mod
disable/re-enable cycles) without touching any other setting, and
confirm it lands registered into the stack every time within a few
seconds, with no manual nudging needed.

## Incident 4: registered-mode width now comes from the stack's shared layout (2026-09-20)

Registered-mode width now comes from `taskbar-widget-stack`'s shared
`layout.minWidth`/`maxWidth` instead of this mod's own reported size -
see
[docs/superpowers/specs/2026-09-20-stack-width-abi-design.md](../docs/superpowers/specs/2026-09-20-stack-width-abi-design.md)
and `taskbar-widget-stack/PLAN.md`'s own Incident 49 for the full
design/root cause. In this file: `BuildPlayerGrid()`'s
`playerMinWidth`/`playerMaxWidth` clamp is now gated to
standalone-only (`if (!g_mpRemoteRegistered)`), and
`MediaPlayer_Create`'s `container.HorizontalAlignment` changed from
`Left` to `Stretch` so registered content actually fills the host's
negotiated width instead of clamping to its own settings.

## Next retest

Install and enable this mod alongside `taskbar-widget-stack` (with the
original, unmodified upstream mod disabled - only one copy of the player
should ever run). Confirm: the player registers as a widget in the
stack (dots/snap-scroll/right-click menu shared with the other widgets);
album art, track title/artist, and playback controls all render and
respond correctly at the stack's own pane height; disabling
`taskbar-widget-stack` falls this mod back to its own standalone
placement; re-enabling it picks registration back up. Then confirm the
opposite direction still works too: with `taskbar-widget-stack` not
installed/enabled at all, this mod behaves exactly like the unmodified
upstream mod.
