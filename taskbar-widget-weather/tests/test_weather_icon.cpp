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
