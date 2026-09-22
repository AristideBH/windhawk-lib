// ==WindhawkMod==
// @id              taskbar-widget-home-assistant
// @name            Taskbar Widget: Home Assistant
// @description     Shows and controls Home Assistant entities in the taskbar. Registers into taskbar-widget-stack's pane if installed, falls back to standalone injection otherwise.
// @version         0.7.2
// @author          Aristide
// @github          https://github.com/AristideBH
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Widget: Home Assistant

Shows Home Assistant entities (lights, switches, sensors) directly in the
taskbar over a live WebSocket connection, with independently configurable
widget "profiles" - single-entity, a multi-entity strip, or a dashboard tile
grid. Registers into taskbar-widget-stack's pane when installed, falls back
to standalone otherwise. See PLAN.md for the design.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- ConnectionSettings:
  - serverUrl: ""
    $name: Server address
    $description: Home Assistant host and port, e.g. "homeassistant.local:8123" or an IP - no scheme prefix.
  - useTls: false
    $name: Use HTTPS/WSS
    $description: Enable if your Home Assistant instance uses HTTPS (wss:// instead of ws://).
  - accessToken: ""
    $name: Long-lived access token
    $description: Create one from your Home Assistant user profile page (Security tab -> Long-lived access tokens).
  $name: Connection
- profiles:
  - - profileId: ""
      $name: Profile ID
      $description: >-
        A short, unique identifier for this widget instance. Leave empty to
        stop defining more profiles - entries after the first empty one are
        ignored.
    - displayName: ""
      $name: Display name
      $description: Optional friendlier name for the taskbar tooltip. Falls back to Profile ID if empty.
    - mode: single
      $name: Mode
      $options:
      - single: Single entity
      - multi: Multiple entities (list)
      - dashboard: Multiple entities (tile grid)
    - entityIds: [""]
      $name: Entity IDs
      $description: >-
        One Home Assistant entity_id per entry (e.g. light.living_room,
        sensor.outdoor_temp). Single-entity mode uses only the first one.
  $name: Widget profiles
- StyleSettings:
  - styleConstants: [""]
    $name: Style constants
    $description: >-
      Raw "Key=Value" theme entries - the same shorthand as
      taskbar-widget-weather's own Style constants setting (colors as
      "R G B", or a full XAML brush fragment like <AcrylicBrush .../>). A
      blank entry ends the list. Never interpreted directly; see Style
      aliases below.
  - styleAliases: [""]
    $name: Style aliases
    $description: >-
      "SlotName=rawKey" entries mapping a widget style slot to one of the
      keys above. Slots: CardCornerRadius, PanelCornerRadius, HeaderPadding,
      PanelPadding, MutedTextOpacity (numbers); PanelBackgroundBrush,
      HeaderBackgroundBrush, BorderBrush, SeparatorBrush, HoverBrush,
      PressedBrush, OnColor, OffColor (colors/brushes). A slot with no
      alias, or an alias pointing at a missing key, keeps its built-in
      default. As with styleConstants above, a blank entry ends the list.
  $name: Style
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
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;
using namespace winrt::Windows::UI::Xaml::Media::Animation;
using namespace winrt::Windows::Data::Json;

enum class ProfileMode { Single, Multi, Dashboard };

struct EntityState {
    std::wstring entityId;
    std::wstring domain;
    std::wstring state;
    std::wstring friendlyName;
    std::wstring unitOfMeasurement;
    bool hasData = false;
};

enum class HaConnectionState { Disconnected, Connecting, AuthFailed, Connected };

struct HaSettings {
    std::wstring serverUrl;
    bool useTls = false;
    std::wstring accessToken;
};

struct ProfileConfig {
    std::wstring profileId;
    std::wstring displayName;
    ProfileMode mode = ProfileMode::Single;
    std::vector<std::wstring> entityIds;
};

HaSettings g_settings;
std::vector<ProfileConfig> g_profileConfigs;
std::mutex g_settingsMutex;

std::wstring DomainOf(const std::wstring& entityId) {
    size_t dot = entityId.find(L'.');
    if (dot == std::wstring::npos) {
        return L"";
    }
    return entityId.substr(0, dot);
}

std::wstring GetStringSetting(PCWSTR name, PCWSTR fallback) {
    auto* value = Wh_GetStringSetting(name);
    std::wstring result = value ? value : fallback;
    if (value) {
        Wh_FreeStringSetting(value);
    }
    return result;
}

// Terminates on the first EMPTY result, not a null pointer -
// Wh_GetStringSetting never returns null for a missing array element, it
// returns a valid pointer to an empty string (verified against Taskbar
// Styler's own styleConstants[%d] reading loop).
std::vector<std::wstring> GetStringArraySetting(const std::wstring& settingName) {
    std::vector<std::wstring> result;
    std::wstring format = settingName + L"[%d]";
    for (int i = 0;; i++) {
        auto* raw = Wh_GetStringSetting(format.c_str(), i);
        bool empty = !raw || !raw[0];
        std::wstring entry = raw ? raw : L"";
        if (raw) {
            Wh_FreeStringSetting(raw);
        }
        if (empty) {
            break;
        }
        result.push_back(std::move(entry));
    }
    return result;
}

// ---------------------------------------------------------------------
// StyleSettings engine (Task 7): byte-for-byte the same engine as
// taskbar-widget-weather.wh.cpp's own (duplicated fresh - no shared header
// between mods in this repo), adjusted only to reuse this file's own
// GetStringArraySetting (Task 1) in place of weather's differently-named
// ParseKeyValueArraySetting for the array-reading half of the work.
// styleConstants holds raw "Key=Value" theme entries; styleAliases maps a
// widget style slot name to one of those keys. A slot with no alias, or an
// alias pointing at a missing key, resolves to "" and callers fall back to
// their own hardcoded default.
// ---------------------------------------------------------------------

std::map<std::wstring, std::wstring> g_styleConstants;
std::map<std::wstring, std::wstring> g_styleAliases;
std::mutex g_styleMutex;

