# taskbar-widget-weather Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a new Windhawk mod, `taskbar-widget-weather`, that shows current weather + forecast in the taskbar, registers into `taskbar-widget-stack` when present (falling back to standalone injection otherwise), and matches the existing mods' visuals (hover, fonts, spacing).

**Architecture:** One self-contained `.wh.cpp` file (repo convention - no shared headers between mods). Pure/host-independent logic (icon mapping, unit conversion) lives in free functions with no WinRT dependency; a dedicated background thread owns all networking/location I/O and hands off a `WeatherState` snapshot to the UI thread via `RunFromWindowThread`; the widget itself is dual-registered exactly like `taskbar-widget-media-player` - a `WidgetStackWidgetAbiV1` provider plus a standalone-injection fallback with its own retry thread.

**Tech Stack:** C++/WinRT (`winrt::Windows::UI::Xaml::*`), `Windows::Devices::Geolocation::Geolocator`, `Windows::Web::Http::HttpClient`, `Windows::Data::Json`, Win32 (`CreateThread`, `WaitForMultipleObjects`, `SetPropW`/`GetPropW`), Open-Meteo REST APIs (no key).

**Spec:** [docs/superpowers/specs/2026-09-18-taskbar-widget-weather-design.md](../specs/2026-09-18-taskbar-widget-weather-design.md)

## Global Constraints

