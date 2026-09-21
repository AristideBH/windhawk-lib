// ==WindhawkMod==
// @id              taskbar-widget-weather
// @name            Taskbar Widget: Weather
// @description     Shows current weather + forecast in the taskbar. Registers into taskbar-widget-stack's pane if installed, falls back to standalone injection otherwise.
// @version         1.22
// @author          Aristide
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32
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
  - displayMode: mixed
    $name: Compact display mode
    $options:
    - now: Now
    - forecast: Forecast strip
    - mixed: "Now + short forecast"
  - contentAlignment: left
    $name: Content alignment
    $description: Horizontal alignment of the compact widget's content within its pane.
    $options:
    - left: Left
    - center: Center
    - right: Right
  - iconStyle: colored
    $name: Icon style
    $description: Reserved for a future vector-icon set; has no effect while icons are emoji.
    $options:
    - colored: Colored
    - monochrome: Monochrome
  - forecastDaysInline: 3
    $name: Forecast days (compact strip)
    $description: 2-7 days shown when compact display mode is "Forecast strip".
  - forecastDaysMixed: 3
    $name: Forecast days (mixed view)
    $description: >-
      1-5 days shown next to "Now" when compact display mode is "Now +
      short forecast".
  - forecastDaysPanel: 5
    $name: Forecast days (panel)
    $description: 3-7 days shown in the details panel's forecast list.
  - forecastDateFormat: "{rel}"
    $name: Forecast day format
    $description: >-
      How each day is labeled in the panel's forecast list. Tokens:
      {rel} (Today/Tomorrow/After tomorrow, then falls back to
      {weekday_short}), {weekday}/{weekday_short} (Wednesday/Wed),
      {month}/{month_short} (September/Sep), {day} (24), {year} (2026).
      Anything else in the field is kept as-is, so e.g.
      "{weekday_short}, {month_short} {day}" gives "Wed, Sep 24".
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
#include <winrt/Windows.Web.Http.Filters.h>
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
    std::wstring displayMode = L"mixed";  // now|forecast|mixed (persisted, mutated at runtime)
    std::wstring contentAlignment = L"left";  // left|center|right
    std::wstring iconStyle = L"colored";  // colored|monochrome (reserved, no-op)
    int forecastDaysInline = 3;
    int forecastDaysMixed = 3;
    int forecastDaysPanel = 5;
    std::wstring forecastDateFormat = L"{rel}";
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
                GetStringSetting(L"DisplaySettings.displayMode", L"mixed");
        }
    }
    newSettings.contentAlignment =
        GetStringSetting(L"DisplaySettings.contentAlignment", L"left");
    newSettings.iconStyle = GetStringSetting(L"DisplaySettings.iconStyle", L"colored");
    newSettings.forecastDaysInline =
        std::clamp((int)Wh_GetIntSetting(L"DisplaySettings.forecastDaysInline"), 2, 7);
    newSettings.forecastDaysMixed =
        std::clamp((int)Wh_GetIntSetting(L"DisplaySettings.forecastDaysMixed"), 1, 5);
    newSettings.forecastDaysPanel =
        std::clamp((int)Wh_GetIntSetting(L"DisplaySettings.forecastDaysPanel"), 3, 7);
    newSettings.forecastDateFormat =
        GetStringSetting(L"DisplaySettings.forecastDateFormat", L"{rel}");
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
    std::wstring locationName;  // set from ResolvedLocation, not re-derived
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

// Zeller's congruence (Gregorian) - day-of-week from a Y-M-D date, with
// no calendar library dependency. Returns 0=Sunday..6=Saturday.
int ComputeWeekday(int year, int month, int day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        year -= 1;
    }
    int h = (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
    return ((h % 7) + 7) % 7;
}