std::map<std::wstring, std::wstring> ParseKeyValueMapSetting(
    const std::wstring& settingName) {
    std::map<std::wstring, std::wstring> result;
    for (auto const& entry : GetStringArraySetting(settingName)) {
        size_t eq = entry.find(L'=');
        if (eq == std::wstring::npos) {
            continue;
        }
        result[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    return result;
}

std::wstring ResolveStyleRawValue(const std::wstring& slotName) {
    std::lock_guard<std::mutex> lock(g_styleMutex);
    auto aliasIt = g_styleAliases.find(slotName);
    if (aliasIt == g_styleAliases.end()) {
        return L"";
    }
    auto constIt = g_styleConstants.find(aliasIt->second);
    if (constIt == g_styleConstants.end()) {
        return L"";
    }
    return constIt->second;
}

double GetStyleNumber(const std::wstring& slotName, double fallback) {
    std::wstring raw = ResolveStyleRawValue(slotName);
    if (raw.empty()) {
        return fallback;
    }
    try {
        return std::stod(raw);
    } catch (...) {
        return fallback;
    }
}

// XamlReader::Load requires an xmlns on the root element; user-pasted
// fragments (copied straight from a Taskbar Styler theme) never have one.
// Injects the two standard namespaces right after the root tag's name -
// mirrors weather's own InjectXamlNamespaces exactly.
std::wstring InjectXamlNamespaces(const std::wstring& fragment) {
    size_t ltPos = fragment.find(L'<');
    if (ltPos == std::wstring::npos) {
        return fragment;
    }
    size_t nameStart = ltPos + 1;
    size_t nameEnd = fragment.find_first_of(L" \t\r\n>", nameStart);
    if (nameEnd == std::wstring::npos) {
        return fragment;
    }
    std::wstring result = fragment;
    result.insert(nameEnd,
        L" xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
        L"xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\"");
    return result;
}

winrt::Windows::UI::Color ParseRgbColor(const std::wstring& value) {
    int r = 0, g = 0, b = 0;
    swscanf_s(value.c_str(), L"%d %d %d", &r, &g, &b);
    auto clamp8 = [](int v) { return (BYTE)std::clamp(v, 0, 255); };
    return winrt::Windows::UI::ColorHelper::FromArgb(255, clamp8(r), clamp8(g),
                                                       clamp8(b));
}

Brush GetStyleBrush(const std::wstring& slotName, Brush const& fallback) {
    std::wstring raw = ResolveStyleRawValue(slotName);
    if (raw.empty()) {
        return fallback;
    }
    size_t firstNonSpace = raw.find_first_not_of(L" \t\r\n");
    if (firstNonSpace == std::wstring::npos) {
        return fallback;
    }
    try {
        if (raw[firstNonSpace] == L'<') {
            auto wrapped = InjectXamlNamespaces(raw);
            auto obj = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(
                winrt::hstring(wrapped));
            return obj.as<Brush>();
        }
        return SolidColorBrush{ParseRgbColor(raw)};
    } catch (...) {
        return fallback;
    }
}

ProfileMode ParseProfileMode(const std::wstring& value) {
    if (value == L"multi") {
        return ProfileMode::Multi;
    }
    if (value == L"dashboard") {
        return ProfileMode::Dashboard;
    }
    return ProfileMode::Single;
}

void LoadSettings() {
    HaSettings newSettings;
    newSettings.serverUrl = GetStringSetting(L"ConnectionSettings.serverUrl", L"");
    newSettings.useTls = Wh_GetIntSetting(L"ConnectionSettings.useTls") != 0;
    newSettings.accessToken = GetStringSetting(L"ConnectionSettings.accessToken", L"");

    std::vector<ProfileConfig> newProfiles;
    for (int i = 0;; i++) {
        std::wstring idKey = L"profiles[" + std::to_wstring(i) + L"].profileId";
        auto* rawId = Wh_GetStringSetting(idKey.c_str());
        bool empty = !rawId || !rawId[0];
        std::wstring profileId = rawId ? rawId : L"";
        if (rawId) {
            Wh_FreeStringSetting(rawId);
        }
        if (empty) {
            break;
        }

        ProfileConfig profile;
        profile.profileId = profileId;
        std::wstring displayNameKey =
            L"profiles[" + std::to_wstring(i) + L"].displayName";
        profile.displayName = GetStringSetting(displayNameKey.c_str(), L"");
        std::wstring modeKey = L"profiles[" + std::to_wstring(i) + L"].mode";
        profile.mode = ParseProfileMode(GetStringSetting(modeKey.c_str(), L"single"));
        std::wstring entityIdsKey =
            L"profiles[" + std::to_wstring(i) + L"].entityIds";
        profile.entityIds = GetStringArraySetting(entityIdsKey);

        newProfiles.push_back(std::move(profile));
    }

    auto constants = ParseKeyValueMapSetting(L"StyleSettings.styleConstants");
    auto aliases = ParseKeyValueMapSetting(L"StyleSettings.styleAliases");
    {
        std::lock_guard<std::mutex> styleLock(g_styleMutex);
        g_styleConstants = std::move(constants);
        g_styleAliases = std::move(aliases);
    }

    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = std::move(newSettings);
    g_profileConfigs = std::move(newProfiles);
}

// ---------------------------------------------------------------------
// Cross-mod widget ABI: lets taskbar-widget-stack host this widget inside
// its shared pane instead of standalone. The struct layout below must stay
// byte-identical (field order and types) to the copy in
// taskbar-widget-stack.wh.cpp and taskbar-widget-weather.wh.cpp - there's
// no shared header across these DLLs, so this is a hand-synced ABI.
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

// ---------------------------------------------------------------------
// Per-profile instance: one of these per configured profile. `context` in
// every ABI callback above is always a `HomeAssistantProfileInstance*` -
// that's the entire multi-instance mechanism, no separate dispatch table.
// ---------------------------------------------------------------------

struct HomeAssistantProfileInstance {
    ProfileConfig config;
    Grid wrapper{nullptr};
    Border background{nullptr};
    void* taskbarHwnd = nullptr;
    bool registeredWithStack = false;
};

// Process-shutdown safety (see the windhawk-mod-development skill's
// "Process-shutdown safety" section): each HomeAssistantProfileInstance
// holds a Grid/Border (strong XAML references), so this container can't be
// allowed to run its destructor automatically on a hard process exit
// (Explorer restart/sign-out/reboot skip Wh_ModUninit entirely, and by the
// time global destructors run under the loader lock, the XAML core may
// already be torn down). [[clang::no_destroy]] suppresses that automatic
// destructor; Wh_ModUninit below releases it explicitly instead, via
// UnregisterAllProfileInstances (marshaled onto the UI thread) followed by
// an explicit .reset() (vector::clear() alone would leave the buffer
// allocated).
[[clang::no_destroy]] std::optional<
    std::vector<std::unique_ptr<HomeAssistantProfileInstance>>>
    g_profileInstances{std::in_place};
std::mutex g_profileInstancesMutex;

// Forward declarations: GetEntityState/CallHaService/g_haConnectionState are
// defined further down in the WebSocket connection engine section (Task 3),
// but the single-entity UI built here (Task 4) needs them earlier in the
// file, ahead of BuildCompactView/HaWidget_Create's existing position.
EntityState GetEntityState(const std::wstring& entityId);
void CallHaService(const std::wstring& domain,
                    const std::wstring& service,
                    const std::wstring& entityId);
extern std::atomic<HaConnectionState> g_haConnectionState;

// ---------------------------------------------------------------------
// Single-entity mode UI (Task 4): compact taskbar view, details panel, and
// click-to-toggle for light/switch entities. Reused (via the mode switches
// below) as the fallback rendering for every mode until Tasks 5/6 add their
// own Multi/Dashboard branches.
// ---------------------------------------------------------------------

std::wstring DomainIcon(const std::wstring& domain) {
    if (domain == L"light") {
        return L"💡";
    }
    if (domain == L"switch") {
        return L"🔌";
    }
    if (domain == L"sensor") {
        return L"📊";
    }
    return L"❔";
}

bool IsToggleableDomain(const std::wstring& domain) {
    return domain == L"light" || domain == L"switch";
}

constexpr double kHaIconFontSize = 20;
constexpr double kHaStateFontSize = 12;

// OnColor/OffColor (Task 7 StyleSettings slots): applied to every
// light/switch icon's Foreground, everywhere one renders (compact single,
// compact multi/dashboard strip, panel list, panel tiles) - callers only
// invoke this once they've already checked IsToggleableDomain(domain), so
// non-toggleable domains (sensor, etc.) keep their default inherited
// Foreground instead of being forced into an on/off color that doesn't
// apply to them. Defaults (no existing hardcoded value to match, since this
// is a new mod, not a refactor - implementer's choice, picked to read well
// against this file's dark/translucent panels): a warm amber for "on"
// (matches a typical smart-bulb-lit look), a muted mid-gray for "off".
Brush HaOnOffIconBrush(bool isOn) {
    if (isOn) {
        SolidColorBrush defaultOn{
            winrt::Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF, 0xC1, 0x07)};
        return GetStyleBrush(L"OnColor", defaultOn);
    }
    SolidColorBrush defaultOff{
        winrt::Windows::UI::ColorHelper::FromArgb(0xFF, 0x8C, 0x8C, 0x8C)};
    return GetStyleBrush(L"OffColor", defaultOff);
}

FrameworkElement BuildSingleEntityCompactView(
    HomeAssistantProfileInstance* instance) {
    Grid root;
    root.VerticalAlignment(VerticalAlignment::Center);
    root.ColumnSpacing(8);
    root.Padding({6, 2, 6, 2});
    root.ColumnDefinitions().Append(ColumnDefinition{});
    root.ColumnDefinitions().Append(ColumnDefinition{});

    if (instance->config.entityIds.empty()) {
        TextBlock placeholder;
        placeholder.FontSize(kHaStateFontSize);
        placeholder.Text(L"No entity configured");
        root.Children().Append(placeholder);
        return root;
    }

    const std::wstring& entityId = instance->config.entityIds[0];
    EntityState state = GetEntityState(entityId);
    std::wstring domain = state.domain.empty() ? DomainOf(entityId) : state.domain;

    TextBlock icon;
    icon.FontSize(kHaIconFontSize);
    icon.VerticalAlignment(VerticalAlignment::Center);
    icon.Text(winrt::hstring(DomainIcon(domain)));
    if (IsToggleableDomain(domain)) {
        icon.Foreground(HaOnOffIconBrush(state.hasData && state.state == L"on"));
    }
    Grid::SetColumn(icon, 0);
    root.Children().Append(icon);

    TextBlock stateText;
    stateText.FontSize(kHaStateFontSize);
    stateText.VerticalAlignment(VerticalAlignment::Center);
    std::wstring displayState =
        state.hasData ? state.state + (state.unitOfMeasurement.empty()
                                            ? L""
                                            : L" " + state.unitOfMeasurement)
                       : L"…";
    stateText.Text(winrt::hstring(displayState));
    Grid::SetColumn(stateText, 1);
    root.Children().Append(stateText);

    if (g_haConnectionState.load() != HaConnectionState::Connected &&
        g_haConnectionState.load() != HaConnectionState::Connecting) {
        root.Opacity(0.5);
    }

    return root;
}

// A strong XAML reference - needs [[clang::no_destroy]] (see the
// process-shutdown-safety note above g_profileInstances); released
// explicitly (= nullptr) in Wh_ModUninit instead, on the UI thread.
[[clang::no_destroy]] Flyout g_haProfileFlyout{nullptr};

// Wraps a panel-content root (built by the Single/Multi/Dashboard
// panel-content functions below) in a themed Border - background, corner
// radius, padding and border color all driven by the PanelCornerRadius/
// PanelPadding/PanelBackgroundBrush/BorderBrush style slots (Task 7). None
// of the three functions build this Border themselves; they hand back a
// bare StackPanel/Grid root and this is the single place that wraps it.
// Owns the fixed 360px panel width too - moved here from each root below
// rather than duplicated on both the wrapper and the inner root - matching
// taskbar-widget-weather.wh.cpp's own BuildWeatherFlyoutContent Border-wrap
// pattern (same AcrylicBrush-with-solid-fallback construction for the
// default background, same default border tint).
Border WrapPanelContent(FrameworkElement content) {
    Border panelBg;
    panelBg.MinWidth(360);
    panelBg.MaxWidth(360);
    double panelCornerRadius = GetStyleNumber(L"PanelCornerRadius", 8.0);
    panelBg.CornerRadius({panelCornerRadius, panelCornerRadius,
                           panelCornerRadius, panelCornerRadius});
    double panelPadding = GetStyleNumber(L"PanelPadding", 16.0);
    panelBg.Padding({panelPadding, panelPadding, panelPadding, panelPadding});

    Brush defaultPanelBackground{nullptr};
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
        defaultPanelBackground = acrylic;
        panelBg.Background(GetStyleBrush(L"PanelBackgroundBrush", defaultPanelBackground));
    } catch (...) {
        defaultPanelBackground = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0xF0, 0x2B, 0x2B, 0x2B)};
        panelBg.Background(GetStyleBrush(L"PanelBackgroundBrush", defaultPanelBackground));
    }
    Brush defaultPanelBorder = SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0x18, 0xFF, 0xFF, 0xFF)};
    panelBg.BorderBrush(GetStyleBrush(L"BorderBrush", defaultPanelBorder));
    panelBg.BorderThickness({1, 1, 1, 1});
    panelBg.Child(content);
    return panelBg;
}