- No API key for any external service (Open-Meteo only).
- No automated test framework exists in this repo, and no local C++ compiler is available in this working environment - Windhawk itself compiles `.wh.cpp` files live, and every existing mod here is verified by the *user* manually loading it into Windhawk and testing against a real Explorer session (see any existing `PLAN.md`'s dated "Incident" entries). This plan's "test" step for XAML/WinRT/Win32 code is therefore **a written manual verification checklist**, not an automated run - the one exception is pure, host-independent logic (icon mapping, unit conversion), which gets real standalone-runnable C++ unit tests since those functions don't touch WinRT/Explorer at all.
- Single file: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp`. Every task after Task 1 appends to this same file - do not create additional `.cpp`/`.h` files (matches every other mod in this repo).
- Match existing visual conventions exactly: corner radius `4` (small elements) / `6` (panel cards), font size `12`/`11` (compact title/subtitle), `16`/`13` (panel header/subtitle), hover overlay alpha range `0x0F`-`0x2C` over white.
- `ClickActionSettings.right` defaults to `none` and any non-`none` right-click action must call `args.Handled(true)` so it doesn't bubble into `taskbar-widget-stack`'s own `RightTapped` menu (spec: "Click actions").
- `DisplaySettings.iconStyle` is declared in settings.yaml but is a no-op in `GetWeatherIcon` for this plan (spec: "Icon rendering").
- `WidgetStackWidgetAbiV1`/`WidgetStackHostAbiV1` (Task 12) currently have no `cbSize`/version field and no SEH crash isolation on the host side - see `taskbar-widget-stack/PLAN.md`'s "Known follow-ups" section. Not this plan's problem to fix, but if that struct gains a `cbSize` field before Task 12 lands, Task 12's copy must match the host's updated copy field-for-field.

---

## File Structure

```
taskbar-widget-weather/
  taskbar-widget-weather.wh.cpp   # everything - single self-contained mod
  PLAN.md                          # design rationale + live-test incident log (seeded in Task 1, appended to in Task 15)
  README.md                        # short description, matching sibling mods' README shape
```

Internal organization within the one `.wh.cpp` file, in the order tasks add them (mirrors `taskbar-widget-media-player.wh.cpp`'s own top-to-bottom shape: settings metadata comment → includes/usings → data structures/pure helpers → globals → UI builders → ABI/registration → `Wh_Mod*` entry points):

1. `// ==WindhawkMod==` / `// ==WindhawkModSettings==` metadata comments (Task 1)
2. Includes, `using namespace`, forward declarations (Task 1)
3. `WeatherSettings` struct + `LoadSettings()` (Task 1)
4. `WeatherState`/`DailyForecast` structs + `GetWeatherIcon()` (Task 2)
5. Unit conversion helpers (Task 3)
6. Location resolution (Task 4)
7. Weather fetch + background thread (Task 5)
8. Compact "Now" view builder (Task 6)
9. Compact "Forecast" view builder + mode switching (Task 7)
10. Hover state helpers (Task 8)
11. Click action dispatch (Task 9)
12. Panel (Flyout) header/details (Task 10)
13. Panel forecast list + refresh button (Task 11)
14. Cross-mod widget ABI (`WidgetStackWidgetAbiV1` provider) (Task 12)
15. Standalone injection target: taskbar XAML root lookup (Task 13)
16. Standalone injection + retry thread (Task 14)
17. `Wh_ModInit`/`Wh_ModAfterInit`/`Wh_ModUninit`/`Wh_ModSettingsChanged` (Task 15)

---

### Task 1: Mod scaffold, settings, and `LoadSettings()`

**Files:**
- Create: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp`
- Create: `taskbar-widget-weather/PLAN.md`
- Create: `taskbar-widget-weather/README.md`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `struct WeatherSettings` (all fields below), global `WeatherSettings g_settings`, `void LoadSettings()`. Every later task reads `g_settings.*`.

- [ ] **Step 1: Write the settings metadata block and includes**

Create `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` starting with:

```cpp
// ==WindhawkMod==
// @id              taskbar-widget-weather
// @name            Taskbar Widget: Weather
// @description     Shows current weather + forecast in the taskbar. Registers into taskbar-widget-stack's pane if installed, falls back to standalone injection otherwise.
// @version         1.0
// @author          Aristide
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Widget: Weather

Shows current weather (icon + temperature + condition) or a short daily
forecast strip directly in the taskbar, with a details panel (humidity,
wind, pressure, multi-day forecast) on click. Uses Open-Meteo - no API
key required. See PLAN.md for the design.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- LocationSettings:
  - mode: auto
    $name: Location mode
    $description: "auto" uses Windows' own location service; "manual" uses the city/lat-lon below.
    $options:
    - auto: Automatic (Windows location)
    - manual: Manual
  - manualCity: ""
    $name: Manual city
    $description: Only used when location mode is "manual" and no lat/lon override is set below. Geocoded via Open-Meteo on save.
  - manualLat: "0"
    $name: Manual latitude override
    $description: Takes precedence over manual city when non-zero. Leave "0" to use the city field instead.
  - manualLon: "0"
    $name: Manual longitude override
  $name: Location
- UnitSettings:
  - temperature: celsius
    $name: Temperature unit
    $options:
    - celsius: Celsius
    - fahrenheit: Fahrenheit
  - windSpeed: kmh
    $name: Wind speed unit
    $options:
    - kmh: km/h
    - mph: mph
    - ms: m/s
    - knots: knots
  $name: Units
- DisplaySettings:
  - displayMode: now
    $name: Compact display mode
    $options:
    - now: Now
    - forecast: Forecast strip
  - iconStyle: colored
    $name: Icon style
    $description: Reserved for a future vector-icon set; has no effect while icons are emoji.
    $options:
    - colored: Colored
    - monochrome: Monochrome
  - forecastDaysInline: 3
    $name: Forecast days (compact strip)
    $description: 2-7 days shown when compact display mode is "Forecast strip".
  - forecastDaysPanel: 5
    $name: Forecast days (panel)
    $description: 3-7 days shown in the details panel's forecast list.
  - refreshIntervalMinutes: 30
    $name: Refresh interval (minutes)
  $name: Display
- ClickActionSettings:
  - left: open_panel
    $name: Left click
    $options:
    - none: Nothing
    - open_panel: Open details panel
    - refresh: Refresh now
    - toggle_mode: Toggle Now/Forecast
  - right: none
    $name: Right click
    $options:
    - none: Nothing
    - open_panel: Open details panel
    - refresh: Refresh now
    - toggle_mode: Toggle Now/Forecast
  - doubleClick: toggle_mode
    $name: Double click
    $options:
    - none: Nothing
    - open_panel: Open details panel
    - refresh: Refresh now
    - toggle_mode: Toggle Now/Forecast
  - wheel: refresh
    $name: Mouse wheel click
    $options:
    - none: Nothing
    - open_panel: Open details panel
    - refresh: Refresh now
    - toggle_mode: Toggle Now/Forecast
  $name: Click actions
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Data.Json.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;
namespace wuxs = winrt::Windows::UI::Xaml::Shapes;
```

- [ ] **Step 2: Write the settings struct and `LoadSettings()`**

Append:

```cpp
enum class ClickAction { None, OpenPanel, Refresh, ToggleMode };

ClickAction ParseClickAction(const std::wstring& s) {
    if (s == L"open_panel") return ClickAction::OpenPanel;
    if (s == L"refresh") return ClickAction::Refresh;
    if (s == L"toggle_mode") return ClickAction::ToggleMode;
    return ClickAction::None;
}

struct WeatherSettings {
    // Location
    bool locationAuto = true;
    std::wstring manualCity;
    double manualLat = 0.0;
    double manualLon = 0.0;
    // Units
    bool useFahrenheit = false;
    std::wstring windSpeedUnit = L"kmh";  // kmh|mph|ms|knots
    // Display
    std::wstring displayMode = L"now";    // now|forecast (persisted, mutated at runtime)
    std::wstring iconStyle = L"colored";  // colored|monochrome (reserved, no-op)
    int forecastDaysInline = 3;
    int forecastDaysPanel = 5;
    int refreshIntervalMinutes = 30;
    // Click actions
    ClickAction leftClick = ClickAction::OpenPanel;
    ClickAction rightClick = ClickAction::None;
    ClickAction doubleClick = ClickAction::ToggleMode;
    ClickAction wheelClick = ClickAction::Refresh;
};

WeatherSettings g_settings;

std::wstring GetStringSetting(PCWSTR name, PCWSTR fallback) {
    auto* value = Wh_GetStringSetting(name);
    std::wstring result = value ? value : fallback;
    if (value) {
        Wh_FreeStringSetting(value);
    }
    return result;
}

void LoadSettings() {
    g_settings.locationAuto =
        GetStringSetting(L"LocationSettings.mode", L"auto") == L"auto";
    g_settings.manualCity = GetStringSetting(L"LocationSettings.manualCity", L"");
    g_settings.manualLat = Wh_GetIntSetting(L"LocationSettings.manualLat");
    g_settings.manualLon = Wh_GetIntSetting(L"LocationSettings.manualLon");
    g_settings.useFahrenheit =
        GetStringSetting(L"UnitSettings.temperature", L"celsius") == L"fahrenheit";
    g_settings.windSpeedUnit = GetStringSetting(L"UnitSettings.windSpeed", L"kmh");
    g_settings.displayMode =
        GetStringSetting(L"DisplaySettings.displayMode", L"now");
    g_settings.iconStyle = GetStringSetting(L"DisplaySettings.iconStyle", L"colored");
    g_settings.forecastDaysInline =
        std::clamp((int)Wh_GetIntSetting(L"DisplaySettings.forecastDaysInline"), 2, 7);
    g_settings.forecastDaysPanel =
        std::clamp((int)Wh_GetIntSetting(L"DisplaySettings.forecastDaysPanel"), 3, 7);
    g_settings.refreshIntervalMinutes =
        std::max(1, (int)Wh_GetIntSetting(L"DisplaySettings.refreshIntervalMinutes"));
    g_settings.leftClick =
        ParseClickAction(GetStringSetting(L"ClickActionSettings.left", L"open_panel"));
    g_settings.rightClick =
        ParseClickAction(GetStringSetting(L"ClickActionSettings.right", L"none"));
    g_settings.doubleClick = ParseClickAction(
        GetStringSetting(L"ClickActionSettings.doubleClick", L"toggle_mode"));
    g_settings.wheelClick =
        ParseClickAction(GetStringSetting(L"ClickActionSettings.wheel", L"refresh"));
}
```

Note: `Wh_GetIntSetting` returning a `double`-precision lat/lon isn't
exact for a numeric-string field in Windhawk's real settings API -
Task 4 revisits `manualLat`/`manualLon` storage if the actual
`Wh_GetIntSetting`/`Wh_GetStringSetting` signatures available at
compile time don't support fractional degrees; flag this explicitly
during Task 4's manual verification rather than silently truncating
coordinates.

- [ ] **Step 3: Seed PLAN.md and README.md**

`taskbar-widget-weather/README.md`:

```markdown
# Taskbar Widget: Weather

Shows current weather or a short forecast strip in the taskbar, with a
details panel on click. Registers into
[Taskbar Widget Stack](../taskbar-widget-stack/README.md)'s pane when
that mod is installed and enabled, falls back to its own standalone
placement otherwise. Uses [Open-Meteo](https://open-meteo.com/) - no
API key needed.

Not yet compiled or tested - see [`PLAN.md`](PLAN.md) for the design
and live-test log.
```

`taskbar-widget-weather/PLAN.md`:

```markdown
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

See the plan's Task 16 for the full checklist to run once compiled.
```

- [ ] **Step 4: Add the mod to the repo README's table**

Modify `README.md:16` area - add a row for the new mod, following the
exact format of the existing media-player row (name link, one-line
description, "Not yet compiled/tested." suffix).

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp taskbar-widget-weather/PLAN.md taskbar-widget-weather/README.md README.md
git commit -m "Scaffold taskbar-widget-weather: settings.yaml + LoadSettings()"
```

---

### Task 2: Weather data model + icon mapping

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)
- Test: `taskbar-widget-weather/tests/test_weather_icon.cpp` (standalone, no WinRT - see below)

**Interfaces:**
- Consumes: nothing new.
- Produces: `struct DailyForecast`, `struct WeatherState`, global `WeatherState g_weather` + `std::mutex g_weatherMutex`, `struct WeatherIconInfo { std::wstring glyph; }`, `WeatherIconInfo GetWeatherIcon(int wmoCode, bool isDay)`. Later tasks (6, 7, 10, 11) call `GetWeatherIcon` and read `g_weather` under `g_weatherMutex`.

This task's core logic (`GetWeatherIcon`) has zero WinRT/Win32
dependency - it's pure `int, bool -> std::wstring`. Since no compiler
is available in this working environment, write it as a **standalone,
header-only-style function** duplicated into a tiny throwaway test
file so a real compiler (the user's, or CI) can validate it before
it's pasted into the mod - the mod's own copy and the test's copy must
stay byte-identical.

- [ ] **Step 1: Write the failing test**

Create `taskbar-widget-weather/tests/test_weather_icon.cpp`:

```cpp
// Standalone test for GetWeatherIcon's WMO-code-to-glyph mapping - no
// WinRT dependency, compiles with any C++17 compiler. Not part of the
// mod build; run manually (e.g. `g++ -std=c++17 test_weather_icon.cpp
// -o test_weather_icon && ./test_weather_icon`) whenever
// GetWeatherIcon changes, then re-paste the verified function body
// into taskbar-widget-weather.wh.cpp.
#include <cassert>
#include <cstdio>
#include <string>

struct WeatherIconInfo {
    std::wstring glyph;
};

WeatherIconInfo GetWeatherIcon(int wmoCode, bool isDay);  // under test

int main() {
    assert(GetWeatherIcon(0, true).glyph == L"☀️");    // clear day: sun
    assert(GetWeatherIcon(0, false).glyph == L"\U0001F319");     // clear night: moon
    assert(GetWeatherIcon(1, true).glyph == L"\U0001F324️");  // mainly clear day
    assert(GetWeatherIcon(2, true).glyph == L"⛅");           // partly cloudy
    assert(GetWeatherIcon(3, true).glyph == L"☁️");     // overcast
    assert(GetWeatherIcon(45, true).glyph == L"\U0001F32B️"); // fog
    assert(GetWeatherIcon(48, true).glyph == L"\U0001F32B️"); // rime fog
    assert(GetWeatherIcon(51, true).glyph == L"\U0001F326️"); // light drizzle
    assert(GetWeatherIcon(61, true).glyph == L"\U0001F327️"); // rain
    assert(GetWeatherIcon(66, true).glyph == L"\U0001F327️"); // freezing rain
    assert(GetWeatherIcon(71, true).glyph == L"\U0001F328️"); // snow
    assert(GetWeatherIcon(80, true).glyph == L"\U0001F326️"); // rain showers
    assert(GetWeatherIcon(85, true).glyph == L"\U0001F328️"); // snow showers
    assert(GetWeatherIcon(95, true).glyph == L"⛈️");     // thunderstorm
    assert(GetWeatherIcon(99, true).glyph == L"⛈️");     // thunderstorm + hail
    assert(GetWeatherIcon(1234, true).glyph == L"☁️");   // unknown code: safe default
    std::printf("all GetWeatherIcon assertions passed\n");
    return 0;
}
```

- [ ] **Step 2: Confirm the test fails to link (no `GetWeatherIcon` yet)**

Leave `GetWeatherIcon` undeclared-but-unimplemented at this point (only
the forward declaration above exists in the test file itself) - this
step is a paper check: read through the test file and confirm every
assertion's expected glyph matches the WMO code table below before any
implementation exists, so the implementation in Step 3 is written
*to* these assertions, not the other way around.

- [ ] **Step 3: Implement `GetWeatherIcon`, `DailyForecast`, `WeatherState`**

Append to `taskbar-widget-weather.wh.cpp`:

```cpp
struct WeatherIconInfo {
    std::wstring glyph;
};

// Open-Meteo returns WMO weather codes (table 4677). Bucketed into the
// families that matter visually; exact sub-codes within a bucket (e.g.
// 61 light rain vs 65 heavy rain) don't get distinct glyphs in v1.
// `iconStyle` (colored/monochrome) is intentionally not consulted here
// yet - reserved no-op until real vector icons replace emoji (design
// doc's "Icon rendering").
WeatherIconInfo GetWeatherIcon(int wmoCode, bool isDay) {
    if (wmoCode == 0) {
        return {isDay ? L"☀️" : L"\U0001F319"};
    }
    if (wmoCode == 1) {
        return {isDay ? L"\U0001F324️" : L"\U0001F319"};
    }
    if (wmoCode == 2) {
        return {L"⛅"};
    }
    if (wmoCode == 3) {
        return {L"☁️"};
    }
    if (wmoCode == 45 || wmoCode == 48) {
        return {L"\U0001F32B️"};
    }
    if ((wmoCode >= 51 && wmoCode <= 57) || (wmoCode >= 80 && wmoCode <= 82)) {
        return {L"\U0001F326️"};
    }
    if ((wmoCode >= 61 && wmoCode <= 67)) {
        return {L"\U0001F327️"};
    }
    if ((wmoCode >= 71 && wmoCode <= 77) || wmoCode == 85 || wmoCode == 86) {
        return {L"\U0001F328️"};
    }
    if (wmoCode == 95 || wmoCode == 96 || wmoCode == 99) {
        return {L"⛈️"};
    }
    return {L"☁️"};  // unknown code: safe default, never blank
}

struct DailyForecast {
    std::wstring date;   // ISO "YYYY-MM-DD" as returned by Open-Meteo
    int wmoCode = 0;
    bool isDay = true;
    double tempMin = 0.0;
    double tempMax = 0.0;
};

struct WeatherState {
    bool hasData = false;
    double currentTemp = 0.0;
    double feelsLike = 0.0;
    int wmoCode = 0;
    bool isDay = true;
    double humidityPercent = 0.0;
    double windSpeed = 0.0;    // stored in the unit the fetch requested
    double windDirectionDeg = 0.0;
    double pressureHpa = 0.0;
    std::vector<DailyForecast> daily;
};

WeatherState g_weather;
std::mutex g_weatherMutex;
```

- [ ] **Step 4: Run the standalone test and confirm it passes**

Run (any available C++17 compiler; substitute for whichever the user
has installed):

```bash
g++ -std=c++17 taskbar-widget-weather/tests/test_weather_icon.cpp -o /tmp/test_weather_icon && /tmp/test_weather_icon
```

Expected: `all GetWeatherIcon assertions passed`. If no compiler is
available in the environment running this task, this step is
performed by the user before Task 16's live-test pass - note that
explicitly in the task's completion notes rather than skipping
verification silently.

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp taskbar-widget-weather/tests/test_weather_icon.cpp
git commit -m "Add WeatherState/DailyForecast model and GetWeatherIcon mapping"
```

---

### Task 3: Unit conversion helpers

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)
- Test: `taskbar-widget-weather/tests/test_units.cpp` (standalone, no WinRT)

**Interfaces:**
- Consumes: `WeatherSettings` (Task 1).
- Produces: `double CelsiusToFahrenheit(double c)`, `std::wstring FormatTemperature(double celsius, bool useFahrenheit)`, `double ConvertWindSpeedFromKmh(double kmh, const std::wstring& unit)`, `std::wstring FormatWindSpeed(double kmh, const std::wstring& unit)`, `std::wstring CompassDirection(double degrees)`. Tasks 6, 7, 10, 11 call these for display.

- [ ] **Step 1: Write the failing test**

Create `taskbar-widget-weather/tests/test_units.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

double CelsiusToFahrenheit(double c);
std::wstring FormatTemperature(double celsius, bool useFahrenheit);
double ConvertWindSpeedFromKmh(double kmh, const std::wstring& unit);
std::wstring FormatWindSpeed(double kmh, const std::wstring& unit);
std::wstring CompassDirection(double degrees);

int main() {
    assert(std::abs(CelsiusToFahrenheit(0.0) - 32.0) < 0.01);
    assert(std::abs(CelsiusToFahrenheit(100.0) - 212.0) < 0.01);
    assert(FormatTemperature(20.0, false) == L"20°");
    assert(FormatTemperature(20.0, true) == L"68°");
    assert(std::abs(ConvertWindSpeedFromKmh(36.0, L"mph") - 22.37) < 0.1);
    assert(std::abs(ConvertWindSpeedFromKmh(36.0, L"ms") - 10.0) < 0.1);
    assert(std::abs(ConvertWindSpeedFromKmh(36.0, L"knots") - 19.44) < 0.1);
    assert(std::abs(ConvertWindSpeedFromKmh(36.0, L"kmh") - 36.0) < 0.01);
    assert(FormatWindSpeed(36.0, L"kmh") == L"36 km/h");
    assert(CompassDirection(0.0) == L"N");
    assert(CompassDirection(90.0) == L"E");
    assert(CompassDirection(180.0) == L"S");
    assert(CompassDirection(270.0) == L"W");
    assert(CompassDirection(45.0) == L"NE");
    assert(CompassDirection(359.0) == L"N");
    std::printf("all unit-conversion assertions passed\n");
    return 0;
}
```

- [ ] **Step 2: Confirm expected failures by inspection**

Read through each assertion and confirm the expected numeric/string
values are independently correct (32°F/212°F boiling/freezing points;
1 km/h = 0.6214 mph = 0.2778 m/s = 0.5400 knots; 16-point compass
reduced to 8 cardinal/intercardinal labels) before implementing -
these are the target the implementation must hit, not values to
reverse-engineer from a first attempt.

- [ ] **Step 3: Implement the conversion helpers**

Append:

```cpp
double CelsiusToFahrenheit(double c) {
    return c * 9.0 / 5.0 + 32.0;
}

std::wstring FormatTemperature(double celsius, bool useFahrenheit) {
    double value = useFahrenheit ? CelsiusToFahrenheit(celsius) : celsius;
    return std::to_wstring((long long)std::lround(value)) + L"°";
}

double ConvertWindSpeedFromKmh(double kmh, const std::wstring& unit) {
    if (unit == L"mph") return kmh * 0.621371;
    if (unit == L"ms") return kmh / 3.6;
    if (unit == L"knots") return kmh * 0.539957;
    return kmh;  // kmh, or unrecognized -> pass through unchanged
}

std::wstring FormatWindSpeed(double kmh, const std::wstring& unit) {
    double converted = ConvertWindSpeedFromKmh(kmh, unit);
    std::wstring suffix = L"km/h";
    if (unit == L"mph") suffix = L"mph";
    else if (unit == L"ms") suffix = L"m/s";
    else if (unit == L"knots") suffix = L"kn";
    return std::to_wstring((long long)std::lround(converted)) + L" " + suffix;
}

std::wstring CompassDirection(double degrees) {
    static const wchar_t* kLabels[8] = {
        L"N", L"NE", L"E", L"SE", L"S", L"SW", L"W", L"NW"};
    double normalized = std::fmod(degrees, 360.0);
    if (normalized < 0) normalized += 360.0;
    int index = (int)std::lround(normalized / 45.0) % 8;
    return kLabels[index];
}
```

Note: `#include <cmath>` must be added to the mod's includes (Task 1's
`Step 1` block) for `std::lround`/`std::fmod` - add it there when this
task lands.

