// ==WindhawkMod==
// @id              taskbar-widget-home-assistant
// @name            Taskbar Widget: Home Assistant
// @description     Shows and controls Home Assistant entities in the taskbar. Registers into taskbar-widget-stack's pane if installed, falls back to standalone injection otherwise.
// @version         0.3.0
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

std::vector<std::unique_ptr<HomeAssistantProfileInstance>> g_profileInstances;
std::mutex g_profileInstancesMutex;

// Minimal placeholder - Tasks 4/5/6 replace this body with real per-mode
// rendering (single/multi/dashboard). Signature is final; do not change it.
FrameworkElement BuildCompactView(HomeAssistantProfileInstance* instance) {
    TextBlock text;
    text.Text(winrt::hstring(instance->config.displayName.empty()
                                  ? instance->config.profileId
                                  : instance->config.displayName));
    return text;
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
    SendMessageW(hWnd, kMsg, 0, reinterpret_cast<LPARAM>(&pay));
    UnhookWindowsHookEx(hook);
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

        Border background;
        background.CornerRadius({5.33, 5.33, 5.33, 5.33});
        background.Margin({0, 3, 0, 3});

        instance->wrapper = wrapper;
        instance->background = background;
        RebuildProfileInstanceUi(instance);

        wrapper.Children().Append(background);
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

        Border background;
        background.CornerRadius({5.33, 5.33, 5.33, 5.33});
        background.Margin({0, 3, 0, 3});

        instance->wrapper = wrapper;
        instance->background = background;
        instance->taskbarHwnd = taskbarHwnd;
        RebuildProfileInstanceUi(instance);

        wrapper.Children().Append(background);
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
    for (auto& instance : g_profileInstances) {
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
    g_profileInstances.clear();
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

        g_profileInstances.push_back(std::move(instance));
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
        for (auto& instance : g_profileInstances) {
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

HANDLE g_haStopEvent = nullptr;
winrt::Windows::Networking::Sockets::MessageWebSocket g_haSocket{nullptr};
winrt::Windows::Storage::Streams::DataWriter g_haWriter{nullptr};
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
                    for (auto& instance : g_profileInstances) {
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
            g_haWriter = winrt::Windows::Storage::Streams::DataWriter(
                socket.OutputStream());
            {
                std::lock_guard<std::mutex> lock(g_haSocketMutex);
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
                RebuildAllProfileInstances(g_haTaskbarHwnd);
                StopHaWebSocketThread();
                StartHaWebSocketThread();
            },
            nullptr);
    }
}

void Wh_ModUninit() {
    StopHaWebSocketThread();
    if (g_haTaskbarHwnd) {
        UnregisterAllProfileInstances(g_haTaskbarHwnd);
    }
}
