# Home Assistant Widget — Design

**Status:** Approved by user, ready for planning.
**New mod:** `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp` (new file, new
Windhawk mod, registers into `taskbar-widget-stack`'s pane like every sibling widget here, with
standalone-injection fallback if the stack mod isn't installed).

## Problem

The user runs a Home Assistant instance and wants to see and control a handful of entities (lights,
switches, sensor readings) directly from the taskbar, the same way `taskbar-widget-weather` surfaces
weather without opening a browser. Unlike every existing widget here, they want **multiple**
independently-configured widget instances from one mod install (e.g. one widget showing a single
thermostat-like entity, another showing "a few lights" as a strip, another as a dashboard-style tile
grid) — not just one fixed widget per mod.

## Goal

One mod, one shared Home Assistant connection (URL + long-lived access token), settings define a
list of **profiles** — each profile becomes its own registered widget instance in the taskbar
stack, independently configured as `single` (one entity, direct toggle), `multi` (several entities,
compact icon strip + panel list), or `dashboard` (several entities, compact icon strip + panel tile
grid). State arrives in real time over a WebSocket, not polling. Visual customization matches
`taskbar-widget-weather`'s `StyleSettings` system (same engine, duplicated per this repo's
no-shared-header convention).

## Non-goals

- No embedded browser / real Home Assistant Lovelace dashboard rendering (`WebView2` or similar) —
  explicitly considered and declined; all rendering is native WinUI, built the same way as every
  other widget here.
- No per-tile icon/label overrides in `dashboard` mode — icon is derived from entity domain, label
  from the entity's own `friendly_name` attribute, same as `single`/`multi` modes. Avoids a
  double-nested array-of-groups settings pattern that hasn't been verified to work in Windhawk's
  settings schema (only single-level nesting, via `controlStyles`-style groups, is confirmed).
- No `climate`/`media_player` (or any other) domain gets dedicated rendering in v1 — only
  `light`/`switch` (toggle) and `sensor` (read-only value+unit) do; everything else falls back to a
  generic read-only icon+state display. `media_player` specifically would overlap significantly with
  `taskbar-widget-media-player`'s own existing UI.
- No per-profile Home Assistant connection — one shared URL + token for every profile.
- No live entity picker/browser at configuration time — entities are typed as `entity_id` strings
  directly into a settings array, same shorthand style as `styleConstants`.
- No optimistic local toggling — clicking a light/switch sends `call_service` and waits for the
  resulting `state_changed` event to update the UI, same as Home Assistant's own frontend.

## Architecture overview

### Multi-instance via the existing ABI, not a new one

`WidgetStackWidgetAbiV1`'s `context` field already exists for exactly this purpose — every existing
widget (weather, media-player, system-usage) just never needed more than one `context` value because
they only ever register once. This mod allocates one `HomeAssistantProfileInstance` per configured
profile and passes `&instance` as `context` when calling `RegisterWidget`; the five callback function
pointers (`Create`/`Tick`/`OnSettingsChanged`/`Destroy`/`GetId`/`GetDisplayName`) are ordinary
non-capturing `extern "C"` functions shared across all instances, each casting `context` back to
`HomeAssistantProfileInstance*` to know which profile it's operating on. No ABI change, no new
pattern at the registration-interface level — the existing struct already supports this.

```cpp
struct HomeAssistantProfileInstance {
    std::wstring profileId;
    std::wstring displayName;
    ProfileMode mode;  // enum: Single, Multi, Dashboard
    std::vector<std::wstring> entityIds;

    // Per-instance UI state, set by Create(), read/rebuilt by the WS thread's
    // dispatch and by OnSettingsChanged():
    winrt::Windows::UI::Xaml::Controls::Grid wrapper{nullptr};
    winrt::Windows::UI::Xaml::Controls::Border background{nullptr};
    void* taskbarHwnd = nullptr;
    bool registered = false;  // true once successfully registered with the stack host
};

std::vector<std::unique_ptr<HomeAssistantProfileInstance>> g_profileInstances;
std::mutex g_profileInstancesMutex;
```

`g_profileInstances` is populated once at `Wh_ModInit()` (after `LoadSettings()`) and rebuilt on
every `Wh_ModSettingsChanged()` (see "Settings-change handling" below) — never incrementally diffed.

### Standalone fallback, per profile

If `taskbar-widget-stack` isn't installed (or its registration function pointers aren't present),
each profile falls back to its own independent standalone injection into the taskbar's `RootGrid`,
exactly like `taskbar-widget-weather`'s existing fallback — just repeated once per profile instead
of once per mod. Each standalone instance gets its own retry thread watching for the stack host to
appear later, matching weather's `WeatherRetryRegisterThreadProc` pattern.

### Settings-change handling

On `Wh_ModSettingsChanged()`: unregister every currently-registered profile instance
(`UnregisterWidget(context)` for stack-hosted ones, remove-from-`RootGrid` for standalone ones),
reload settings, rebuild `g_profileInstances` fresh from the new profile list, and re-register/
re-inject everything from scratch. This is a full rebuild, not incremental add/remove diffing —
simpler to reason about and implement correctly, at the cost of every profile's widget briefly
flickering (destroy+recreate) whenever *any* setting changes, not just the profile list. Documented
trade-off, not something to optimize in v1.