- [ ] **Step 4: Run the standalone test and confirm it passes**

```bash
g++ -std=c++17 taskbar-widget-weather/tests/test_units.cpp -o /tmp/test_units && /tmp/test_units
```

Expected: `all unit-conversion assertions passed`. Same environment
caveat as Task 2 Step 4 if no compiler is available here.

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp taskbar-widget-weather/tests/test_units.cpp
git commit -m "Add temperature/wind unit conversion helpers"
```

---

### Task 4: Location resolution

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `WeatherSettings` (Task 1, `g_settings.locationAuto/.manualCity/.manualLat/.manualLon`).
- Produces: `struct ResolvedLocation { double lat; double lon; bool valid; }`, global `ResolvedLocation g_location` + `std::mutex g_locationMutex`, `bool ResolveLocation()` (blocking, call only from a background thread - returns true and updates `g_location` on success, leaves the previous value untouched on failure). Task 5's fetch thread calls this once at startup and on-demand.

This task is Win32/WinRT-dependent (Geolocator, HttpClient for
geocoding) - no standalone unit test is possible without a live
network call and, for the auto path, a real Windows location-service
state. Verified in Task 16's manual checklist instead.

- [ ] **Step 1: Implement geocoding for the manual-city path**

Append:

```cpp
struct ResolvedLocation {
    double lat = 0.0;
    double lon = 0.0;
    bool valid = false;
};

ResolvedLocation g_location;
std::mutex g_locationMutex;

// Blocking - call only from a background thread. Returns false without
// touching `out` if the city can't be resolved (network error, no
// match) so callers can keep the previous known-good location instead
// of blanking it (design doc: "Manual city fails to geocode").
bool GeocodeCity(const std::wstring& city, ResolvedLocation& out) {
    if (city.empty()) {
        return false;
    }
    try {
        winrt::Windows::Web::Http::HttpClient client;
        std::wstring url =
            L"https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" +
            city;
        auto response =
            client.GetAsync(winrt::Windows::Foundation::Uri(url)).get();
        response.EnsureSuccessStatusCode();
        auto body = response.Content().ReadAsStringAsync().get();
        auto json = winrt::Windows::Data::Json::JsonObject::Parse(body);
        if (!json.HasKey(L"results")) {
            return false;
        }
        auto results = json.GetNamedArray(L"results");
        if (results.Size() == 0) {
            return false;
        }
        auto first = results.GetObjectAt(0);
        out.lat = first.GetNamedNumber(L"latitude");
        out.lon = first.GetNamedNumber(L"longitude");
        out.valid = true;
        return true;
    } catch (...) {
        Wh_Log(L"GeocodeCity: exception resolving '%s'", city.c_str());
        return false;
    }
}

// Blocking - call only from a background thread.
bool GeolocateAuto(ResolvedLocation& out) {
    try {
        winrt::Windows::Devices::Geolocation::Geolocator geolocator;
        auto position = geolocator.GetGeopositionAsync().get();
        auto coord = position.Coordinate();
        out.lat = coord.Point().Position().Latitude;
        out.lon = coord.Point().Position().Longitude;
        out.valid = true;
        return true;
    } catch (...) {
        Wh_Log(L"GeolocateAuto: exception (denied, disabled, or timed out)");
        return false;
    }
}

// Resolves g_location once, per the design doc's "resolve once at
// startup / mode switch / explicit relocate, never on every weather
// fetch" rule. Returns true if g_location now holds a valid position
// (either freshly resolved, or already valid from a previous call).
bool ResolveLocation() {
    ResolvedLocation resolved;
    bool ok = false;
    if (g_settings.locationAuto) {
        ok = GeolocateAuto(resolved);
    }
    if (!ok && g_settings.manualLat != 0.0 && g_settings.manualLon != 0.0) {
        resolved.lat = g_settings.manualLat;
        resolved.lon = g_settings.manualLon;
        resolved.valid = true;
        ok = true;
    }
    if (!ok && !g_settings.manualCity.empty()) {
        ok = GeocodeCity(g_settings.manualCity, resolved);
    }
    if (ok) {
        std::lock_guard<std::mutex> lock(g_locationMutex);
        g_location = resolved;
        return true;
    }
    std::lock_guard<std::mutex> lock(g_locationMutex);
    return g_location.valid;  // keep whatever we had, per design doc
}
```

- [ ] **Step 2: Manual verification checklist (run once Task 15 wires threads up)**

Documented here, executed as part of Task 16:
1. With Windows location services + "let desktop apps access location"
   both enabled, and `LocationSettings.mode = auto`: confirm
   `GeolocateAuto` succeeds (add a temporary `Wh_Log` of the resolved
   lat/lon and check the Windhawk debug log).
2. Disable "let desktop apps access location" system-wide: confirm
   `GeolocateAuto` fails cleanly (logged, no crash) and, with a
   `manualCity` set, `GeocodeCity` fills in a reasonable lat/lon.
3. Set `manualCity` to a deliberately unresolvable string (e.g.
   `"zzzzznotacity"`): confirm `ResolveLocation` returns whatever
   `g_location` already held (or `false` on a fully fresh install with
   nothing cached yet) rather than crashing or zeroing a previously
   good location.

- [ ] **Step 3: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add Windows Geolocation + Open-Meteo geocoding location resolution"
```

---

### Task 5: Weather fetch + background thread + registry cache

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `g_settings` (Task 1), `WeatherState`/`g_weather`/`g_weatherMutex` (Task 2), `ResolveLocation()`/`g_location`/`g_locationMutex` (Task 4).
- Produces: `HANDLE g_weatherStopEvent`, `HANDLE g_weatherRefreshEvent`, `DWORD WINAPI WeatherThreadProc(LPVOID)`, `void StartWeatherThread()`, `void StopWeatherThread()`, `void RequestWeatherRefresh()` (sets the refresh event - this is what the `refresh` click action calls), `void OnWeatherStateUpdated()` (forward-declared here, **implemented in Task 6** as the UI-refresh callback - Task 5 only calls it after a successful fetch). Task 15 calls `StartWeatherThread`/`StopWeatherThread` from `Wh_ModAfterInit`/`Wh_ModUninit`; Task 9's `refresh` click action calls `RequestWeatherRefresh`.

- [ ] **Step 1: Implement JSON parsing into `WeatherState`**

Append:

```cpp
bool ParseForecastResponse(const std::wstring& body, WeatherState& out) {
    try {
        auto json = winrt::Windows::Data::Json::JsonObject::Parse(body);
        if (!json.HasKey(L"current") || !json.HasKey(L"daily")) {
            return false;
        }
        auto current = json.GetNamedObject(L"current");
        out.currentTemp = current.GetNamedNumber(L"temperature_2m");
        out.feelsLike = current.GetNamedNumber(L"apparent_temperature");
        out.wmoCode = (int)current.GetNamedNumber(L"weather_code");
        out.isDay = current.GetNamedNumber(L"is_day") != 0.0;
        out.humidityPercent = current.GetNamedNumber(L"relative_humidity_2m");
        out.windSpeed = current.GetNamedNumber(L"wind_speed_10m");
        out.windDirectionDeg = current.GetNamedNumber(L"wind_direction_10m");
        out.pressureHpa = current.GetNamedNumber(L"surface_pressure");

        auto daily = json.GetNamedObject(L"daily");
        auto dates = daily.GetNamedArray(L"time");
        auto codes = daily.GetNamedArray(L"weather_code");
        auto tempsMax = daily.GetNamedArray(L"temperature_2m_max");
        auto tempsMin = daily.GetNamedArray(L"temperature_2m_min");

        out.daily.clear();
        uint32_t count = dates.Size();
        for (uint32_t i = 0; i < count; i++) {
            DailyForecast day;
            day.date = dates.GetStringAt(i).c_str();
            day.wmoCode = (int)codes.GetNumberAt(i);
            day.isDay = true;  // daily entries have no is_day - always render the day glyph
            day.tempMax = tempsMax.GetNumberAt(i);
            day.tempMin = tempsMin.GetNumberAt(i);
            out.daily.push_back(day);
        }
        out.hasData = true;
        return true;
    } catch (...) {
        Wh_Log(L"ParseForecastResponse: exception parsing response body");
        return false;
    }
}

// Blocking - call only from a background thread.
bool FetchWeather(double lat, double lon, int days, WeatherState& out) {
    try {
        winrt::Windows::Web::Http::HttpClient client;
        wchar_t urlBuf[512];
        swprintf_s(urlBuf,
            L"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
            L"&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
            L"weather_code,wind_speed_10m,wind_direction_10m,surface_pressure,is_day"
            L"&daily=weather_code,temperature_2m_max,temperature_2m_min"
            L"&timezone=auto&forecast_days=%d&wind_speed_unit=kmh",
            lat, lon, days);
        auto response =
            client.GetAsync(winrt::Windows::Foundation::Uri(urlBuf)).get();
        response.EnsureSuccessStatusCode();
        auto body = response.Content().ReadAsStringAsync().get();
        return ParseForecastResponse(body.c_str(), out);
    } catch (...) {
        Wh_Log(L"FetchWeather: exception during fetch");
        return false;
    }
}
```

Note: `wind_speed_unit=kmh` is always requested regardless of
`g_settings.windSpeedUnit` - conversion to the display unit happens at
render time via Task 3's `ConvertWindSpeedFromKmh`/`FormatWindSpeed`,
so a unit-setting change never needs a re-fetch, only a UI rebuild.

- [ ] **Step 2: Implement the registry cache (persist last-known state)**

Append:

```cpp
// Compact single-line cache: "temp|feelsLike|wmoCode|isDay|humidity|wind|windDir|pressure"
// - just enough for an instant non-blank display on reload, not the
// full forecast (re-fetched on the next thread wake instead of also
// serializing the whole `daily` vector).
void SaveWeatherCache(const WeatherState& state) {
    wchar_t buf[256];
    swprintf_s(buf, L"%.1f|%.1f|%d|%d|%.0f|%.1f|%.0f|%.0f", state.currentTemp,
               state.feelsLike, state.wmoCode, state.isDay ? 1 : 0,
               state.humidityPercent, state.windSpeed, state.windDirectionDeg,
               state.pressureHpa);
    Wh_SetStringValue(L"weatherCache", buf);
}

bool LoadWeatherCache(WeatherState& out) {
    auto* raw = Wh_GetStringValue(L"weatherCache");
    if (!raw) {
        return false;
    }
    std::wstring cached = raw;
    Wh_FreeStringValue(raw);
    swscanf_s(cached.c_str(), L"%lf|%lf|%d|%d|%lf|%lf|%lf|%lf", &out.currentTemp,
              &out.feelsLike, &out.wmoCode, (int*)&out.isDay,
              &out.humidityPercent, &out.windSpeed, &out.windDirectionDeg,
              &out.pressureHpa);
    out.hasData = true;
    out.daily.clear();  // forecast list re-populates on the first real fetch
    return true;
}
```

- [ ] **Step 3: Implement the background thread**

Append:

```cpp
HANDLE g_weatherStopEvent = nullptr;
HANDLE g_weatherRefreshEvent = nullptr;
HANDLE g_weatherThread = nullptr;

void OnWeatherStateUpdated();  // implemented in Task 6

DWORD WINAPI WeatherThreadProc(LPVOID) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    {
        WeatherState cached;
        if (LoadWeatherCache(cached)) {
            std::lock_guard<std::mutex> lock(g_weatherMutex);
            g_weather = cached;
        }
    }

    bool locationEverResolved = false;
    HANDLE waitHandles[2] = {g_weatherStopEvent, g_weatherRefreshEvent};

    while (true) {
        if (!locationEverResolved) {
            locationEverResolved = ResolveLocation();
        }

        if (locationEverResolved) {
            ResolvedLocation loc;
            {
                std::lock_guard<std::mutex> lock(g_locationMutex);
                loc = g_location;
            }
            if (loc.valid) {
                int days = std::max(g_settings.forecastDaysInline,
                                     g_settings.forecastDaysPanel);
                WeatherState fetched;
                if (FetchWeather(loc.lat, loc.lon, days, fetched)) {
                    {
                        std::lock_guard<std::mutex> lock(g_weatherMutex);
                        g_weather = fetched;
                    }
                    SaveWeatherCache(fetched);
                    OnWeatherStateUpdated();
                }
                // On failure: g_weather is left untouched (stale data
                // stays visible), per design doc's error handling.
            }
        }

        DWORD waitMs = (DWORD)g_settings.refreshIntervalMinutes * 60 * 1000;
        DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, waitMs);
        if (result == WAIT_OBJECT_0) {
            break;  // stop event
        }
        // WAIT_OBJECT_0 + 1 (refresh event) or WAIT_TIMEOUT: loop again
        if (result == WAIT_OBJECT_0 + 1) {
            ResetEvent(g_weatherRefreshEvent);
        }
    }

    winrt::uninit_apartment();
    return 0;
}

void StartWeatherThread() {
    g_weatherStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_weatherRefreshEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_weatherThread =
        CreateThread(nullptr, 0, WeatherThreadProc, nullptr, 0, nullptr);
}

void StopWeatherThread() {
    if (g_weatherStopEvent) {
        SetEvent(g_weatherStopEvent);
    }
    if (g_weatherThread) {
        WaitForSingleObject(g_weatherThread, 5000);
        CloseHandle(g_weatherThread);
        g_weatherThread = nullptr;
    }
    if (g_weatherStopEvent) {
        CloseHandle(g_weatherStopEvent);
        g_weatherStopEvent = nullptr;
    }
    if (g_weatherRefreshEvent) {
        CloseHandle(g_weatherRefreshEvent);
        g_weatherRefreshEvent = nullptr;
    }
}

void RequestWeatherRefresh() {
    if (g_weatherRefreshEvent) {
        SetEvent(g_weatherRefreshEvent);
    }
}
```

- [ ] **Step 4: Manual verification (executed in Task 16)**

1. Temporarily call `StartWeatherThread()`/`StopWeatherThread()` from a
   throwaway `Wh_ModInit`/`Wh_ModUninit` pair (Task 15 will wire the
   real ones) and confirm via `Wh_Log` that a fetch completes and
   `g_weather.hasData` becomes true within a few seconds on a machine
   with internet access.
2. Kill network access (airplane mode) and confirm the thread logs the
   failure but doesn't crash, and `g_weather` keeps whatever it had.
3. Call `RequestWeatherRefresh()` and confirm a fetch happens
   immediately rather than waiting for the full interval.

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add Open-Meteo fetch, JSON parsing, background thread, and registry cache"
```

---

### Task 6: Compact "Now" view builder

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `WeatherState`/`g_weather`/`g_weatherMutex` (Task 2), `GetWeatherIcon` (Task 2), `FormatTemperature` (Task 3), `g_settings.useFahrenheit` (Task 1).
- Produces: `std::wstring ConditionName(int wmoCode)`, `Grid BuildNowView()`, `FrameworkElement g_weatherRoot` (the currently-displayed compact element, either mode), `void OnWeatherStateUpdated()` (the real implementation promised in Task 5 - rebuilds `g_weatherRoot`'s content in place if it exists, safe to call from any thread via `RunFromWindowThread` internally). Task 7 adds `BuildForecastView()` as the sibling this dispatches to; Task 10 also calls `ConditionName`; Task 12/14 call whichever builder `g_settings.displayMode` selects when constructing the widget's root.

- [ ] **Step 1: Implement `ConditionName` (human-readable label, not just the glyph)**

Append - defined before `BuildNowView` (Step 2 below) specifically so
that step doesn't need a forward declaration:

```cpp
std::wstring ConditionName(int wmoCode) {
    if (wmoCode == 0) return L"Clear";
    if (wmoCode == 1) return L"Mainly clear";
    if (wmoCode == 2) return L"Partly cloudy";
    if (wmoCode == 3) return L"Overcast";
    if (wmoCode == 45 || wmoCode == 48) return L"Fog";
    if (wmoCode >= 51 && wmoCode <= 57) return L"Drizzle";
    if (wmoCode >= 61 && wmoCode <= 67) return L"Rain";
    if (wmoCode >= 71 && wmoCode <= 77) return L"Snow";
    if (wmoCode >= 80 && wmoCode <= 82) return L"Rain showers";
    if (wmoCode == 85 || wmoCode == 86) return L"Snow showers";
    if (wmoCode == 95 || wmoCode == 96 || wmoCode == 99) return L"Thunderstorm";
    return L"Unknown";
}
```

- [ ] **Step 2: Implement `BuildNowView`**

Append:

```cpp
constexpr double kIconFontSize = 20;
constexpr double kTempFontSize = 12;    // matches media-player's title font size
constexpr double kConditionFontSize = 11;  // matches media-player's artist font size

Grid BuildNowView() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }

    Grid root;
    root.ColumnDefinitions().Append(ColumnDefinition{});
    root.ColumnDefinitions().Append(ColumnDefinition{});
    root.ColumnDefinitions().GetAt(0).Width({1.0, GridUnitType::Auto});
    root.ColumnDefinitions().GetAt(1).Width({1.0, GridUnitType::Auto});
    root.VerticalAlignment(VerticalAlignment::Center);

    TextBlock icon;
    icon.FontSize(kIconFontSize);
    icon.VerticalAlignment(VerticalAlignment::Center);
    icon.Margin({0, 0, 6, 0});
    icon.Text(winrt::hstring(
        snapshot.hasData ? GetWeatherIcon(snapshot.wmoCode, snapshot.isDay).glyph
                          : L"☁️"));
    Grid::SetColumn(icon, 0);
    root.Children().Append(icon);

    StackPanel textStack;
    textStack.Orientation(Orientation::Vertical);
    textStack.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(textStack, 1);

    TextBlock tempText;
    tempText.FontSize(kTempFontSize);
    tempText.Text(winrt::hstring(
        snapshot.hasData
            ? FormatTemperature(snapshot.currentTemp, g_settings.useFahrenheit)
            : L"…"));
    textStack.Children().Append(tempText);

    TextBlock conditionText;
    conditionText.FontSize(kConditionFontSize);
    conditionText.Opacity(0.7);
    conditionText.Text(winrt::hstring(
        snapshot.hasData ? ConditionName(snapshot.wmoCode) : L""));
    textStack.Children().Append(conditionText);

    root.Children().Append(textStack);
    return root;
}
```

- [ ] **Step 3: Implement `OnWeatherStateUpdated`**

Append:

```cpp
FrameworkElement g_weatherRoot{nullptr};
Panel g_weatherRootParent{nullptr};
HWND g_weatherTaskbarWnd = nullptr;

Grid BuildForecastView();  // Task 7

FrameworkElement BuildCompactView() {
    return g_settings.displayMode == L"forecast" ? (FrameworkElement)BuildForecastView()
                                                   : (FrameworkElement)BuildNowView();
}

void RebuildCompactViewInPlace() {
    if (!g_weatherRootParent) {
        return;
    }
    auto newView = BuildCompactView();
    uint32_t index;
    if (g_weatherRoot && g_weatherRootParent.Children().IndexOf(g_weatherRoot, index)) {
        g_weatherRootParent.Children().RemoveAt(index);
        g_weatherRootParent.Children().InsertAt(index, newView);
    } else {
        g_weatherRootParent.Children().Append(newView);
    }
    g_weatherRoot = newView;
}

void OnWeatherStateUpdated() {
    if (!g_weatherTaskbarWnd) {
        return;
    }
    RunFromWindowThread(g_weatherTaskbarWnd, [](void*) {
        RebuildCompactViewInPlace();
    }, nullptr);
}
```

`RunFromWindowThread` is defined in Task 14 (standalone injection) -
forward-declare it here:

```cpp
using WindowThreadProc = void(*)(void*);
static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param);
```

- [ ] **Step 4: Manual verification (executed in Task 16)**

With `g_weather` populated by Task 5's thread and a throwaway host
panel, confirm `BuildNowView()` produces an icon + two-line text block
matching the reference screenshot's proportions, and that an unknown
WMO code or `hasData == false` renders the loading placeholder
(`…`/cloud glyph) instead of a blank/crashing view.

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add compact Now view and in-place UI refresh on weather updates"
```

---

### Task 7: Compact "Forecast" view + mode toggle

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `WeatherState`/`g_weather`/`g_weatherMutex`, `GetWeatherIcon`, `ConditionName` (unused here), `FormatTemperature`, `g_settings.forecastDaysInline`, `BuildCompactView`/`RebuildCompactViewInPlace` (Task 6).
- Produces: `Grid BuildForecastView()` (forward-declared and called in Task 6), `void ToggleDisplayMode()`. Task 9's `toggle_mode` click action calls `ToggleDisplayMode`.

- [ ] **Step 1: Implement `BuildForecastView`**

Append:

```cpp
Grid BuildForecastView() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }

    Grid root;
    root.VerticalAlignment(VerticalAlignment::Center);
    root.ColumnSpacing(8);

    int days = std::min((int)snapshot.daily.size(), g_settings.forecastDaysInline);
    if (days == 0) {
        TextBlock placeholder;
        placeholder.FontSize(kTempFontSize);
        placeholder.Text(L"…");
        root.Children().Append(placeholder);
        return root;
    }

    for (int i = 0; i < days; i++) {
        root.ColumnDefinitions().Append(ColumnDefinition{});
        const auto& day = snapshot.daily[i];

        StackPanel cell;
        cell.Orientation(Orientation::Vertical);
        cell.HorizontalAlignment(HorizontalAlignment::Center);
        Grid::SetColumn(cell, i);

        TextBlock icon;
        icon.FontSize(kIconFontSize * 0.7);
        icon.HorizontalAlignment(HorizontalAlignment::Center);
        icon.Text(winrt::hstring(GetWeatherIcon(day.wmoCode, day.isDay).glyph));
        cell.Children().Append(icon);

        TextBlock temp;
        temp.FontSize(kConditionFontSize);
        temp.HorizontalAlignment(HorizontalAlignment::Center);
        temp.Text(winrt::hstring(FormatTemperature(day.tempMax, g_settings.useFahrenheit)));
        cell.Children().Append(temp);

        root.Children().Append(cell);
    }
    return root;
}
```

- [ ] **Step 2: Implement `ToggleDisplayMode`**

Append:

```cpp
void ToggleDisplayMode() {
    g_settings.displayMode =
        g_settings.displayMode == L"now" ? L"forecast" : L"now";
    Wh_SetStringValue(L"DisplaySettings.displayMode", g_settings.displayMode.c_str());
    if (g_weatherTaskbarWnd) {
        RunFromWindowThread(g_weatherTaskbarWnd, [](void*) {
            RebuildCompactViewInPlace();
        }, nullptr);
    }
}
```

Note: `Wh_SetStringValue` persists into the mod's own private storage,
not the user-visible settings.yaml value shown in the Windhawk editor
- this matches the design doc's "persisted to the registry so it
survives an Explorer restart" without making a runtime toggle silently
rewrite the user's configured *default* in the settings UI itself.

- [ ] **Step 3: Manual verification (executed in Task 16)**

1. With `forecastDaysInline = 3` and at least 3 days of cached
   forecast data, confirm `BuildForecastView()` renders exactly 3
   cells, "today" first, each with icon-above-temp.
2. Set `forecastDaysInline = 7` and confirm it clamps to however many
   days `g_weather.daily` actually has if the fetch returned fewer.
3. Call `ToggleDisplayMode()` twice and confirm it alternates between
   the Now and Forecast layouts and the choice survives a simulated
   Explorer restart (re-read `DisplaySettings.displayMode` on next
   `LoadSettings()` call).

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add compact Forecast strip view and Now/Forecast mode toggle"
```

---

### Task 8: Hover state

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: nothing new (operates on whatever wrapper element Task 12/14 build around `BuildCompactView()`'s output).
- Produces: `void EnsureHoverBrushes()`, `SolidColorBrush g_weatherHoverBrush`, `SolidColorBrush g_weatherPressedBrush`, `void ApplyWeatherHoverState(Border background, bool hovered, bool pressed)`, `void WireUpHover(FrameworkElement wrapper, Border background)`. Task 12/14 call `WireUpHover(wrapper, background)` once when building the widget's outer `Button` wrapper and its `Border` background.

Mirrors `taskbar-widget-media-player.wh.cpp:2149-2191` (`EnsureHoverBrushes`/`UpdateHoverBrushColors`) and `:8639-8646` (wrapper `PointerEntered`/`PointerExited`) - read that file at those line ranges before implementing this task, since the exact system-color API calls (`GetSystemButtonHoverColor`-equivalent) must come from there rather than being guessed here.

- [ ] **Step 1: Implement the hover/pressed brush cache**

Append:

```cpp
SolidColorBrush g_weatherHoverBrush{nullptr};
SolidColorBrush g_weatherPressedBrush{nullptr};