FrameworkElement BuildSingleEntityPanelContent(
    HomeAssistantProfileInstance* instance) {
    StackPanel root;

    if (instance->config.entityIds.empty()) {
        TextBlock placeholder;
        placeholder.Text(L"No entity configured for this profile.");
        root.Children().Append(placeholder);
        return WrapPanelContent(root);
    }

    const std::wstring& entityId = instance->config.entityIds[0];
    EntityState state = GetEntityState(entityId);
    std::wstring domain = state.domain.empty() ? DomainOf(entityId) : state.domain;

    TextBlock icon;
    icon.FontSize(kHaIconFontSize * 2);
    icon.Text(winrt::hstring(DomainIcon(domain)));
    if (IsToggleableDomain(domain)) {
        icon.Foreground(HaOnOffIconBrush(state.hasData && state.state == L"on"));
    }
    root.Children().Append(icon);

    TextBlock name;
    name.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    name.Text(winrt::hstring(state.friendlyName.empty() ? entityId
                                                          : state.friendlyName));
    root.Children().Append(name);

    TextBlock stateText;
    stateText.Text(winrt::hstring(
        state.hasData
            ? state.state + (state.unitOfMeasurement.empty()
                                  ? L""
                                  : L" " + state.unitOfMeasurement)
            : L"No data yet"));
    root.Children().Append(stateText);

    return WrapPanelContent(root);
}

FrameworkElement BuildMultiEntityCompactView(
    HomeAssistantProfileInstance* instance) {
    Grid root;
    root.VerticalAlignment(VerticalAlignment::Center);
    root.ColumnSpacing(0);
    root.Padding({6, 2, 6, 2});

    if (instance->config.entityIds.empty()) {
        TextBlock placeholder;
        placeholder.FontSize(kHaStateFontSize);
        placeholder.Text(L"No entities configured");
        root.Children().Append(placeholder);
        return root;
    }

    for (size_t i = 0; i < instance->config.entityIds.size(); i++) {
        root.ColumnDefinitions().Append(ColumnDefinition{});
        const std::wstring& entityId = instance->config.entityIds[i];
        EntityState state = GetEntityState(entityId);
        std::wstring domain =
            state.domain.empty() ? DomainOf(entityId) : state.domain;

        Border cell;
        cell.Margin({4, 0, 4, 0});
        cell.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
        Grid::SetColumn(cell, (int)i);

        TextBlock icon;
        icon.FontSize(kHaIconFontSize * 0.7);
        icon.HorizontalAlignment(HorizontalAlignment::Center);
        icon.Text(winrt::hstring(DomainIcon(domain)));
        if (state.hasData && state.state != L"on") {
            icon.Opacity(0.5);
        }
        if (IsToggleableDomain(domain)) {
            icon.Foreground(HaOnOffIconBrush(state.hasData && state.state == L"on"));
        }
        cell.Child(icon);

        if (IsToggleableDomain(domain)) {
            cell.PointerPressed(
                [entityId, domain](
                    winrt::Windows::Foundation::IInspectable const&,
                    winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
                    CallHaService(domain, L"toggle", entityId);
                });
        }

        root.Children().Append(cell);
    }

    return root;
}

FrameworkElement BuildMultiEntityPanelContent(
    HomeAssistantProfileInstance* instance) {
    StackPanel root;
    root.Spacing(8);

    if (instance->config.entityIds.empty()) {
        TextBlock placeholder;
        placeholder.Text(L"No entities configured for this profile.");
        root.Children().Append(placeholder);
        return WrapPanelContent(root);
    }

    for (auto const& entityId : instance->config.entityIds) {
        EntityState state = GetEntityState(entityId);
        std::wstring domain =
            state.domain.empty() ? DomainOf(entityId) : state.domain;

        Grid row;
        row.ColumnSpacing(8);
        row.ColumnDefinitions().Append(ColumnDefinition{});
        row.ColumnDefinitions().Append(ColumnDefinition{});
        row.ColumnDefinitions().GetAt(1).Width({1.0, GridUnitType::Star});
        row.ColumnDefinitions().Append(ColumnDefinition{});

        TextBlock icon;
        icon.FontSize(kHaIconFontSize);
        icon.Text(winrt::hstring(DomainIcon(domain)));
        if (IsToggleableDomain(domain)) {
            icon.Foreground(HaOnOffIconBrush(state.hasData && state.state == L"on"));
        }
        Grid::SetColumn(icon, 0);
        row.Children().Append(icon);

        StackPanel textStack;
        textStack.Orientation(Orientation::Vertical);
        Grid::SetColumn(textStack, 1);
        TextBlock name;
        name.Text(winrt::hstring(state.friendlyName.empty() ? entityId
                                                              : state.friendlyName));
        textStack.Children().Append(name);
        TextBlock stateText;
        stateText.FontSize(kHaStateFontSize);
        stateText.Opacity(GetStyleNumber(L"MutedTextOpacity", 0.7));
        stateText.Text(winrt::hstring(
            state.hasData
                ? state.state + (state.unitOfMeasurement.empty()
                                      ? L""
                                      : L" " + state.unitOfMeasurement)
                : L"No data yet"));
        textStack.Children().Append(stateText);
        row.Children().Append(textStack);

        if (IsToggleableDomain(domain)) {
            ToggleSwitch toggleControl;
            toggleControl.IsOn(state.hasData && state.state == L"on");
            toggleControl.Toggled(
                [entityId, domain](
                    winrt::Windows::Foundation::IInspectable const&,
                    RoutedEventArgs const&) {
                    CallHaService(domain, L"toggle", entityId);
                });
            Grid::SetColumn(toggleControl, 2);
            row.Children().Append(toggleControl);
        }

        root.Children().Append(row);
    }

    return WrapPanelContent(root);
}

constexpr int kHaDashboardColumns = 3;

