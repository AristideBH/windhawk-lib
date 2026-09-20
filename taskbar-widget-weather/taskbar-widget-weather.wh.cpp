// ==WindhawkMod==
// @id              taskbar-widget-weather
// @name            Taskbar Widget: Weather
// @description     Shows current weather + forecast in the taskbar. Registers into taskbar-widget-stack's pane if installed, falls back to standalone injection otherwise.
// @version         1.5
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
    $description: "'auto' uses Windows' own location service; 'manual' uses the city/lat-lon below."
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
- PanelSettings:
  - placementMode: screen
    $name: Placement mode
    $description: Choose whether the weather details panel opens near the widget, or at a fixed position on the screen.
    $options:
    - near: Near the widget
    - screen: Placement on the screen

  - PanelSettingsNear:
    - panelHorizontalOffsetNear: 0
      $name: Panel horizontal offset (right/left)
    - panelVerticalPlacementNear: top
      $name: Vertical placement
      $description: Whether the panel opens above or below the widget. Also controls the slide-in animation direction.
      $options:
      - top: Top
      - bottom: Bottom
    $name: Near the widget

  - PanelSettingsScreen:
    - panelHorizontalPlacement: right
      $name: Horizontal placement on the screen
      $options:
      - left: Left
      - center: Center
      - right: Right

    - panelHorizontalDistanceFromScreenEdge: 0
      $name: Distance from the right/left side of the screen

    - panelVerticalPlacement: bottom
      $name: Vertical placement on the screen
      $options:
      - top: Top
      - center: Center
      - bottom: Bottom

    - panelVerticalDistanceFromScreenEdge: 0
      $name: Distance from the bottom/top side of the screen

    - panelAnimation: auto
      $name: Appearance animation
      $options:
      - auto: Automatic
      - top: From top
      - bottom: From bottom
      - left: From left
      - right: From right
    $name: Placement on the screen
  $name: Panel placement
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

// winbase.h's `#define GetCurrentTime() GetTickCount()` collides with
// IStoryboard::GetCurrentTime in the WinRT Animation headers pulled in
// below - undef it first, same fix taskbar-widget-media-player.wh.cpp
// already needed for the same headers.
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;
using namespace winrt::Windows::UI::Xaml::Media::Animation;

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
    // Panel placement (details Flyout position - see ShowWeatherPanel)
    std::wstring panelPlacementMode = L"screen";  // near|screen
    int panelHorizontalOffsetNear = 0;
    std::wstring panelVerticalPlacementNear = L"top";     // top|bottom
    std::wstring panelHorizontalPlacement = L"right";     // left|center|right
    int panelHorizontalDistanceFromScreenEdge = 0;
    std::wstring panelVerticalPlacement = L"bottom";      // top|center|bottom
    int panelVerticalDistanceFromScreenEdge = 0;
    std::wstring panelAnimation = L"auto";  // auto|top|bottom|left|right
};

WeatherSettings g_settings;
std::mutex g_settingsMutex;

std::wstring GetStringSetting(PCWSTR name, PCWSTR fallback) {
    auto* value = Wh_GetStringSetting(name);
    std::wstring result = value ? value : fallback;
    if (value) {
        Wh_FreeStringSetting(value);
    }
    return result;
}