// Subtle white overlay, same alpha range as media-player's own hover
// brushes (0x0F-0x2C over white) - not a full system-hover-color read
// like media-player's EnsureHoverBrushes does (that reads live
// Fluent Reveal colors via a Windows API media-player already hooks;
// duplicating that hook here for a first version isn't worth the
// risk of getting the undocumented call wrong - a fixed subtle
// overlay reads correctly in both light and dark taskbars, which is
// what actually matters here. Revisit if it looks visually off against
// media-player's own hover in a live side-by-side).
void EnsureHoverBrushes() {
    if (!g_weatherHoverBrush) {
        g_weatherHoverBrush = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0x14, 0xFF, 0xFF, 0xFF)};
    }
    if (!g_weatherPressedBrush) {
        g_weatherPressedBrush = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0x28, 0xFF, 0xFF, 0xFF)};
    }
}

void ApplyWeatherHoverState(Border background, bool hovered, bool pressed) {
    EnsureHoverBrushes();
    if (pressed) {
        background.Background(g_weatherPressedBrush);
    } else if (hovered) {
        background.Background(g_weatherHoverBrush);
    } else {
        background.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
    }
}
```

- [ ] **Step 2: Implement `WireUpHover`**

Append:

```cpp
// `background` is a full-bounds Border sitting behind the compact
// view's content (built by the caller - Task 12/14 - specifically so
// this function never needs to know whether it's wiring a stack-hosted
// or standalone wrapper). `wrapper` is the outer interactive element
// pointer events are attached to.
void WireUpHover(FrameworkElement wrapper, Border background) {
    auto hovered = std::make_shared<bool>(false);
    auto pressed = std::make_shared<bool>(false);

    wrapper.PointerEntered(
        [hovered, pressed, background](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *hovered = true;
            ApplyWeatherHoverState(background, *hovered, *pressed);
        });
    wrapper.PointerExited(
        [hovered, pressed, background](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *hovered = false;
            *pressed = false;
            ApplyWeatherHoverState(background, *hovered, *pressed);
        });
    wrapper.PointerPressed(
        [hovered, pressed, background](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *pressed = true;
            ApplyWeatherHoverState(background, *hovered, *pressed);
        });
    wrapper.PointerReleased(
        [hovered, pressed, background](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *pressed = false;
            ApplyWeatherHoverState(background, *hovered, *pressed);
        });
    ApplyWeatherHoverState(background, false, false);
}
```

- [ ] **Step 3: Manual verification (executed in Task 16)**

Once Task 12/14 wire a real wrapper, hover the mouse over the weather
widget in both a light and dark taskbar theme and confirm the overlay
is visible-but-subtle in both, matching media-player's own hover
weight side-by-side (not stronger or weaker).

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add hover/pressed visual state for the weather widget"
```

---

### Task 9: Click action dispatch

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `ClickAction`/`g_settings.leftClick/.rightClick/.doubleClick/.wheelClick` (Task 1), `RequestWeatherRefresh` (Task 5), `ToggleDisplayMode` (Task 7), `ShowWeatherPanel` (forward-declared here, **implemented in Task 10**).
- Produces: `void ExecuteClickAction(ClickAction action, FrameworkElement anchor)`, `void WireUpClickActions(FrameworkElement wrapper)`. Task 12/14 call `WireUpClickActions` on the same wrapper `WireUpHover` was attached to.

- [ ] **Step 1: Implement `ExecuteClickAction`**

Append:

```cpp
void ShowWeatherPanel(FrameworkElement anchor);  // Task 10

void ExecuteClickAction(ClickAction action, FrameworkElement anchor) {
    switch (action) {
        case ClickAction::OpenPanel:
            ShowWeatherPanel(anchor);
            break;
        case ClickAction::Refresh:
            RequestWeatherRefresh();
            break;
        case ClickAction::ToggleMode:
            ToggleDisplayMode();
            break;
        case ClickAction::None:
        default:
            break;
    }
}
```

- [ ] **Step 2: Implement `WireUpClickActions`, including the right-click `Handled()` interaction**

Append:

```cpp
// Right click defaults to ClickAction::None specifically so
// taskbar-widget-stack's own stack-wide RightTapped (wired on its
// root, covering every widget's bounds - see that mod's Incident 47)
// keeps being the right-click behavior here by default. If the user
// explicitly assigns a non-None action to right-click, this handler
// marks the event Handled so it does NOT also bubble into the stack's
// context menu - see the design doc's "Click actions" section for the
// full reasoning. This is the one interaction in this file that
// crosses into another mod's event-handling assumptions; don't remove
// the Handled() call without re-reading that section.
void WireUpClickActions(FrameworkElement wrapper) {
    wrapper.Tapped(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           winrt::Windows::UI::Xaml::Input::TappedRoutedEventArgs const&) {
            ExecuteClickAction(g_settings.leftClick,
                                sender.try_as<FrameworkElement>());
        });
    wrapper.DoubleTapped(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           winrt::Windows::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&) {
            ExecuteClickAction(g_settings.doubleClick,
                                sender.try_as<FrameworkElement>());
        });
    wrapper.RightTapped(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           winrt::Windows::UI::Xaml::Input::RightTappedRoutedEventArgs const& args) {
            if (g_settings.rightClick == ClickAction::None) {
                return;  // let it bubble to the stack's own menu
            }
            args.Handled(true);
            ExecuteClickAction(g_settings.rightClick,
                                sender.try_as<FrameworkElement>());
        });
    wrapper.PointerWheelChanged(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (g_settings.wheelClick == ClickAction::None) {
                return;
            }
            args.Handled(true);
            ExecuteClickAction(g_settings.wheelClick,
                                sender.try_as<FrameworkElement>());
        });
}
```

- [ ] **Step 3: Manual verification (executed in Task 16)**

1. With default settings, confirm left-click opens the panel,
   double-click toggles mode, wheel-click refreshes, and right-click
   still opens the *stack's* context menu (not a weather-specific
   action) when the widget is registered into the stack.
2. Set `ClickActionSettings.right = refresh` and confirm right-clicking
   directly on the weather widget now refreshes instead of opening the
   stack's menu, while right-clicking elsewhere in the stack (a gap, a
   different widget) still opens the stack's menu normally.

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add configurable click action dispatch with right-click Handled() guard"
```

---

### Task 10: Panel (Flyout) - header + details grid

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `WeatherState`/`g_weather`/`g_weatherMutex`, `GetWeatherIcon`, `ConditionName`, `FormatTemperature`, `FormatWindSpeed`, `CompassDirection` (Tasks 2, 3, 6).
- Produces: `Flyout g_weatherFlyout`, `bool g_weatherFlyoutOpen`, `Grid BuildWeatherHeaderAndDetails()`, `void ShowWeatherPanel(FrameworkElement anchor)` (the real implementation of Task 9's forward declaration). Task 11 appends the forecast list + refresh button into the same content grid this task builds.

Mirrors `taskbar-widget-media-player.wh.cpp:7237-7336` (`ShowMiniPlayerFlyout` + its `FlyoutPresenterStyle`/fade-in) - read that range before implementing.

- [ ] **Step 1: Implement the header + details grid**

Append:

```cpp
Grid BuildWeatherHeaderAndDetails() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }

    Grid card;
    card.CornerRadius({6, 6, 6, 6});
    card.Background(SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0x20, 0, 0, 0)});
    card.Padding({16, 16, 16, 16});
    card.RowDefinitions().Append(RowDefinition{});
    card.RowDefinitions().Append(RowDefinition{});

    // Header row: icon + temp/condition/feels-like
    Grid header;
    header.ColumnDefinitions().Append(ColumnDefinition{});
    header.ColumnDefinitions().Append(ColumnDefinition{});
    Grid::SetRow(header, 0);

    TextBlock headerIcon;
    headerIcon.FontSize(36);
    headerIcon.VerticalAlignment(VerticalAlignment::Center);
    headerIcon.Margin({0, 0, 12, 0});
    headerIcon.Text(winrt::hstring(
        snapshot.hasData ? GetWeatherIcon(snapshot.wmoCode, snapshot.isDay).glyph
                          : L"☁️"));
    Grid::SetColumn(headerIcon, 0);
    header.Children().Append(headerIcon);

    StackPanel headerText;
    headerText.Orientation(Orientation::Vertical);
    Grid::SetColumn(headerText, 1);

    TextBlock tempText;
    tempText.FontSize(16);
    tempText.Text(winrt::hstring(
        snapshot.hasData
            ? FormatTemperature(snapshot.currentTemp, g_settings.useFahrenheit) +
                  L" - " + ConditionName(snapshot.wmoCode)
            : L"Loading..."));
    headerText.Children().Append(tempText);

    TextBlock feelsLikeText;
    feelsLikeText.FontSize(13);
    feelsLikeText.Opacity(0.7);
    feelsLikeText.Text(winrt::hstring(
        snapshot.hasData
            ? L"Feels like " +
                  FormatTemperature(snapshot.feelsLike, g_settings.useFahrenheit)
            : L""));
    headerText.Children().Append(feelsLikeText);

    header.Children().Append(headerText);
    card.Children().Append(header);

    // Details row: humidity / wind / pressure
    Grid details;
    details.Margin({0, 12, 0, 0});
    details.ColumnDefinitions().Append(ColumnDefinition{});
    details.ColumnDefinitions().Append(ColumnDefinition{});
    details.ColumnDefinitions().Append(ColumnDefinition{});
    Grid::SetRow(details, 1);

    auto makeDetailCell = [](const std::wstring& label, const std::wstring& value,
                              int column) {
        StackPanel cell;
        cell.Orientation(Orientation::Vertical);
        cell.HorizontalAlignment(HorizontalAlignment::Center);
        Grid::SetColumn(cell, column);

        TextBlock valueText;
        valueText.FontSize(13);
        valueText.HorizontalAlignment(HorizontalAlignment::Center);
        valueText.Text(winrt::hstring(value));
        cell.Children().Append(valueText);

        TextBlock labelText;
        labelText.FontSize(11);
        labelText.Opacity(0.7);
        labelText.HorizontalAlignment(HorizontalAlignment::Center);
        labelText.Text(winrt::hstring(label));
        cell.Children().Append(labelText);

        return cell;
    };

    details.Children().Append(makeDetailCell(
        L"Humidity",
        snapshot.hasData ? std::to_wstring((int)snapshot.humidityPercent) + L"%"
                          : L"-",
        0));
    details.Children().Append(makeDetailCell(
        L"Wind",
        snapshot.hasData
            ? FormatWindSpeed(snapshot.windSpeed, g_settings.windSpeedUnit) + L" " +
                  CompassDirection(snapshot.windDirectionDeg)
            : L"-",
        1));
    details.Children().Append(makeDetailCell(
        L"Pressure",
        snapshot.hasData ? std::to_wstring((int)snapshot.pressureHpa) + L" hPa"
                          : L"-",
        2));

    card.Children().Append(details);
    return card;
}
```

- [ ] **Step 2: Implement `ShowWeatherPanel` up through the Flyout's presenter style (forecast list added in Task 11)**

Append:

```cpp
Flyout g_weatherFlyout{nullptr};
bool g_weatherFlyoutOpen = false;

StackPanel BuildWeatherFlyoutContent();  // Task 11 fills this in fully;
                                          // this task calls it as-is.

