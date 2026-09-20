# taskbar-widget-weather — PLAN

## Context

New mod, designed 2026-09-18. Full design rationale lives in
[docs/superpowers/specs/2026-09-18-taskbar-widget-weather-design.md](../docs/superpowers/specs/2026-09-18-taskbar-widget-weather-design.md)
and the implementation plan that built this file in
[docs/superpowers/plans/2026-09-18-taskbar-widget-weather.md](../docs/superpowers/plans/2026-09-18-taskbar-widget-weather.md).
Live-test findings from here on are logged below as dated Incidents,
same convention as `taskbar-widget-media-player`/`taskbar-widget-system-usage`/`taskbar-widget-stack`.

## Incident 1: first live compile - duplicate widget, taskbar remnants, fetch exception (2026-09-20)

**Symptom** (user report, live test on real Explorer): (1) the weather
widget duplicates in the stack when moving/activating itself or
another widget, or when changing a stack setting - a recurrence of
`taskbar-widget-stack`'s own Incident 46, despite that mod's
dedup-by-context fix. (2) On Explorer restart, or when this mod is
disabled, a remnant of the weather widget stays visible in the
taskbar. (3) The details panel opens away from the widget rather than
anchored near it, unlike media-player's mini-player flyout (which has
placement settings). (4) `FetchWeather` throws on every manual
refresh - no weather data ever loads, and the compact widget stays at
its narrow loading-placeholder width as a direct consequence.

**Root cause (1, duplicate widget)**: `WeatherWidget_Destroy` removed
`g_weatherWrapper` from `g_weatherRootParent.Children()` with no
try/catch around the `IndexOf`/`RemoveAt` calls. `RebuildStackContents`
(the host's own destroy-then-create cycle, triggered by exactly the
actions in the symptom) wraps every widget's `Destroy()` in its own
`try/catch(...)`, which silently swallows any exception from ours -
leaving `g_weatherWrapper` still attached to the panel while the very
next `Create()` call appends a fresh one. `g_widgets` (the host's own
list) never grows past one entry, so Incident 46's dedup - which only
guards against a second *registration* - never sees anything wrong;
this was a failed *de*-registration, a different bug wearing the same
symptom.

**Root cause (2, remnants)**: two separate gaps, not one. (a)
`TrayUI_StartTaskbar_Hook` nulled `g_weatherWrapper`/`g_weatherRootParent`
without ever detaching the old wrapper or unregistering from the host
first - safe only if the entire old XAML tree always dies with the
`StartTaskbar` call, which isn't guaranteed (media-player's equivalent
hook makes the same assumption; this mod hit the case where it doesn't
hold). (b) `Wh_ModUninit`'s teardown was gated on `g_weatherTaskbarWnd`
being non-null, which is only set from `Wh_ModAfterInit` or the
`StartTaskbar` hook - disabling the mod before either had run skipped
teardown entirely.