void LoadSettings() {
    // Built up locally (no synchronization needed - it's not shared yet)
    // and then swapped into g_settings under g_settingsMutex in a single
    // assignment, so background-thread/UI-render readers of g_settings
    // never observe a partially-updated struct and never wait on this
    // function's Wh_Get*Setting calls.
    WeatherSettings newSettings;
    newSettings.locationAuto =
        GetStringSetting(L"LocationSettings.mode", L"auto") == L"auto";
    newSettings.manualCity = GetStringSetting(L"LocationSettings.manualCity", L"");
    {
        auto* latStr = Wh_GetStringSetting(L"LocationSettings.manualLat");
        newSettings.manualLat = latStr ? wcstod(latStr, nullptr) : 0.0;
        if (latStr) {
            Wh_FreeStringSetting(latStr);
        }
        auto* lonStr = Wh_GetStringSetting(L"LocationSettings.manualLon");
        newSettings.manualLon = lonStr ? wcstod(lonStr, nullptr) : 0.0;
        if (lonStr) {
            Wh_FreeStringSetting(lonStr);
        }
    }
    newSettings.useFahrenheit =
        GetStringSetting(L"UnitSettings.temperature", L"celsius") == L"fahrenheit";
    newSettings.windSpeedUnit = GetStringSetting(L"UnitSettings.windSpeed", L"kmh");
    {
        // displayMode can be toggled at runtime (ToggleDisplayMode), which
        // persists via a separate value key (`displayModeRuntime`, not the
        // settings.yaml key) so a settings change doesn't blow away a
        // runtime toggle. Prefer that runtime value when present.
        // Wh_GetStringValue fills a caller-owned buffer (unlike
        // Wh_GetStringSetting, which allocates/returns a pointer to free
        // separately) and returns the number of characters written, or 0
        // if the value doesn't exist / didn't fit.
        wchar_t runtimeValueBuf[16] = {};
        size_t runtimeValueLen = Wh_GetStringValue(
            L"displayModeRuntime", runtimeValueBuf, ARRAYSIZE(runtimeValueBuf));
        if (runtimeValueLen > 0) {
            newSettings.displayMode = runtimeValueBuf;
        } else {
            newSettings.displayMode =
                GetStringSetting(L"DisplaySettings.displayMode", L"now");
        }
    }
    newSettings.iconStyle = GetStringSetting(L"DisplaySettings.iconStyle", L"colored");
    newSettings.forecastDaysInline =
        std::clamp((int)Wh_GetIntSetting(L"DisplaySettings.forecastDaysInline"), 2, 7);
    newSettings.forecastDaysPanel =
        std::clamp((int)Wh_GetIntSetting(L"DisplaySettings.forecastDaysPanel"), 3, 7);
    newSettings.refreshIntervalMinutes =
        std::max(1, (int)Wh_GetIntSetting(L"DisplaySettings.refreshIntervalMinutes"));
    newSettings.leftClick =
        ParseClickAction(GetStringSetting(L"ClickActionSettings.left", L"open_panel"));
    newSettings.rightClick =
        ParseClickAction(GetStringSetting(L"ClickActionSettings.right", L"none"));
    newSettings.doubleClick = ParseClickAction(
        GetStringSetting(L"ClickActionSettings.doubleClick", L"toggle_mode"));
    newSettings.wheelClick =
        ParseClickAction(GetStringSetting(L"ClickActionSettings.wheel", L"refresh"));
    newSettings.panelPlacementMode =
        GetStringSetting(L"PanelSettings.placementMode", L"screen");
    newSettings.panelHorizontalOffsetNear = (int)Wh_GetIntSetting(
        L"PanelSettings.PanelSettingsNear.panelHorizontalOffsetNear");
    newSettings.panelVerticalPlacementNear = GetStringSetting(
        L"PanelSettings.PanelSettingsNear.panelVerticalPlacementNear", L"top");
    newSettings.panelHorizontalPlacement = GetStringSetting(
        L"PanelSettings.PanelSettingsScreen.panelHorizontalPlacement", L"right");
    newSettings.panelHorizontalDistanceFromScreenEdge = (int)Wh_GetIntSetting(
        L"PanelSettings.PanelSettingsScreen.panelHorizontalDistanceFromScreenEdge");
    newSettings.panelVerticalPlacement = GetStringSetting(
        L"PanelSettings.PanelSettingsScreen.panelVerticalPlacement", L"bottom");
    newSettings.panelVerticalDistanceFromScreenEdge = (int)Wh_GetIntSetting(
        L"PanelSettings.PanelSettingsScreen.panelVerticalDistanceFromScreenEdge");
    newSettings.panelAnimation = GetStringSetting(
        L"PanelSettings.PanelSettingsScreen.panelAnimation", L"auto");

    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = newSettings;
}

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

// Called once per WeatherThreadProc loop iteration (i.e. roughly once per
// refresh cycle, not on every render), so location mode/city/lat/lon
// changes made via Wh_ModSettingsChanged take effect on the next wake
// instead of requiring an Explorer restart. Returns true if g_location
// now holds a valid position (either freshly resolved, or already valid
// from a previous call).
bool ResolveLocation() {
    bool locationAuto;
    double manualLat;
    double manualLon;
    std::wstring manualCity;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        locationAuto = g_settings.locationAuto;
        manualLat = g_settings.manualLat;
        manualLon = g_settings.manualLon;
        manualCity = g_settings.manualCity;
    }

    ResolvedLocation resolved;
    bool ok = false;
    if (locationAuto) {
        ok = GeolocateAuto(resolved);
    }
    if (!ok && manualLat != 0.0 && manualLon != 0.0) {
        resolved.lat = manualLat;
        resolved.lon = manualLon;
        resolved.valid = true;
        ok = true;
    }
    if (!ok && !manualCity.empty()) {
        ok = GeocodeCity(manualCity, resolved);
    }
    if (ok) {
        std::lock_guard<std::mutex> lock(g_locationMutex);
        g_location = resolved;
        return true;
    }
    std::lock_guard<std::mutex> lock(g_locationMutex);
    return g_location.valid;  // keep whatever we had, per design doc
}

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
    } catch (const winrt::hresult_error& ex) {
        // A bare `catch (...)` here used to swallow every detail,
        // making a live-tested "exception during fetch, no data ever"
        // report undiagnosable from the log alone - log the actual
        // HRESULT/message plus the URL attempted (lat/lon at 0,0 is a
        // strong signal location resolution silently failed upstream,
        // not that the fetch itself is broken).
        Wh_Log(L"FetchWeather: hresult_error 0x%08X: %s (lat=%.4f, lon=%.4f)",
               (unsigned int)ex.code().value, ex.message().c_str(), lat, lon);
        return false;
    } catch (const std::exception& ex) {
        wchar_t msgBuf[256];
        MultiByteToWideChar(CP_UTF8, 0, ex.what(), -1, msgBuf, ARRAYSIZE(msgBuf));
        Wh_Log(L"FetchWeather: std::exception: %s (lat=%.4f, lon=%.4f)", msgBuf,
               lat, lon);
        return false;
    } catch (...) {
        Wh_Log(L"FetchWeather: unknown exception (lat=%.4f, lon=%.4f)", lat, lon);
        return false;
    }
}