FrameworkElement BuildDashboardPanelContent(
    HomeAssistantProfileInstance* instance) {
    Grid root;
    root.ColumnSpacing(8);
    root.RowSpacing(8);
    for (int i = 0; i < kHaDashboardColumns; i++) {
        root.ColumnDefinitions().Append(ColumnDefinition{});
        root.ColumnDefinitions().GetAt(i).Width({1.0, GridUnitType::Star});
    }

    if (instance->config.entityIds.empty()) {
        TextBlock placeholder;
        placeholder.Text(L"No entities configured for this profile.");
        Grid::SetColumnSpan(placeholder, kHaDashboardColumns);
        root.Children().Append(placeholder);
        return WrapPanelContent(root);
    }

    int rowCount =
        ((int)instance->config.entityIds.size() + kHaDashboardColumns - 1) /
        kHaDashboardColumns;
    for (int r = 0; r < rowCount; r++) {
        root.RowDefinitions().Append(RowDefinition{});
    }

    for (size_t i = 0; i < instance->config.entityIds.size(); i++) {
        const std::wstring& entityId = instance->config.entityIds[i];
        EntityState state = GetEntityState(entityId);
        std::wstring domain =
            state.domain.empty() ? DomainOf(entityId) : state.domain;

        StackPanel tile;
        tile.Orientation(Orientation::Vertical);
        tile.HorizontalAlignment(HorizontalAlignment::Center);
        tile.Padding({8, 8, 8, 8});
        Grid::SetRow(tile, (int)(i / kHaDashboardColumns));
        Grid::SetColumn(tile, (int)(i % kHaDashboardColumns));

        TextBlock icon;
        icon.FontSize(kHaIconFontSize);
        icon.HorizontalAlignment(HorizontalAlignment::Center);
        icon.Text(winrt::hstring(DomainIcon(domain)));
        if (IsToggleableDomain(domain)) {
            icon.Foreground(HaOnOffIconBrush(state.hasData && state.state == L"on"));
        }
        tile.Children().Append(icon);

        TextBlock name;
        name.FontSize(kHaStateFontSize);
        name.HorizontalAlignment(HorizontalAlignment::Center);
        name.TextAlignment(TextAlignment::Center);
        name.Text(winrt::hstring(state.friendlyName.empty() ? entityId
                                                              : state.friendlyName));
        tile.Children().Append(name);

        TextBlock stateText;
        stateText.FontSize(kHaStateFontSize);
        stateText.Opacity(GetStyleNumber(L"MutedTextOpacity", 0.7));
        stateText.HorizontalAlignment(HorizontalAlignment::Center);
        stateText.Text(winrt::hstring(
            state.hasData
                ? state.state + (state.unitOfMeasurement.empty()
                                      ? L""
                                      : L" " + state.unitOfMeasurement)
                : L"No data yet"));
        tile.Children().Append(stateText);

        if (IsToggleableDomain(domain)) {
            Border tileBorder;
            tileBorder.Background(SolidColorBrush{
                winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
            tileBorder.Child(tile);
            tileBorder.PointerPressed(
                [entityId, domain](
                    winrt::Windows::Foundation::IInspectable const&,
                    winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
                    CallHaService(domain, L"toggle", entityId);
                });
            Grid::SetRow(tileBorder, (int)(i / kHaDashboardColumns));
            Grid::SetColumn(tileBorder, (int)(i % kHaDashboardColumns));
            root.Children().Append(tileBorder);
        } else {
            root.Children().Append(tile);
        }
    }

    return WrapPanelContent(root);
}

// Dispatches to the right panel-content builder by mode. Task 6 extends
// this switch with its own Dashboard branch - do not duplicate
// ShowProfilePanel itself.
FrameworkElement BuildProfilePanelContent(HomeAssistantProfileInstance* instance) {
    switch (instance->config.mode) {
        case ProfileMode::Single:
            return BuildSingleEntityPanelContent(instance);
        case ProfileMode::Multi:
            return BuildMultiEntityPanelContent(instance);
        case ProfileMode::Dashboard:
            return BuildDashboardPanelContent(instance);
        default:
            // Tasks 5/6 replace this default case with real Multi/
            // Dashboard branches; a plain placeholder here would violate
            // the no-placeholder rule for a finished plan, so this task
            // must leave a real, working fallback: reuse the single-entity
            // layout for the first configured entity, which always
            // produces a valid, non-empty panel even before Tasks 5/6 run.
            return BuildSingleEntityPanelContent(instance);
    }
}

void ShowProfilePanel(HomeAssistantProfileInstance* instance) {
    if (!instance || !instance->background) {
        return;
    }
    try {
        Flyout flyout;
        flyout.ShouldConstrainToRootBounds(false);
        flyout.Content(BuildProfilePanelContent(instance));
        g_haProfileFlyout = flyout;
        Controls::Primitives::FlyoutBase::SetAttachedFlyout(
            instance->background, flyout);
        Controls::Primitives::FlyoutBase::ShowAttachedFlyout(
            instance->background);
    } catch (...) {
    }
}

void HandleProfileClick(HomeAssistantProfileInstance* instance) {
    if (!instance) {
        return;
    }
    if (instance->config.mode == ProfileMode::Single &&
        !instance->config.entityIds.empty()) {
        const std::wstring& entityId = instance->config.entityIds[0];
        std::wstring domain = DomainOf(entityId);
        if (IsToggleableDomain(domain)) {
            CallHaService(domain, L"toggle", entityId);
            return;
        }
    }
    ShowProfilePanel(instance);
}

// Signature is final (set by Task 2); do not change it. Task 6 extends the
// switch below with a real Dashboard compact view.
FrameworkElement BuildCompactView(HomeAssistantProfileInstance* instance) {
    switch (instance->config.mode) {
        case ProfileMode::Single:
            return BuildSingleEntityCompactView(instance);
        case ProfileMode::Multi:
            return BuildMultiEntityCompactView(instance);
        case ProfileMode::Dashboard:
            return BuildMultiEntityCompactView(instance);
        default:
            // Tasks 5/6 replace this default case with real Multi/
            // Dashboard compact views. Until then, fall back to the
            // single-entity view over the profile's first configured
            // entity (if any) so every mode renders something real and
            // non-empty, never a placeholder string.
            return BuildSingleEntityCompactView(instance);
    }
}

void RebuildProfileInstanceUi(HomeAssistantProfileInstance* instance) {
    if (!instance || !instance->background) {
        return;
    }
    try {
        instance->background.Child(BuildCompactView(instance));
    } catch (...) {
    }
}

// ---------------------------------------------------------------------
// HoverBrush/PressedBrush style tokens (Task 7): hover/press visual state
// for each profile instance's `background` Border, mirroring
// taskbar-widget-weather.wh.cpp's own EnsureHoverBrushes/
// ApplyWeatherHoverState/WireUpHover trio - same lazily-cached brushes
// (computed once per process, invalidated on settings change), same
// gradient hover border, same default alpha values. Unlike weather (a
// single global widget), this mod can have several live profile
// instances at once, each with its own wrapper/background - so hovered/
// pressed state is captured locally per WireUpHaHover call instead of
// living in a shared global, and there's no "flyout open" cross-state to
// track (weather's own ShowWeatherPanel keeps the compact widget looking
// hovered while its details flyout is open; this mod's flyouts are
// per-instance and simpler to leave as plain hover/press for a first
// version).
// ---------------------------------------------------------------------

// Strong XAML references - same [[clang::no_destroy]] + explicit-release-
// on-the-UI-thread treatment as g_haProfileFlyout above.
[[clang::no_destroy]] SolidColorBrush g_haHoverBrush{nullptr};
[[clang::no_destroy]] SolidColorBrush g_haPressedBrush{nullptr};
[[clang::no_destroy]] winrt::Windows::UI::Xaml::Media::Brush g_haPressedBorderBrush{nullptr};

// Forces the three brushes above to recompute from current style
// constants the next time EnsureHaHoverBrushes() runs. Called from
// Wh_ModSettingsChanged (below), marshaled onto the UI thread - same
// requirement as weather's own InvalidateHoverBrushCache, since these are
// live WinRT smart pointers only safe to touch on the thread that owns
// the XAML objects.
void InvalidateHaHoverBrushCache() {
    g_haHoverBrush = nullptr;
    g_haPressedBrush = nullptr;
    g_haPressedBorderBrush = nullptr;
}

void EnsureHaHoverBrushes() {
    if (!g_haHoverBrush) {
        SolidColorBrush defaultHover{
            winrt::Windows::UI::ColorHelper::FromArgb(0x14, 0xFF, 0xFF, 0xFF)};
        try {
            g_haHoverBrush =
                GetStyleBrush(L"HoverBrush", defaultHover).as<SolidColorBrush>();
        } catch (...) {
            g_haHoverBrush = defaultHover;
        }
    }
    if (!g_haPressedBrush) {
        SolidColorBrush defaultPressed{
            winrt::Windows::UI::ColorHelper::FromArgb(0x28, 0xFF, 0xFF, 0xFF)};
        try {
            g_haPressedBrush =
                GetStyleBrush(L"PressedBrush", defaultPressed).as<SolidColorBrush>();
        } catch (...) {
            g_haPressedBrush = defaultPressed;
        }
    }
    if (!g_haPressedBorderBrush) {
        g_haPressedBorderBrush = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0x0A, 0xFF, 0xFF, 0xFF)};
    }
}

