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

## Incident 3: hardened against double-registration (stack + standalone at once) (2026-09-21)

**Fix**: `TryRegisterOrShowStandalone` used to call `registerFn` first and
only look up `unregisterFn` afterward; if a successful `registerFn` call
was ever paired with a missing `unregisterFn`, the widget fell back to
standalone injection while still registered with the host - both a
stack-managed copy and a standalone copy visible at once. Given
`taskbar-widget-stack` always publishes both props together
(`InjectWidgetStackGrid`) and removes both together
(`RemoveWidgetStackGrid`), this exact state can't occur today, but this
mod now fetches `unregisterFn` alongside `registerFn` up front and only
attempts registration when both are present - removing the possibility
entirely instead of relying on the host's current atomicity.

**Next retest**: no live-test dependency - this only removes an
otherwise-unreachable code path. Covered by the existing
register/unregister live-test steps.

## Incident 4: styling/UX pass from a live-tested screenshot comparison (2026-09-21)

**Symptom** (user-supplied screenshot comparing this mod's compact
widget + panel against `taskbar-widget-media-player`'s own reference
mockups): (1) the compact widget showed a persistent faint background
even at rest, not just on hover, unlike media-player's. (2) The panel's
header had its own separate semi-opaque background nested inside the
Flyout's own chrome, reading as a floating rectangle rather than one
uniform panel. (3) Panel width (260-320px) didn't match media-player's
fixed 360px, so the two mods' panels were visibly different sizes. (4)
The compact "Now" view's temperature had no unit letter and sat flush
against the pane edges. (5) The panel header's icon was a bare
oversized glyph with 12px of dead space before the text, and had no
location name. (6) The panel's forecast list showed raw ISO dates
("2026-09-21") with no way to reformat them.

**Root cause (1)**: `WireUpHover`'s `background` Border already went
fully transparent at rest (`ApplyWeatherHoverState`) - but the outer
`wrapper` is a plain `Button`, which carries its own themed idle/
PointerOver/Pressed template backgrounds that were never explicitly
cleared, so the Button's own chrome showed through underneath
`background`'s correct transparent state.
`taskbar-widget-media-player`'s own wrapper already clears this
(`wrapper.Background(MakeBrush({0x00,0,0,0}))`) - this mod's two
wrapper-construction sites (registered-mode and standalone) didn't.

**Fix**: added the same explicit `wrapper.Background(transparent)` at
both construction sites. Worth remembering for any *future* mod in this
repo too: a plain `Button` (or any templated control) used as a custom
hover surface needs its own default background cleared explicitly, or
it'll show through underneath whatever custom hover/press visual sits
on top of it - this bit both this mod and (until now) wasn't obvious
from media-player's code alone since that mod already had the fix
baked in from the start.

**Fix (2)**: removed `BuildWeatherHeaderAndDetails`'s own `card.Background`
entirely - the panel now relies on the Flyout's own default
FlyoutPresenter background, same as a plain native flyout, instead of
double-nesting an extra fill inside it.

**Fix (3)**: `BuildWeatherFlyoutContent`'s `MinWidth`/`MaxWidth` changed
from 260/320 to a fixed 360, matching media-player's own panel width
exactly.