// Note: `wind_speed_unit=kmh` is always requested regardless of
// `g_settings.windSpeedUnit` - conversion to the display unit happens at
// render time via ConvertWindSpeedFromKmh/FormatWindSpeed, so a unit-setting
// change never needs a re-fetch, only a UI rebuild.

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
    wchar_t cacheBuf[256] = {};
    size_t cacheLen =
        Wh_GetStringValue(L"weatherCache", cacheBuf, ARRAYSIZE(cacheBuf));
    if (cacheLen == 0) {
        return false;
    }
    std::wstring cached = cacheBuf;
    int dayFlag = 0;
    swscanf_s(cached.c_str(), L"%lf|%lf|%d|%d|%lf|%lf|%lf|%lf", &out.currentTemp,
              &out.feelsLike, &out.wmoCode, &dayFlag,
              &out.humidityPercent, &out.windSpeed, &out.windDirectionDeg,
              &out.pressureHpa);
    out.isDay = (dayFlag != 0);
    out.hasData = true;
    out.daily.clear();  // forecast list re-populates on the first real fetch
    return true;
}

HANDLE g_weatherStopEvent = nullptr;
HANDLE g_weatherRefreshEvent = nullptr;
HANDLE g_weatherThread = nullptr;

void OnWeatherStateUpdated();

DWORD WINAPI WeatherThreadProc(LPVOID) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    {
        WeatherState cached;
        if (LoadWeatherCache(cached)) {
            std::lock_guard<std::mutex> lock(g_weatherMutex);
            g_weather = cached;
        }
    }

    HANDLE waitHandles[2] = {g_weatherStopEvent, g_weatherRefreshEvent};

    while (true) {
        // Re-checked every iteration (not latched) so a location-mode
        // switch or a manual city/lat/lon edit surfaces on the next wake
        // instead of requiring an Explorer restart - see ResolveLocation's
        // own comment.
        bool locationResolved = ResolveLocation();

        if (locationResolved) {
            ResolvedLocation loc;
            {
                std::lock_guard<std::mutex> lock(g_locationMutex);
                loc = g_location;
            }
            if (loc.valid) {
                int forecastDaysInline;
                int forecastDaysPanel;
                {
                    std::lock_guard<std::mutex> lock(g_settingsMutex);
                    forecastDaysInline = g_settings.forecastDaysInline;
                    forecastDaysPanel = g_settings.forecastDaysPanel;
                }
                int days = std::max(forecastDaysInline, forecastDaysPanel);
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

        int refreshIntervalMinutes;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            refreshIntervalMinutes = g_settings.refreshIntervalMinutes;
        }
        DWORD waitMs = (DWORD)refreshIntervalMinutes * 60 * 1000;
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
        DWORD waitResult = WaitForSingleObject(g_weatherThread, 5000);
        if (waitResult != WAIT_OBJECT_0) {
            // Thread is still running (likely blocked in a fetch with no
            // timeout). Do NOT close the thread or event handles here -
            // the still-running thread may pass g_weatherStopEvent /
            // g_weatherRefreshEvent to WaitForMultipleObjects again, and
            // closing them out from under it would turn that call into
            // an immediate WAIT_FAILED, which the loop treats like a
            // timeout - spinning forever with no way to signal it again.
            // Leaking these handles until the thread eventually exits (or
            // Explorer restarts) is the safer failure mode.
            Wh_Log(L"StopWeatherThread: thread did not exit within 5000ms; "
                   L"leaving thread/event handles open rather than risk "
                   L"closing handles still in use");
            return;
        }
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

// ConditionName - human-readable label for WMO code
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

// BuildNowView - compact view showing icon + temperature + condition
constexpr double kIconFontSize = 20;
constexpr double kTempFontSize = 12;    // matches media-player's title font size
constexpr double kConditionFontSize = 11;  // matches media-player's artist font size

Grid BuildNowView() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }
    bool useFahrenheit;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        useFahrenheit = g_settings.useFahrenheit;
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
            ? FormatTemperature(snapshot.currentTemp, useFahrenheit)
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

// Global state and view builders for in-place UI updates
FrameworkElement g_weatherRoot{nullptr};
Border g_weatherBackground{nullptr};
Button g_weatherWrapper{nullptr};
Panel g_weatherRootParent{nullptr};
HWND g_weatherTaskbarWnd = nullptr;

// Forward declaration of RunFromWindowThread (defined further below, near
// the standalone-injection code that owns the window-thread marshaling).
using WindowThreadProc = void(*)(void*);
static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param);