## Connection: one shared WebSocket

### Protocol (Home Assistant's documented WebSocket API)

Endpoint: `ws://<serverUrl>/api/websocket` or `wss://<serverUrl>/api/websocket` depending on
`ConnectionSettings.useTls`.

1. Server → client on connect: `{"type":"auth_required"}`.
2. Client → server: `{"type":"auth","access_token":"<token>"}`.
3. Server → client: `{"type":"auth_ok"}` (proceed) or `{"type":"auth_invalid"}` (stop retrying —
   see "Connection state" below, this is not a transient failure).
4. Client → server, once: `{"id":1,"type":"get_states"}` — response
   `{"id":1,"type":"result","success":true,"result":[...every entity's current state...]}`, used to
   populate `g_entityStates` immediately after connecting rather than waiting for the first change.
5. Client → server, once: `{"id":2,"type":"subscribe_events","event_type":"state_changed"}` —
   acknowledged with `{"id":2,"type":"result","success":true,"result":null}`, then every future
   state change arrives as
   `{"id":2,"type":"event","event":{"event_type":"state_changed","data":{"entity_id":"...","new_state":{"state":"...","attributes":{...}}}}}`.
   HA does not support server-side per-entity filtering at this level — the client filters locally
   against the union of every profile's configured `entityIds`.