// Same top/bottom gradient border shape as weather's own
// MakeWeatherHoverBorderBrush, at a given alpha scale (1.0 = full
// 0x28/0x0A stops, 0.0 = fully transparent but the same brush type) - one
// brush shape across idle/hover keeps BorderThickness constant so the
// border's footprint (and the content inside it) never shifts on hover.
winrt::Windows::UI::Xaml::Media::Brush MakeHaHoverBorderBrush(double alphaScale) {
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

void ApplyHaHoverState(Border background, bool hovered, bool pressed) {
    EnsureHaHoverBrushes();
    background.BorderThickness({1, 1, 1, 1});
    if (pressed) {
        background.Background(g_haPressedBrush);
        background.BorderBrush(g_haPressedBorderBrush);
    } else if (hovered) {
        background.Background(g_haHoverBrush);
        background.BorderBrush(MakeHaHoverBorderBrush(1.0));
    } else {
        background.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
        background.BorderBrush(MakeHaHoverBorderBrush(0.0));
    }
}

// `background` is the full-bounds Border sitting behind this instance's
// compact view content; `wrapper` is the outer Grid pointer events are
// attached to (same shape as weather's own WireUpHover). hovered/pressed
// are captured per call - one instance's hover state never leaks into
// another's, since each profile instance owns its own wrapper/background.
void WireUpHaHover(FrameworkElement wrapper, Border background) {
    auto hovered = std::make_shared<bool>(false);
    auto pressed = std::make_shared<bool>(false);

    wrapper.PointerEntered(
        [hovered, pressed, background](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *hovered = true;
            ApplyHaHoverState(background, *hovered, *pressed);
        });
    wrapper.PointerExited(
        [hovered, pressed, background](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *hovered = false;
            *pressed = false;
            ApplyHaHoverState(background, *hovered, *pressed);
        });
    wrapper.PointerPressed(
        [hovered, pressed, background](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *pressed = true;
            ApplyHaHoverState(background, *hovered, *pressed);
        });
    wrapper.PointerReleased(
        [hovered, pressed, background](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            *pressed = false;
            ApplyHaHoverState(background, *hovered, *pressed);
        });
    ApplyHaHoverState(background, false, false);
}

// ---------------------------------------------------------------------
// RunFromWindowThread: marshals a call onto the taskbar window's own
// thread. Mirrors taskbar-widget-weather.wh.cpp's actual, working
// implementation verbatim (not the simpler PostMessage+heap-payload sketch
// in this task's brief) - a short-lived WH_CALLWNDPROC hook plus a
// synchronous SendMessageW, unhooked again once the call has run. Every
// lambda passed in as `proc` must have an empty capture list:
// WindowThreadProc is a plain function pointer, not std::function, and
// only a non-capturing lambda converts to one implicitly.
// ---------------------------------------------------------------------

using WindowThreadProc = void (*)(void*);

bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param) {
    static const UINT kMsg =
        RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
    struct Payload {
        WindowThreadProc proc;
        void* param;
    };
    if (!hWnd) {
        return false;
    }
    DWORD tid = GetWindowThreadProcessId(hWnd, nullptr);
    if (!tid) {
        return false;
    }
    if (tid == GetCurrentThreadId()) {
        proc(param);
        return true;
    }
    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM w, LPARAM l) CALLBACK -> LRESULT {
            if (code == HC_ACTION) {
                auto* cwp = reinterpret_cast<const CWPSTRUCT*>(l);
                static const UINT kM = RegisterWindowMessage(
                    L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
                if (cwp->message == kM) {
                    auto* p = reinterpret_cast<Payload*>(cwp->lParam);
                    p->proc(p->param);
                }
            }
            return CallNextHookEx(nullptr, code, w, l);
        },
        nullptr, tid);
    if (!hook) {
        return false;
    }
    Payload pay{proc, param};
    // SendMessageTimeoutW with SMTO_ABORTIFHUNG instead of a plain
    // SendMessageW: this function is also called from Wh_ModUninit to
    // release XAML globals safely on the UI thread, and an unresponsive
    // (but not dead - that's the tid==0 case above) target thread would
    // otherwise block a synchronous SendMessageW forever, hanging the very
    // shutdown/unload path this safety mechanism exists to protect. A
    // 5000ms timeout is long enough for a genuinely busy UI thread to
    // respond, short enough not to meaningfully hang a user-facing unload.
    DWORD_PTR result = 0;
    LRESULT sendResult =
        SendMessageTimeoutW(hWnd, kMsg, 0, reinterpret_cast<LPARAM>(&pay),
                             SMTO_ABORTIFHUNG, 5000, &result);
    DWORD sendError = GetLastError();
    UnhookWindowsHookEx(hook);
    if (!sendResult && sendError == ERROR_TIMEOUT) {
        // Target thread is alive but hung/unresponsive - treat exactly like
        // any other failure path (return false) rather than waiting it out.
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// The five ABI callbacks. Each casts `context` back to
// `HomeAssistantProfileInstance*` to know which profile it's for, and only
// ever touches that instance's own fields.
// ---------------------------------------------------------------------

extern "C" double __cdecl HaWidget_Create(void* context,
                                           const WidgetStackHostAbiV1* host) {
    auto* instance = (HomeAssistantProfileInstance*)context;
    if (!instance || !host) {
        return 0.0;
    }
    try {
        Panel parent{nullptr};
        winrt::copy_from_abi(parent, host->parentPanelAbi);
        if (!parent) {
            return 0.0;
        }
        instance->taskbarHwnd = host->taskbarHwnd;

        Grid wrapper;
        wrapper.HorizontalAlignment(HorizontalAlignment::Stretch);
        wrapper.Height(host->paneHeight);
        wrapper.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});

        double cardCornerRadius = GetStyleNumber(L"CardCornerRadius", 5.33);
        Border background;
        background.CornerRadius({cardCornerRadius, cardCornerRadius,
                                  cardCornerRadius, cardCornerRadius});
        background.Margin({0, 3, 0, 3});

        instance->wrapper = wrapper;
        instance->background = background;
        RebuildProfileInstanceUi(instance);

        wrapper.Children().Append(background);
        WireUpHaHover(wrapper, background);
        wrapper.Tapped(
            [instance](winrt::Windows::Foundation::IInspectable const&,
                       winrt::Windows::UI::Xaml::Input::TappedRoutedEventArgs const&) {
                HandleProfileClick(instance);
            });
        parent.Children().Append(wrapper);
        return 1.0;
    } catch (...) {
        return 0.0;
    }
}

extern "C" void __cdecl HaWidget_Tick(void* /*context*/) {
    // State updates arrive via the WebSocket thread's own dispatch (Task
    // 3), not a poll tick - intentionally empty.
}

extern "C" double __cdecl HaWidget_OnSettingsChanged(void* context) {
    // Full-rebuild convention (per spec): settings changes are handled by
    // completely re-registering every profile instance from scratch
    // (RebuildAllProfileInstances below), not by this per-instance
    // callback mutating in place - present for ABI completeness only.
    (void)context;
    return 1.0;
}

extern "C" void __cdecl HaWidget_Destroy(void* context) {
    auto* instance = (HomeAssistantProfileInstance*)context;
    if (!instance) {
        return;
    }
    instance->wrapper = nullptr;
    instance->background = nullptr;
}

extern "C" void __cdecl HaWidget_GetId(void* context,
                                        wchar_t* buffer,
                                        int bufferSize) {
    auto* instance = (HomeAssistantProfileInstance*)context;
    if (!instance || !buffer || bufferSize <= 0) {
        return;
    }
    wcsncpy_s(buffer, bufferSize, instance->config.profileId.c_str(),
              _TRUNCATE);
}

extern "C" void __cdecl HaWidget_GetDisplayName(void* context,
                                                 wchar_t* buffer,
                                                 int bufferSize) {
    auto* instance = (HomeAssistantProfileInstance*)context;
    if (!instance || !buffer || bufferSize <= 0) {
        return;
    }
    const std::wstring& name = instance->config.displayName.empty()
                                    ? instance->config.profileId
                                    : instance->config.displayName;
    wcsncpy_s(buffer, bufferSize, name.c_str(), _TRUNCATE);
}