**Fix (4)**: `BuildNowView`'s `tempText` now appends "C"/"F" per
`UnitSettings.temperature`; `root` (the compact view's own Grid) got a
`{6, 2, 6, 2}` `Padding`.

**Fix (5)**: the header icon is now a fixed 48x48 `Border` ("icon
zone", background tint + rounded corners) with the glyph centered
inside, rather than a bare 36pt glyph; the gap to the text next to it
shrank from 12px to 8px (now measured from the zone's own edge, not the
glyph's). A new `locationName` field (`WeatherState`, `ResolvedLocation`)
shows above the temperature line when available:
`GeocodeCity` (manual-city location mode) reads it straight out of its
existing geocoding response at no extra cost; the GPS and manual-lat/lon
paths have coordinates only, so a new best-effort `ReverseGeocodeLocation`
(BigDataCloud's free `reverse-geocode-client` endpoint, no API key,
same `CreateNoCompressionHttpClient` pattern as every other fetch in
this file) fills it in separately in `ResolveLocation`. A reverse-geocode
failure doesn't fail location resolution as a whole - the weather itself
still loads, just without a location label.

**Fix (6)**: new `DisplaySettings.forecastDateFormat` string setting
(default `"{rel}"`) plus a small token-based `FormatForecastDate`
(no calendar-library dependency - day-of-week comes from a
self-contained Zeller's-congruence `ComputeWeekday`). Tokens: `{rel}`
(Today/Tomorrow/After tomorrow for the first three days, then falls
back to `{weekday_short}` on its own - so `"{rel}"` alone is already a
complete format, not just a building block), `{weekday}`/
`{weekday_short}`, `{month}`/`{month_short}`, `{day}`, `{year}` - freely
combinable, e.g. `"{weekday_short}, {month_short} {day}"` → "Wed, Sep
24". Applied in `BuildForecastListPanel`'s day-label text; the compact
forecast strip (`BuildForecastView`) never showed a date label at all,
so it's untouched.

**Next retest**: hover/click the compact widget - confirm no background
shows at rest, only on hover/press (both stack-registered and
standalone). Open the details panel - confirm no floating card
background behind the header, panel width matches media-player's own
panel, the icon sits in a visible square zone with a tighter gap to the
text, and a location name appears above the temperature (test all three
location modes: auto/GPS, manual city, manual lat/lon - the last two
via `ReverseGeocodeLocation` specifically, which is new and untested).
Confirm the compact "Now" view shows a unit letter and isn't flush
against the pane edges. In the panel's forecast list, confirm the
default `{rel}` format shows "Today"/"Tomorrow"/"After tomorrow" then
weekday abbreviations for the rest; try a custom format like
`"{weekday_short}, {month_short} {day}"` and confirm it renders
correctly.

## Incident 5: follow-up pass from live testing Incident 4's changes (2026-09-21)

**Symptom** (user feedback after live-testing Incident 4, screenshot):
(1) the panel overall correctly had no background, but the header block
specifically looked "un-anchored" without one - wanted it back, scoped
to just that block. (2) The icon-to-text gap in the header, tightened
from 12px to 8px in Incident 4, needed "a bit more" room again now that
the header has its own background. (3) The compact widget wrapper was
still a `Button` under the hood (Incident 4 only cleared its default
Background) - since there's only ever one clickable area, no `Button`
chrome is needed at all. (4) No way to align the compact widget's
content within its pane. (5) No compact view combining "now" with a
short forecast.

**Fix (1, 2)**: `header` (the icon+temp/condition/feels-like row inside
`BuildWeatherHeaderAndDetails`) now carries its own `CornerRadius`/
`Background`/`Padding`, independent of `card`'s (the panel's own outer
Grid, still background-free). The details row (humidity/wind/pressure)
and forecast list stay background-free. Icon-to-text gap went from 8px
back up to 12px.

**Fix (3)**: both `WeatherWidget_Create` and `InjectWeatherStandalone`'s
wrapper changed from `Button` to a plain `Grid` - `WireUpHover`/
`WireUpClickActions` both already took `FrameworkElement`, not `Button`
specifically, so no signature changes were needed; `g_weatherWrapper`'s
type changed from `Button` to `FrameworkElement` to match (only
`.ActualWidth()` was ever called on it elsewhere). A `Grid` isn't
hit-testable without an explicit `Background` the way a `Button` is by
default, so the transparent `Background` Incident 4 added for the
Button-chrome fix is now load-bearing for hit-testing too, not just
cosmetic. Matches `taskbar-widget-media-player`'s own wrapper, which is
also a plain `Grid`.

**Fix (4)**: new `DisplaySettings.contentAlignment` setting
(left/center/right, default left). Applied in `BuildCompactView` via
`HorizontalAlignment` on whichever view it built (`BuildNowView`/
`BuildForecastView`/`BuildMixedView`, all Auto-sized internally, never
on `wrapper`/`background` themselves, which stay `Stretch` so the hover
surface still covers the whole pane regardless of alignment).

**Fix (5)**: new `DisplaySettings.displayMode` option `mixed`
(`BuildMixedView`) - `BuildNowView`'s own icon+temp/condition, a thin
separator, then up to `kMixedForecastDays` (3) forecast cells (same
icon+high-temp cell style as `BuildForecastView`'s, smaller icon).
`mixed` is now the default (`g_settings.displayMode`'s in-code default
and the YAML default both changed from `now`). `ToggleDisplayMode` now
cycles `now -> forecast -> mixed -> now` instead of only toggling
between the first two.

**Next retest**: confirm the header block shows its own rounded
background again while the rest of the panel stays transparent, and the
icon-to-text gap reads less cramped than Incident 4's version. Hover/
click the compact widget - confirm identical hover/press behavior to
before (now via `Grid` pointer events instead of `Button`), and that
right-click still bubbles to `taskbar-widget-stack`'s own menu when
`ClickActionSettings.right` is "Nothing". Try all three
`contentAlignment` options and confirm the compact widget's content
visibly shifts within its pane while the hover surface itself still
covers the full pane width in every case. Confirm the compact display
mode now defaults to "Now + short forecast" on a fresh install, and
that cycling via `ToggleDisplayMode`'s bound click action visits all
three modes in order.

## Incident 6: hover styling still didn't match the media-player reference; panel background genuinely never rendered (2026-09-21)

**Symptom** (user feedback, side-by-side screenshot comparison of just
the hover rectangle, plus a fresh panel screenshot): (1) the compact
widget's hover highlight had no top/bottom margin (flush full pane
height, unlike media-player's own inset hover surface), a different-
looking corner radius, and no "lighter on top" gradient border that
media-player's own hover has. (2) The panel *still* had no visible
background, despite Incident 4 removing the old per-header background
specifically to rely on "the Flyout's own default presenter chrome" -
described as needing a fix "for good" this time.

**Root cause (1)**: media-player's own hover surface
(`taskbar-widget-media-player.wh.cpp`'s `playerButton`) is a *fixed*
40x40 element (`playerMinHeight`/`playerMaxHeight`, both defaulting to
40) centered within its own taller pane - the inset is a height
difference, not an explicit margin. It also uses a real
`LinearGradientBrush` (`MakeElevationBorderBrush`) as `BorderBrush`
specifically in its *hovered* visual state (transparent at rest, solid
at pressed) - this mod's own `ApplyWeatherHoverState` only ever touched
`Background`, never `BorderBrush`, so no gradient (or any border at
all) could have shown regardless of color values.

**Root cause (2)**: this was a real miss, not a system-default
limitation. `ShowWeatherPanel` sets an explicit `FlyoutPresenterStyle`
with `Background` set to `Transparent` (and `BorderThickness`/`Padding`
to 0) - present since before Incident 4, to stop the *system* chrome
from creating a second background on top of the panel's own. Incident
4 removed the header's own background and assumed the Flyout's default
chrome would show *something* - but that assumption was wrong twice
over: the presenter's background isn't just unstyled, it's explicitly
forced transparent by this mod's own code, so there was never any
background rendering in this Explorer-XAML-island context, before or
after Incident 4's change.

**Fix (1)**: `background` (the compact widget's hover `Border`, both
construction sites) got a `{0, 6, 0, 6}` `Margin` (media-player has no
direct equivalent margin value to copy since its inset comes from a
fixed height instead, and this mod has no per-widget configurable
height setting of its own to match that approach with) and its
`CornerRadius` raised from 4 to 8. `ApplyWeatherHoverState` now also
sets `BorderBrush`/`BorderThickness` per state: transparent at rest,
a new `MakeWeatherHoverBorderBrush()` (a top-0x28/bottom-0x0A white
`LinearGradientBrush`, the same stops as media-player's own
`MakeElevationBorderBrush`) while hovered, and a solid `0x0A` white
brush while pressed - matching media-player's Normal/PointerOver/
Pressed border progression exactly, just via direct property sets
instead of a VisualStateManager-templated `Button` (this mod doesn't
use `Button` at all any more, per Incident 5).

**Fix (2)**: `BuildWeatherFlyoutContent` now returns a `Border`
(`panelBg`), not the inner `StackPanel` directly - `panelBg` carries
its own unconditional `Background` (solid `0xF0/0x2B2B2B`, not
acrylic - simpler and doesn't depend on anything that could silently
fail to render again), `BorderBrush`/`BorderThickness` for a subtle
edge, `CornerRadius`, and the panel's fixed 360px width (moved here
from `content` itself). `BuildWeatherHeaderAndDetails`'s `card` no
longer carries its own 16px padding, since `panelBg` now owns that one
inset for every section uniformly instead of double-padding just the
header/details area. `ShowWeatherPanel`'s `FlyoutPresenterStyle` is
unchanged (still forces the system chrome transparent) - that's now
correct instead of incidentally hiding a broken assumption, since
`panelBg` is the actual, only source of the background.

**Next retest**: compare the compact widget's hover highlight directly
against media-player's own, side by side - margin, radius, and the
gradient border should now visually match. Open the details panel -
confirm it now has a real, solid, always-visible background regardless
of desktop wallpaper/theme, with consistent padding around the header
block, details row, forecast list, and Refresh button alike (not
double-padded around the header specifically).

## Incident 7: hover layout shift, panel content clipped at the bottom, no persistent hover/blur (2026-09-21)

**Symptom** (user feedback after live-testing Incident 6): (1) content
visibly shifted by a pixel or two when hovering the compact widget. (2)
Border radius (8px, set in Incident 6) still read as too large next to
media-player's own. (3) The compact widget's hover highlight dropped
back to idle as soon as the pointer left it to move into the now-open
details panel, reading as broken/flickery. (4) The panel background
(Incident 6) was solid but not blurred. (5) Panel content looked
slightly cropped at the bottom. (6) Top/bottom margin on the compact
widget's hover surface (added in Incident 6) was still too much - asked
for half.

**Root cause (1)**: `ApplyWeatherHoverState` switched `background`'s
`BorderThickness` between `{0,0,0,0}` (idle) and `{1,1,1,1}` (hover/
pressed) - a `Border`'s `BorderThickness` shrinks its *inner* content
area without changing its own outer footprint, so toggling it moved
`background`'s content by 1px on every hover/unhover.

**Root cause (3)**: nothing connected the details panel's open/closed
state to the compact widget's own hover visual - `ApplyWeatherHoverState`
was only ever driven by the widget's own `PointerEntered`/`PointerExited`,
with no path for `ShowWeatherPanel`'s Flyout `Opened`/`Closed` handlers
(a different function, wired later, with no access to `WireUpHover`'s
local `hovered`/`pressed` variables) to influence it.

**Root cause (5)**: `flyout.Opened`'s slide-in animation set
`transform.TranslateY(8)` as the starting position for a slide-up
entrance, but only ever animated `Opacity` back to 1 - nothing animated
`TranslateY` back to 0. `RenderTransform` affects where an element
*renders*, not its layout bounds, so `content` rendered permanently 8px
below where the Flyout had actually sized/clipped itself for - visible
as the bottom ~8px of content being cut off, on every single open, not
an intermittent glitch.

**Fix (1)**: `BorderThickness` is now set to `{1,1,1,1}` unconditionally
inside `ApplyWeatherHoverState`, every call, regardless of state - only
the brush changes between states now. The idle border brush is built
from the same gradient shape as the hover one
(`MakeWeatherHoverBorderBrush`, now taking an `alphaScale` parameter -
`1.0` for hover, `0.0` for idle) rather than a flat transparent
`SolidColorBrush`, so idle and hover are the same brush *type* even
though nothing currently animates between them.

**Fix (2, 6)**: `CornerRadius` 8 -> 4 (media-player's own default
`cornerRadiusTL` etc. value, copied exactly this time). `background`'s
top/bottom `Margin` halved, `{0,6,0,6}` -> `{0,3,0,3}`.

**Fix (3)**: new `g_weatherPointerHovered`/`g_weatherPointerPressed`
(persistent `shared_ptr<bool>` globals, not `WireUpHover`-local
variables) plus `RefreshWeatherWidgetHoverVisual()`, which computes the
widget's hover visual as "real pointer hover OR the panel is open"
(`g_weatherFlyoutOpen`, moved earlier in the file so this function can
see it) and applies it via the existing `ApplyWeatherHoverState`.
`WireUpHover`'s own pointer handlers and `ShowWeatherPanel`'s
`Opened`/`Closed` handlers both now call this same function instead of
computing/applying hover state independently.

**Fix (4)**: `panelBg`'s `Background` is now an `AcrylicBrush`
(`BackgroundSource::Backdrop`, same construction media-player's own
panel background uses), falling back to the previous flat solid color
in a `catch (...)` if `AcrylicBrush` throws, so the panel never ends up
with literally no background either way.

**Fix (5)**: added the missing `TranslateY` `DoubleAnimation` (8 -> 0,
same 150ms duration as the existing opacity fade), targeting
`transform` directly rather than a `"(UIElement.RenderTransform).(...)"`
property-path string on `content` - `transform` is a `DependencyObject`
in its own right and a valid `Storyboard` target by itself.

**Next retest**: hover the compact widget repeatedly and confirm no
visible content shift at all (compare a fixed reference point, e.g. the
icon's left edge, across idle/hover/pressed). Compare corner radius and
margin directly against media-player's own hover surface again. Open
the details panel, then move the pointer off the widget and into the
panel - confirm the widget's hover highlight stays on the whole time
the panel is open, and drops back to idle only after closing it (with
the pointer no longer over the widget). Confirm the panel background
now visibly blurs whatever is behind it (desktop/taskbar content),
not just a flat color. Confirm the forecast list's last row and the
Refresh button are both fully visible, nothing clipped at the bottom,
on every single panel open (not just most of the time).

## Incident 8: forecast day count for the mixed view wasn't configurable (2026-09-21)

**Fix**: `BuildMixedView`'s forecast day count (Incident 5) was a
hardcoded `constexpr int kMixedForecastDays = 3`. New
`DisplaySettings.forecastDaysMixed` setting (1-5, default 3) replaces
it, loaded/clamped in `LoadSettings` the same way
`forecastDaysInline`/`forecastDaysPanel` already are.

**Next retest**: change "Forecast days (mixed view)" in settings and
confirm the compact "Now + short forecast" view's forecast strip
immediately reflects the new count (clamped to however many days are
actually cached).

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