void ShowWeatherPanel(FrameworkElement anchor) {
    if (!anchor) {
        return;
    }
    if (g_weatherFlyoutOpen && g_weatherFlyout) {
        g_weatherFlyout.Hide();
        return;
    }

    Flyout flyout;
    auto content = BuildWeatherFlyoutContent();
    flyout.Content(content);

    FlyoutPresenterStyle style{
        winrt::Windows::UI::Xaml::Interop::TypeName{winrt::hstring(L"Windows.UI.Xaml.Controls.FlyoutPresenter"),
                                                      winrt::Windows::UI::Xaml::Interop::TypeKind::Metadata}};
    style.Setters().Append(Setter{Control::BackgroundProperty(),
                                   winrt::box_value(SolidColorBrush{
                                       winrt::Windows::UI::Colors::Transparent()})});
    style.Setters().Append(Setter{Control::BorderThicknessProperty(),
                                   winrt::box_value(Thickness{0, 0, 0, 0})});
    style.Setters().Append(Setter{Control::PaddingProperty(),
                                   winrt::box_value(Thickness{0, 0, 0, 0})});
    flyout.FlyoutPresenterStyle(style);

    flyout.Opened([content](winrt::Windows::Foundation::IInspectable const&,
                             winrt::Windows::Foundation::IInspectable const&) {
        g_weatherFlyoutOpen = true;
        CompositeTransform transform;
        content.RenderTransform(transform);
        content.Opacity(0.0);
        transform.TranslateY(8);
        DoubleAnimation opacityAnim;
        opacityAnim.To(1.0);
        opacityAnim.Duration(winrt::Windows::Foundation::TimeSpan{
            std::chrono::milliseconds(150)});
        Storyboard::SetTarget(opacityAnim, content);
        Storyboard::SetTargetProperty(opacityAnim, L"Opacity");
        Storyboard sb;
        sb.Children().Append(opacityAnim);
        sb.Begin();
    });
    flyout.Closed([](winrt::Windows::Foundation::IInspectable const&,
                      winrt::Windows::Foundation::IInspectable const&) {
        g_weatherFlyoutOpen = false;
        g_weatherFlyout = nullptr;
    });

    g_weatherFlyout = flyout;
    flyout.ShowAt(anchor);
}
```

- [ ] **Step 3: Manual verification (executed in Task 16)**

With cached weather data present, trigger `open_panel` and confirm the
header shows icon/temp/condition/feels-like, and the details row shows
humidity/wind (with correct unit + compass direction)/pressure,
visually close to the reference screenshot's density (not the
forecast list yet - that's Task 11).

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add weather panel Flyout: header and details grid"
```

---

### Task 11: Panel forecast list + refresh button

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `WeatherState`/`g_weather`/`g_weatherMutex`, `GetWeatherIcon`, `FormatTemperature`, `g_settings.forecastDaysPanel`, `RequestWeatherRefresh` (Task 5), `BuildWeatherHeaderAndDetails` (Task 10).
- Produces: the real `StackPanel BuildWeatherFlyoutContent()` promised in Task 10.

- [ ] **Step 1: Implement the forecast list**

Append:

```cpp
StackPanel BuildForecastListPanel() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }

    StackPanel list;
    list.Orientation(Orientation::Vertical);
    list.Margin({0, 12, 0, 0});
    list.Spacing(4);

    int days = std::min((int)snapshot.daily.size(), g_settings.forecastDaysPanel);
    for (int i = 0; i < days; i++) {
        const auto& day = snapshot.daily[i];

        Grid row;
        row.ColumnDefinitions().Append(ColumnDefinition{});
        row.ColumnDefinitions().Append(ColumnDefinition{});
        row.ColumnDefinitions().Append(ColumnDefinition{});
        row.ColumnDefinitions().GetAt(0).Width({1.0, GridUnitType::Auto});
        row.ColumnDefinitions().GetAt(1).Width({1.0, GridUnitType::Star});
        row.ColumnDefinitions().GetAt(2).Width({1.0, GridUnitType::Auto});

        TextBlock icon;
        icon.FontSize(16);
        icon.Margin({0, 0, 8, 0});
        icon.Text(winrt::hstring(GetWeatherIcon(day.wmoCode, day.isDay).glyph));
        Grid::SetColumn(icon, 0);
        row.Children().Append(icon);

        TextBlock dayLabel;
        dayLabel.FontSize(13);
        dayLabel.VerticalAlignment(VerticalAlignment::Center);
        dayLabel.Text(winrt::hstring(day.date));
        Grid::SetColumn(dayLabel, 1);
        row.Children().Append(dayLabel);

        TextBlock range;
        range.FontSize(13);
        range.VerticalAlignment(VerticalAlignment::Center);
        range.Text(winrt::hstring(
            FormatTemperature(day.tempMax, g_settings.useFahrenheit) + L" / " +
            FormatTemperature(day.tempMin, g_settings.useFahrenheit)));
        Grid::SetColumn(range, 2);
        row.Children().Append(range);

        list.Children().Append(row);
    }
    return list;
}
```

- [ ] **Step 2: Implement `BuildWeatherFlyoutContent` (assembles header+details+forecast+refresh)**

Append:

```cpp
StackPanel BuildWeatherFlyoutContent() {
    StackPanel content;
    content.Orientation(Orientation::Vertical);
    content.MinWidth(260);
    content.MaxWidth(320);

    content.Children().Append(BuildWeatherHeaderAndDetails());
    content.Children().Append(BuildForecastListPanel());

    Button refreshButton;
    refreshButton.Content(winrt::box_value(winrt::hstring(L"Refresh")));
    refreshButton.Margin({0, 8, 0, 0});
    refreshButton.HorizontalAlignment(HorizontalAlignment::Stretch);
    refreshButton.Click(
        [](winrt::Windows::Foundation::IInspectable const&,
           RoutedEventArgs const&) { RequestWeatherRefresh(); });
    content.Children().Append(refreshButton);

    return content;
}
```

- [ ] **Step 3: Manual verification (executed in Task 16)**

1. With `forecastDaysPanel = 5` and 5+ days of cached data, confirm
   the panel's forecast list shows exactly 5 rows, each with
   icon/date/max-min, distinct from the compact strip (which only
   shows max).
2. Click "Refresh" in the panel and confirm it triggers a fetch (same
   effect as the `refresh` click action) without closing the panel.

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add forecast list and refresh button to the weather panel"
```

---

### Task 12: Cross-mod widget ABI provider

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `BuildCompactView` (Task 6), `WireUpHover` (Task 8), `WireUpClickActions` (Task 9), `g_weatherRoot`/`g_weatherRootParent`/`g_weatherTaskbarWnd` (Task 6).
- Produces: `WidgetStackHostAbiV1`, `WidgetStackWidgetAbiV1`, `kRegisterWidgetPropName`, `kUnregisterWidgetPropName` (constants, copied verbatim from `taskbar-widget-stack.wh.cpp:317-361` - byte-identical field order/types, since this ABI is hand-synced across DLLs with no shared header), `double __cdecl WeatherWidget_Create(void*, const WidgetStackHostAbiV1*)`, the other five ABI callbacks, `void FillWeatherWidgetAbi(WidgetStackWidgetAbiV1&)`. Task 14 calls `FillWeatherWidgetAbi` from its registration attempt.

Read `taskbar-widget-stack.wh.cpp:317-361` (the exact struct/typedef
block) and `taskbar-widget-media-player.wh.cpp:7649-7696`
(`MediaPlayer_Create`'s host-unwrap pattern) before implementing - the
ABI struct fields below must match the host's copy exactly.

- [ ] **Step 1: Declare the ABI struct (byte-identical to the host's copy)**

Append:

```cpp
constexpr wchar_t kRegisterWidgetPropName[] =
    L"TaskbarWidgetStack_RegisterWidgetFn_v1";
constexpr wchar_t kUnregisterWidgetPropName[] =
    L"TaskbarWidgetStack_UnregisterWidgetFn_v1";

extern "C" {

struct WidgetStackHostAbiV1 {
    void* taskbarHwnd;
    void* parentPanelAbi;
    double paneHeight;
};

struct WidgetStackWidgetAbiV1 {
    void* context;
    double(__cdecl* Create)(void* context, const WidgetStackHostAbiV1* host);
    void(__cdecl* Tick)(void* context);
    double(__cdecl* OnSettingsChanged)(void* context);
    void(__cdecl* Destroy)(void* context);
    void(__cdecl* GetId)(void* context, wchar_t* buffer, int bufferSize);
    void(__cdecl* GetDisplayName)(void* context,
                                   wchar_t* buffer,
                                   int bufferSize);
};

using WidgetStack_RegisterWidget_t =
    bool(__cdecl*)(const WidgetStackWidgetAbiV1* widget);
using WidgetStack_UnregisterWidget_t = void(__cdecl*)(void* context);

}  // extern "C"

int g_weatherWidgetContextTag = 0;
void* const kWeatherWidgetContext = &g_weatherWidgetContextTag;
```

- [ ] **Step 2: Implement the six ABI callbacks**

Append:

```cpp
extern "C" double __cdecl WeatherWidget_Create(void* /*context*/,
                                                const WidgetStackHostAbiV1* host) {
    if (!host) {
        return 0.0;
    }
    try {
        Panel parent{nullptr};
        winrt::copy_from_abi(parent, host->parentPanelAbi);
        if (!parent) {
            return 0.0;
        }

        Button wrapper;
        wrapper.HorizontalAlignment(HorizontalAlignment::Left);
        wrapper.Height(host->paneHeight);
        wrapper.Padding({0, 0, 0, 0});
        wrapper.BorderThickness({0, 0, 0, 0});

        Border background;
        background.CornerRadius({4, 4, 4, 4});

        auto compact = BuildCompactView();
        background.Child(compact);
        wrapper.Content(background);

        WireUpHover(wrapper, background);
        WireUpClickActions(wrapper);

        parent.Children().Append(wrapper);
        g_weatherRoot = compact;
        g_weatherRootParent = parent;
        g_weatherTaskbarWnd = (HWND)host->taskbarHwnd;

        wrapper.UpdateLayout();
        double desiredWidth = wrapper.ActualWidth();
        return desiredWidth > 0 ? desiredWidth : 80.0;
    } catch (...) {
        Wh_Log(L"WeatherWidget_Create: exception");
        return 0.0;
    }
}

extern "C" void __cdecl WeatherWidget_Tick(void* /*context*/) {
    // No-op: this mod drives its own refresh cadence via
    // WeatherThreadProc, independent of the host's shared tick signal
    // - same reasoning as media-player/system-usage.
}

extern "C" double __cdecl WeatherWidget_OnSettingsChanged(void* /*context*/) {
    return g_weatherRoot ? g_weatherRoot.ActualWidth() : 0.0;
}

extern "C" void __cdecl WeatherWidget_Destroy(void* /*context*/) {
    if (g_weatherFlyoutOpen && g_weatherFlyout) {
        try {
            g_weatherFlyout.Hide();
        } catch (...) {
        }
    }
    g_weatherRoot = nullptr;
    g_weatherRootParent = nullptr;
}

extern "C" void __cdecl WeatherWidget_GetId(void* /*context*/,
                                             wchar_t* buffer,
                                             int bufferSize) {
    wcsncpy_s(buffer, bufferSize, L"weather", _TRUNCATE);
}

extern "C" void __cdecl WeatherWidget_GetDisplayName(void* /*context*/,
                                                       wchar_t* buffer,
                                                       int bufferSize) {
    wcsncpy_s(buffer, bufferSize, L"Weather", _TRUNCATE);
}

void FillWeatherWidgetAbi(WidgetStackWidgetAbiV1& abi) {
    abi = {};
    abi.context = kWeatherWidgetContext;
    abi.Create = &WeatherWidget_Create;
    abi.Tick = &WeatherWidget_Tick;
    abi.OnSettingsChanged = &WeatherWidget_OnSettingsChanged;
    abi.Destroy = &WeatherWidget_Destroy;
    abi.GetId = &WeatherWidget_GetId;
    abi.GetDisplayName = &WeatherWidget_GetDisplayName;
}
```

- [ ] **Step 3: Manual verification (executed in Task 16)**

With `taskbar-widget-stack` installed and enabled, confirm the weather
widget appears inside its pane (not standalone) once Task 14 wires
registration up, sized reasonably, with working hover/click.

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add WidgetStackWidgetAbiV1 provider for the weather widget"
```

---

### Task 13: Standalone injection target - taskbar XAML root lookup

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: nothing new.
- Produces: globals `CTaskBand_ITaskListWndSite_vftable`, `CTaskBand_GetTaskbarHost_Original`, `TaskbarHost_FrameHeight_Original`, `Std_Ref_Decref_Original` (populated by symbol hooks Task 15 adds to `HookTaskbarDllSymbols`), `HRESULT TryGetTaskbarElementAbi(HWND, void**)`, `XamlRoot GetTaskbarXamlRoot(HWND)`, `FrameworkElement FindChildByName(FrameworkElement const&, std::wstring_view, int depth = 32)`, `Grid FindTaskbarRootGrid(FrameworkElement const&)`. Task 14's `InjectWeatherStandalone` calls `GetTaskbarXamlRoot` + `FindTaskbarRootGrid` to get its injection target.