// ---------------------------------------------------------------------
// Standalone injection fallback, per profile. Mirrors
// taskbar-widget-weather.wh.cpp's own InjectWeatherStandalone: find the
// taskbar's XAML root, walk down to Taskbar.TaskbarFrame's "RootGrid" by
// name, append a wrapper Grid there.
//
// GAP (flagged for review, not silently worked around): weather's real
// GetTaskbarXamlRoot() depends on taskbar-internals plumbing that is NOT
// part of this file or of Task 1/2's declared scope - a CTaskBand vtable
// walk (ITaskListWndSite), a symbol-hooked CTaskBand::_GetTaskbarHost and
// TaskbarHost::FrameHeight (the latter's prologue bytes are pattern-matched
// at runtime to recover a non-stable field offset, separately for x64 and
// ARM64EC), plus the Wh_SetFunctionHook symbol table and the
// TrayUI::StartTaskbar hook that (re)drives it, all set up once in
// Wh_ModInit. That's ~150 lines of architecture-sensitive, hand-verified
// pointer code specific to weather's own mod, not something this task's
// brief (Steps 1-9) or the 7-task plan's task list ever asks this file to
// duplicate. Porting it here blind, with no compiler to validate offsets
// or hook symbols against, would be a much larger and riskier change than
// "multi-instance ABI registration and profile lifecycle."
//
// GetTaskbarXamlRoot() below is therefore a deliberate, clearly-marked
// stub: it always returns nullptr, so InjectProfileStandalone() below
// safely no-ops (profiles simply don't render standalone) until a
// dedicated follow-up task ports weather's real CTaskBand infrastructure.
// Everything downstream (FindTaskbarRootGrid's signature,
// InjectProfileStandalone's call shape) is written to match weather's real
// code so that port is a drop-in replacement of this one function's body.
// ---------------------------------------------------------------------

XamlRoot GetTaskbarXamlRoot(HWND /*hTaskbarWnd*/) {
    // See the GAP comment above InjectProfileStandalone: real
    // implementation requires porting weather's CTaskBand hook
    // infrastructure, out of scope for this task.
    return nullptr;
}

FrameworkElement FindChildByName(FrameworkElement const& root,
                                  std::wstring_view name,
                                  int depth = 32) {
    if (!root || depth == 0) {
        return nullptr;
    }
    int n = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < n; ++i) {
        auto child =
            VisualTreeHelper::GetChild(root, i).try_as<FrameworkElement>();
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

// The taskbar's root Grid (parent of both TaskbarFrameRepeater and
// SystemTrayFrameGrid) - matches weather's FindTaskbarRootGrid exactly.
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

void InjectProfileStandalone(HomeAssistantProfileInstance* instance,
                              HWND taskbarHwnd) {
    if (!instance || !taskbarHwnd) {
        return;
    }
    try {
        auto xamlRoot = GetTaskbarXamlRoot(taskbarHwnd);
        if (!xamlRoot) {
            return;
        }
        auto rootElement = xamlRoot.Content().try_as<FrameworkElement>();
        if (!rootElement) {
            return;
        }
        Grid rootGrid = FindTaskbarRootGrid(rootElement);
        if (!rootGrid) {
            return;
        }

        Grid wrapper;
        wrapper.HorizontalAlignment(HorizontalAlignment::Left);
        wrapper.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});

        double cardCornerRadius = GetStyleNumber(L"CardCornerRadius", 5.33);
        Border background;
        background.CornerRadius({cardCornerRadius, cardCornerRadius,
                                  cardCornerRadius, cardCornerRadius});
        background.Margin({0, 3, 0, 3});

        instance->wrapper = wrapper;
        instance->background = background;
        instance->taskbarHwnd = taskbarHwnd;
        RebuildProfileInstanceUi(instance);

        wrapper.Children().Append(background);
        WireUpHaHover(wrapper, background);
        wrapper.Tapped(
            [instance](winrt::Windows::Foundation::IInspectable const&,
                       winrt::Windows::UI::Xaml::Input::TappedRoutedEventArgs const&) {
                HandleProfileClick(instance);
            });
        rootGrid.Children().Append(wrapper);
    } catch (...) {
    }
}

// ---------------------------------------------------------------------
// Registration loop and full-rebuild settings-change handling.
//
// NOTE (flagged for review): unlike weather's real
// TryRegisterOrShowStandalone/WeatherRetryRegisterThreadProc pair, this is
// a single one-shot attempt per profile with no background retry if
// taskbar-widget-stack's registration props aren't present yet at the
// moment this runs (e.g. load-order race where this mod's Wh_ModInit runs
// before taskbar-widget-stack's). This matches the brief's own Steps 6-7
// verbatim (no retry step is specified there), but the design spec's
// "Standalone fallback, per profile" section says each standalone instance
// should get its own retry thread "matching weather's
// WeatherRetryRegisterThreadProc pattern" - that pattern needs its own
// event/thread bookkeeping (creation in Wh_ModInit, teardown in
// Wh_ModUninit) not present here. Left out rather than half-ported,
// because with GetTaskbarXamlRoot() stubbed above (see GAP comment) a
// retry loop would only ever retry the stack-registration half anyway;
// worth reinstating together with the CTaskBand port above as one
// follow-up.
// ---------------------------------------------------------------------

void UnregisterAllProfileInstances(HWND taskbarHwnd) {
    std::lock_guard<std::mutex> lock(g_profileInstancesMutex);
    auto unregisterFn = (WidgetStack_UnregisterWidget_t)GetPropW(
        taskbarHwnd, kUnregisterWidgetPropName);
    for (auto& instance : *g_profileInstances) {
        if (instance->registeredWithStack && unregisterFn) {
            unregisterFn(instance.get());
        } else if (instance->wrapper) {
            try {
                if (auto parent =
                        instance->wrapper.Parent().try_as<Panel>()) {
                    uint32_t idx;
                    if (parent.Children().IndexOf(instance->wrapper, idx)) {
                        parent.Children().RemoveAt(idx);
                    }
                }
            } catch (...) {
            }
        }
    }
    g_profileInstances->clear();
}

void RegisterAllProfileInstances(HWND taskbarHwnd) {
    std::vector<ProfileConfig> profiles;
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        profiles = g_profileConfigs;
    }

    auto registerFn = (WidgetStack_RegisterWidget_t)GetPropW(
        taskbarHwnd, kRegisterWidgetPropName);
    auto unregisterFn = (WidgetStack_UnregisterWidget_t)GetPropW(
        taskbarHwnd, kUnregisterWidgetPropName);
    bool stackAvailable = registerFn && unregisterFn;

    std::lock_guard<std::mutex> lock(g_profileInstancesMutex);
    for (auto& profile : profiles) {
        auto instance = std::make_unique<HomeAssistantProfileInstance>();
        instance->config = profile;

        if (stackAvailable) {
            WidgetStackWidgetAbiV1 abi{};
            abi.context = instance.get();
            abi.Create = HaWidget_Create;
            abi.Tick = HaWidget_Tick;
            abi.OnSettingsChanged = HaWidget_OnSettingsChanged;
            abi.Destroy = HaWidget_Destroy;
            abi.GetId = HaWidget_GetId;
            abi.GetDisplayName = HaWidget_GetDisplayName;

            // Set true before calling registerFn: the host's own
            // RebuildStackContents() calls Create() synchronously inside
            // this call (same ordering requirement as
            // taskbar-widget-weather's own registration).
            instance->registeredWithStack = true;
            if (!registerFn(&abi)) {
                instance->registeredWithStack = false;
                InjectProfileStandalone(instance.get(), taskbarHwnd);
            }
        } else {
            InjectProfileStandalone(instance.get(), taskbarHwnd);
        }

        g_profileInstances->push_back(std::move(instance));
    }
}

void RebuildAllProfileInstances(HWND taskbarHwnd) {
    UnregisterAllProfileInstances(taskbarHwnd);
    LoadSettings();
    RegisterAllProfileInstances(taskbarHwnd);
}

// ---------------------------------------------------------------------
// Mod entry points (HWND declared here; the WebSocket engine below needs
// it before this point, so it's forward-declared just above that section).
// ---------------------------------------------------------------------

extern HWND g_haTaskbarHwnd;

// ---------------------------------------------------------------------
// WebSocket connection engine (Task 3): one shared connection to the
// configured Home Assistant instance - auth handshake, get_states,
// subscribe_events for state_changed, call_service for control - with
// reconnect/backoff and a mutex-guarded entity-state map that fans updates
// out to whichever registered profile instances care about each changed
// entity id.
// ---------------------------------------------------------------------

std::atomic<HaConnectionState> g_haConnectionState{HaConnectionState::Disconnected};
std::map<std::wstring, EntityState> g_entityStates;
std::mutex g_entityStatesMutex;
std::atomic<int> g_haNextMessageId{1};

EntityState GetEntityState(const std::wstring& entityId) {
    std::lock_guard<std::mutex> lock(g_entityStatesMutex);
    auto it = g_entityStates.find(entityId);
    if (it == g_entityStates.end()) {
        return EntityState{};
    }
    return it->second;
}