Grid BuildForecastView();

FrameworkElement BuildCompactView() {
    std::wstring displayMode;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        displayMode = g_settings.displayMode;
    }
    return displayMode == L"forecast" ? (FrameworkElement)BuildForecastView()
                                       : (FrameworkElement)BuildNowView();
}

// Replaces the compact view's content in place. `g_weatherBackground` is
// the Border that hosts the compact view as its single Child (set up at
// both widget-construction sites); swapping the Child directly avoids
// ever needing to locate the compact view inside the host panel's
// Children collection, which it is never a direct member of (only the
// outer wrapper Button is).
void RebuildCompactViewInPlace() {
    if (!g_weatherBackground) {
        return;
    }
    auto newView = BuildCompactView();
    g_weatherBackground.Child(newView);
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

// BuildForecastView - compact strip showing 3-7 days forecast
Grid BuildForecastView() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }
    int forecastDaysInline;
    bool useFahrenheit;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        forecastDaysInline = g_settings.forecastDaysInline;
        useFahrenheit = g_settings.useFahrenheit;
    }

    Grid root;
    root.VerticalAlignment(VerticalAlignment::Center);
    root.ColumnSpacing(8);

    int days = std::min((int)snapshot.daily.size(), forecastDaysInline);
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
        temp.Text(winrt::hstring(FormatTemperature(day.tempMax, useFahrenheit)));
        cell.Children().Append(temp);

        root.Children().Append(cell);
    }
    return root;
}

// ToggleDisplayMode - switch between now and forecast views
void ToggleDisplayMode() {
    std::wstring newMode;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        g_settings.displayMode =
            g_settings.displayMode == L"now" ? L"forecast" : L"now";
        newMode = g_settings.displayMode;
    }
    // Persisted to a runtime-value key (not the settings.yaml key) so
    // LoadSettings can prefer it over the configured default without a
    // settings change stomping the toggle - see LoadSettings.
    Wh_SetStringValue(L"displayModeRuntime", newMode.c_str());
    if (g_weatherTaskbarWnd) {
        RunFromWindowThread(g_weatherTaskbarWnd, [](void*) {
            RebuildCompactViewInPlace();
        }, nullptr);
    }
}

// Hover/pressed visual state helpers
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

// `background` is a full-bounds Border sitting behind the compact
// view's content (built by the caller - specifically so
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

void ShowWeatherPanel(FrameworkElement anchor);

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
            ClickAction leftClick;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                leftClick = g_settings.leftClick;
            }
            ExecuteClickAction(leftClick, sender.try_as<FrameworkElement>());
        });
    wrapper.DoubleTapped(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           winrt::Windows::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&) {
            ClickAction doubleClick;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                doubleClick = g_settings.doubleClick;
            }
            ExecuteClickAction(doubleClick, sender.try_as<FrameworkElement>());
        });
    wrapper.RightTapped(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           winrt::Windows::UI::Xaml::Input::RightTappedRoutedEventArgs const& args) {
            ClickAction rightClick;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                rightClick = g_settings.rightClick;
            }
            if (rightClick == ClickAction::None) {
                return;  // let it bubble to the stack's own menu
            }
            args.Handled(true);
            ExecuteClickAction(rightClick, sender.try_as<FrameworkElement>());
        });
    wrapper.PointerWheelChanged(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            ClickAction wheelClick;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                wheelClick = g_settings.wheelClick;
            }
            if (wheelClick == ClickAction::None) {
                return;
            }
            args.Handled(true);
            ExecuteClickAction(wheelClick, sender.try_as<FrameworkElement>());
        });
}

