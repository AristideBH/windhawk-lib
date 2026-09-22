# Home Assistant Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a new Windhawk mod, `taskbar-widget-home-assistant`, that registers multiple
independent taskbar widgets (one per configured "profile") sharing a single WebSocket connection to
a Home Assistant instance, with `single`/`multi`/`dashboard` display modes and a `StyleSettings`
customization system matching `taskbar-widget-weather`'s.

**Architecture:** One mod file. `Wh_ModInit()` loads settings, opens one shared WebSocket connection
on its own thread, and registers one `WidgetStackWidgetAbiV1` per profile (using the ABI's existing
`context` field to disambiguate instances — no ABI change needed). The WebSocket thread owns a
mutex-guarded `entity_id -> EntityState` map; on each `state_changed` event it updates the map and
marshals a UI rebuild onto the taskbar's window thread for every profile instance whose configured
entities changed.

**Tech Stack:** C++/WinRT, Windhawk mod SDK, `winrt::Windows::Networking::Sockets::MessageWebSocket`,
`winrt::Windows::Data::Json`.

**Spec:** [docs/superpowers/specs/2026-09-22-home-assistant-widget-design.md](../specs/2026-09-22-home-assistant-widget-design.md)

## Global Constraints

- New file: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`. No other mod file
  is touched by this plan.
- No shared header exists between mods in this repo. Every helper this mod needs — including ones
  that look identical to code in `taskbar-widget-weather.wh.cpp` (style-constants engine,
  `ParseRgbColor`, the widget-stack ABI structs, `RunFromWindowThread`) — must be written fresh into
  this file, byte-for-byte matching the ABI struct layouts (field order and types) since those two
  DLLs communicate through them with no shared type definitions.
- No automated test harness exists for Windhawk mods in this repo, and no compiler is available in
  this environment. "Testing" a step means reading the finished code back and confirming it matches
  this plan's exact signatures, types, and logic — the user compiles and live-tests by loading the
  mod into Windhawk themselves, against a real Home Assistant instance, after the plan is complete.
- `@compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32` (same as `taskbar-widget-weather`) —
  `Windows.Networking.Sockets`/`Windows.Data.Json` are WinRT projections backed by the same
  combase/runtimeobject infrastructure weather already links against for `Windows.Web.Http`; no
  additional linker flags are expected, but if a task's implementer hits an unresolved-symbol
  concern they cannot verify without a compiler, they note it in their report rather than guessing.
- Match `taskbar-widget-weather.wh.cpp`'s conventions throughout: `try/catch (...)` around anything
  that can throw (JSON parsing, WinRT calls that touch live UI/network state), `std::lock_guard`
  around every access to a mutex-guarded global, comments only where the reasoning isn't obvious
  from the code itself, state structs copied in/out of locks rather than held across UI work.
- Domain icons use plain Unicode/emoji characters (matching weather's own `L"☁️"` fallback
  convention), not guessed Segoe Fluent Icons private-use-area codepoints — this repo has a
  documented past incident (referenced in `taskbar-widget-stack`'s own `PLAN.md`) of guessed
  codepoints rendering wrong or missing. Exact glyphs: `L"💡"` (light), `L"🔌"` (switch), `L"📊"`
  (sensor), `L"❔"` (generic/unknown domain), `L"⚠️"` (connection failed / auth invalid).
- Every settings array-of-strings or array-of-groups default is `[""]` (one empty-string
  placeholder), never `[]` — a real bug already hit and fixed in the weather widget's own
  `styleConstants`/`styleAliases` settings; verified against Windows 11 Taskbar Styler's published
  source for both the flat (`styleConstants[%d]`) and grouped (`controlStyles[%d].target`,
  `controlStyles[%d].styles[%d]`) forms.

---

### Task 1: Mod skeleton, settings schema, and core data structures

**Files:**
- Create: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`
- Create: `taskbar-widget-home-assistant/PLAN.md`

**Interfaces:**
- Produces (consumed by every later task): the mod metadata header, the full settings schema, and
  these types/globals:
  - `enum class ProfileMode { Single, Multi, Dashboard };`
  - `struct EntityState { std::wstring entityId, domain, state, friendlyName, unitOfMeasurement; bool hasData = false; };`
  - `enum class HaConnectionState { Disconnected, Connecting, AuthFailed, Connected };`
  - `struct HaSettings { std::wstring serverUrl; bool useTls = false; std::wstring accessToken; };`
  - `struct ProfileConfig { std::wstring profileId, displayName; ProfileMode mode; std::vector<std::wstring> entityIds; };`
  - `void LoadSettings();` populating `HaSettings g_settings` and `std::vector<ProfileConfig> g_profileConfigs`, both mutex-guarded (`g_settingsMutex`).
  - `std::wstring DomainOf(const std::wstring& entityId);` — substring before the first `.`.