// Parses one HA "state object" (as returned by get_states, or as the
// new_state field of a state_changed event) into an EntityState. Returns
// false (leaving `out` untouched) if entityId/state fields are missing.
bool ParseHaStateObject(const std::wstring& entityId,
                         JsonObject const& stateObj,
                         EntityState& out) {
    if (!stateObj) {
        return false;
    }
    EntityState result;
    result.entityId = entityId;
    result.domain = DomainOf(entityId);
    try {
        result.state = std::wstring(stateObj.GetNamedString(L"state", L""));
    } catch (...) {
        return false;
    }
    try {
        auto attrs = stateObj.GetNamedObject(L"attributes", nullptr);
        if (attrs) {
            result.friendlyName =
                std::wstring(attrs.GetNamedString(L"friendly_name", L""));
            result.unitOfMeasurement = std::wstring(
                attrs.GetNamedString(L"unit_of_measurement", L""));
        }
    } catch (...) {
    }
    result.hasData = true;
    out = std::move(result);
    return true;
}

void NotifyEntityChanged(const std::wstring& entityId) {
    std::vector<HomeAssistantProfileInstance*> affected;
    {
        std::lock_guard<std::mutex> lock(g_profileInstancesMutex);
        for (auto& instance : *g_profileInstances) {
            auto& ids = instance->config.entityIds;
            if (std::find(ids.begin(), ids.end(), entityId) != ids.end()) {
                affected.push_back(instance.get());
            }
        }
    }
    if (affected.empty() || !g_haTaskbarHwnd) {
        return;
    }
    for (auto* instance : affected) {
        RunFromWindowThread(g_haTaskbarHwnd, [](void* param) {
            RebuildProfileInstanceUi((HomeAssistantProfileInstance*)param);
        }, instance);
    }
}

// g_haStopEvent: a raw HANDLE, safe without [[clang::no_destroy]] per the
// skill doc (no destructor) as long as Wh_ModUninit signals/waits/closes it
// - StopHaWebSocketThread below does exactly that, and is called first
// thing in Wh_ModUninit.
HANDLE g_haStopEvent = nullptr;
// Strong WinRT/COM references (MessageWebSocket, DataWriter) - need
// [[clang::no_destroy]]. Unlike the XAML globals above, these aren't
// UI-thread-affine (they're only ever touched from the WebSocket thread,
// under g_haSocketMutex), so Wh_ModUninit releases them directly rather
// than via RunFromWindowThread - StopHaWebSocketThread has already joined
// that thread by the time Wh_ModUninit does so.
[[clang::no_destroy]] winrt::Windows::Networking::Sockets::MessageWebSocket
    g_haSocket{nullptr};
[[clang::no_destroy]] winrt::Windows::Storage::Streams::DataWriter g_haWriter{
    nullptr};
std::mutex g_haSocketMutex;

void SendHaMessage(JsonObject const& message) {
    std::lock_guard<std::mutex> lock(g_haSocketMutex);
    if (!g_haWriter) {
        return;
    }
    try {
        g_haWriter.WriteString(message.Stringify());
        g_haWriter.StoreAsync();
    } catch (...) {
    }
}

void CallHaService(const std::wstring& domain,
                    const std::wstring& service,
                    const std::wstring& entityId) {
    if (g_haConnectionState.load() != HaConnectionState::Connected) {
        return;
    }
    JsonObject serviceData;
    serviceData.SetNamedValue(L"entity_id", JsonValue::CreateStringValue(entityId));
    JsonObject msg;
    msg.SetNamedValue(L"id", JsonValue::CreateNumberValue(
                                  g_haNextMessageId.fetch_add(1)));
    msg.SetNamedValue(L"type", JsonValue::CreateStringValue(L"call_service"));
    msg.SetNamedValue(L"domain", JsonValue::CreateStringValue(domain));
    msg.SetNamedValue(L"service", JsonValue::CreateStringValue(service));
    msg.SetNamedValue(L"service_data", serviceData);
    SendHaMessage(msg);
}

void HandleHaStateChangedEvent(JsonObject const& event) {
    try {
        auto data = event.GetNamedObject(L"data", nullptr);
        if (!data) {
            return;
        }
        auto entityId = std::wstring(data.GetNamedString(L"entity_id", L""));
        if (entityId.empty()) {
            return;
        }
        auto newState = data.GetNamedObject(L"new_state", nullptr);
        if (!newState) {
            return;
        }
        EntityState parsed;
        if (!ParseHaStateObject(entityId, newState, parsed)) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_entityStatesMutex);
            g_entityStates[entityId] = parsed;
        }
        NotifyEntityChanged(entityId);
    } catch (...) {
    }
}

void HandleHaMessage(const winrt::hstring& rawMessage) {
    JsonObject msg{nullptr};
    try {
        msg = JsonObject::Parse(rawMessage);
    } catch (...) {
        return;
    }
    std::wstring type;
    try {
        type = std::wstring(msg.GetNamedString(L"type", L""));
    } catch (...) {
        return;
    }

    if (type == L"auth_ok") {
        g_haConnectionState.store(HaConnectionState::Connected);
        JsonObject getStates;
        getStates.SetNamedValue(L"id", JsonValue::CreateNumberValue(
                                            g_haNextMessageId.fetch_add(1)));
        getStates.SetNamedValue(L"type",
                                 JsonValue::CreateStringValue(L"get_states"));
        SendHaMessage(getStates);

        JsonObject subscribe;
        subscribe.SetNamedValue(L"id", JsonValue::CreateNumberValue(
                                            g_haNextMessageId.fetch_add(1)));
        subscribe.SetNamedValue(
            L"type", JsonValue::CreateStringValue(L"subscribe_events"));
        subscribe.SetNamedValue(
            L"event_type", JsonValue::CreateStringValue(L"state_changed"));
        SendHaMessage(subscribe);
    } else if (type == L"auth_invalid") {
        g_haConnectionState.store(HaConnectionState::AuthFailed);
    } else if (type == L"event") {
        try {
            auto event = msg.GetNamedObject(L"event", nullptr);
            if (event) {
                auto eventType =
                    std::wstring(event.GetNamedString(L"event_type", L""));
                if (eventType == L"state_changed") {
                    HandleHaStateChangedEvent(event);
                }
            }
        } catch (...) {
        }
    } else if (type == L"result") {
        // get_states' bulk response: an array under "result", each entry a
        // full state object with its own top-level "entity_id".
        try {
            bool success = msg.GetNamedBoolean(L"success", false);
            auto resultValue = msg.GetNamedValue(L"result", nullptr);
            if (success && resultValue &&
                resultValue.ValueType() == JsonValueType::Array) {
                auto array = resultValue.GetArray();
                for (auto const& item : array) {
                    auto obj = item.GetObject();
                    auto entityId =
                        std::wstring(obj.GetNamedString(L"entity_id", L""));
                    if (entityId.empty()) {
                        continue;
                    }
                    EntityState parsed;
                    if (ParseHaStateObject(entityId, obj, parsed)) {
                        std::lock_guard<std::mutex> lock(g_entityStatesMutex);
                        g_entityStates[entityId] = parsed;
                    }
                }
                // Bulk initial population: refresh every currently
                // registered instance once, rather than one
                // NotifyEntityChanged call per entity.
                std::vector<HomeAssistantProfileInstance*> all;
                {
                    std::lock_guard<std::mutex> lock(g_profileInstancesMutex);
                    for (auto& instance : *g_profileInstances) {
                        all.push_back(instance.get());
                    }
                }
                if (g_haTaskbarHwnd) {
                    for (auto* instance : all) {
                        RunFromWindowThread(g_haTaskbarHwnd, [](void* param) {
                            RebuildProfileInstanceUi(
                                (HomeAssistantProfileInstance*)param);
                        }, instance);
                    }
                }
            }
        } catch (...) {
        }
    }
}