// Forecast panel's own tiny format-string language (DisplaySettings.
// forecastDateFormat) - tokens are replaced literally, anything else
// (spaces, commas, ...) passes through unchanged. `{rel}` is the one
// token with real logic behind it: "Today"/"Tomorrow"/"After tomorrow"
// for the first three days (dayIndex is the same index used to read
// `WeatherState::daily`, where index 0 is always today per Open-Meteo's
// own daily API convention), falling back to the short weekday name
// beyond that - so a format of just "{rel}" alone is already a complete,
// sensible format on its own, not just a building block. The other
// tokens (weekday/weekday_short/month/month_short/day/year) are there
// for building a fully custom format, e.g. "{weekday_short}, {month_short}
// {day}", with or without combining them alongside `{rel}`.
std::wstring FormatForecastDate(const std::wstring& isoDate, int dayIndex,
                                 const std::wstring& fmt) {
    static const wchar_t* kWeekdayFull[7] = {
        L"Sunday",   L"Monday", L"Tuesday", L"Wednesday",
        L"Thursday", L"Friday", L"Saturday"};
    static const wchar_t* kWeekdayShort[7] = {L"Sun", L"Mon", L"Tue", L"Wed",
                                               L"Thu", L"Fri", L"Sat"};
    static const wchar_t* kMonthFull[12] = {
        L"January", L"February", L"March",     L"April",
        L"May",     L"June",     L"July",      L"August",
        L"September", L"October", L"November", L"December"};
    static const wchar_t* kMonthShort[12] = {L"Jan", L"Feb", L"Mar", L"Apr",
                                              L"May", L"Jun", L"Jul", L"Aug",
                                              L"Sep", L"Oct", L"Nov", L"Dec"};

    int y = 0, m = 0, d = 0;
    if (swscanf_s(isoDate.c_str(), L"%d-%d-%d", &y, &m, &d) != 3 || m < 1 ||
        m > 12 || d < 1 || d > 31) {
        return isoDate;  // malformed - show the raw string rather than guess
    }
    int weekday = ComputeWeekday(y, m, d);

    std::wstring result;
    result.reserve(fmt.size());
    size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] != L'{') {
            result += fmt[i];
            i++;
            continue;
        }
        size_t close = fmt.find(L'}', i);
        if (close == std::wstring::npos) {
            result += fmt.substr(i);  // unterminated token - pass through
            break;
        }
        std::wstring token = fmt.substr(i + 1, close - i - 1);
        if (token == L"rel") {
            if (dayIndex == 0) result += L"Today";
            else if (dayIndex == 1) result += L"Tomorrow";
            else if (dayIndex == 2) result += L"After tomorrow";
            else result += kWeekdayShort[weekday];
        } else if (token == L"weekday") {
            result += kWeekdayFull[weekday];
        } else if (token == L"weekday_short") {
            result += kWeekdayShort[weekday];
        } else if (token == L"month") {
            result += kMonthFull[m - 1];
        } else if (token == L"month_short") {
            result += kMonthShort[m - 1];
        } else if (token == L"day") {
            result += std::to_wstring(d);
        } else if (token == L"year") {
            result += std::to_wstring(y);
        } else {
            result += L'{';
            result += token;
            result += L'}';  // unknown token - pass through literally
        }
        i = close + 1;
    }
    return result;
}

struct ResolvedLocation {
    double lat = 0.0;
    double lon = 0.0;
    bool valid = false;
    std::wstring name;  // e.g. "Paris, France" - best-effort, may be empty
};

ResolvedLocation g_location;
std::mutex g_locationMutex;

// Both Open-Meteo endpoints sit behind Cloudflare, which by default
// compresses responses (commonly Brotli). WinRT HttpClient's automatic
// decompression doesn't reliably handle every encoding a CDN might
// pick, and a decode failure there surfaces as a bare
// winrt::hresult_error 0x80072F8F (WinINet's
// ERROR_INTERNET_DECODING_FAILED) with no indication it's a
// compression problem rather than a real network failure - live-tested
// on a real fetch (lat=48.8626, lon=2.4814) before this fix. These
// responses are small JSON anyway, so there's no real cost to just
// disabling automatic decompression and letting the server send plain
// text instead of debugging which encoding the decoder doesn't like.
winrt::Windows::Web::Http::HttpClient CreateNoCompressionHttpClient() {
    winrt::Windows::Web::Http::Filters::HttpBaseProtocolFilter filter;
    filter.AutomaticDecompression(false);
    return winrt::Windows::Web::Http::HttpClient(filter);
}

// Blocking - call only from a background thread. Returns false without
// touching `out` if the city can't be resolved (network error, no
// match) so callers can keep the previous known-good location instead
// of blanking it (design doc: "Manual city fails to geocode").
bool GeocodeCity(const std::wstring& city, ResolvedLocation& out) {
    if (city.empty()) {
        return false;
    }
    try {
        auto client = CreateNoCompressionHttpClient();
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
        // Name comes free with this same response - no extra call needed,
        // unlike the GPS/lat-lon paths (see ReverseGeocodeLocation).
        std::wstring name;
        if (first.HasKey(L"name")) {
            name = first.GetNamedString(L"name").c_str();
        }
        if (first.HasKey(L"country") && !name.empty()) {
            name += L", " + std::wstring(first.GetNamedString(L"country").c_str());
        }
        out.name = name;
        return true;
    } catch (...) {
        Wh_Log(L"GeocodeCity: exception resolving '%s'", city.c_str());
        return false;
    }
}