Grid BuildWeatherHeaderAndDetails() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }
    bool useFahrenheit;
    std::wstring windSpeedUnit;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        useFahrenheit = g_settings.useFahrenheit;
        windSpeedUnit = g_settings.windSpeedUnit;
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
            ? FormatTemperature(snapshot.currentTemp, useFahrenheit) +
                  L" - " + ConditionName(snapshot.wmoCode)
            : L"Loading..."));
    headerText.Children().Append(tempText);

    TextBlock feelsLikeText;
    feelsLikeText.FontSize(13);
    feelsLikeText.Opacity(0.7);
    feelsLikeText.Text(winrt::hstring(
        snapshot.hasData
            ? L"Feels like " +
                  FormatTemperature(snapshot.feelsLike, useFahrenheit)
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
            ? FormatWindSpeed(snapshot.windSpeed, windSpeedUnit) + L" " +
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

StackPanel BuildForecastListPanel() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }
    int forecastDaysPanel;
    bool useFahrenheit;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        forecastDaysPanel = g_settings.forecastDaysPanel;
        useFahrenheit = g_settings.useFahrenheit;
    }

    StackPanel list;
    list.Orientation(Orientation::Vertical);
    list.Margin({0, 12, 0, 0});
    list.Spacing(4);

    int days = std::min((int)snapshot.daily.size(), forecastDaysPanel);
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
            FormatTemperature(day.tempMax, useFahrenheit) + L" / " +
            FormatTemperature(day.tempMin, useFahrenheit)));
        Grid::SetColumn(range, 2);
        row.Children().Append(range);

        list.Children().Append(row);
    }
    return list;
}

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

Flyout g_weatherFlyout{nullptr};
bool g_weatherFlyoutOpen = false;

// ---------------------------------------------------------------------
// Panel placement (ported from taskbar-widget-media-player's
// MiniPlayerAnchor/GetTaskbarMonitorWorkAreaInfo/
// ComputeScreenPlacementAnchor - same math, renamed for this mod and
// using g_weatherTaskbarWnd instead of media-player's g_taskbarWnd).
// Lets the details panel open either near the widget or at a fixed
// screen position, per PanelSettings.
// ---------------------------------------------------------------------

struct WeatherPanelAnchor {
    winrt::Windows::Foundation::Point placementPoint{0.f, 0.f};
    Controls::Primitives::FlyoutPlacementMode placement =
        Controls::Primitives::FlyoutPlacementMode::Top;
    // animAxis/animSign are computed for parity with the reference
    // implementation but currently unused here - ShowWeatherPanel's
    // fade-in animation (opacity + fixed TranslateY(8)) doesn't vary
    // by placement side, unlike media-player's slide-in animation.
    int animAxis = 0;
    double animSign = 1.0;
};

// Gets the taskbar's monitor work area (screen pixels), the taskbar
// window's screen origin, and the XAML root's DPI scale, so a screen
// pixel rect can be converted into the XAML root's own coordinate
// space (what Flyout placement points are expressed in).
bool GetWeatherTaskbarMonitorWorkAreaInfo(FrameworkElement const& rootContent,
                                           RECT& outWorkAreaPx,
                                           POINT& outOriginPx,
                                           double& outScale) {
    if (!g_weatherTaskbarWnd || !rootContent) return false;

    HMONITOR mon = MonitorFromWindow(g_weatherTaskbarWnd, MONITOR_DEFAULTTONEAREST);
    if (!mon) return false;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(mon, &mi)) return false;
    outWorkAreaPx = mi.rcWork;

    POINT originPx{0, 0};
    if (!ClientToScreen(g_weatherTaskbarWnd, &originPx)) return false;
    outOriginPx = originPx;

    double scale = 1.0;
    try {
        auto xamlRoot = rootContent.XamlRoot();
        if (xamlRoot) scale = xamlRoot.RasterizationScale();
    } catch (...) {}
    if (scale <= 0.0) scale = 1.0;
    outScale = scale;

    return true;
}

