# Weather Widget Style Constants — Design

**Status:** Approved by user, ready for planning.
**Scope:** `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` only. No other mod in this
repo is touched by this feature.

## Problem

`taskbar-widget-weather` builds its whole UI tree imperatively in C++ (`SolidColorBrush{...}`,
`CornerRadius({...})`, etc.), with visual values hardcoded at each call site. The user runs
[Windows 11 Taskbar Styler](https://windhawk.net/mods/windows-11-taskbar-styler) on the real
taskbar and maintains a personal theme (e.g. the
[Luminosity](https://github.com/ramensoftware/windows-11-taskbar-styling-guide/blob/main/Themes/Luminosity/README.md)
theme) expressed as Taskbar Styler `styleConstants` — `Key=Value` pairs, several of them raw XAML
brush fragments (`<AcrylicBrush .../>`, `<SolidColorBrush Color="{ThemeResource ...}" />`,
`<LinearGradientBrush>...</LinearGradientBrush>`). They want the weather widget to pick up the
same visual language without editing C++ every time they tweak the theme.

## Goal

Let the user restyle a fixed set of the weather widget's visual properties (corner radii, padding,
opacity, and several brushes) from Windhawk's own mod-settings screen, using the same raw
`Key=Value` theme entries they already maintain for Taskbar Styler — copy-pasted verbatim, no
per-widget retyping — plus a second, separate mapping that says which semantic style slot in the
widget's code reads which raw key. Changing what feeds a slot, or updating the theme itself, never
requires touching `taskbar-widget-weather.wh.cpp`.

## Non-goals

- No custom in-app settings UI (color pickers, live preview). Editing happens in Windhawk's own
  plain array-of-strings settings screen, identical UX to Taskbar Styler.
- No live/hot editing while the widget is running — constants are read on the same settings-load
  path as every other setting in this file (`LoadSettings()`, called at startup and on Windhawk's
  settings-changed callback).
- No custom XAML types. `<WindhawkBlur .../>` (Taskbar Styler's own pseudo-element for a real
  blur backdrop) is not reproduced; the closest real WinUI equivalent is `AcrylicBrush`, which the
  panel background already uses by default today.
- No cross-mod settings sharing. Windhawk mods cannot read each other's settings; `styleConstants`
  and `styleAliases` are per-mod, and the user re-pastes the same theme into each mod they extend
  this way in the future.
- `taskbar-widget-stack` is explicitly out of scope — its only stylable surface (indicator dots)
  already has dedicated settings with a color-picker UI.

## Two-level indirection

Two independent array-of-strings settings, both `Key=Value` per entry, same shape Taskbar Styler
uses:

1. **`StyleSettings.styleConstants`** — the raw theme, copy-pasted from the user's Taskbar Styler
   theme file as-is. Raw keys are whatever the theme uses (`mbg`, `wcr`, `mcr`, `xcr`, `bb`, `bt`,
   `nbb`, `nbt`, `nbth`, `nbtp`, `AccentColor`, ...). This mod never interprets a raw key's name —
   it's opaque theme data.
2. **`StyleSettings.styleAliases`** — semantic slot name → raw key, e.g. `HeaderBackgroundBrush=mbg`,
   `PanelCornerRadius=mcr`. This is the only place that ties a specific widget-code slot to a
   specific theme entry, and it's what the user edits when they want a slot to pull from a
   different raw token — never the C++.

Resolution for a slot is two hops: alias map (slot → raw key) → constants map (raw key → value) →
type-specific parse. Any missing hop, or a parse failure, falls back to the slot's hardcoded
default (which reproduces today's exact visuals) — the widget never renders broken or blank
because of a bad or absent setting.

## Value kinds

Only two, resolved by which typed getter the call site uses (not declared in the setting itself):

- **Number** (`GetStyleNumber`) — the raw value parsed with `std::stod` in a `try/catch`; falls
  back to the caller's default `double` on empty/missing/malformed input.
- **Brush** (`GetStyleBrush`) — if the raw value's first non-whitespace character is `<`, it's
  parsed as a XAML fragment (see below); otherwise it's parsed as `"R G B"` shorthand into a
  `SolidColorBrush`. `taskbar-widget-weather.wh.cpp` has no existing color-parsing helper (that's
  `taskbar-widget-stack.wh.cpp`'s `ParseRgbColor` - not shared, per this repo's no-shared-header
  convention), so a local `ParseRgbColor` is added here too, mirroring `taskbar-widget-stack`'s
  implementation exactly (`swscanf_s(value.c_str(), L"%d %d %d", &r, &g, &b)`, clamped to 0-255,
  falls back to opaque black on unparsable input). Falls back to the caller's default `Brush` on
  empty/missing/malformed input (including a fragment that fails to parse, or parses to a
  non-`Brush` object).

## XAML fragment parsing

User-pasted fragments (e.g. `<SolidColorBrush Color="{ThemeResource ControlFillColorDefault}" />`)
have no `xmlns` — `XamlReader::Load` requires one on the root element. The engine injects the two
standard namespaces into the root tag before loading, mirroring
`taskbar-widget-media-player.wh.cpp`'s own `XamlReader::Load` usage (see
`GetFluentMediaButtonStyle`, `kStyleXaml`, which declares these same two namespaces on its root
`<Style>` element):

```cpp
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
```

`x:Key` attributes present on a pasted fragment (e.g. the user's `nbb` example,
`<LinearGradientBrush x:Key="..." ...>`) are harmless — `XamlReader::Load` accepts `x:Key` on a
standalone root element and simply ignores it; the returned object is still the `LinearGradientBrush`
itself via `.as<Brush>()`.

`{ThemeResource ...}` references (e.g. `SystemAccentColorLight2`, `ControlFillColorDefault`)
resolve normally: `XamlReader::Load` runs inside Explorer's already-live XAML application, which
has the standard Fluent theme resource dictionaries merged in — the same environment
`taskbar-widget-media-player.wh.cpp`'s own `XamlReader::Load` call already relies on.

## Engine (added near the top of the "views" section, before `BuildNowView`)

```cpp
std::map<std::wstring, std::wstring> g_styleConstants;
std::map<std::wstring, std::wstring> g_styleAliases;
std::mutex g_styleMutex;

// Windhawk's array-setting convention (verified against Windows 11 Taskbar
// Styler's own published source, which reads its `styleConstants` array
// the same way): index the same setting name with [%d], and stop at the
// first EMPTY result - Wh_GetStringSetting never returns null for a missing
// array element, it returns a valid pointer to an empty string. Each entry
// is "Key=Value"; the first '=' splits key from value.
std::map<std::wstring, std::wstring> ParseKeyValueArraySetting(
    const std::wstring& settingName) {
    std::map<std::wstring, std::wstring> result;
    std::wstring format = settingName + L"[%d]";
    for (int i = 0; ; i++) {
        auto* raw = Wh_GetStringSetting(format.c_str(), i);
        bool empty = !raw || !raw[0];
        std::wstring entry = raw ? raw : L"";
        if (raw) {
            Wh_FreeStringSetting(raw);
        }
        if (empty) {
            break;
        }
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

// InjectXamlNamespaces is defined earlier in this same block (see the
// "XAML fragment parsing" section above) - reused here unchanged.

// Mirrors taskbar-widget-stack.wh.cpp's ParseRgbColor exactly (not shared -
// see the no-shared-header note above). Malformed/missing components fall
// back to opaque black via swscanf_s leaving them at their zero-init value.
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

`g_styleConstants`/`g_styleAliases` are populated in `LoadSettings()` (same function that already
loads every other setting into `g_settings`), via:

```cpp
{
    auto constants = ParseKeyValueArraySetting(L"StyleSettings.styleConstants");
    auto aliases = ParseKeyValueArraySetting(L"StyleSettings.styleAliases");
    std::lock_guard<std::mutex> lock(g_styleMutex);
    g_styleConstants = std::move(constants);
    g_styleAliases = std::move(aliases);
}
```

`Wh_GetStringSetting`/`Wh_FreeStringSetting` already appear in this file (the existing
`GetStringSetting` helper's free-after-copy pattern, near `LoadSettings`). `<mutex>` is already
included; `<map>` is not (verified - no `std::map` usage anywhere in this file today) and must be
added alongside the existing `#include <memory>` in the includes block.

## Settings block addition

Added as a new top-level section in the `==WindhawkModSettings==` block, after `PanelSettings`:

```yaml
- StyleSettings:
  - styleConstants: []
    $name: Style constants
    $description: >-
      Raw "Key=Value" theme entries - paste your Windows 11 Taskbar Styler
      styleConstants here as-is (colors as "R G B", or a full XAML brush
      fragment like <AcrylicBrush .../> or <SolidColorBrush Color="{ThemeResource ...}" />).
      This list is never interpreted directly; see Style aliases below.
  - styleAliases: []
    $name: Style aliases
    $description: >-
      "SlotName=rawKey" entries mapping a widget style slot to one of the
      keys above. Slots: CardCornerRadius, PanelCornerRadius,
      CompactCornerRadius, HeaderPadding, PanelPadding, MutedTextOpacity,
      SeparatorOpacity (numbers); PanelBackgroundBrush, HeaderBackgroundBrush,
      BorderBrush, HoverBrush, PressedBrush (colors/brushes). A slot with no
      alias, or an alias pointing at a missing key, keeps its built-in default.
  $name: Style
```

## Token table

Every default reproduces today's exact hardcoded value — an install with no `styleConstants`/
`styleAliases` set renders identically to the current mod.

| Slot | Kind | Default | Call site(s) today |
|---|---|---|---|
| `CardCornerRadius` | number | `6` | `card.CornerRadius`, `headerIconZone.CornerRadius` |
| `PanelCornerRadius` | number | `8` | `header.CornerRadius`, `panelBg.CornerRadius` |
| `CompactCornerRadius` | number | `5.33` | compact-widget hover background corner radius (both instances) |
| `HeaderPadding` | number | `10` | `header.Padding` |
| `PanelPadding` | number | `16` | `panelBg.Padding` |
| `MutedTextOpacity` | number | `0.7` | `conditionText.Opacity`, `feelsLikeText.Opacity`, `labelText.Opacity` |
| `SeparatorOpacity` | number | `0.3` | mixed-view day-separator `Opacity` |
| `PanelBackgroundBrush` | brush | today's `AcrylicBrush` (see below) | `panelBg.Background` |
| `HeaderBackgroundBrush` | brush | today's `0x20 000000` | `header.Background` only (verified `headerIconZone.Background` is a *different* default color, `0x20 FFFFFF` - kept hardcoded, out of scope, to avoid changing its visual when only `HeaderBackgroundBrush` is set) |
| `BorderBrush` | brush | today's `0x18 FFFFFF` | `panelBg.BorderBrush`, mixed-view separator `Background` |
| `HoverBrush` | brush | today's `0x14 FFFFFF` | compact widget hover fill (`g_weatherHoverBrush`) |
| `PressedBrush` | brush | today's `0x28 FFFFFF` | compact widget pressed fill (`g_weatherPressedBrush`) |

`CornerRadius({n, n, n, n})` call sites take `GetStyleNumber(...)` as `n` on all four corners
(uniform radius — none of today's call sites use non-uniform radii, so the token stays a single
number, not four).

### `PanelBackgroundBrush` default

The panel already builds a real `AcrylicBrush` with a `try/catch` → solid-color fallback
(`BuildForecastListPanel`'s panel-background block). `GetStyleBrush(L"PanelBackgroundBrush",
fallback)`'s `fallback` argument is exactly that existing `AcrylicBrush` (constructed the same way
as today, unconditionally, before the styleConstants lookup), preserving the current
try/catch-to-solid safety net as the innermost fallback layer even when a user-supplied brush also
fails to parse.

### `HoverBrush`/`PressedBrush` must invalidate the existing lazy cache

`g_weatherHoverBrush`/`g_weatherPressedBrush`/`g_weatherPressedBorderBrush` (in `EnsureHoverBrushes`)
are lazily computed exactly once per process, guarded by `if (!g_weatherHoverBrush)`. Verified via
`Wh_ModSettingsChanged`: it calls `LoadSettings()` in-process (no mod unload/reload), so without
a fix, changing `HoverBrush`/`PressedBrush` in `styleConstants` and saving would never take effect
until the mod actually reloads. `LoadSettings()` must reset all three globals to `nullptr` after
refreshing `g_styleConstants`/`g_styleAliases`, so the next `EnsureHoverBrushes()` call recomputes
them from the new constants.

### `BorderBrush` reused for the separator

The mixed-view separator currently uses a fixed white `Background` plus its own `Opacity(0.3)`.
It switches to `GetStyleBrush(L"BorderBrush", <today's fixed white>)` for `Background`, and keeps
using `GetStyleNumber(L"SeparatorOpacity", 0.3)` for `Opacity` — same as today's visual result when
no constants are set, and consistent with the panel's own border once the user sets `BorderBrush`.

## Version

Bump `@version` from `1.23` to `1.24` per this repo's standing rule (every commit touching a
mod's `.wh.cpp` bumps it).

## Testing

No automated test harness in this repo (Windhawk mods are tested by loading them into Windhawk and
observing the live taskbar) — verification is manual: load with no `styleConstants`/`styleAliases`
set (confirm unchanged visuals), then with a small theme + alias sample (confirm the change lands
on the right element), then with a deliberately malformed entry (confirm silent fallback, no crash,
no `Wh_Log` spam beyond what already exists in this file's convention).
