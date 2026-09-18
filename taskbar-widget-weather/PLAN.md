# taskbar-widget-weather — PLAN

## Context

New mod, designed 2026-09-18. Full design rationale lives in
[docs/superpowers/specs/2026-09-18-taskbar-widget-weather-design.md](../docs/superpowers/specs/2026-09-18-taskbar-widget-weather-design.md)
and the implementation plan that built this file in
[docs/superpowers/plans/2026-09-18-taskbar-widget-weather.md](../docs/superpowers/plans/2026-09-18-taskbar-widget-weather.md).
Live-test findings from here on are logged below as dated Incidents,
same convention as `taskbar-widget-media-player`/`taskbar-widget-system-usage`/`taskbar-widget-stack`.

**Never compiled or tested at all yet** - everything below is reasoned
through by inspection of the sibling mods' equivalent code, exactly
like those mods' own PLAN.md files describe for their first pass.

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