// Computes the FlyoutPlacementMode + anchor point (in the XAML root's
// coordinate space) for "screen" placement mode, from
// PanelSettingsScreen (horizontal/vertical placement + distance from
// edge) and PanelSettingsScreen.panelAnimation.
bool ComputeWeatherScreenPlacementAnchor(FrameworkElement const& rootContent,
                                          WeatherPanelAnchor& outAnchor) {
    RECT workPx;
    POINT originPx;
    double scale;
    if (!GetWeatherTaskbarMonitorWorkAreaInfo(rootContent, workPx, originPx, scale)) {
        return false;
    }

    double left   = (workPx.left   - originPx.x) / scale;
    double top    = (workPx.top    - originPx.y) / scale;
    double right  = (workPx.right  - originPx.x) / scale;
    double bottom = (workPx.bottom - originPx.y) / scale;

    const std::wstring& hPlace = g_settings.panelHorizontalPlacement;
    const std::wstring& vPlace = g_settings.panelVerticalPlacement;
    int hDist = g_settings.panelHorizontalDistanceFromScreenEdge;
    int vDist = g_settings.panelVerticalDistanceFromScreenEdge;

    bool hLeft  = (hPlace == L"left");
    bool hRight = (hPlace == L"right");
    bool vTop   = (vPlace == L"top");
    bool vBottom = (vPlace == L"bottom");

    double anchorX;
    if (hLeft)       anchorX = left + hDist;
    else if (hRight) anchorX = right - hDist;
    else              anchorX = (left + right) / 2.0 + hDist;

    double anchorY;
    if (vTop)         anchorY = top + vDist;
    else if (vBottom) anchorY = bottom - vDist;
    else               anchorY = (top + bottom) / 2.0 + vDist;

    using FPM = Controls::Primitives::FlyoutPlacementMode;
    FPM placement = FPM::Top;
    int animAxis = 0;
    double animSign = 1.0;

    if (vBottom) {
        placement = hLeft ? FPM::TopEdgeAlignedLeft
                  : hRight ? FPM::TopEdgeAlignedRight
                  : FPM::Top;
        animAxis = 0;
        animSign = 1.0;
    } else if (vTop) {
        placement = hLeft ? FPM::BottomEdgeAlignedLeft
                  : hRight ? FPM::BottomEdgeAlignedRight
                  : FPM::Bottom;
        animAxis = 0;
        animSign = -1.0;
    } else {
        if (hLeft) {
            placement = FPM::Right;
            animAxis = 1;
            animSign = -1.0;
        } else if (hRight) {
            placement = FPM::Left;
            animAxis = 1;
            animSign = 1.0;
        } else {
            placement = FPM::Top;
            animAxis = 0;
            animSign = 1.0;
        }
    }

    if (g_settings.panelAnimation == L"top") {
        animAxis = 0; animSign = -1.0;
    } else if (g_settings.panelAnimation == L"bottom") {
        animAxis = 0; animSign = 1.0;
    } else if (g_settings.panelAnimation == L"left") {
        animAxis = 1; animSign = -1.0;
    } else if (g_settings.panelAnimation == L"right") {
        animAxis = 1; animSign = 1.0;
    }

    outAnchor.placementPoint = {(float)anchorX, (float)anchorY};
    outAnchor.placement = placement;
    outAnchor.animAxis = animAxis;
    outAnchor.animSign = animSign;
    return true;
}

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

    Style style{
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
        opacityAnim.Duration(
            winrt::Windows::UI::Xaml::DurationHelper::FromTimeSpan(
                std::chrono::milliseconds(150)));
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

    // Decide near-the-widget vs. fixed-screen-position placement from
    // PanelSettings.placementMode (ported from ShowMiniPlayerFlyout's
    // equivalent block). showAtElem is the XAML root's content element
    // (not the widget button itself) so the anchor point below - which
    // is expressed in that root's coordinate space - lines up with
    // where FlyoutShowOptions.Position() expects it.
    bool useScreenPlacement = (g_settings.panelPlacementMode == L"screen");
    Controls::Primitives::FlyoutPlacementMode placementMode =
        Controls::Primitives::FlyoutPlacementMode::Top;
    winrt::Windows::Foundation::Point anchorPoint{0.f, 0.f};
    FrameworkElement showAtElem = anchor;
    bool anchorComputed = false;

    try {
        auto xamlRoot = anchor.XamlRoot();
        if (xamlRoot) {
            auto rootContent = xamlRoot.Content().try_as<FrameworkElement>();
            if (rootContent) {
                showAtElem = rootContent;

                if (useScreenPlacement) {
                    WeatherPanelAnchor screenAnchor;
                    if (ComputeWeatherScreenPlacementAnchor(rootContent, screenAnchor)) {
                        anchorPoint = screenAnchor.placementPoint;
                        placementMode = screenAnchor.placement;
                        anchorComputed = true;
                    } else {
                        Wh_Log(L"ShowWeatherPanel: Failed to compute screen anchor, "
                               L"falling back to 'near the widget' placement");
                    }
                }

                if (!anchorComputed) {
                    // Near mode: anchor relative to the widget's own
                    // bounds (the `anchor` parameter), same as
                    // ShowMiniPlayerFlyout's near-mode branch uses its
                    // `target` parameter.
                    auto xform = anchor.TransformToVisual(rootContent);
                    auto pt = xform.TransformPoint({0.f, 0.f});
                    float cx = pt.X + (float)anchor.ActualWidth() * 0.5f +
                               (float)g_settings.panelHorizontalOffsetNear;

                    bool placeBelow = (g_settings.panelVerticalPlacementNear == L"bottom");
                    if (placeBelow) {
                        float ty = pt.Y + (float)anchor.ActualHeight();
                        anchorPoint = {cx, ty};
                        placementMode = Controls::Primitives::FlyoutPlacementMode::Bottom;
                    } else {
                        float ty = pt.Y;
                        anchorPoint = {cx, ty};
                        placementMode = Controls::Primitives::FlyoutPlacementMode::Top;
                    }
                }
            }
        }
    } catch (...) {
        Wh_Log(L"ShowWeatherPanel: Exception setting position");
    }

    try {
        // Without this, WinRT constrains the flyout to stay near its
        // anchor element's own bounds, which defeats screen-position
        // placement (whose anchor point can be far from `anchor`).
        // Matches ShowMiniPlayerFlyout's equivalent call.
        flyout.ShouldConstrainToRootBounds(false);
        flyout.Placement(placementMode);
    } catch (...) {}

    Controls::Primitives::FlyoutShowOptions opts;
    opts.Placement(placementMode);
    opts.Position(anchorPoint);

    flyout.ShowAt(showAtElem, opts);
}