// Blocking - call only from a background thread. Best-effort: a failure
// here leaves `out.name` untouched (caller already has valid
// coordinates from GeolocateAuto/manual lat-lon by the time this runs,
// so a missing name is cosmetic, not a reason to fail the whole
// resolution). Uses BigDataCloud's free reverse-geocode-client endpoint
// (no API key, same client pattern as GeocodeCity) since Open-Meteo's
// own geocoding API is forward-only (name -> coordinates).
bool ReverseGeocodeLocation(double lat, double lon, std::wstring& outName) {
    try {
        auto client = CreateNoCompressionHttpClient();
        wchar_t urlBuf[256];
        swprintf_s(urlBuf,
            L"https://api.bigdatacloud.net/data/reverse-geocode-client"
            L"?latitude=%.4f&longitude=%.4f&localityLanguage=en",
            lat, lon);
        auto response =
            client.GetAsync(winrt::Windows::Foundation::Uri(urlBuf)).get();
        response.EnsureSuccessStatusCode();
        auto body = response.Content().ReadAsStringAsync().get();
        auto json = winrt::Windows::Data::Json::JsonObject::Parse(body);
        std::wstring city;
        if (json.HasKey(L"city")) {
            city = json.GetNamedString(L"city").c_str();
        }
        if (city.empty() && json.HasKey(L"locality")) {
            city = json.GetNamedString(L"locality").c_str();
        }
        if (city.empty()) {
            return false;
        }
        std::wstring name = city;
        if (json.HasKey(L"countryName")) {
            name += L", " + std::wstring(json.GetNamedString(L"countryName").c_str());
        }
        outName = name;
        return true;
    } catch (...) {
        Wh_Log(L"ReverseGeocodeLocation: exception resolving (lat=%.4f, lon=%.4f)",
               lat, lon);
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
    // GeocodeCity already fills `name` from the same response; the GPS
    // and manual-lat/lon paths above only have coordinates, so look the
    // name up separately - best-effort, a failure here doesn't fail
    // `ok` (see ReverseGeocodeLocation's own comment).
    if (ok && resolved.name.empty()) {
        ReverseGeocodeLocation(resolved.lat, resolved.lon, resolved.name);
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
        auto client = CreateNoCompressionHttpClient();
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
                    fetched.locationName = loc.name;
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
    // A bit of breathing room around the compact content - it used to
    // sit flush against the pane's own edges (live feedback, 2026-09-21).
    root.Padding({6, 2, 6, 2});

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
    // Unlike the panel/forecast views (which pair the number with a
    // "Feels like"/range/label giving it context), the compact "Now"
    // view is just a bare number - without the unit letter it reads
    // ambiguously at a glance (live feedback, 2026-09-21).
    tempText.Text(winrt::hstring(
        snapshot.hasData
            ? FormatTemperature(snapshot.currentTemp, useFahrenheit) +
                  (useFahrenheit ? L"F" : L"C")
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
FrameworkElement g_weatherWrapper{nullptr};
Panel g_weatherRootParent{nullptr};
HWND g_weatherTaskbarWnd = nullptr;
// Declared here (moved up from its own Flyout-related block below) so
// WireUpHover can read it - the compact widget's hover visual should
// stay "on" for as long as the details panel triggered by it is open,
// not just while the pointer happens to still be over the widget (live
// feedback, 2026-09-21: clicking to open the panel, then moving the
// mouse into the panel itself, dropped the widget back to its idle
// look, which read as broken/flickery).
bool g_weatherFlyoutOpen = false;

// Forward declaration of RunFromWindowThread (defined further below, near
// the standalone-injection code that owns the window-thread marshaling).
using WindowThreadProc = void(*)(void*);
static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param);

Grid BuildForecastView();
Grid BuildMixedView();

FrameworkElement BuildCompactView() {
    std::wstring displayMode;
    std::wstring contentAlignment;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        displayMode = g_settings.displayMode;
        contentAlignment = g_settings.contentAlignment;
    }
    FrameworkElement view{nullptr};
    if (displayMode == L"forecast") {
        view = BuildForecastView();
    } else if (displayMode == L"mixed") {
        view = BuildMixedView();
    } else {
        view = BuildNowView();
    }
    // All three views are Auto-sized internally (no Star columns), so
    // setting HorizontalAlignment here - rather than on `wrapper`/
    // `background`, both of which stay Stretch to keep the hover
    // surface covering the whole pane - is what actually moves the
    // visible content left/center/right within it.
    if (contentAlignment == L"center") {
        view.HorizontalAlignment(HorizontalAlignment::Center);
    } else if (contentAlignment == L"right") {
        view.HorizontalAlignment(HorizontalAlignment::Right);
    } else {
        view.HorizontalAlignment(HorizontalAlignment::Left);
    }
    return view;
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

// BuildMixedView - "now" (icon + temp/condition, reusing BuildNowView's own
// layout) plus a short same-line forecast strip for the next few days
// (icon + high temp only, same cell style as BuildForecastView's, just
// capped lower) - the default compact display mode (2026-09-21). Day
// count is DisplaySettings.forecastDaysMixed (1-5, added 2026-09-21 -
// was a hardcoded constant at first).

Grid BuildMixedView() {
    WeatherState snapshot;
    {
        std::lock_guard<std::mutex> lock(g_weatherMutex);
        snapshot = g_weather;
    }
    bool useFahrenheit;
    int forecastDaysMixed;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        useFahrenheit = g_settings.useFahrenheit;
        forecastDaysMixed = g_settings.forecastDaysMixed;
    }

    Grid root;
    root.VerticalAlignment(VerticalAlignment::Center);
    root.ColumnSpacing(10);
    root.Padding({6, 2, 6, 2});
    root.ColumnDefinitions().Append(ColumnDefinition{});
    root.ColumnDefinitions().GetAt(0).Width({1.0, GridUnitType::Auto});

    // BuildNowView already applies its own {6,2,6,2} padding for when it's
    // used standalone - reset it here since this view's own root already
    // owns the one outer padding for the whole mixed layout.
    auto nowBlock = BuildNowView();
    nowBlock.Padding({0, 0, 0, 0});
    Grid::SetColumn(nowBlock, 0);
    root.Children().Append(nowBlock);

    int days = std::min((int)snapshot.daily.size(), forecastDaysMixed);
    if (days > 0) {
        root.ColumnDefinitions().Append(ColumnDefinition{});
        root.ColumnDefinitions().GetAt(1).Width({1.0, GridUnitType::Auto});
        Border separator;
        separator.Width(1);
        separator.Opacity(0.3);
        separator.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF, 0xFF, 0xFF)});
        separator.Margin({0, 4, 0, 4});
        Grid::SetColumn(separator, 1);
        root.Children().Append(separator);

        root.ColumnDefinitions().Append(ColumnDefinition{});
        root.ColumnDefinitions().GetAt(2).Width({1.0, GridUnitType::Auto});
        Grid forecastStrip;
        forecastStrip.VerticalAlignment(VerticalAlignment::Center);
        forecastStrip.ColumnSpacing(8);
        Grid::SetColumn(forecastStrip, 2);
        for (int i = 0; i < days; i++) {
            forecastStrip.ColumnDefinitions().Append(ColumnDefinition{});
            const auto& day = snapshot.daily[i];

            StackPanel cell;
            cell.Orientation(Orientation::Vertical);
            cell.HorizontalAlignment(HorizontalAlignment::Center);
            Grid::SetColumn(cell, i);

            TextBlock icon;
            icon.FontSize(kIconFontSize * 0.6);
            icon.HorizontalAlignment(HorizontalAlignment::Center);
            icon.Text(winrt::hstring(GetWeatherIcon(day.wmoCode, day.isDay).glyph));
            cell.Children().Append(icon);

            TextBlock temp;
            temp.FontSize(kConditionFontSize);
            temp.HorizontalAlignment(HorizontalAlignment::Center);
            temp.Text(winrt::hstring(FormatTemperature(day.tempMax, useFahrenheit)));
            cell.Children().Append(temp);

            forecastStrip.Children().Append(cell);
        }
        root.Children().Append(forecastStrip);
    }

    return root;
}