6. Toggling: `{"id":N,"type":"call_service","domain":"light","service":"toggle","service_data":{"entity_id":"..."}}`
   (same shape for `switch.toggle`), `N` from a shared monotonically-increasing counter (`auth`
   doesn't need an `id`; every other command does).

### Implementation

- `winrt::Windows::Networking::Sockets::MessageWebSocket` (`Control().MessageType(SocketMessageType::Utf8)`),
  driven from one dedicated background thread (not the UI thread, not the taskbar's window thread —
  matches weather's `WeatherThreadProc` isolation).
- `winrt::Windows::Data::Json` (`JsonObject::Parse`) for message parsing — already used elsewhere in
  this repo (weather's Open-Meteo response parsing), no new JSON dependency.
- Shared entity-state map, mutex-guarded, mirroring weather's `WeatherState`/`g_weatherMutex` pattern:

```cpp
struct EntityState {
    std::wstring entityId;
    std::wstring domain;             // derived: substring before the first '.'
    std::wstring state;              // raw state string, e.g. "on"/"off"/"21.5"
    std::wstring friendlyName;
    std::wstring unitOfMeasurement;  // sensor only, from attributes.unit_of_measurement
    bool hasData = false;
};
std::map<std::wstring, EntityState> g_entityStates;
std::mutex g_entityStatesMutex;
```

- On a `state_changed` event for an entity in `g_entityStates`' tracked set: update the map entry
  under lock, then determine which profile instances have that `entityId` in their `entityIds` list,
  and for each affected instance, marshal a UI rebuild onto the taskbar's window thread via the same
  `RunFromWindowThread` mechanism weather already uses.

### Connection state and reconnect

```cpp
enum class HaConnectionState { Disconnected, Connecting, AuthFailed, Connected };
std::atomic<HaConnectionState> g_haConnectionState{HaConnectionState::Disconnected};
```

- **Transient disconnect** (network drop, HA restart, socket error): reconnect with exponential
  backoff, 1s → 2s → 4s → ... capped at 60s, reset to 1s on the next successful `auth_ok`. Compact
  UI keeps showing the last-known entity states (per the user's explicit choice), with a subtle
  dimmed/desaturated treatment on the icon(s) to indicate staleness — no textual "disconnected"
  label, no blanking.
- **`auth_invalid`** (bad token, or URL points at something that isn't HA): stop retrying entirely,
  set `HaConnectionState::AuthFailed`. This is not recoverable by waiting — retrying forever would be
  both wasteful and misleading. Compact UI shows a distinct "connection failed" glyph (not the
  dimmed-stale treatment), and the panel (if opened) states plainly that the token/URL should be
  checked in settings. A settings change (`Wh_ModSettingsChanged`) always retries fresh regardless of
  prior `AuthFailed` state, since the user may have just fixed the token.

### Connection settings

```yaml
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
```

## Profiles settings schema

Verified against Windows 11 Taskbar Styler's own published source (`controlStyles[%d].target`,
outer loop `for (int i = 0;; i++)` breaking when that field is empty at index `i`) — confirms
single-level array-of-groups settings are schema-legal, with the same empty-string-sentinel
termination convention as a plain array-of-strings, applied to one designated required field per
group item.

```yaml
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
```

Read via `profiles[i].profileId`, `profiles[i].displayName`, `profiles[i].mode`,
`profiles[i].entityIds[j]` — same indexing convention `ParseKeyValueArraySetting` already
established for `styleConstants[i]`, one level deeper. `profileId` is the outer-loop termination
sentinel, mirroring `controlStyles[i].target`'s role exactly. The default value for `profiles`
itself must be a list containing **one** placeholder item (`profileId: ""`, etc.) — not an empty
list — matching the already-fixed `[]` vs `[""]` schema requirement from the style-constants work.

## Entity domain handling

Domain = the substring of `entity_id` before its first `.` (`light.living_room` → `light`).

| Domain | Rendering | Interaction |
|---|---|---|
| `light`, `switch` | icon tinted `OnColor`/`OffColor` by state | click toggles directly (`call_service` toggle) |
| `sensor` | icon + `state` + `unitOfMeasurement` text | read-only, click opens panel |
| anything else | icon + raw `state` text (generic fallback) | read-only, click opens panel |

Icon glyphs per domain are chosen the same way weather picks weather-condition glyphs (a lookup
function returning a Segoe Fluent Icons codepoint) — exact glyph choices are an implementation
detail for the plan, not a design constraint.

## Compact view & panel layout

- **`single` compact**: icon + state text side by side, mirroring `BuildNowView`'s icon-left/
  text-stack-right layout. Light/switch icon tinted `OnColor`/`OffColor`; sensor shows value+unit.
- **`single` panel**: larger icon + state + friendly name + last-changed timestamp (mirrors
  weather panel's header treatment) — no additional controls, since compact already toggles
  light/switch entities directly.
- **`multi` compact**: row of small per-entity icons (mirrors weather's forecast strip structurally),
  each individually click-toggleable (light/switch) or opens the panel (sensor/generic), each tinted
  by state where applicable.
- **`multi` panel**: list of rows, one per entity — icon + friendly name + state + toggle control
  (mirrors weather's forecast-list-panel structure).
- **`dashboard` compact**: same as `multi` compact (icon strip) — dashboard mode only changes the
  panel's layout, not the compact view.
- **`dashboard` panel**: grid of tiles (icon + friendly name + state, toggle where applicable)
  instead of `multi`'s vertical list — same entity data, denser/larger visual treatment. Icon and
  label are always auto-derived (domain lookup, `friendly_name` attribute), never overridden per
  tile (see Non-goals).

Every panel uses the same fixed `MinWidth(360)`/`MaxWidth(360)` cross-mod convention as weather and
media-player.

## StyleSettings

Same engine as `taskbar-widget-weather` (`GetStyleNumber`, `GetStyleBrush`, `ParseRgbColor`,
`ParseKeyValueArraySetting`, `InjectXamlNamespaces`, the `styleConstants`/`styleAliases` two-list
indirection with `[""]` defaults) — duplicated fresh into this file, no shared header, matching this
repo's established convention.

| Token | Kind | Purpose |
|---|---|---|
| `CardCornerRadius` | number | compact card corner radius |
| `PanelCornerRadius` | number | panel corner radius |
| `HeaderPadding` | number | panel header padding |
| `PanelPadding` | number | panel body padding |
| `MutedTextOpacity` | number | secondary text (last-changed, unit label) |
| `PanelBackgroundBrush` | brush | panel background |
| `HeaderBackgroundBrush` | brush | panel header background |
| `BorderBrush` | brush | panel border |
| `SeparatorBrush` | brush | multi-mode panel row separators (independent slot, per the
  weather widget's own live-tested lesson: never couple a separator's brush to a border brush that
  also carries its own alpha plus a separately-applied opacity multiplier) |
| `HoverBrush` / `PressedBrush` | brush | compact widget hover/press fill |
| `OnColor` / `OffColor` | brush | light/switch icon tint by state |

12 tokens total. Every default is a sensible fixed value chosen during implementation (this is a new
mod, not a refactor of hardcoded values — there is no "today's exact visual" to preserve, unlike the
weather widget's style-constants work).

## Mod metadata & docs

- New file: `taskbar-widget-home-assistant/taskbar-widget-home-assistant.wh.cpp`, with the same
  `==WindhawkMod==`/`==WindhawkModReadme==`/`==WindhawkModSettings==` header structure as every
  sibling mod, `@include explorer.exe`, `@architecture x86-64`.
- `PLAN.md` created near-empty (a short `## Context` section linking back to this spec and the
  implementation plan) — reserved for post-build live-test incident logging per this repo's
  established convention, not upfront design.
- `@compilerOptions` needs whatever WinRT/networking libs `MessageWebSocket` and
  `winrt::Windows::Data::Json` require — confirmed at implementation time by checking what weather's
  own `@compilerOptions` already links (`-lole32 -loleaut32 -lruntimeobject -luser32`) and adding
  anything `Windows.Networking.Sockets` needs beyond that (likely nothing extra, since it's part of
  the same WinRT projection weather already links against for `Windows.Web.Http`).

## Testing

No automated test harness (same as every mod here) — verification is manual, in Windhawk, against a
real Home Assistant instance: connect with a valid token (states populate, toggling a light/switch
works), an invalid token (`AuthFailed` state shown, no infinite retry), a transient disconnect
(stale-but-dimmed display, auto-reconnects), and multiple profiles simultaneously registered
(single + multi + dashboard side by side) to confirm the per-profile `context` isolation actually
works and one profile's rebuild never touches another's UI.