// ---------------------------------------------------------------------
// Cross-mod widget ABI: lets taskbar-widget-stack host this widget
// inside its shared pane instead of standalone. The struct layout
// below must stay byte-identical (field order and types) to the copy
// in taskbar-widget-stack.wh.cpp - there's no shared header across
// these two DLLs, so this is a hand-synced ABI.
// ---------------------------------------------------------------------

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
        g_weatherBackground = background;
        g_weatherWrapper = wrapper;
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
    if (g_weatherWrapper) {
        return g_weatherWrapper.ActualWidth();
    }
    return g_weatherRoot ? g_weatherRoot.ActualWidth() : 0.0;
}

extern "C" void __cdecl WeatherWidget_Destroy(void* /*context*/) {
    if (g_weatherFlyoutOpen && g_weatherFlyout) {
        try {
            g_weatherFlyout.Hide();
        } catch (...) {
        }
    }
    // Incident: an unguarded exception here (e.g. a WinRT marshaling
    // hiccup during a rapid move/settings-change rebuild) used to
    // propagate out of RemoteWidget::Destroy(), which the stack's own
    // outer try/catch (taskbar-widget-stack.wh.cpp's RebuildStackContents
    // destroy loop) silently swallows - leaving g_weatherWrapper still
    // attached to the panel while the very next Create() call appends a
    // second one, producing a visible duplicate with no trace in
    // g_widgets (that side's dedup-by-context was never touched, since
    // this was never a second registration - it was a failed
    // de-registration). Matches media-player's MediaPlayer_Destroy,
    // which wraps the equivalent removal in try/catch for the same
    // reason.
    try {
        if (g_weatherRootParent && g_weatherWrapper) {
            uint32_t index;
            if (g_weatherRootParent.Children().IndexOf(g_weatherWrapper, index)) {
                g_weatherRootParent.Children().RemoveAt(index);
            }
        }
    } catch (...) {
        Wh_Log(L"WeatherWidget_Destroy: exception removing wrapper from parent");
    }
    g_weatherWrapper = nullptr;
    g_weatherBackground = nullptr;
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

// ---------------------------------------------------------------------
// Standalone injection fallback + retry thread. When taskbar-widget-stack
// isn't installed (or hasn't registered yet), this mod injects its own
// widget directly into the taskbar's RootGrid, then keeps retrying
// registration with the stack host in the background so it can switch
// over cleanly once the host becomes available.
// ---------------------------------------------------------------------

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

// v1 standalone placement: appends at the end of the taskbar's own
// RootGrid (via FindTaskbarRootGrid), i.e. the trailing edge,
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
    g_weatherBackground = background;
    g_weatherWrapper = wrapper;
    g_weatherRootParent = rootGrid;
    g_weatherTaskbarWnd = hWnd;
}

bool g_weatherRemoteRegistered = false;
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
            auto unregisterFn = (WidgetStack_UnregisterWidget_t)GetPropW(
                hWnd, kUnregisterWidgetPropName);
            if (!unregisterFn) {
                // Registered with the host but have no way to cleanly
                // unregister later - treat this the same as a failed
                // registration and fall through to standalone injection
                // rather than leaving a dangling remote registration.
                Wh_Log(L"WidgetStack_RegisterWidget succeeded but "
                       L"unregister prop is missing, falling back to "
                       L"standalone");
            } else {
                g_weatherRemoteRegistered = true;
                g_weatherHostUnregisterFn = unregisterFn;
                Wh_Log(L"Registered with taskbar-widget-stack");
                return;
            }
        } else {
            Wh_Log(L"WidgetStack_RegisterWidget failed, falling back to standalone");
        }
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

// ---------------------------------------------------------------------
// Mod entry points: TrayUI::StartTaskbar hook + Wh_Mod*
// ---------------------------------------------------------------------