// ToggleDisplayMode - cycles now -> forecast -> mixed -> now
void ToggleDisplayMode() {
    std::wstring newMode;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        if (g_settings.displayMode == L"now") {
            g_settings.displayMode = L"forecast";
        } else if (g_settings.displayMode == L"forecast") {
            g_settings.displayMode = L"mixed";
        } else {
            g_settings.displayMode = L"now";
        }
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
winrt::Windows::UI::Xaml::Media::Brush g_weatherPressedBorderBrush{nullptr};

// Fill: subtle white overlay, same alpha range as media-player's own
// hover brushes (0x0F-0x2C over white) - not a full system-hover-color
// read like media-player's EnsureHoverBrushes does (that reads live
// Fluent Reveal colors via a Windows API media-player already hooks;
// duplicating that hook here for a first version isn't worth the risk
// of getting the undocumented call wrong).
//
// Border: idle has none; hover gets a top-lighter/bottom-darker white
// gradient (media-player's own "elevation" border - MakeElevationBorderBrush,
// same 0x28/0x0A alpha stops); pressed flattens to a single solid
// color, same as media-player's own pressed-state border. Live feedback
// (2026-09-21) specifically asked for this gradient border - a flat
// hover fill alone read visually different from media-player's own
// hover surface side-by-side.
void EnsureHoverBrushes() {
    if (!g_weatherHoverBrush) {
        // 0x14 = ~8% opacity (live feedback, 2026-09-21). Went 0x14 ->
        // 0x54 (33%) -> 0x14 -> 0x0A (4%) -> back to 0x14 the same day.
        g_weatherHoverBrush = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0x14, 0xFF, 0xFF, 0xFF)};
    }
    if (!g_weatherPressedBrush) {
        g_weatherPressedBrush = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0x28, 0xFF, 0xFF, 0xFF)};
    }
    if (!g_weatherPressedBorderBrush) {
        g_weatherPressedBorderBrush = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0x0A, 0xFF, 0xFF, 0xFF)};
    }
}

