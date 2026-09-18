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
