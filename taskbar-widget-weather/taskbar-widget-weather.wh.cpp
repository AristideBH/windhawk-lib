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
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;
namespace wuxs = winrt::Windows::UI::Xaml::Shapes;

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