// Builds the top/bottom gradient border brush at a given alpha scale
// (1.0 = full 0x28/0x0A stops, 0.0 = fully transparent but still the
// same gradient shape/type). Idle and hover both go through this now
// (2026-09-21, live feedback: hovering visibly shifted content by a
// pixel or two) - BorderThickness used to switch 0<->1 between idle and
// hover, which changes `background`'s inner content area size since
// nothing compensated for it. BorderThickness now stays permanently
// {1,1,1,1}; only the brush's alpha changes, so the border's own
// footprint - and therefore the content inside it - never moves.
// Building the idle brush from the same LinearGradientBrush shape
// (rather than a flat transparent SolidColorBrush) keeps both ends of
// that transition the same brush type, matching media-player's own
// pattern of using one brush shape across a state's Normal/PointerOver
// range rather than swapping brush types per state.
winrt::Windows::UI::Xaml::Media::Brush MakeWeatherHoverBorderBrush(double alphaScale) {
    try {
        winrt::Windows::UI::Xaml::Media::LinearGradientBrush brush;
        brush.StartPoint(winrt::Windows::Foundation::Point(0.5f, 0.0f));
        brush.EndPoint(winrt::Windows::Foundation::Point(0.5f, 1.0f));
        winrt::Windows::UI::Xaml::Media::GradientStop top, bottom;
        top.Color(winrt::Windows::UI::ColorHelper::FromArgb(
            (BYTE)std::lround(0x28 * alphaScale), 0xFF, 0xFF, 0xFF));
        top.Offset(0.0);
        bottom.Color(winrt::Windows::UI::ColorHelper::FromArgb(
            (BYTE)std::lround(0x0A * alphaScale), 0xFF, 0xFF, 0xFF));
        bottom.Offset(1.0);
        brush.GradientStops().Append(top);
        brush.GradientStops().Append(bottom);
        return brush;
    } catch (...) {
        return SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(
            (BYTE)std::lround(0x28 * alphaScale), 0xFF, 0xFF, 0xFF)};
    }
}

void ApplyWeatherHoverState(Border background, bool hovered, bool pressed) {
    EnsureHoverBrushes();
    background.BorderThickness({1, 1, 1, 1});
    if (pressed) {
        background.Background(g_weatherPressedBrush);
        background.BorderBrush(g_weatherPressedBorderBrush);
    } else if (hovered) {
        background.Background(g_weatherHoverBrush);
        background.BorderBrush(MakeWeatherHoverBorderBrush(1.0));
    } else {
        background.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
        background.BorderBrush(MakeWeatherHoverBorderBrush(0.0));
    }
}

// Tracks actual pointer state separately from the "should the widget
// look hovered" question `RefreshWeatherWidgetHoverVisual` answers
// (real pointer hover OR the details panel being open) - shared_ptrs so
// both WireUpHover's own pointer-event lambdas and the panel's
// open/close handlers (in ShowWeatherPanel, wired much later but
// against the same widget instance) can read and update the same
// state.
auto g_weatherPointerHovered = std::make_shared<bool>(false);
auto g_weatherPointerPressed = std::make_shared<bool>(false);

void RefreshWeatherWidgetHoverVisual() {
    if (!g_weatherBackground) {
        return;
    }
    // While the details panel is open, the widget should look exactly
    // like plain hover - never pressed (2026-09-21). Opening the Flyout
    // shifts pointer capture away from the widget, which isn't
    // guaranteed to deliver a matching PointerReleased first - without
    // this, a stuck `*g_weatherPointerPressed == true` would render the
    // darker, flat pressed style for as long as the panel stayed open
    // instead of the intended hover look.
    bool hovered = *g_weatherPointerHovered || g_weatherFlyoutOpen;
    bool pressed = *g_weatherPointerPressed && !g_weatherFlyoutOpen;
    ApplyWeatherHoverState(g_weatherBackground, hovered, pressed);
}

