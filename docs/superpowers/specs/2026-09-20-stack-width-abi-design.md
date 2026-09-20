# Cross-mod widget stack: shared width negotiation — Design

Status: approved by user (2026-09-20), ready for implementation planning.

## Summary

Today, `taskbar-widget-stack`'s `RebuildStackContents` computes the
pane's shared content width as `min(max(every enabled widget's own
"desired width" from Create()), layout.maxWidth)` — each widget mod
reports whatever width it personally wants, and the stack just takes
the largest one. This makes width tuning a cross-mod juggling act: a
widget author (or the user, via each mod's own width settings) has to
guess what the *other* installed widgets will report, since the
biggest reported width wins for everyone.

This change flips the contract: each widget reports its **minimum
readable width** (the point below which its own content gets
truncated/illegible), and the stack decides the actual shared width by
clamping the widest minimum against two new stack-owned bounds,
`layout.minWidth`/`layout.maxWidth`. Every widget continues to stretch
to fill that shared width via the `HorizontalAlignment::Stretch`
default already in use — no widget-side layout change needed beyond
what `Create()`/`OnSettingsChanged()` returns.

This only applies to **stack-registered** widgets. Standalone-injected
widgets (no `taskbar-widget-stack` installed/enabled) keep governing
their own width exactly as today — this design doesn't touch that
path.

## Non-goals

- Two-way width negotiation (host telling a widget the final width
  *before* it builds its content, so the widget can adapt what it
  shows based on available space). A widget can already read its own
  `ActualWidth()` after `UpdateLayout()` if it wants to react to the
  final size — see media-player's/weather's existing `Create()`
  implementations, which already do this. Not needed for this change.
- Any change to standalone-mode width behavior.
- A version-negotiation/compat shim for a hypothetical third-party
  widget mod outside this repo. All four mods that implement this ABI
  are in this repo, under one author's control — this is a direct
  breaking change to `WidgetStackWidgetAbiV1`/`WidgetStackHostAbiV1`,
  not a new `V2` struct living alongside the old one.

## Changes by file

### `taskbar-widget-stack/taskbar-widget-stack.wh.cpp`

**New setting**: `layout.minWidth` (int, DIPs), declared alongside the
existing `layout.maxWidth` in the settings.yaml block, same group.
Default: a small, sane floor (e.g. `80` — see Task-level detail in the
plan for the exact chosen default; must be `<=` a sane default for
`layout.maxWidth` so the two don't invert by default).

**`RebuildStackContents`** (the `contentWidth` computation currently
around the `entry.desiredWidth`/`ApplyStackWidth` call): replace
```
contentWidth = max(over enabled, non-crashed widgets: entry.desiredWidth)
contentWidth = min(contentWidth, layout.maxWidth)
```
with
```
contentWidth = max(over enabled, non-crashed widgets: entry.minWidth)
contentWidth = clamp(contentWidth, layout.minWidth, layout.maxWidth)
```
`WidgetEntry::desiredWidth` is renamed to `minWidth` throughout this
file (field rename, not a new field — same storage, new name reflects
the new meaning, per YAGNI: no need to keep both an old and new field
during an in-repo breaking change).

**`IWidget::Create()`/`OnSettingsChanged()`** (the local, same-file
widget interface used by `PlaceholderWidget` and `RemoteWidget`): the
doc comment on `Create()` changes to describe "minimum width the
widget needs to stay readable" instead of "desired width, widest
enabled widget wins." No signature change — still returns `double`.
`PlaceholderWidget::Create()`'s own returned value is re-examined: it
currently returns a fixed `desiredWidth_` constructor parameter
(placeholders are demo/test widgets with hardcoded colored bars) — this
becomes its minimum-readable width instead, likely unchanged in value
since a placeholder's "content" is just its label text, but the
semantic label in code/comments must be corrected.

**`RemoteWidget::Create()`** (the ABI adapter, unwraps a remote widget's
return value into the local `IWidget` contract): no logic change needed
— it already just forwards whatever the remote `Create()`/
`OnSettingsChanged()` callback returns. Only its own doc comments need
updating to describe the new "minimum width" meaning, since the value
it forwards now means something different.

**Cross-mod ABI struct** (`WidgetStackWidgetAbiV1`, the
`Create`/`OnSettingsChanged` function-pointer fields — no field
added/removed, only the *documented contract* of what the `double`
return value means changes): the struct's own field types/order are
unchanged, so this is NOT a binary layout break — it's a semantic
contract break enforced by convention/comments only, since C++ function
pointer types can't encode "this double now means something else."
Every widget-providing mod's own `*Widget_Create`/
`*Widget_OnSettingsChanged` implementation must be updated to return
the new value, or a widget updated to the new stack while still
returning its old "desired width" value would silently get a wrong
(likely oversized) shared width — this is the actual coordination risk
of this change, and why all four mods must land together in one plan
rather than independently.

### `taskbar-widget-media-player/taskbar-widget-media-player.wh.cpp`

**`MediaPlayer_Create`/`MediaPlayer_OnSettingsChanged`** (the ABI
callbacks, used only when stack-registered): currently return
`container.ActualWidth()` after building the full player UI at its own
preferred size — this already produces something closer to a "desired"
width than a "minimum," since the player's own `BuildPlayerGrid()`
sizes text/buttons/album-art at their natural size, not a
minimum-viable size. Needs its own investigation during planning:
either (a) build the same UI and treat the resulting `ActualWidth()` as
"minimum" too (acceptable if the player's natural UI size already *is*
close to its minimum readable size — plausible, since there isn't a lot
of optional/collapsible content once album art + title + controls are
shown), or (b) compute a tighter minimum (e.g. album art off, single-line
truncated title) as a fallback — deferred to the plan to decide, since
it depends on reading `BuildPlayerGrid()`'s actual layout, not just this
design doc.

**`playerMinWidth`/`playerMaxWidth` settings** (existing, used today in
standalone mode's `BuildPlayerGrid`/`ApplySettings` path via
`hasTextOrButtons && g_settings.playerMinWidth > 0` etc.): kept
unchanged for standalone mode. When stack-registered
(`g_mpRemoteRegistered == true`), these settings no longer influence
the width the stack computes — the mod's `Wh_ModSettingsChanged`/
settings-description text should note this, so the user isn't confused
about why the setting appears to do nothing while registered.

### `taskbar-widget-system-usage/taskbar-widget-system-usage.wh.cpp`

**`FillWidgetAbi`'s `Create`/`OnSettingsChanged` targets**: same
treatment as media-player — re-examine what's currently returned
(likely the bar table's own natural/configured width) and confirm it's
a reasonable "minimum," or compute a tighter one. This mod has no
per-widget width settings of its own today (confirmed during the
weather-mod research pass earlier this session) so there's no
standalone-mode setting to preserve here — simpler than media-player's
case.

### `taskbar-widget-weather/taskbar-widget-weather.wh.cpp`

**`WeatherWidget_Create`/`WeatherWidget_OnSettingsChanged`**: currently
return `wrapper.ActualWidth()` (falling back to `80.0`) after building
whichever compact view (`Now`/`Forecast`) is active. Same
question as the other two mods — is the natural built size already a
reasonable minimum, or does it need a tighter deliberate minimum
computation? The compact views are short (icon + 1-2 lines of text, or
an N-cell forecast strip) with no optional/collapsible sub-elements, so
this is likely the simplest case of the three — the natural size *is*
the minimum, since there's nothing else to strip out.

## Error handling / edge cases

- A widget whose true minimum exceeds `layout.maxWidth`: the stack
  already has existing clipping/min-content-width behavior
  (`kMinContentWidth`, the `clipHost` geometry clip) for an
  over-wide widget — this change doesn't need to add new clipping
  logic, just confirm the existing clip still applies correctly when
  `contentWidth` is now clamped by `layout.minWidth` on the low end too
  (a new failure mode to rule out: `layout.minWidth >
  layout.maxWidth` from a user misconfiguration — the stack should
  clamp/normalize this defensively, e.g. treat `maxWidth < minWidth` as
  `maxWidth = minWidth`, rather than produce an inverted/invalid
  range).
- `layout.minWidth` bigger than every widget's actual minimum: fine,
  it's the floor — every widget just gets extra stretch room, same as
  today when `layout.maxWidth` already forces extra room beyond what
  any single widget asked for.

## Testing

Same as every other mod in this repo: no automated test harness, no
local compiler available in the current working environment — verified
by careful code reading during planning/implementation, then a real
manual live-test pass by the user in Windhawk (per the pattern already
established for `taskbar-widget-weather`'s own build). The plan's final
task should include a live-test checklist: install all three widget
mods (media-player, system-usage, weather) plus the stack, confirm the
shared pane width lands at a sane value without any per-mod width
tuning, confirm dragging `layout.minWidth`/`layout.maxWidth` in the
stack's own settings actually changes the shared width, and confirm
disabling all but one widget still produces a sane width (not stuck at
some stale multi-widget-derived value).
