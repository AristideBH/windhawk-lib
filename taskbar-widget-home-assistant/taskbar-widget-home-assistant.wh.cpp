// ==WindhawkMod==
// @id              taskbar-widget-home-assistant
// @name            Taskbar Widget: Home Assistant
// @description     Shows and controls Home Assistant entities in the taskbar. Registers into taskbar-widget-stack's pane if installed, falls back to standalone injection otherwise.
// @version         0.1.0
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

    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = std::move(newSettings);
    g_profileConfigs = std::move(newProfiles);
}