// `background` is a full-bounds Border sitting behind the compact
// view's content (built by the caller - specifically so
// this function never needs to know whether it's wiring a stack-hosted
// or standalone wrapper). `wrapper` is the outer interactive element
// pointer events are attached to.
void WireUpHover(FrameworkElement wrapper, Border background) {
    auto hovered = g_weatherPointerHovered;
    auto pressed = g_weatherPointerPressed;

    wrapper.PointerEntered(
        [hovered, pressed](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *hovered = true;
            RefreshWeatherWidgetHoverVisual();
        });
    wrapper.PointerExited(
        [hovered, pressed](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *hovered = false;
            *pressed = false;
            RefreshWeatherWidgetHoverVisual();
        });
    wrapper.PointerPressed(
        [hovered, pressed](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *pressed = true;
            RefreshWeatherWidgetHoverVisual();
        });
    wrapper.PointerReleased(
        [hovered, pressed](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *pressed = false;
            RefreshWeatherWidgetHoverVisual();
        });
    // Reset explicitly rather than assuming a fresh false: `hovered`/
    // `pressed` alias the persistent globals above, which survive a
    // widget Destroy()/Create() cycle (stack reorder, settings change,
    // re-registration) - without this, a widget destroyed mid-hover
    // would have its new instance start from a stale `true`.
    *hovered = false;
    *pressed = false;
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

    // The overall panel has no background of its own (live feedback,
    // 2026-09-21 - relies on the Flyout's own default presenter chrome,
    // not a nested double background) - but the header block
    // specifically got its background back after a second round of
    // feedback the same day: a plain flat header on the panel's own
    // (nonexistent) background read as un-anchored, so `header` (below)
    // now carries its own surface fill, scoped to just that block - the
    // details row and forecast list underneath stay background-free.
    // No padding of its own - BuildWeatherFlyoutContent's outer panelBg
    // Border now owns the one 16px inset from the panel's own edge, for
    // this and every other section alike (2026-09-21). This used to also
    // carry that same 16px padding, double-padding the header/details
    // area relative to the forecast list and Refresh button below it.
    Grid card;
    card.CornerRadius({6, 6, 6, 6});
    card.RowDefinitions().Append(RowDefinition{});
    card.RowDefinitions().Append(RowDefinition{});

    // Header row: icon + location/temp/condition/feels-like
    Grid header;
    header.CornerRadius({8, 8, 8, 8});
    header.Background(SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0x20, 0, 0, 0)});
    header.Padding({10, 10, 10, 10});
    header.ColumnDefinitions().Append(ColumnDefinition{});
    header.ColumnDefinitions().Append(ColumnDefinition{});
    header.ColumnDefinitions().GetAt(0).Width({1.0, GridUnitType::Auto});
    header.ColumnDefinitions().GetAt(1).Width({1.0, GridUnitType::Star});
    Grid::SetRow(header, 0);

    // Fixed square "icon zone" (like an avatar/album-art square) instead
    // of a bare oversized glyph floating in open space - live feedback,
    // 2026-09-21. The gap to the text next to it went 12px -> 8px on the
    // first feedback pass, then back up a bit (8px -> 12px) on a second
    // pass asking for "a bit more" room now that the header has its own
    // background again and the tighter gap read as cramped against it.
    constexpr double kHeaderIconZoneSize = 48;
    Border headerIconZone;
    headerIconZone.Width(kHeaderIconZoneSize);
    headerIconZone.Height(kHeaderIconZoneSize);
    headerIconZone.CornerRadius({6, 6, 6, 6});
    headerIconZone.Background(SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0x20, 0xFF, 0xFF, 0xFF)});
    headerIconZone.Margin({0, 0, 12, 0});
    TextBlock headerIcon;
    headerIcon.FontSize(28);
    headerIcon.HorizontalAlignment(HorizontalAlignment::Center);
    headerIcon.VerticalAlignment(VerticalAlignment::Center);
    headerIcon.Text(winrt::hstring(
        snapshot.hasData ? GetWeatherIcon(snapshot.wmoCode, snapshot.isDay).glyph
                          : L"☁️"));
    headerIconZone.Child(headerIcon);
    Grid::SetColumn(headerIconZone, 0);
    header.Children().Append(headerIconZone);

    StackPanel headerText;
    headerText.Orientation(Orientation::Vertical);
    headerText.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(headerText, 1);

    if (!snapshot.locationName.empty()) {
        TextBlock locationText;
        locationText.FontSize(12);
        locationText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        locationText.Text(winrt::hstring(snapshot.locationName));
        headerText.Children().Append(locationText);
    }

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
    std::wstring dateFormat;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        forecastDaysPanel = g_settings.forecastDaysPanel;
        useFahrenheit = g_settings.useFahrenheit;
        dateFormat = g_settings.forecastDateFormat;
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
        dayLabel.Text(winrt::hstring(FormatForecastDate(day.date, i, dateFormat)));
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