- [ ] **Step 1: Write the mod metadata header**

```cpp
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
```

- [ ] **Step 2: Write the full settings schema**

```cpp
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
```

- [ ] **Step 3: Write includes and using-namespace block**

```cpp
#include <windhawk_utils.h>

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
```

- [ ] **Step 4: Define core enums/structs and the domain helper**

```cpp
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
```

- [ ] **Step 5: Write the settings array-parsing helpers**

Reuses the exact convention verified against Windows 11 Taskbar Styler's published source (flat
arrays terminate on the first empty string; grouped arrays terminate when their designated
required field is empty at that index):

```cpp
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
```

- [ ] **Step 6: Write `LoadSettings()`**

Iterates `profiles[i].profileId` for the outer-loop sentinel (matching
`controlStyles[i].target`'s role exactly), reading `profiles[i].displayName`, `profiles[i].mode`,
and `profiles[i].entityIds[j]` (via `GetStringArraySetting(L"profiles[" + std::to_wstring(i) + L"].entityIds")`)
for each surviving index:

```cpp
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
```

- [ ] **Step 7: Write `PLAN.md`**

```markdown
# Taskbar Widget: Home Assistant - Development Plan

## Context

Design spec: `docs/superpowers/specs/2026-09-22-home-assistant-widget-design.md`
Implementation plan: `docs/superpowers/plans/2026-09-22-home-assistant-widget.md`

This file follows the same convention as `taskbar-widget-weather`/
`taskbar-widget-media-player`/`taskbar-widget-system-usage`/
`taskbar-widget-stack`'s own PLAN.md files: reserved for post-build,
live-tested incident logging (what broke during real use, root cause, fix),
not upfront design - the spec and plan linked above own the design record.

No live-test incidents yet - first pass pending.
```

- [ ] **Step 8: Self-check**

Read the finished file back and confirm: the settings YAML is valid (indentation matches
`taskbar-widget-weather.wh.cpp`'s own group/subgroup conventions exactly); `profiles`' default is a
one-item placeholder list (`- profileId: ""`, ...), not `[]`; `styleConstants`/`styleAliases` default
to `[""]`; every struct/enum name and field matches this task's own Interfaces section exactly (later
tasks depend on these names verbatim).

- [ ] **Step 9: Commit**

```bash
git add taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp taskbar-widget-home-assistant/PLAN.md
git commit -m "Add Home Assistant widget mod skeleton, settings schema, and core types"
```

---

### Task 2: Multi-instance ABI registration and profile lifecycle

**Files:**
- Modify: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`

**Interfaces:**
- Consumes: `ProfileConfig`, `g_profileConfigs`, `g_settingsMutex`, `LoadSettings()` from Task 1.
- Produces (consumed by Task 3 for real-data wiring, and by Tasks 4-6 for UI building):
  - `struct HomeAssistantProfileInstance { ProfileConfig config; Grid wrapper{nullptr}; Border background{nullptr}; void* taskbarHwnd = nullptr; bool registeredWithStack = false; };`
  - `std::vector<std::unique_ptr<HomeAssistantProfileInstance>> g_profileInstances; std::mutex g_profileInstancesMutex;`
  - `FrameworkElement BuildCompactView(HomeAssistantProfileInstance* instance);` (declared here, minimally implemented in this task as a plain `TextBlock` showing `instance->config.displayName.empty() ? instance->config.profileId : instance->config.displayName` — Tasks 4-6 replace this body with real per-mode rendering; the declaration/signature must not change).
  - `void RebuildProfileInstanceUi(HomeAssistantProfileInstance* instance);` — rebuilds `instance->background`'s child from `BuildCompactView(instance)`. Called by this task's own registration/settings-change code, and later by Task 3's WebSocket dispatch.

This task duplicates the widget-stack ABI (byte-identical to `taskbar-widget-weather.wh.cpp`'s own
copy) and the registration/standalone-fallback/settings-change pattern, generalized from "one
instance" to "one instance per configured profile".

- [ ] **Step 1: Duplicate the widget-stack ABI structs**

```cpp
// Cross-mod widget ABI: lets taskbar-widget-stack host this widget inside
// its shared pane instead of standalone. The struct layout below must stay
// byte-identical (field order and types) to the copy in
// taskbar-widget-stack.wh.cpp and taskbar-widget-weather.wh.cpp - there's
// no shared header across these DLLs, so this is a hand-synced ABI.
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
```

- [ ] **Step 2: Define the per-profile instance struct and the compact-view stub**

```cpp
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
```

- [ ] **Step 3: Write `RunFromWindowThread`, matching weather's marshaling pattern**

```cpp
using WindowThreadProc = void(*)(void*);

constexpr UINT kRunFromWindowThreadMessage = WM_APP + 0x1900;

struct RunFromWindowThreadPayload {
    WindowThreadProc proc;
    void* param;
};

LRESULT CALLBACK HaCallWndProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* cwp = (CWPSTRUCT*)lParam;
        if (cwp->message == kRunFromWindowThreadMessage) {
            auto* payload = (RunFromWindowThreadPayload*)cwp->lParam;
            payload->proc(payload->param);
            delete payload;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

HHOOK g_haCallWndProcHook = nullptr;

bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param) {
    if (!hWnd) {
        return false;
    }
    DWORD threadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (threadId == 0) {
        return false;
    }
    if (!g_haCallWndProcHook) {
        g_haCallWndProcHook = SetWindowsHookEx(WH_CALLWNDPROC, HaCallWndProc,
                                                nullptr, threadId);
        if (!g_haCallWndProcHook) {
            return false;
        }
    }
    auto* payload = new RunFromWindowThreadPayload{proc, param};
    if (!PostMessage(hWnd, kRunFromWindowThreadMessage, 0, (LPARAM)payload)) {
        delete payload;
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Write the five ABI callbacks**

Each casts `context` back to `HomeAssistantProfileInstance*` to know which profile it's for — this
is the whole multi-instance mechanism, no other dispatch table needed:

```cpp
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
    // (RebuildAllProfileInstances, this task's Step 6), not by this
    // per-instance callback mutating in place - present for ABI
    // completeness only.
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
    wcsncpy_s(buffer, bufferSize, instance->config.profileId.c_str(), _TRUNCATE);
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
```

- [ ] **Step 5: Write standalone-fallback injection (per instance)**

Mirrors `taskbar-widget-weather.wh.cpp`'s `InjectWeatherStandalone`/retry-thread pattern, generalized
to take an instance pointer instead of operating on module-global UI state:

```cpp
Grid FindTaskbarRootGrid(FrameworkElement rootElement) {
    if (!rootElement) {
        return nullptr;
    }
    return rootElement.FindName(L"RootGrid").try_as<Grid>();
}

void InjectProfileStandalone(HomeAssistantProfileInstance* instance,
                              HWND taskbarHwnd) {
    if (!instance || !taskbarHwnd) {
        return;
    }
    try {
        auto rootElement =
            windhawk_utils::GetXamlRootElementForHwnd(taskbarHwnd)
                .try_as<FrameworkElement>();
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
```

Note: `windhawk_utils::GetXamlRootElementForHwnd` is illustrative of the kind of helper weather's own
standalone path uses to obtain the taskbar's XAML root from an `HWND` — read
`taskbar-widget-weather.wh.cpp`'s actual `InjectWeatherStandalone` function first and use whatever
real helper/approach it uses verbatim (function name and signature may differ from this
illustration; match weather's real, working code exactly rather than this sketch).

- [ ] **Step 6: Write the registration loop and full-rebuild settings-change handling**

```cpp
void UnregisterAllProfileInstances(HWND taskbarHwnd) {
    std::lock_guard<std::mutex> lock(g_profileInstancesMutex);
    auto unregisterFn = (WidgetStack_UnregisterWidget_t)GetPropW(
        taskbarHwnd, kUnregisterWidgetPropName);
    for (auto& instance : g_profileInstances) {
        if (instance->registeredWithStack && unregisterFn) {
            unregisterFn(instance.get());
        } else if (instance->wrapper) {
            try {
                if (auto parent = instance->wrapper.Parent()
                                       .try_as<Panel>()) {
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
```

- [ ] **Step 7: Wire `Wh_ModInit`/`Wh_ModSettingsChanged`/`Wh_ModUninit`**

```cpp
HWND g_haTaskbarHwnd = nullptr;

BOOL Wh_ModInit() {
    LoadSettings();
    g_haTaskbarHwnd = FindWindow(L"Shell_TrayWnd", nullptr);
    if (g_haTaskbarHwnd) {
        RegisterAllProfileInstances(g_haTaskbarHwnd);
    }
    return TRUE;
}

void Wh_ModSettingsChanged() {
    if (g_haTaskbarHwnd) {
        RunFromWindowThread(g_haTaskbarHwnd, [](void*) {
            RebuildAllProfileInstances(g_haTaskbarHwnd);
        }, nullptr);
    }
}

void Wh_ModUninit() {
    if (g_haTaskbarHwnd) {
        UnregisterAllProfileInstances(g_haTaskbarHwnd);
    }
    if (g_haCallWndProcHook) {
        UnhookWindowsHookEx(g_haCallWndProcHook);
        g_haCallWndProcHook = nullptr;
    }
}
```

Note: the `Wh_ModSettingsChanged` lambda above has an empty capture list (`[]`) and reaches
`g_haTaskbarHwnd` as a global rather than capturing it — this is required, not stylistic:
`RunFromWindowThread`'s `WindowThreadProc` parameter type is a plain `void(*)(void*)` function
pointer, which only a non-capturing lambda converts to implicitly. Keep every lambda passed to
`RunFromWindowThread` in this file non-capturing the same way (pass any needed data through the
`void* param` argument instead, as `NotifyEntityChanged` in Task 3 already does).

- [ ] **Step 8: Self-check**

Read the finished diff back and confirm: the ABI struct layout is byte-identical field-for-field to
`taskbar-widget-weather.wh.cpp`'s copy; every one of the five callbacks correctly casts `context`
back to `HomeAssistantProfileInstance*` and never touches any other instance's state; `GetId`/
`GetDisplayName` never overflow `buffer` (use `wcsncpy_s`/`_TRUNCATE` as shown, never raw `wcscpy`);
`RebuildAllProfileInstances` fully unregisters before reloading settings and re-registering (no
leaked/duplicate instances across a settings change); every lambda passed to `RunFromWindowThread`
has an empty capture list.

- [ ] **Step 9: Commit**

```bash
git add taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp
git commit -m "Add multi-instance ABI registration and profile lifecycle for Home Assistant widget"
```

---

### Task 3: WebSocket connection engine

**Files:**
- Modify: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`

**Interfaces:**
- Consumes: `EntityState`, `HaConnectionState`, `HaSettings`, `g_settings`, `g_settingsMutex`,
  `DomainOf` from Task 1; `HomeAssistantProfileInstance`, `g_profileInstances`,
  `g_profileInstancesMutex`, `RebuildProfileInstanceUi`, `RunFromWindowThread`, `g_haTaskbarHwnd`
  from Task 2.
- Produces (consumed by Tasks 4-6 for rendering): `std::map<std::wstring, EntityState> g_entityStates;
  std::mutex g_entityStatesMutex;`, `std::atomic<HaConnectionState> g_haConnectionState;`,
  `EntityState GetEntityState(const std::wstring& entityId);` (returns a default-constructed
  `EntityState` with `hasData = false` if unknown — never throws, never blocks longer than the lock),
  `void CallHaService(const std::wstring& domain, const std::wstring& service, const std::wstring& entityId);`
  (fire-and-forget toggle/service call over the shared connection).

- [ ] **Step 1: Write the connection-state global and JSON helpers**

```cpp
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
```

- [ ] **Step 2: Write the "which profiles care about this entity" dispatch**

```cpp
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
```

(The lambda above passes `instance` through `RunFromWindowThread`'s `void* param` argument rather
than capturing it, keeping its capture list empty — same requirement as Task 2 Step 7's note.)

- [ ] **Step 3: Write the WebSocket thread**

```cpp
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
                // this thread fresh; see Step 4).
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
```

- [ ] **Step 4: Wire thread start/stop into `Wh_ModInit`/`Wh_ModUninit`/`Wh_ModSettingsChanged`**

Modify Task 2's `Wh_ModInit`/`Wh_ModUninit`/`Wh_ModSettingsChanged` (read their current exact text
first — this step edits them, it doesn't replace them wholesale):

```cpp
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
```

In `Wh_ModInit()`, call `StartHaWebSocketThread();` after `RegisterAllProfileInstances(...)`. In
`Wh_ModUninit()`, call `StopHaWebSocketThread();` before `UnregisterAllProfileInstances(...)`. In
`Wh_ModSettingsChanged()`'s `RebuildAllProfileInstances` path, also stop and restart the WebSocket
thread (`StopHaWebSocketThread(); StartHaWebSocketThread();`) so a token/URL change takes effect
immediately and an `AuthFailed` thread-exit gets a fresh chance after the user fixes their settings —
add this inside the same `RunFromWindowThread` callback Task 2 Step 7 already wired up, around the
existing `RebuildAllProfileInstances` call.

- [ ] **Step 5: Self-check**

Read the finished diff back and confirm: every `JsonObject`/`JsonValue` access that can throw (a
missing key, a type mismatch) is inside a `try/catch`; `g_haSocketMutex` guards every read/write of
`g_haWriter`/`g_haSocket`; the `authFailed` exit path actually breaks the outer reconnect loop
(never retries a bad token); `backoffSeconds` resets to `1` only on a successful `Connected`
transition, not on every loop iteration; `NotifyEntityChanged`/the `result` handler's bulk-refresh
path both go through `RunFromWindowThread` (never touch XAML objects directly from the WebSocket
thread).

- [ ] **Step 6: Commit**

```bash
git add taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp
git commit -m "Add WebSocket connection engine for Home Assistant widget"
```

---

### Task 4: Single-entity mode UI (compact + panel)

**Files:**
- Modify: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`

**Interfaces:**
- Consumes: `GetEntityState`, `CallHaService`, `EntityState` from Task 3; `HomeAssistantProfileInstance`,
  `ProfileMode`, `BuildCompactView` (replaces the Task-2 stub body) from Task 2.
- Produces (consumed by Task 7 for style-token wiring, and reused conceptually by Tasks 5/6):
  `FrameworkElement BuildSingleEntityCompactView(HomeAssistantProfileInstance* instance);`,
  `std::wstring DomainIcon(const std::wstring& domain);`, `void ShowProfilePanel(HomeAssistantProfileInstance* instance);`
  (Flyout open, reused by every mode — this task implements it generically enough that Tasks 5/6 only
  add their own panel-content-building function, not a new `ShowProfilePanel`).

- [ ] **Step 1: Write the domain-icon lookup**

```cpp
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
```

- [ ] **Step 2: Write the single-entity compact view**

```cpp
constexpr double kHaIconFontSize = 20;
constexpr double kHaStateFontSize = 12;

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

    TextBlock icon;
    icon.FontSize(kHaIconFontSize);
    icon.VerticalAlignment(VerticalAlignment::Center);
    icon.Text(winrt::hstring(DomainIcon(state.domain.empty()
                                             ? DomainOf(entityId)
                                             : state.domain)));
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
```

- [ ] **Step 3: Wire click handling for single-entity toggle**

```cpp
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
```

Wire `HandleProfileClick(instance)` to `instance->wrapper`'s `PointerPressed` (or `Tapped`) event in
`HaWidget_Create`/`InjectProfileStandalone` (Task 2) — read those two functions' current text and
add the event handler registration right after `instance->background` is appended, following
whatever pointer-event pattern `taskbar-widget-weather.wh.cpp`'s own `WireUpClickActions` uses (a
single left-click handler is sufficient for this mod; this repo's richer `ClickActionSettings`
group from weather is explicitly not part of this mod's settings schema per the spec).

- [ ] **Step 4: Write `ShowProfilePanel` and the single-entity panel content**

```cpp
Flyout g_haProfileFlyout{nullptr};

FrameworkElement BuildSingleEntityPanelContent(
    HomeAssistantProfileInstance* instance) {
    StackPanel root;
    root.MinWidth(360);
    root.MaxWidth(360);
    root.Padding({16, 16, 16, 16});

    if (instance->config.entityIds.empty()) {
        TextBlock placeholder;
        placeholder.Text(L"No entity configured for this profile.");
        root.Children().Append(placeholder);
        return root;
    }

    const std::wstring& entityId = instance->config.entityIds[0];
    EntityState state = GetEntityState(entityId);

    TextBlock icon;
    icon.FontSize(kHaIconFontSize * 2);
    icon.Text(winrt::hstring(DomainIcon(
        state.domain.empty() ? DomainOf(entityId) : state.domain)));
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

    return root;
}

// Dispatches to the right panel-content builder by mode. Tasks 5/6 extend
// this switch with their own Multi/Dashboard branches - do not duplicate
// ShowProfilePanel itself.
FrameworkElement BuildProfilePanelContent(HomeAssistantProfileInstance* instance) {
    switch (instance->config.mode) {
        case ProfileMode::Single:
            return BuildSingleEntityPanelContent(instance);
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
        FlyoutBase::SetAttachedFlyout(instance->background, flyout);
        FlyoutBase::ShowAttachedFlyout(instance->background);
    } catch (...) {
    }
}
```

- [ ] **Step 5: Replace Task 2's `BuildCompactView` stub body**

Modify the `BuildCompactView` function Task 2 declared (keep its signature exactly as-is):

```cpp
FrameworkElement BuildCompactView(HomeAssistantProfileInstance* instance) {
    switch (instance->config.mode) {
        case ProfileMode::Single:
            return BuildSingleEntityCompactView(instance);
        default:
            // Tasks 5/6 replace this default case with real Multi/
            // Dashboard compact views. Until then, fall back to the
            // single-entity view over the profile's first configured
            // entity (if any) so every mode renders something real and
            // non-empty, never a placeholder string.
            return BuildSingleEntityCompactView(instance);
    }
}
```

- [ ] **Step 6: Self-check**

Read the finished diff back and confirm: `BuildCompactView`'s signature is unchanged from Task 2's
declaration; `HandleProfileClick` only calls `CallHaService` for `light`/`switch` domains, never
`sensor`/generic; `ShowProfilePanel`/`BuildProfilePanelContent` are written once, generically, not
duplicated per mode; every `EntityState` read goes through `GetEntityState` (never reads
`g_entityStates` directly, bypassing its mutex).

- [ ] **Step 7: Commit**

```bash
git add taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp
git commit -m "Add single-entity mode UI for Home Assistant widget"
```

---

### Task 5: Multi-entity mode UI (compact strip + panel list)

**Files:**
- Modify: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`

**Interfaces:**
- Consumes: `GetEntityState`, `CallHaService`, `DomainIcon`, `IsToggleableDomain`,
  `BuildProfilePanelContent`, `BuildCompactView` from Task 4.
- Produces: `FrameworkElement BuildMultiEntityCompactView(HomeAssistantProfileInstance* instance);`,
  `FrameworkElement BuildMultiEntityPanelContent(HomeAssistantProfileInstance* instance);` — both
  wired into the two mode-`switch` statements Task 4 left with a `Single`-only fallback default.

- [ ] **Step 1: Write the multi-entity compact strip**

Each cell is individually click-toggleable (per the design's explicit choice) — a small `Border`
per entity with its own `PointerPressed` handler, mirroring `taskbar-widget-weather.wh.cpp`'s
forecast-strip cell structure but adding per-cell interactivity that weather's own (non-interactive)
strip never needed:

```cpp
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
```

- [ ] **Step 2: Write the multi-entity panel list**

```cpp
FrameworkElement BuildMultiEntityPanelContent(
    HomeAssistantProfileInstance* instance) {
    StackPanel root;
    root.MinWidth(360);
    root.MaxWidth(360);
    root.Padding({16, 16, 16, 16});
    root.Spacing(8);

    if (instance->config.entityIds.empty()) {
        TextBlock placeholder;
        placeholder.Text(L"No entities configured for this profile.");
        root.Children().Append(placeholder);
        return root;
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
        stateText.Opacity(0.7);
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

    return root;
}
```

- [ ] **Step 3: Wire into the two mode-`switch` statements**

Modify `BuildCompactView` (Task 4 Step 5) and `BuildProfilePanelContent` (Task 4 Step 4) — add a
`case ProfileMode::Multi:` branch to each, calling `BuildMultiEntityCompactView`/
`BuildMultiEntityPanelContent` respectively. `ProfileMode::Dashboard` stays on the `default:`
fallback until Task 6 adds its own branches.

- [ ] **Step 4: Self-check**

Read the finished diff back and confirm: each compact-strip cell's `PointerPressed` handler captures
`entityId`/`domain` by value (not by reference to the loop variable, which would dangle after the
loop ends — every lambda in this step already does this correctly, verify it stayed that way); the
panel list's `ToggleSwitch.IsOn` reflects the entity's actual current state rather than always
defaulting to off; `Multi` now has real branches in both switches, `Dashboard` still correctly falls
through to `Single`'s fallback (not yet implemented).

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp
git commit -m "Add multi-entity mode UI for Home Assistant widget"
```

---

### Task 6: Dashboard mode UI (panel tile grid)

**Files:**
- Modify: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`

**Interfaces:**
- Consumes: `BuildMultiEntityCompactView` (dashboard's compact view is identical to multi's, per the
  spec), `GetEntityState`, `DomainIcon`, `IsToggleableDomain`, `CallHaService` from Tasks 4/5.
- Produces: `FrameworkElement BuildDashboardPanelContent(HomeAssistantProfileInstance* instance);`,
  wired into both mode-`switch` statements (`Dashboard`'s compact case calls
  `BuildMultiEntityCompactView`, its panel case calls this task's own function).

- [ ] **Step 1: Write the dashboard tile grid**

A `Grid` with a fixed column count (3), tiles wrap into rows automatically by index:

```cpp
constexpr int kHaDashboardColumns = 3;

FrameworkElement BuildDashboardPanelContent(
    HomeAssistantProfileInstance* instance) {
    Grid root;
    root.MinWidth(360);
    root.MaxWidth(360);
    root.Padding({16, 16, 16, 16});
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
        return root;
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
        stateText.Opacity(0.7);
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

    return root;
}
```

- [ ] **Step 2: Wire into both mode-`switch` statements**

Modify `BuildCompactView`: add `case ProfileMode::Dashboard: return BuildMultiEntityCompactView(instance);`
(same compact rendering as `Multi`, per the spec's explicit "dashboard mode only changes the panel's
layout" decision). Modify `BuildProfilePanelContent`: add
`case ProfileMode::Dashboard: return BuildDashboardPanelContent(instance);`. After this step, neither
`switch` statement's `default:` case is reachable for any of the three defined `ProfileMode` values —
leave the `default:` in place as defensive handling (matches this repo's convention of never leaving
an enum switch without a fallback), but it's now dead code for normal operation, not a gap.

- [ ] **Step 3: Self-check**

Read the finished diff back and confirm: a tile whose `Border` wraps it (toggleable case) and a tile
added directly (non-toggleable case) are never both added for the same index (an `if`/`else`, not two
separate appends); `Grid::SetRow`/`SetColumn` are set correctly on whichever element actually got
appended to `root.Children()` for each index; `rowCount` computation handles a non-multiple-of-3
entity count correctly (ceiling division, not truncating).

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp
git commit -m "Add dashboard mode UI for Home Assistant widget"
```

---

### Task 7: StyleSettings engine and token wiring

**Files:**
- Modify: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`

**Interfaces:**
- Consumes: the `StyleSettings.styleConstants`/`StyleSettings.styleAliases` schema from Task 1; every
  UI-building function from Tasks 2, 4, 5, 6 (this task edits their hardcoded visual values in
  place, the same way `taskbar-widget-weather`'s own style-constants work retrofitted its existing
  UI functions).
- Produces: `double GetStyleNumber(const std::wstring& slotName, double fallback);`,
  `Brush GetStyleBrush(const std::wstring& slotName, Brush const& fallback);`,
  `winrt::Windows::UI::Color ParseRgbColor(const std::wstring& value);` — byte-for-byte the same
  engine as `taskbar-widget-weather.wh.cpp`'s (duplicated fresh, no shared header).

- [ ] **Step 1: Add the style-constants engine**

Identical to `taskbar-widget-weather.wh.cpp`'s own engine (read that file's actual current
implementation and copy it verbatim, adjusting only for this file's already-defined
`GetStringArraySetting` from Task 1, which replaces weather's own `ParseKeyValueArraySetting` name —
use `GetStringArraySetting` here instead of re-adding a duplicate parser, since Task 1 already wrote
one with the same behavior under a different name):

```cpp
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
```

- [ ] **Step 2: Wire population into `LoadSettings()`**

Modify Task 1's `LoadSettings()` (read its current text first): add, right before the
`std::lock_guard<std::mutex> lock(g_settingsMutex);` line at the end:

```cpp
    auto constants = ParseKeyValueMapSetting(L"StyleSettings.styleConstants");
    auto aliases = ParseKeyValueMapSetting(L"StyleSettings.styleAliases");
    {
        std::lock_guard<std::mutex> styleLock(g_styleMutex);
        g_styleConstants = std::move(constants);
        g_styleAliases = std::move(aliases);
    }
```

- [ ] **Step 3: Apply the 12 tokens to their call sites**

Number tokens — replace each hardcoded literal with a `GetStyleNumber(...)` call, computed once per
function and reused across all four corners where applicable, matching
`taskbar-widget-weather.wh.cpp`'s own established pattern exactly:

| Token | Default | Call site |
|---|---|---|
| `CardCornerRadius` | `5.33` | `background.CornerRadius(...)` in `HaWidget_Create` and `InjectProfileStandalone` (both occurrences) |
| `PanelCornerRadius` | `8.0` | new: wrap each panel-content root (`BuildSingleEntityPanelContent`/`BuildMultiEntityPanelContent`/`BuildDashboardPanelContent`'s outer element) in a `Border` with this corner radius and `PanelBackgroundBrush`/`PanelPadding` (see below) — none of Tasks 4-6 added an outer panel `Border` themselves, only a bare `StackPanel`/`Grid` root, so this task adds it |
| `HeaderPadding` | `10.0` | not applicable (this mod has no separate header row like weather's) — skip |
| `PanelPadding` | `16.0` | the `{16,16,16,16}` padding already present on each panel-content root from Tasks 4-6 |
| `MutedTextOpacity` | `0.7` | every `.Opacity(0.7)` call across Tasks 4-6's panel/tile state-text elements |

Brush tokens:

| Token | Default | Call site |
|---|---|---|
| `PanelBackgroundBrush` | a plain `AcrylicBrush` (tint `#2B2B2B`, `TintOpacity=0.5`, `TintLuminosityOpacity=0.85`, `FallbackColor` `#F02B2B2B`, same construction as weather's, wrapped in the same try/catch-to-solid pattern) | the new panel-wrapping `Border`'s `Background`, added in this step |
| `HeaderBackgroundBrush` | n/a (no header row) | skip |
| `BorderBrush` | `0x18 FFFFFF` | the new panel-wrapping `Border`'s `BorderBrush`, `BorderThickness({1,1,1,1})` |
| `SeparatorBrush` | `0xFF FFFFFF` at `0.3` opacity applied separately | not applicable in v1 (no separator element exists in this mod's UI — Tasks 4-6 use `Spacing`/`RowSpacing` between rows/tiles, not a drawn separator line); keep the slot defined in settings for forward-compatibility (per the spec's token table) but there's no call site to wire it to yet — note this in the commit message, it is not a bug |
| `HoverBrush` / `PressedBrush` | `0x14 FFFFFF` / `0x28 FFFFFF` | add hover/press visual state handling to `instance->background` in `HaWidget_Create`/`InjectProfileStandalone`, mirroring `taskbar-widget-weather.wh.cpp`'s `ApplyWeatherHoverState`/`WireUpHover`/`EnsureHoverBrushes` pattern exactly (read that code and replicate it here with these two style-token-driven defaults in place of weather's hardcoded ones from the start, since this mod never had a pre-existing hardcoded version to preserve) |
| `OnColor` / `OffColor` | a sensible fixed amber-ish "on" tint and muted gray "off" tint (implementer's choice of exact RGB, consistent with this file's own aesthetic — not prescribed further here since there's no existing value to match) | every compact/panel/tile icon's `TextBlock.Foreground` for `light`/`switch` domains, set via `GetStyleBrush(L"OnColor" or L"OffColor", default)` based on `state.state == L"on"` |

- [ ] **Step 4: Self-check**

Read the finished diff back and confirm: every one of the 12 slots documented in the spec's token
table either has a real call site wired up, or (for `HeaderPadding`/`HeaderBackgroundBrush`/
`SeparatorBrush`, which have no corresponding UI element in this mod) is explicitly left unwired with
a clear reason, not silently dropped; `OnColor`/`OffColor` are applied everywhere a light/switch icon
renders (compact single, compact multi/dashboard strip, panel list, panel tiles) — not just one of
them; the new panel-wrapping `Border` doesn't break any existing `MinWidth(360)`/`MaxWidth(360)`
sizing from Tasks 4-6 (move that sizing onto the new wrapping `Border` if it was previously on the
inner content root, don't duplicate it on both).

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp
git commit -m "Add StyleSettings engine and wire style tokens for Home Assistant widget"
```