using TrayUI_StartTaskbar_t = void(WINAPI*)(void*);
static TrayUI_StartTaskbar_t TrayUI_StartTaskbar_Original = nullptr;
static void WINAPI TrayUI_StartTaskbar_Hook(void* pThis) {
    TrayUI_StartTaskbar_Original(pThis);
    HWND hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hWnd) {
        return;
    }
    // Unlike media-player's equivalent hook, which just nulls its
    // bookkeeping on the assumption the old XamlRoot content (and
    // anything injected into it) dies together with the old taskbar,
    // this explicitly tears the previous instance down first -
    // StartTaskbar doesn't always mean the whole tree was destroyed
    // and recreated (a live-tested remnant: the old wrapper survived a
    // StartTaskbar call with its parent Panel still alive, so nulling
    // these pointers without detaching left it orphaned in a live tree
    // while a second one got created right next to it). Both branches
    // are try/catch-safe inside WeatherWidget_Destroy/the unregister
    // call, so this is a harmless no-op on the genuinely-dead-tree case
    // media-player optimizes for.
    if (g_weatherRemoteRegistered && g_weatherHostUnregisterFn) {
        try {
            g_weatherHostUnregisterFn(kWeatherWidgetContext);
        } catch (...) {
            Wh_Log(L"TrayUI_StartTaskbar_Hook: exception unregistering from host");
        }
    } else {
        WeatherWidget_Destroy(nullptr);
    }
    g_weatherRoot = nullptr;
    g_weatherBackground = nullptr;
    g_weatherWrapper = nullptr;
    g_weatherRootParent = nullptr;
    g_weatherRemoteRegistered = false;
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
        // The four below feed GetTaskbarXamlRoot/
        // TryGetTaskbarElementAbi - no hook function, just resolving
        // each symbol's address into the matching global.
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
    bool retryThreadStillRunning = false;
    {
        std::lock_guard<std::mutex> lock(g_weatherRetryThreadMutex);
        if (g_weatherRetryThread) {
            DWORD waitResult = WaitForSingleObject(g_weatherRetryThread, 3000);
            if (waitResult == WAIT_OBJECT_0) {
                CloseHandle(g_weatherRetryThread);
                g_weatherRetryThread = nullptr;
            } else {
                // Same reasoning as StopWeatherThread: the still-running
                // thread may still be using g_weatherRetryEvent (e.g.
                // blocked inside RunFromWindowThread's SendMessageW if the
                // taskbar thread is busy), so closing handles out from
                // under it is unsafe. Leak them until it eventually exits.
                retryThreadStillRunning = true;
                Wh_Log(L"Wh_ModUninit: retry thread did not exit within "
                       L"3000ms; leaving thread/event handles open rather "
                       L"than risk closing handles still in use");
            }
        }
    }
    if (!retryThreadStillRunning && g_weatherRetryEvent) {
        CloseHandle(g_weatherRetryEvent);
        g_weatherRetryEvent = nullptr;
    }
    StopWeatherThread();

    // Fall back to a fresh lookup rather than skipping teardown outright
    // when g_weatherTaskbarWnd was never set - it's only assigned from
    // Wh_ModAfterInit or TrayUI_StartTaskbar_Hook, so a mod disabled
    // before either had run (or a FindWindowW that returned null at
    // startup) used to leave the widget/registration completely torn
    // down, with no unregister call and no visual removal at all.
    HWND uninitTaskbarWnd = g_weatherTaskbarWnd;
    if (!uninitTaskbarWnd) {
        uninitTaskbarWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    }
    if (uninitTaskbarWnd) {
        RunFromWindowThread(uninitTaskbarWnd, [](void*) {
            if (g_weatherRemoteRegistered && g_weatherHostUnregisterFn) {
                g_weatherHostUnregisterFn(kWeatherWidgetContext);
                g_weatherRemoteRegistered = false;
            } else {
                // WeatherWidget_Destroy removes the wrapper Button from
                // g_weatherRootParent's Children itself (tracked via
                // g_weatherWrapper), so no separate removal is needed here.
                WeatherWidget_Destroy(nullptr);
            }
        }, nullptr);
    }
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    // Force the next WeatherThreadProc iteration to re-resolve location
    // (mode switch, or an edited manual city/lat/lon) instead of keeping
    // whatever was resolved before this settings change, then wake the
    // thread immediately rather than waiting for the next scheduled
    // refresh interval.
    {
        std::lock_guard<std::mutex> lock(g_locationMutex);
        g_location.valid = false;
    }
    RequestWeatherRefresh();
    if (g_weatherTaskbarWnd) {
        RunFromWindowThread(g_weatherTaskbarWnd, [](void*) {
            RebuildCompactViewInPlace();
        }, nullptr);
    }
}