// Returns a Border, not the inner StackPanel, wrapping everything in one
// real, always-visible panel background (2026-09-21). ShowWeatherPanel's
// own FlyoutPresenterStyle deliberately sets the FlyoutPresenter's
// Background to Transparent - the intent was to avoid a second
// background stacking on top of this content's own, but relying on the
// *system* FlyoutPresenter default for that background never actually
// rendered anything in this Explorer-XAML-island context, before or
// after that style existed - the panel was invisible-background from
// the start. This Border is now the *only* source of the panel's
// background, unconditionally, rather than depending on a system
// default that doesn't render here.
Border BuildWeatherFlyoutContent() {
    StackPanel content;
    content.Orientation(Orientation::Vertical);

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

    Border panelBg;
    // Matches taskbar-widget-media-player's own fixed panel width
    // (360px) so every mod's details panel is the same size regardless
    // of which one happens to be open (live feedback, 2026-09-21) - was
    // on `content` itself before this Border wrapped it.
    panelBg.MinWidth(360);
    panelBg.MaxWidth(360);
    panelBg.CornerRadius({8, 8, 8, 8});
    panelBg.Padding({16, 16, 16, 16});
    // Real backdrop blur (2026-09-21, live feedback: "blur panel bg
    // too") via AcrylicBrush/Backdrop, same approach
    // taskbar-widget-media-player's own panel background uses - falls
    // back to the previous flat solid color if AcrylicBrush throws
    // (unsupported OS/theme edge case), so the panel is never left with
    // no background at all either way.
    try {
        winrt::Windows::UI::Xaml::Media::AcrylicBrush acrylic;
        acrylic.BackgroundSource(
            winrt::Windows::UI::Xaml::Media::AcrylicBackgroundSource::Backdrop);
        acrylic.TintColor(
            winrt::Windows::UI::ColorHelper::FromArgb(0xFF, 0x2B, 0x2B, 0x2B));
        acrylic.TintOpacity(0.5);
        acrylic.TintLuminosityOpacity(0.85);
        acrylic.FallbackColor(
            winrt::Windows::UI::ColorHelper::FromArgb(0xF0, 0x2B, 0x2B, 0x2B));
        panelBg.Background(acrylic);
    } catch (...) {
        panelBg.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0xF0, 0x2B, 0x2B, 0x2B)});
    }
    panelBg.BorderBrush(SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0x18, 0xFF, 0xFF, 0xFF)});
    panelBg.BorderThickness({1, 1, 1, 1});
    panelBg.Child(content);
    return panelBg;
}