This whole block is transcribed verbatim from
`taskbar-widget-system-usage.wh.cpp:196-341` (its own
`TryGetTaskbarElementAbi`/`GetTaskbarXamlRoot`/`FindChildByName`/
`FindTaskbarRootGrid`) rather than media-player's older, x64-only
equivalent - system-usage's version is both mods' current, ARM64EC-aware
implementation of the same undocumented-offset recovery technique
(reading `TaskbarHost::FrameHeight`'s own compiled prologue to recover
a struct offset nothing documents). Don't re-derive or simplify any of
this - it's already fragile, reverse-engineered code; a well-intentioned
"simplification" is how a working mod silently breaks on the next
Windows update.

- [ ] **Step 1: Implement `TryGetTaskbarElementAbi`, `GetTaskbarXamlRoot`, `FindChildByName`, `FindTaskbarRootGrid`**

Append (transcribed verbatim from
`taskbar-widget-system-usage.wh.cpp:196-341`):

```cpp
void* CTaskBand_ITaskListWndSite_vftable = nullptr;
using CTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void*, void*);
CTaskBand_GetTaskbarHost_t CTaskBand_GetTaskbarHost_Original = nullptr;
using TaskbarHost_FrameHeight_t = int(WINAPI*)(void*);
TaskbarHost_FrameHeight_t TaskbarHost_FrameHeight_Original = nullptr;
using Std_Ref_Decref_t = void(WINAPI*)(void*);
Std_Ref_Decref_t Std_Ref_Decref_Original = nullptr;

HRESULT TryGetTaskbarElementAbi(HWND hTaskbarWnd, void** result) {
    *result = nullptr;
    void* taskbarHostSharedPtr[2]{};

    auto cleanup = [&]() {
        if (taskbarHostSharedPtr[1] && Std_Ref_Decref_Original) {
            Std_Ref_Decref_Original(taskbarHostSharedPtr[1]);
        }
    };

    HWND hTaskSwWnd = (HWND)GetPropW(hTaskbarWnd, L"TaskbandHWND");
    if (!hTaskSwWnd) {
        return E_HANDLE;
    }

    void* taskBand = (void*)GetWindowLongPtrW(hTaskSwWnd, 0);
    if (!taskBand) {
        return E_POINTER;
    }

    if (!CTaskBand_ITaskListWndSite_vftable || !CTaskBand_GetTaskbarHost_Original) {
        return E_NOINTERFACE;
    }

    void* taskBandForTaskListWndSite = taskBand;
    for (int i = 0; *(void**)taskBandForTaskListWndSite !=
                    CTaskBand_ITaskListWndSite_vftable;
         i++) {
        if (i == 20) {
            return E_NOINTERFACE;
        }
        taskBandForTaskListWndSite = (void**)taskBandForTaskListWndSite + 1;
        if (!taskBandForTaskListWndSite) {
            return E_POINTER;
        }
    }

    CTaskBand_GetTaskbarHost_Original(taskBandForTaskListWndSite,
                                       taskbarHostSharedPtr);
    if (!taskbarHostSharedPtr[0]) {
        cleanup();
        return E_POINTER;
    }

    // TaskbarHost::FrameHeight's prologue moves `this + offset` into
    // rcx/x0 to reach the taskbar element pointer; the offset isn't a
    // stable, documented constant, so it's recovered by matching the
    // compiled function's own machine code. Checks both x64 and ARM64
    // prologue shapes at *runtime*, unconditionally - Explorer can run
    // as an ARM64EC process where x64-compiled code (this mod) and
    // native ARM64 system DLL code (taskbar.dll) coexist, so this
    // mod's own compile-time target says nothing about the target
    // function's architecture.
    size_t taskbarElementIUnknownOffset;
    {
        const BYTE* b = (const BYTE*)TaskbarHost_FrameHeight_Original;
        const DWORD* p = (const DWORD*)TaskbarHost_FrameHeight_Original;

        if (b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC && b[4] == 0x48 &&
            b[5] == 0x83 && b[6] == 0xC1 && b[7] <= 0x7F) {
            taskbarElementIUnknownOffset = b[7];
        } else if (p[0] == 0xD503237F && (p[1] & 0xFFC07FFF) == 0xA9807BFD &&
                   p[2] == 0x910003FD &&
                   (p[3] & 0xFFF00FE0) == 0xF8400C00) {
            taskbarElementIUnknownOffset = (p[3] >> 12) & 0xFF;
        } else {
            wchar_t hex[64] = {};
            for (int i = 0; i < 16; i++) {
                wchar_t byteStr[4];
                wsprintfW(byteStr, L"%02X ", b[i]);
                wcscat_s(hex, byteStr);
            }
            Wh_Log(L"Unsupported TaskbarHost::FrameHeight, bytes: %s", hex);
            cleanup();
            return E_NOINTERFACE;
        }
    }

    auto* taskbarElementIUnknown = *(IUnknown**)(
        (BYTE*)taskbarHostSharedPtr[0] + taskbarElementIUnknownOffset);
    if (!taskbarElementIUnknown) {
        cleanup();
        return E_POINTER;
    }

    HRESULT hr = taskbarElementIUnknown->QueryInterface(
        winrt::guid_of<winrt::Windows::Foundation::IInspectable>(), result);
    cleanup();
    return hr;
}

XamlRoot GetTaskbarXamlRoot(HWND hTaskbarWnd) {
    if (!CTaskBand_ITaskListWndSite_vftable || !CTaskBand_GetTaskbarHost_Original ||
        !TaskbarHost_FrameHeight_Original) {
        return nullptr;
    }

    void* taskbarElementAbi = nullptr;
    if (FAILED(TryGetTaskbarElementAbi(hTaskbarWnd, &taskbarElementAbi)) ||
        !taskbarElementAbi) {
        return nullptr;
    }

    FrameworkElement taskbarElement{nullptr};
    winrt::attach_abi(taskbarElement, taskbarElementAbi);
    return taskbarElement ? taskbarElement.XamlRoot() : nullptr;
}

FrameworkElement FindChildByName(FrameworkElement const& root,
                                  std::wstring_view name, int depth = 32) {
    if (!root || depth == 0) {
        return nullptr;
    }
    int n = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < n; ++i) {
        auto child = VisualTreeHelper::GetChild(root, i).try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        if (child.Name() == name) {
            return child;
        }
        if (auto found = FindChildByName(child, name, depth - 1)) {
            return found;
        }
    }
    return nullptr;
}

// The taskbar's root Grid (parent of both TaskbarFrameRepeater - pinned/
// running app icons and the Start button - and SystemTrayFrameGrid).
Grid FindTaskbarRootGrid(FrameworkElement const& root) {
    int count = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; i++) {
        auto c = VisualTreeHelper::GetChild(root, i).try_as<FrameworkElement>();
        if (c && winrt::get_class_name(c) == L"Taskbar.TaskbarFrame") {
            auto rootGrid = FindChildByName(c, L"RootGrid");
            return rootGrid ? rootGrid.try_as<Grid>() : nullptr;
        }
    }
    return nullptr;
}
```

`#include <winrt/Windows.UI.Xaml.Media.h>` (already added in Task 1)
covers `VisualTreeHelper`; no new includes needed.

- [ ] **Step 2: Manual verification (executed in Task 16)**

Once Task 15 wires the required symbol hooks into
`HookTaskbarDllSymbols`, call `GetTaskbarXamlRoot(hWnd)` for the real
taskbar window and confirm it returns a non-null `XamlRoot`, and that
`FindTaskbarRootGrid(xamlRoot.Content().try_as<FrameworkElement>())`
returns a non-null `Grid` - log both outcomes via `Wh_Log` temporarily
if needed to confirm on a real machine.

- [ ] **Step 3: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Port taskbar XAML root lookup for standalone injection (from system-usage)"
```

---

### Task 14: Standalone injection fallback + retry thread

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)

**Interfaces:**
- Consumes: `FillWeatherWidgetAbi`, `kRegisterWidgetPropName`, `kUnregisterWidgetPropName`, `WidgetStack_RegisterWidget_t`/`WidgetStack_UnregisterWidget_t` (Task 12), `BuildCompactView`, `WireUpHover`, `WireUpClickActions` (Tasks 6, 8, 9), `GetTaskbarXamlRoot`, `FindTaskbarRootGrid` (Task 13).
- Produces: `bool RunFromWindowThread(HWND, WindowThreadProc, void*)` (the real implementation of Task 6's forward declaration - copied verbatim from `taskbar-widget-media-player.wh.cpp:1991-2017`), `bool g_weatherRemoteRegistered`, `WidgetStack_RegisterWidget_t g_weatherHostRegisterFn`, `WidgetStack_UnregisterWidget_t g_weatherHostUnregisterFn`, `void TryRegisterOrShowStandalone(HWND hWnd)`, `DWORD WINAPI WeatherRetryRegisterThreadProc(LPVOID)`, `void StartWeatherRetryRegister()`, `void InjectWeatherStandalone(HWND hWnd)`. Task 15 wires `StartWeatherRetryRegister`/`TryRegisterOrShowStandalone` into the mod's entry points and the `TrayUI::StartTaskbar` hook.

Copy `WeatherRetryRegisterThreadProc`/`StartWeatherRetryRegister` from
`taskbar-widget-media-player.wh.cpp:7840-7881`
(`MPRetryRegisterThreadProc`/`StartMPRetryRegister`), renaming the `mp`
prefix to `weather`/`g_weather*` and swapping the inner call from
`TryRegisterOrApplySettings` to this task's own
`TryRegisterOrShowStandalone`.

- [ ] **Step 1: Implement `RunFromWindowThread`**

Append (transcribed verbatim from
`taskbar-widget-media-player.wh.cpp:1991-2017` - this exact function,
unchanged; its correctness already cost that mod an Incident to get
right, so it's reused byte-for-byte rather than re-derived):

```cpp
using WindowThreadProc = void(*)(void*);
static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param) {
    static const UINT kMsg = RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
    struct Payload { WindowThreadProc proc; void* param; };
    DWORD tid = GetWindowThreadProcessId(hWnd, nullptr);
    if (!tid) return false;
    if (tid == GetCurrentThreadId()) {
        proc(param);
        return true;
    }
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC,
        [](int code, WPARAM w, LPARAM l) CALLBACK -> LRESULT {
            if (code == HC_ACTION) {
                auto* cwp = reinterpret_cast<const CWPSTRUCT*>(l);
                static const UINT kM = RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
                if (cwp->message == kM) {
                    auto* p = reinterpret_cast<Payload*>(cwp->lParam);
                    p->proc(p->param);
                }
            }
            return CallNextHookEx(nullptr, code, w, l);
        }, nullptr, tid);
    if (!hook) return false;
    Payload pay{proc, param};
    SendMessageW(hWnd, kMsg, 0, reinterpret_cast<LPARAM>(&pay));
    UnhookWindowsHookEx(hook);
    return true;
}
```

- [ ] **Step 2: Implement standalone injection**

Append:

```cpp
HWND g_weatherStandaloneParentWnd = nullptr;

// v1 standalone placement: appends at the end of the taskbar's own
// RootGrid (Task 13's FindTaskbarRootGrid), i.e. the trailing edge,
// with no tracked-anchor positioning (Start/Search/Task View button
// tracking, configurable left/right edge, etc.) - that richer
// positioning system is `taskbar-widget-stack`'s and
// `taskbar-widget-system-usage`'s own ~150-line
// ResolveTrackingAnchor/UpdateTrackedPosition machinery, explicitly
// out of scope for this mod's first version per the design doc's
// non-goals (kept minimal since the stack-registered path - the
// common case once taskbar-widget-stack is installed - already gets
// full positioning for free from the host). Revisit as a fast-follow
// if standalone placement needs to be configurable.
void InjectWeatherStandalone(HWND hWnd) {
    auto xamlRoot = GetTaskbarXamlRoot(hWnd);
    if (!xamlRoot) {
        Wh_Log(L"InjectWeatherStandalone: could not get taskbar XAML root");
        return;
    }
    auto rootElement = xamlRoot.Content().try_as<FrameworkElement>();
    if (!rootElement) {
        Wh_Log(L"InjectWeatherStandalone: XAML root has no content");
        return;
    }
    Grid rootGrid = FindTaskbarRootGrid(rootElement);
    if (!rootGrid) {
        Wh_Log(L"InjectWeatherStandalone: RootGrid not found");
        return;
    }

    Button wrapper;
    wrapper.HorizontalAlignment(HorizontalAlignment::Left);
    wrapper.Padding({0, 0, 0, 0});
    wrapper.BorderThickness({0, 0, 0, 0});

    Border background;
    background.CornerRadius({4, 4, 4, 4});
    auto compact = BuildCompactView();
    background.Child(compact);
    wrapper.Content(background);

    WireUpHover(wrapper, background);
    WireUpClickActions(wrapper);

    rootGrid.Children().Append(wrapper);
    g_weatherRoot = compact;
    g_weatherRootParent = rootGrid;
    g_weatherTaskbarWnd = hWnd;
    g_weatherStandaloneParentWnd = hWnd;
}
```

- [ ] **Step 3: Implement `TryRegisterOrShowStandalone` and the retry thread**

Append:

```cpp
bool g_weatherRemoteRegistered = false;
HWND g_weatherRemoteHostHwnd = nullptr;
WidgetStack_RegisterWidget_t g_weatherHostRegisterFn = nullptr;
WidgetStack_UnregisterWidget_t g_weatherHostUnregisterFn = nullptr;

