# taskbar-widget-weather — Design

Status: approved by user (2026-09-18), ready for implementation planning.

## Summary

A new, standalone Windhawk mod (`taskbar-widget-weather/taskbar-widget-weather.wh.cpp`)
showing current weather + forecast in the taskbar, visually matching
`taskbar-widget-media-player` and `taskbar-widget-system-usage`. Integrates
with `taskbar-widget-stack`'s cross-mod widget ABI when present, with a
standalone injection fallback when it isn't — same dual-mode pattern as
the two existing widget-providing mods.

Data source: [Open-Meteo](https://open-meteo.com/) (forecast + geocoding
APIs), chosen for requiring no API key/registration. Location: Windows
Geolocation API by default, with a manual city/lat-lon override.

## Non-goals (v1)

- Hourly forecast (only "now" + daily forecast).
- Weather alerts/warnings.
- Multiple saved locations / location switching UI.
- Real vector weather icons (emoji for now — see "Icon rendering").
- Exponential backoff / sophisticated retry on fetch failure (fixed
  interval retry is enough for v1).

## Architecture

### File/mod structure

New top-level folder, matching the other three mods:

```
taskbar-widget-weather/
  taskbar-widget-weather.wh.cpp
  PLAN.md
  README.md
```

### Dual registration (stack + standalone)

Implements `WidgetStackWidgetAbiV1` as a widget provider, copying the
established boilerplate almost verbatim from
`taskbar-widget-media-player.wh.cpp`/`taskbar-widget-system-usage.wh.cpp`:

- ABI struct + fill-ABI function (`FillWeatherWidgetAbi`).
- `TryRegisterOrApplySettings`-equivalent (`TryRegisterOrShowStandalone`):
  attempts host registration via `GetPropW` on `Shell_TrayWnd`, falls
  back to standalone injection.
- Retry thread (`WeatherRetryRegisterThreadProc`/
  `StartWeatherRetryRegister`), ported from media-player's
  `MPRetryRegisterThreadProc`/`StartMPRetryRegister` — polls every
  500ms for up to ~5 minutes so a host that appears later (or a taskbar
  restart) is still picked up, exactly the fix from that mod's own
  Incident 3.
- Standalone injection uses the same tracked-anchor positioning system
  (`layout.position`: left_edge / right_edge / left_of_tray /
  right_of_tray / tracked Start/Search/Task View/Widgets buttons) as
  the other two mods, for visual/config consistency.

### Data layer

**Location resolution** (once at startup, on auto/manual mode switch,
or via a "Relocate" action — never re-resolved on every weather fetch):

1. If `LocationSettings.mode == auto`: call
   `Windows::Devices::Geolocation::Geolocator().GetGeopositionAsync()`
   from a background thread. On success, cache lat/lon.
2. On failure (denied/disabled/timeout) or `mode == manual`: use
   `LocationSettings.manualLat`/`.manualLon` if set, otherwise geocode
   `LocationSettings.manualCity` via Open-Meteo's free
   `geocoding-api.open-meteo.com` endpoint and cache the result.
3. If manual city geocoding fails: keep the last known good
   lat/lon (if any), log via `Wh_Log`, don't crash/blank the widget.

**Weather fetch**: a dedicated background thread
(`WeatherThreadProc`), started in `Wh_ModAfterInit`, structured like
the existing retry threads — `WaitForMultipleObjects` on three
handles: a periodic interval, a manual-refresh event (set by the
`refresh` click action), and a stop event (set in `Wh_ModUninit`).
On wake:

1. Resolve current lat/lon (from the cached location, re-resolving
   only if never successfully resolved yet).
2. `GET https://api.open-meteo.com/v1/forecast?...` via
   `Windows::Web::Http::HttpClient`, requesting current conditions +
   daily forecast (temp min/max, WMO weather code, is-day) for up to
   `max(forecastDaysInline, forecastDaysPanel)` days.
3. Parse the JSON response with `Windows::Data::Json` (built into the
   WinRT SDK already in use — no external JSON library).
4. On success: write the parsed result into a mutex-guarded
   `WeatherState` struct (current: temp, feels-like, WMO code, is-day,
   humidity, wind speed/direction, pressure; `daily`: vector of
   {date, WMO code, is-day, tempMin, tempMax}). Persist a compact
   cache of this state into the mod's registry storage (mirroring
   media-player's cached-album-art fields) so a reload shows
   last-known data immediately instead of a blank widget.
5. On failure (network error, non-200, parse error): log via
   `Wh_Log`, leave the existing `WeatherState` untouched (stale data
   stays visible), retry on the next scheduled wake.
6. `RunFromWindowThread` back onto the taskbar's UI thread to refresh
   whichever compact view (now/forecast) and open panel (if any) are
   currently showing.

Default `DisplaySettings.refreshIntervalMinutes` = 30.

### Icon rendering

A single seam function, so a future switch to real vector icons only
touches this one place:

```cpp
struct WeatherIconInfo { std::wstring glyph; };
WeatherIconInfo GetWeatherIcon(int wmoCode, bool isDay);
```

v1 implementation maps WMO code ranges to representative emoji
(clear/sunny, partly cloudy, cloudy/overcast, fog, drizzle/rain,
showers, thunderstorm, snow), with day/night variants where it matters
(☀️/🌙 for clear, etc.) via Segoe UI Emoji (no asset embedding, no new
"self-contained .wh.cpp" convention violation).

`DisplaySettings.iconStyle` (colored/monochrome) is declared in
settings.yaml now as a no-op reserved for when vector icons land, per
explicit user decision — `GetWeatherIcon` ignores it in v1.

