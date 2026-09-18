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
using namespace winrt::Windows::UI::Xaml::Media::Animation;
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
    {
        auto* latStr = Wh_GetStringSetting(L"LocationSettings.manualLat");
        g_settings.manualLat = latStr ? wcstod(latStr, nullptr) : 0.0;
        if (latStr) {
            Wh_FreeStringSetting(latStr);
        }
        auto* lonStr = Wh_GetStringSetting(L"LocationSettings.manualLon");
        g_settings.manualLon = lonStr ? wcstod(lonStr, nullptr) : 0.0;
        if (lonStr) {
            Wh_FreeStringSetting(lonStr);
        }
    }
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

// Step 1: ConditionName - human-readable label for WMO code
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

// Step 2: BuildNowView - compact view showing icon + temperature + condition
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

// Step 3: Global state and view builders for in-place UI updates
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

// Forward declaration of RunFromWindowThread (defined in Task 14)
using WindowThreadProc = void(*)(void*);
static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param);

// Step 4: BuildForecastView - compact strip showing 3-7 days forecast
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

// Step 5: ToggleDisplayMode - switch between now and forecast views
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

// Task 8: Hover/pressed visual state helpers
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