void TryRegisterOrShowStandalone(HWND hWnd) {
    if (g_weatherRemoteRegistered) {
        return;
    }
    auto registerFn = (WidgetStack_RegisterWidget_t)GetPropW(
        hWnd, kRegisterWidgetPropName);
    if (registerFn) {
        if (g_weatherRootParent) {
            WeatherWidget_Destroy(nullptr);  // tear down standalone first
        }
        WidgetStackWidgetAbiV1 abi;
        FillWeatherWidgetAbi(abi);
        if (registerFn(&abi)) {
            g_weatherRemoteRegistered = true;
            g_weatherRemoteHostHwnd = hWnd;
            g_weatherHostRegisterFn = registerFn;
            g_weatherHostUnregisterFn = (WidgetStack_UnregisterWidget_t)GetPropW(
                hWnd, kUnregisterWidgetPropName);
            Wh_Log(L"Registered with taskbar-widget-stack");
            return;
        }
        Wh_Log(L"WidgetStack_RegisterWidget failed, falling back to standalone");
    }
    if (!g_weatherRootParent) {
        InjectWeatherStandalone(hWnd);
    }
}

HANDLE g_weatherRetryThread = nullptr;
HANDLE g_weatherRetryEvent = nullptr;
std::mutex g_weatherRetryThreadMutex;
std::atomic<bool> g_weatherRetryStopRequested{false};

DWORD WINAPI WeatherRetryRegisterThreadProc(LPVOID) {
    for (int attempt = 0; attempt < 600; attempt++) {
        if (g_weatherRetryStopRequested.load(std::memory_order_acquire)) {
            return 0;
        }
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (tray) {
            RunFromWindowThread(tray, [](void* p) {
                TryRegisterOrShowStandalone((HWND)p);
            }, tray);
            if (g_weatherRemoteRegistered) {
                return 0;
            }
        }
        WaitForSingleObject(g_weatherRetryEvent, 500);
        ResetEvent(g_weatherRetryEvent);
    }
    Wh_Log(L"Giving up on taskbar-widget-stack registration retries, staying standalone");
    return 0;
}

void StartWeatherRetryRegister() {
    std::lock_guard<std::mutex> lock(g_weatherRetryThreadMutex);
    if (g_weatherRetryStopRequested.load(std::memory_order_acquire)) {
        return;
    }
    if (g_weatherRetryThread &&
        WaitForSingleObject(g_weatherRetryThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_weatherRetryThread);
        g_weatherRetryThread = nullptr;
    }
    if (!g_weatherRetryThread) {
        g_weatherRetryThread = CreateThread(
            nullptr, 0, WeatherRetryRegisterThreadProc, nullptr, 0, nullptr);
    } else if (g_weatherRetryEvent) {
        SetEvent(g_weatherRetryEvent);
    }
}
```

Note: `RunFromWindowThread`'s callback here is passed `tray` via
`param` (not a capture) because `WindowThreadProc` is a plain
capture-less function pointer - same constraint media-player's own
Incident 1 (PLAN.md) hit and fixed; this task's lambda already avoids
it by using the `param` slot instead of a capture.

- [ ] **Step 4: Manual verification (executed in Task 16)**

1. With `taskbar-widget-stack` **disabled**, confirm the weather widget
   still appears standalone at a reasonable taskbar position.
2. Enable `taskbar-widget-stack` while the weather mod is already
   running: confirm it detects the host within a few seconds (via the
   retry thread) and switches from standalone to registered, without a
   duplicate appearing.
3. Restart Explorer with both mods enabled: confirm the weather widget
   ends up registered into the stack every time across several
   restarts (this is the exact flakiness class fixed by media-player's
   own Incident 3 - confirm this mod doesn't regress it from day one).

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add standalone injection fallback with retry-thread host registration"
```

---

### Task 15: Wire up Wh_Mod* entry points

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` (append)
- Modify: `taskbar-widget-weather/PLAN.md` (append design-decision notes made during implementation, if any diverged from the spec)

**Interfaces:**
- Consumes: everything from Tasks 1-14.
- Produces: `BOOL Wh_ModInit()`, `void Wh_ModAfterInit()`, `void Wh_ModUninit()`, `void Wh_ModSettingsChanged()`, plus the `TrayUI::StartTaskbar` hook wiring (`HookTaskbarDllSymbols`, mirroring `taskbar-widget-media-player.wh.cpp:10420-10516`, extended with the four extra symbol hooks Task 13's `GetTaskbarXamlRoot` needs).

- [ ] **Step 1: Implement the `TrayUI::StartTaskbar` hook and `HookTaskbarDllSymbols`**

Append (the `TrayUI::StartTaskbar` hook mirrors
`taskbar-widget-media-player.wh.cpp:10419-10445`; the symbol table adds
Task 13's four extra symbols on top of `TrayUI::StartTaskbar` itself -
those four exact symbol strings copied verbatim from
`taskbar-widget-media-player.wh.cpp:10430-10442`, since a mistyped
mangled/demangled symbol name fails silently at `HookSymbols` time
rather than at compile time):

```cpp
using TrayUI_StartTaskbar_t = void(WINAPI*)(void*);
static TrayUI_StartTaskbar_t TrayUI_StartTaskbar_Original = nullptr;
static void WINAPI TrayUI_StartTaskbar_Hook(void* pThis) {
    TrayUI_StartTaskbar_Original(pThis);
    HWND hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hWnd) {
        return;
    }
    g_weatherRoot = nullptr;
    g_weatherRootParent = nullptr;
    g_weatherRemoteRegistered = false;
    g_weatherRemoteHostHwnd = nullptr;
    g_weatherHostRegisterFn = nullptr;
    g_weatherHostUnregisterFn = nullptr;
    g_weatherTaskbarWnd = hWnd;
    StartWeatherRetryRegister();
}

static bool HookTaskbarDllSymbols() {
    HMODULE h = LoadLibraryExW(L"taskbar.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        return false;
    }
    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {{LR"(public: virtual void __cdecl TrayUI::StartTaskbar(void))"},
         &TrayUI_StartTaskbar_Original,
         TrayUI_StartTaskbar_Hook},
        // The four below feed Task 13's GetTaskbarXamlRoot/
        // TryGetTaskbarElementAbi - no hook function, just resolving
        // each symbol's address into the matching Task 13 global.
        {{LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"},
         &CTaskBand_ITaskListWndSite_vftable},
        {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"},
         &CTaskBand_GetTaskbarHost_Original},
        {{LR"(public: int __cdecl TaskbarHost::FrameHeight(void)const )"},
         &TaskbarHost_FrameHeight_Original},
        {{LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"},
         &Std_Ref_Decref_Original},
    };
    return WindhawkUtils::HookSymbols(h, taskbarDllHooks,
                                       ARRAYSIZE(taskbarDllHooks));
}
```

- [ ] **Step 2: Implement `Wh_ModInit`/`Wh_ModAfterInit`/`Wh_ModUninit`/`Wh_ModSettingsChanged`**

Append:

```cpp
BOOL Wh_ModInit() {
    LoadSettings();
    g_weatherRetryStopRequested = false;
    g_weatherRetryEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_weatherRetryEvent) {
        return FALSE;
    }
    if (!HookTaskbarDllSymbols()) {
        Wh_Log(L"Wh_ModInit: HookTaskbarDllSymbols failed");
        CloseHandle(g_weatherRetryEvent);
        g_weatherRetryEvent = nullptr;
        return FALSE;
    }
    return TRUE;
}

void Wh_ModAfterInit() {
    StartWeatherThread();
    HWND hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (hWnd) {
        g_weatherTaskbarWnd = hWnd;
    }
    StartWeatherRetryRegister();
}

void Wh_ModUninit() {
    g_weatherRetryStopRequested = true;
    if (g_weatherRetryEvent) {
        SetEvent(g_weatherRetryEvent);
    }
    {
        std::lock_guard<std::mutex> lock(g_weatherRetryThreadMutex);
        if (g_weatherRetryThread) {
            WaitForSingleObject(g_weatherRetryThread, 3000);
            CloseHandle(g_weatherRetryThread);
            g_weatherRetryThread = nullptr;
        }
    }
    if (g_weatherRetryEvent) {
        CloseHandle(g_weatherRetryEvent);
        g_weatherRetryEvent = nullptr;
    }
    StopWeatherThread();

    if (g_weatherTaskbarWnd) {
        RunFromWindowThread(g_weatherTaskbarWnd, [](void*) {
            if (g_weatherRemoteRegistered && g_weatherHostUnregisterFn) {
                g_weatherHostUnregisterFn(kWeatherWidgetContext);
                g_weatherRemoteRegistered = false;
            } else {
                WeatherWidget_Destroy(nullptr);
                uint32_t index;
                if (g_weatherRootParent && g_weatherRoot &&
                    g_weatherRootParent.Children().IndexOf(g_weatherRoot, index)) {
                    g_weatherRootParent.Children().RemoveAt(index);
                }
            }
        }, nullptr);
    }
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    if (g_weatherTaskbarWnd) {
        RunFromWindowThread(g_weatherTaskbarWnd, [](void*) {
            RebuildCompactViewInPlace();
        }, nullptr);
    }
}
```

- [ ] **Step 3: Add the live-test checklist to `PLAN.md`**

Append to `taskbar-widget-weather/PLAN.md`, replacing the "See the
plan's Task 16" placeholder line from Task 1 Step 3 with the actual
checklist (copy of Task 16's steps below verbatim, so `PLAN.md` is
self-contained the same way every sibling mod's is).

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp taskbar-widget-weather/PLAN.md
git commit -m "Wire up Wh_ModInit/AfterInit/Uninit/SettingsChanged and the StartTaskbar hook"
```

---

### Task 16: Full live-test pass

**Files:**
- Modify: `taskbar-widget-weather/PLAN.md` (append results as a dated "Incident" entry, or "no issues found" if everything passes clean - matching every sibling mod's convention)

**Interfaces:**
- Consumes: the fully assembled mod from Tasks 1-15.
- Produces: nothing new - this is verification only.

- [ ] **Step 1: Compile via Windhawk**

Load `taskbar-widget-weather.wh.cpp` into the Windhawk app on a real
Windows machine and compile it. Fix any compile errors that surface
(none of this plan's code has been compiled yet, per the Global
Constraints note - expect at least minor WinRT API surface mismatches,
same as media-player's own Incident 1).

- [ ] **Step 2: Run the full checklist**

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

- [ ] **Step 3: Record results in PLAN.md**

Append a dated `## Incident 1: <summary>` entry (or, if everything
passed with no fixes needed, a short "no issues found on first
live-test pass" note) to `taskbar-widget-weather/PLAN.md`, matching the
exact incident-log format used throughout
`taskbar-widget-media-player/PLAN.md` and the other sibling mods -
symptom, root cause, fix, next retest.

- [ ] **Step 4: Update the repo README's "Not yet compiled/tested" note**

Modify `README.md`'s weather-mod row (added in Task 1 Step 4) to
reflect actual live-test status instead of the placeholder text, once
Step 2 passes clean.

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-weather/PLAN.md README.md
git commit -m "Record first live-test pass for taskbar-widget-weather"
```