### Compact display (in the stack pane, or standalone)

Two modes, both built to `PaneHeight()`, matching the sizing
convention of the other widgets:

- **Now** (matches the reference screenshot): icon + two-line text
  block (temp large / condition small) in a horizontal Grid — same
  font sizes as media-player's title/artist (12 / 11).
- **Forecast**: horizontal row of `DisplaySettings.forecastDaysInline`
  cells (2-7, default 3; "today" always first), each cell = icon above
  + max temp below. No min temp inline — kept for the panel.

`DisplaySettings.displayMode` (now/forecast) tracks which is currently
shown, persisted to the registry so it survives an Explorer restart,
and is what the `toggle_mode` click action flips.

### Hover state

Reuses media-player's exact pattern rather than inventing a new one:
precomputed `SolidColorBrush` hover/pressed brushes sourced from
system Fluent hover/pressed colors (`EnsureHoverBrushes`-equivalent),
applied via `VisualStateManager.GoToCommonState` on
PointerEntered/PointerExited of the widget's own wrapper element.

### Click actions

Four configurable slots — `ClickActionSettings.left` / `.right` /
`.doubleClick` / `.wheel` — each one of `none | open_panel | refresh |
toggle_mode`. Defaults: `left = open_panel`, `doubleClick =
toggle_mode`, `wheel = refresh`, `right = none`.

`right` defaults to `none` specifically because `taskbar-widget-stack`
wires its own `RightTapped` on `g_ui.root` (the whole stack, per
Incident 47's hit-area fix), which already opens the stack's own
context menu no matter which widget pane the cursor is over. If the
user explicitly assigns a non-`none` action to `right`, the widget's
own `RightTapped`/`PointerPressed` handler must call
`args.Handled(true)` so the event doesn't bubble up into the stack's
menu — this must be commented in the code at that exact spot, since
it's the kind of interaction a future debugging session would
otherwise have to rediscover from scratch.

### Panel (Flyout)

Triggered by whichever click action is set to `open_panel` (default:
left click). Toggles closed on a second trigger while already open,
same as media-player's mini-player flyout.

Built as a real `Flyout` (not `Popup`/`MenuFlyout`), with a custom
`FlyoutPresenterStyle` stripping default background/border/padding so
the content fully controls its own appearance, and the same fade-in
`CompositeTransform` open animation as the mini-player flyout.

Content (`BuildWeatherFlyoutContent()`), top to bottom:

1. **Header**: large icon + current temp + condition text + "Feels
   like X°" — title/subtitle font sizes matching the mini-player's own
   header (16 / 13), corner radius 6 on the containing card.
2. **Details grid**: humidity, wind (speed + direction, in the
   configured unit), pressure — small labeled cards/rows.
3. **Forecast list**: `DisplaySettings.forecastDaysPanel` days (3-7,
   default 5, independent of the inline count) — icon + day label +
   min/max temp per row (unlike the compact forecast mode, the panel
   shows both).
4. **Refresh button**: triggers the same manual-refresh path as the
   `refresh` click action.

Dismissal: standard `Flyout` light-dismiss (click-outside/Esc), plus
the open-widget-again-to-close toggle above.

## Settings inventory

Grouped into namespaces, following the existing settings.yaml
conventions (`$name`/`$description`, `$options` for enums):

- `LocationSettings.mode` (`auto` | `manual`, default `auto`)
- `LocationSettings.manualCity` (string, geocoded via Open-Meteo on
  save)
- `LocationSettings.manualLat` / `.manualLon` (optional direct
  override, takes precedence over `manualCity` when both are set)
- `UnitSettings.temperature` (`celsius` | `fahrenheit`)
- `UnitSettings.windSpeed` (`kmh` | `mph` | `ms` | `knots`)
- `DisplaySettings.displayMode` (`now` | `forecast`, persisted,
  mutated at runtime by the `toggle_mode` click action)
- `DisplaySettings.iconStyle` (`colored` | `monochrome`, reserved
  no-op in v1)
- `DisplaySettings.forecastDaysInline` (int, 2-7, default 3)
- `DisplaySettings.forecastDaysPanel` (int, 3-7, default 5)
- `DisplaySettings.refreshIntervalMinutes` (int, default 30)
- `ClickActionSettings.left` / `.right` / `.doubleClick` / `.wheel`
  (`none` | `open_panel` | `refresh` | `toggle_mode`, defaults per
  "Click actions" above)
- `layout.position` and the other tracked-anchor settings, inherited
  unchanged from the existing pattern (standalone positioning).

## Error handling / edge cases

- Geolocation denied, disabled, or timed out → falls back to manual
  location settings; logged, never crashes.
- Manual city fails to geocode → keeps the last known good lat/lon (if
  any) rather than going blank; logged.
- No network / fetch failure → keeps showing the last successfully
  fetched `WeatherState` indefinitely; retried on the next scheduled
  interval. No aggressive "N/A" flash on a single failed fetch.
- No cached data yet on first-ever run (before the first successful
  fetch completes) → neutral loading placeholder (a neutral icon glyph
  + "…" in place of the temperature) instead of a blank pane.

## Testing

No automated test harness exists for these mods (all four so far are
verified by live manual testing against a real Explorer session,
tracked as dated "Incidents" in each mod's `PLAN.md`). This mod follows
the same verification approach: build, install via Windhawk, and
manually confirm each behavior area (data fetch/cache/retry, both
compact modes, hover, each click action, panel content, standalone vs.
stack-registered operation, Explorer-restart resilience) against a
checklist in `PLAN.md`, the same way the other three mods' `PLAN.md`
files document their own live-test retest steps.