DWORD WINAPI HaWebSocketThreadProc(LPVOID) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    int backoffSeconds = 1;

    while (WaitForSingleObject(g_haStopEvent, 0) != WAIT_OBJECT_0) {
        std::wstring serverUrl, accessToken;
        bool useTls;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            serverUrl = g_settings.serverUrl;
            useTls = g_settings.useTls;
            accessToken = g_settings.accessToken;
        }
        if (serverUrl.empty() || accessToken.empty()) {
            WaitForSingleObject(g_haStopEvent, 5000);
            continue;
        }

        g_haConnectionState.store(HaConnectionState::Connecting);
        try {
            winrt::Windows::Networking::Sockets::MessageWebSocket socket;
            socket.Control().MessageType(
                winrt::Windows::Networking::Sockets::SocketMessageType::Utf8);

            winrt::handle authDone(
                CreateEvent(nullptr, TRUE, FALSE, nullptr));
            bool authFailed = false;

            socket.MessageReceived(
                [&](auto const&,
                    winrt::Windows::Networking::Sockets::
                        MessageWebSocketMessageReceivedEventArgs const& args) {
                    try {
                        auto reader = args.GetDataReader();
                        reader.UnicodeEncoding(
                            winrt::Windows::Storage::Streams::
                                UnicodeEncoding::Utf8);
                        auto text = reader.ReadString(
                            reader.UnconsumedBufferLength());
                        HandleHaMessage(text);
                        if (g_haConnectionState.load() ==
                                HaConnectionState::Connected ||
                            g_haConnectionState.load() ==
                                HaConnectionState::AuthFailed) {
                            SetEvent(authDone.get());
                        }
                    } catch (...) {
                    }
                });

            socket.Closed([&](auto const&, auto const&) {
                SetEvent(authDone.get());
            });

            std::wstring scheme = useTls ? L"wss://" : L"ws://";
            std::wstring uriText = scheme + serverUrl + L"/api/websocket";
            winrt::Windows::Foundation::Uri uri{uriText};

            socket.ConnectAsync(uri).get();
            {
                std::lock_guard<std::mutex> lock(g_haSocketMutex);
                g_haWriter = winrt::Windows::Storage::Streams::DataWriter(
                    socket.OutputStream());
                g_haSocket = socket;
            }

            // auth_required arrives via MessageReceived like everything
            // else; the handshake itself is: wait for auth_required (any
            // message before auth_ok/auth_invalid), send auth, then wait
            // for auth_ok/auth_invalid via authDone.
            JsonObject authMsg;
            authMsg.SetNamedValue(L"type", JsonValue::CreateStringValue(L"auth"));
            authMsg.SetNamedValue(
                L"access_token", JsonValue::CreateStringValue(accessToken));
            SendHaMessage(authMsg);

            WaitForSingleObject(authDone.get(), 15000);

            if (g_haConnectionState.load() == HaConnectionState::AuthFailed) {
                authFailed = true;
            } else if (g_haConnectionState.load() ==
                       HaConnectionState::Connected) {
                backoffSeconds = 1;
                // Block here until disconnected/stopped - state_changed
                // events keep arriving via MessageReceived on this same
                // socket in the background.
                HANDLE waitHandles[] = {g_haStopEvent, authDone.get()};
                WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            }

            {
                std::lock_guard<std::mutex> lock(g_haSocketMutex);
                g_haWriter = nullptr;
                g_haSocket = nullptr;
            }
            socket.Close(1000, L"");

            if (authFailed) {
                // Not recoverable by retrying - stop looping until the
                // next settings change (Wh_ModSettingsChanged restarts
                // this thread fresh; see below).
                break;
            }
        } catch (...) {
            g_haConnectionState.store(HaConnectionState::Disconnected);
        }

        if (WaitForSingleObject(g_haStopEvent, backoffSeconds * 1000) ==
            WAIT_OBJECT_0) {
            break;
        }
        backoffSeconds = std::min(backoffSeconds * 2, 60);
    }

    winrt::uninit_apartment();
    return 0;
}

HANDLE g_haWebSocketThread = nullptr;

void StartHaWebSocketThread() {
    g_haStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    g_haWebSocketThread =
        CreateThread(nullptr, 0, HaWebSocketThreadProc, nullptr, 0, nullptr);
}

void StopHaWebSocketThread() {
    if (g_haStopEvent) {
        SetEvent(g_haStopEvent);
    }
    if (g_haWebSocketThread) {
        WaitForSingleObject(g_haWebSocketThread, 5000);
        CloseHandle(g_haWebSocketThread);
        g_haWebSocketThread = nullptr;
    }
    if (g_haStopEvent) {
        CloseHandle(g_haStopEvent);
        g_haStopEvent = nullptr;
    }
}

// ---------------------------------------------------------------------
// Mod entry points.
// ---------------------------------------------------------------------

HWND g_haTaskbarHwnd = nullptr;

BOOL Wh_ModInit() {
    LoadSettings();
    g_haTaskbarHwnd = FindWindow(L"Shell_TrayWnd", nullptr);
    if (g_haTaskbarHwnd) {
        RegisterAllProfileInstances(g_haTaskbarHwnd);
    }
    StartHaWebSocketThread();
    return TRUE;
}

void Wh_ModSettingsChanged() {
    if (g_haTaskbarHwnd) {
        RunFromWindowThread(
            g_haTaskbarHwnd,
            [](void*) {
                // Reset the cached HoverBrush/PressedBrush style-token
                // brushes (EnsureHaHoverBrushes reads/writes these globals
                // on) before rebuilding, so an edited HoverBrush/
                // PressedBrush style constant takes effect immediately -
                // mirrors weather's own InvalidateHoverBrushCache call
                // site.
                InvalidateHaHoverBrushCache();
                RebuildAllProfileInstances(g_haTaskbarHwnd);
                StopHaWebSocketThread();
                StartHaWebSocketThread();
            },
            nullptr);
    }
}

void Wh_ModUninit() {
    // 1. Stop and join the WebSocket thread first - it's the only other
    //    thread this mod runs, and everything released below (the socket/
    //    writer it owns, the profile instances it notifies via
    //    RunFromWindowThread) must not still be in use by it. This also
    //    signals/waits/CloseHandles g_haStopEvent and g_haWebSocketThread
    //    themselves (plain HANDLEs, no [[clang::no_destroy]] needed).
    StopHaWebSocketThread();

    // 2. Strong WinRT/COM references owned by the WebSocket engine
    //    (MessageWebSocket, DataWriter). Not UI-thread-affine, so released
    //    directly here rather than via RunFromWindowThread - the thread
    //    that used to own them has already been joined in step 1, and in
    //    the normal case HaWebSocketThreadProc has already nulled these out
    //    itself before exiting; this is the explicit, unconditional release
    //    that also covers the case where the thread never reached that
    //    point (e.g. still waiting on empty serverUrl/accessToken).
    {
        std::lock_guard<std::mutex> lock(g_haSocketMutex);
        g_haWriter = nullptr;
        g_haSocket = nullptr;
    }

    // 3. XAML/UI-thread-affine globals: g_profileInstances (each instance's
    //    Grid wrapper/Border background), g_haProfileFlyout, and the
    //    HoverBrush/PressedBrush/PressedBorderBrush trio. Per the skill
    //    doc's "thread identity depends on how the mod was loaded" note,
    //    Wh_ModUninit's own calling thread isn't guaranteed to be the
    //    taskbar's UI thread (a mod injected into an already-running
    //    explorer.exe runs every lifecycle callback on the Windhawk Engine
    //    thread instead) - marshal via RunFromWindowThread to be safe
    //    either way; RunFromWindowThread itself already short-circuits to a
    //    direct call when the current thread already is the target
    //    thread, so this is correct (and cheap) even when Wh_ModUninit does
    //    happen to already be running on the UI thread.
    //
    //    UnregisterAllProfileInstances handles detaching/unregistering each
    //    instance's wrapper (existing Task-2/3 logic, now also run through
    //    this marshal instead of assuming its caller already runs on the
    //    right thread); the explicit g_profileInstances.reset() after it
    //    fully releases the optional's own vector buffer (its own
    //    .clear() call, still present inside UnregisterAllProfileInstances,
    //    would otherwise leave that buffer allocated - see the skill doc's
    //    vector::clear()-is-a-partial-leak note).
    //
    //    If g_haTaskbarHwnd was never found (Wh_ModInit's FindWindow came
    //    back empty), there's no parent to unregister these instances'
    //    wrappers from in the first place - just drop the WinRT references
    //    directly.
    if (g_haTaskbarHwnd) {
        bool marshaled = RunFromWindowThread(
            g_haTaskbarHwnd,
            [](void*) {
                UnregisterAllProfileInstances(g_haTaskbarHwnd);
                g_profileInstances.reset();
                g_haProfileFlyout = nullptr;
                g_haHoverBrush = nullptr;
                g_haPressedBrush = nullptr;
                g_haPressedBorderBrush = nullptr;
            },
            nullptr);
        if (!marshaled) {
            // Couldn't marshal onto the UI thread (stale window handle,
            // hook-install failure, or the target thread being hung and
            // timing out per RunFromWindowThread's SendMessageTimeoutW
            // above). Log and accept the resulting resource leak rather
            // than falling back to an unsafe same-thread/off-UI-thread
            // release of these XAML objects - releasing XAML off its
            // owning thread is itself the exact hazard this shutdown-safety
            // work exists to avoid, so a logged, skipped cleanup is
            // strictly safer than a same-thread fallback that could crash.
            Wh_Log(
                L"Failed to marshal XAML global cleanup onto the taskbar UI "
                L"thread; skipping cleanup to avoid an unsafe off-thread "
                L"release (leaking g_profileInstances/g_haProfileFlyout/"
                L"hover brushes).");
        }
    } else {
        g_profileInstances.reset();
        g_haProfileFlyout = nullptr;
        g_haHoverBrush = nullptr;
        g_haPressedBrush = nullptr;
        g_haPressedBorderBrush = nullptr;
    }
}