**Fix (1 and 2)**: wrapped `WeatherWidget_Destroy`'s removal in
try/catch (matching media-player's `MediaPlayer_Destroy` pattern).
`TrayUI_StartTaskbar_Hook` now unregisters from the host (if
registered) or calls `WeatherWidget_Destroy` (if standalone) before
resetting its bookkeeping, rather than only resetting it - both paths
are try/catch-safe, so this is a no-op on the case where the old tree
genuinely already died. `Wh_ModUninit` now falls back to a fresh
`FindWindowW(L"Shell_TrayWnd", ...)` lookup when `g_weatherTaskbarWnd`
was never set, instead of skipping teardown outright.

**Fix (4, partial - evidence gathering)**: `FetchWeather`'s
`catch (...)` gave no diagnostic detail at all. Split into
`winrt::hresult_error` (logs the HRESULT + message), `std::exception`,
and a final `catch (...)` fallback - all three now log `lat`/`lon` too,
since 0.0/0.0 in the log would point at a silent location-resolution
failure upstream rather than the fetch itself. **Root cause of the
exception itself is still open** - needs the actual logged HRESULT
from the next test run to diagnose further (network/firewall,
malformed request, or something else); do not guess further without
that log line.

**Not yet fixed**: (3) panel placement - scoping this against
media-player's `miniPlayerPlacementMode`/near-vs-screen settings before
implementing, since it's a real settings-surface addition, not a small
bug fix.

**Next retest**: confirm no duplicate appears after several
move/enable/disable/settings-change cycles with the stack active;
confirm no remnant survives an Explorer restart or a mod
disable/re-enable cycle (test both while stack-registered and
standalone); trigger a manual refresh and paste back the new
`FetchWeather` log line so the exception's real cause can be diagnosed.

**Never compiled or tested by this repo's own automation** -
everything above the first real live test was reasoned through by
inspection of the sibling mods' equivalent code, exactly like those
mods' own PLAN.md files describe for their first pass.

## Incident 2: registered-mode width now comes from the stack's shared layout (2026-09-20)

**Fix**: registered-mode width now comes from `taskbar-widget-stack`'s
shared `layout.minWidth`/`maxWidth` instead of this mod's own reported
size - see
[docs/superpowers/specs/2026-09-20-stack-width-abi-design.md](../docs/superpowers/specs/2026-09-20-stack-width-abi-design.md)
and `taskbar-widget-stack/PLAN.md`'s own Incident 49 for the full
design/root cause. In this file: `WeatherWidget_Create`'s wrapper
`HorizontalAlignment` changed from `Left` to `Stretch`, so this mod's
content actually fills the host's negotiated width instead of staying
clamped to its own self-reported size.

**Next retest**: see `taskbar-widget-stack/PLAN.md`'s Incident 49
"Next retest" - specifically, dragging "Minimum"/"Maximum stack width"
in the stack's settings window and confirming this mod's compact
widget widens/clips along with the other registered widgets.

## Live-test checklist

Copied verbatim from the implementation plan's Task 16 ("Full
live-test pass"), so this file is self-contained the same way every
sibling mod's `PLAN.md` is.

### Step 1: Compile via Windhawk

Load `taskbar-widget-weather.wh.cpp` into the Windhawk app on a real
Windows machine and compile it. Fix any compile errors that surface
(none of this plan's code has been compiled yet, per the Global
Constraints note - expect at least minor WinRT API surface mismatches,
same as media-player's own Incident 1).

### Step 2: Run the full checklist

Execute every "Manual verification (executed in Task 16)" step listed
across Tasks 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 in order, on a real
Explorer session, with `taskbar-widget-stack` both enabled and
disabled as each step requires. Additionally:

1. Fresh install, no cached data yet: confirm the compact widget shows
   a loading placeholder (not blank, not a crash) until the first
   fetch completes.
2. Change `UnitSettings.temperature` from Celsius to Fahrenheit live:
   confirm the compact view and any open panel update immediately
   without a re-fetch (per Task 5's note that units are a render-time
   conversion, not a re-fetch trigger).
3. Change `DisplaySettings.forecastDaysInline`/`forecastDaysPanel`
   live: confirm both views immediately reflect the new count (clamped
   to however much data is cached).

### Step 3: Record results in PLAN.md

Append a dated `## Incident 1: <summary>` entry (or, if everything
passed with no fixes needed, a short "no issues found on first
live-test pass" note) to `taskbar-widget-weather/PLAN.md`, matching the
exact incident-log format used throughout
`taskbar-widget-media-player/PLAN.md` and the other sibling mods -
symptom, root cause, fix, next retest.

### Step 4: Update the repo README's "Not yet compiled/tested" note

Modify `README.md`'s weather-mod row (added in Task 1 Step 4) to
reflect actual live-test status instead of the placeholder text, once
Step 2 passes clean.

### Step 5: Commit

```bash
git add taskbar-widget-weather/PLAN.md README.md
git commit -m "Record first live-test pass for taskbar-widget-weather"
```
