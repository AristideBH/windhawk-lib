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