Flyout g_weatherFlyout{nullptr};

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
        RefreshWeatherWidgetHoverVisual();
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
        // Was missing entirely (2026-09-21): TranslateY(8) above set the
        // starting offset for a slide-up entrance, but nothing ever
        // animated it back to 0 - content rendered permanently 8px below
        // its allocated layout bounds, so the Flyout clipped its own
        // bottom ~8px (RenderTransform moves where something *renders*,
        // not its layout bounds, so the Flyout's own size/clip never
        // accounted for the offset). Targeting `transform` directly
        // (not a "(UIElement.RenderTransform).(...)" property path on
        // `content`) since it's a DependencyObject in its own right.
        DoubleAnimation slideAnim;
        slideAnim.To(0.0);
        slideAnim.Duration(
            winrt::Windows::UI::Xaml::DurationHelper::FromTimeSpan(
                std::chrono::milliseconds(150)));
        Storyboard::SetTarget(slideAnim, transform);
        Storyboard::SetTargetProperty(slideAnim, L"TranslateY");
        Storyboard sb;
        sb.Children().Append(opacityAnim);
        sb.Children().Append(slideAnim);
        sb.Begin();
    });
    flyout.Closed([](winrt::Windows::Foundation::IInspectable const&,
                      winrt::Windows::Foundation::IInspectable const&) {
        g_weatherFlyoutOpen = false;
        // Closing the panel is almost always triggered by a click
        // elsewhere (light dismiss), well off the widget - reset both
        // explicitly rather than trusting a PointerExited/PointerReleased
        // to have fired on the widget itself, since opening the Flyout
        // in the first place can already disrupt its pointer capture. A
        // genuinely still-hovered pointer self-corrects on its next move.
        *g_weatherPointerHovered = false;
        *g_weatherPointerPressed = false;
        RefreshWeatherWidgetHoverVisual();
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

        // A plain Grid, not a Button (2026-09-21): there's only ever one
        // clickable area here, and a Button brings its own themed
        // idle/PointerOver/Pressed template chrome that has to be fought
        // (clearing just its idle Background wasn't the full fix - other
        // template-driven states could still interfere). `background`
        // (below) already owns every real hover/press visual via
        // ApplyWeatherHoverState, matching taskbar-widget-media-player's
        // own wrapper, which is also a plain Grid for the same reason.
        // A Grid needs an explicit (even fully transparent) Background to
        // be hit-testable at all - unlike a Button, it isn't by default -
        // so this is required, not just cosmetic.
        Grid wrapper;
        wrapper.HorizontalAlignment(HorizontalAlignment::Stretch);
        wrapper.Height(host->paneHeight);
        wrapper.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});

        // Top/bottom margin (was flush - live feedback, 2026-09-21, then
        // halved again on a second pass): media-player's own hover
        // surface is a fixed 40px tall element vertically centered
        // within its own taller pane, not stretched to fill it - this
        // mod doesn't have its own configurable widget height the way
        // media-player does, so a symmetric margin achieves the same
        // "inset, not flush top-to-bottom" look regardless of the host's
        // configured pane height. CornerRadius: matched media-player's
        // own default (4) exactly at first, then bumped 33% (live
        // feedback, 2026-09-21) once the margin/border fixes made a
        // direct comparison possible - 4 read slightly tighter than
        // media-player's own hover surface side by side.
        Border background;
        background.CornerRadius({5.33, 5.33, 5.33, 5.33});
        background.Margin({0, 3, 0, 3});

        auto compact = BuildCompactView();
        background.Child(compact);
        wrapper.Children().Append(background);

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

    // See the registered-mode wrapper's own comment above (same reasoning,
    // standalone-injection site) - a plain Grid, not a Button.
    Grid wrapper;
    wrapper.HorizontalAlignment(HorizontalAlignment::Left);
    wrapper.Background(SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});

    // See the registered-mode wrapper's own comment on the matching
    // construction site - same margin/radius reasoning.
    Border background;
    background.CornerRadius({5.33, 5.33, 5.33, 5.33});
    background.Margin({0, 3, 0, 3});
    auto compact = BuildCompactView();
    background.Child(compact);
    wrapper.Children().Append(background);

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
    // Fetch unregisterFn up front, alongside registerFn, rather than only
    // after a successful registerFn() call: the host
    // (taskbar-widget-stack) always sets/removes both props together in
    // the same scope (InjectWidgetStackGrid/RemoveWidgetStackGrid), so
    // requiring both here before ever calling registerFn means this mod
    // can never end up registered with the host while having no way to
    // unregister - the double-registration hazard (stack-registered AND
    // standalone-injected at once) that a post-hoc "registered but can't
    // unregister, fall back to standalone" branch used to risk.
    auto unregisterFn = (WidgetStack_UnregisterWidget_t)GetPropW(
        hWnd, kUnregisterWidgetPropName);
    if (registerFn && unregisterFn) {
        if (g_weatherRootParent) {
            WeatherWidget_Destroy(nullptr);  // tear down standalone first
        }
        WidgetStackWidgetAbiV1 abi;
        FillWeatherWidgetAbi(abi);
        // Set true BEFORE calling registerFn, not after: registerFn
        // (WidgetStack_RegisterWidget in the host) synchronously calls
        // RebuildStackContents(), which calls this widget's own Create()
        // before registerFn ever returns - so anything that reads
        // g_weatherRemoteRegistered during that call would otherwise see
        // a stale false. Nothing in this widget's own Create() path
        // reads the flag today, but this mirrors the same fix applied to
        // taskbar-widget-media-player.wh.cpp and
        // taskbar-widget-system-usage.wh.cpp for consistency and to
        // close out this bug class across the whole codebase. Reset back
        // to false below on any failure branch.
        g_weatherRemoteRegistered = true;
        if (registerFn(&abi)) {
            g_weatherHostUnregisterFn = unregisterFn;
            Wh_Log(L"Registered with taskbar-widget-stack");
            return;
        }
        g_weatherRemoteRegistered = false;
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
