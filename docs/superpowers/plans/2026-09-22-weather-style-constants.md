# Weather Widget Style Constants Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user restyle a curated set of `taskbar-widget-weather`'s visual properties (corner
radii, padding, opacity, and five brushes) from Windhawk's own settings screen, using the same raw
`Key=Value` theme entries they already maintain for Windows 11 Taskbar Styler, plus a separate
alias mapping so which raw token feeds which slot is settings-only, never a code change.

**Architecture:** Two new Windhawk array-of-strings settings (`StyleSettings.styleConstants`,
`StyleSettings.styleAliases`), each `Key=Value` per entry. A small parsing engine resolves a
semantic slot name through the alias map to a raw key, then through the constants map to a raw
value, then type-parses it (`GetStyleNumber` via `std::stod`, `GetStyleBrush` via `ParseRgbColor`
for `"R G B"` shorthand or `XamlReader::Load` for a XAML brush fragment starting with `<`). Every
call site keeps its current hardcoded value as the fallback, so an install with no constants set is
visually identical to today.

**Tech Stack:** C++/WinRT (winrt::Windows::UI::Xaml::*), Windhawk mod SDK (`Wh_GetStringSetting`,
`Wh_FreeStringSetting`).

**Spec:** [docs/superpowers/specs/2026-09-22-weather-style-constants-design.md](../specs/2026-09-22-weather-style-constants-design.md)

## Global Constraints

- Single file: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp`. No other mod is touched.
- No shared header exists between mods in this repo — any helper this feature needs that isn't
  already in this file (e.g. a color parser) must be added locally, not imported.
- Every default value must reproduce today's exact hardcoded visual — verify each one against the
  current file before changing it, don't trust a description of "the current value" without
  re-reading the actual line.
- No automated test harness exists for Windhawk mods in this repo. "Testing" a step means: read
  the changed code back and confirm it matches the intended behavior exactly (types, defaults,
  fallback paths) — there is no compiler or runner available in this environment. The user compiles
  and live-tests by loading the mod into Windhawk themselves, after this plan is fully implemented.
- Bump `@version` from `1.23` to `1.24` (this repo's standing rule: every commit touching a mod's
  `.wh.cpp` bumps its version) — done once, in Task 1.
- Match this file's existing conventions: `try/catch (...)` around anything that can throw (XAML
  parsing, `std::stod`), `std::lock_guard` around any access to a mutex-protected global, comments
  only where the reasoning isn't obvious from the code itself.

---

### Task 1: Style constants engine, settings schema, and version bump

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp`

**Interfaces:**
- Produces (consumed by Task 2 and Task 3):
  - `double GetStyleNumber(const std::wstring& slotName, double fallback)`
  - `Brush GetStyleBrush(const std::wstring& slotName, Brush const& fallback)` (`Brush` is
    `winrt::Windows::UI::Xaml::Media::Brush`, already `using namespace`'d in this file)
  - `winrt::Windows::UI::Color ParseRgbColor(const std::wstring& value)` (local to this file —
    mirrors `taskbar-widget-stack.wh.cpp`'s function of the same name, not shared)
  - `g_styleConstants` / `g_styleAliases` / `g_styleMutex` are populated by `LoadSettings()` before
    any `GetStyleNumber`/`GetStyleBrush` call happens (both engine and consuming call sites live in
    the same file, and `LoadSettings()` already runs once at mod init before any UI is built).

- [ ] **Step 1: Add `<map>` to the includes block**

Modify `taskbar-widget-weather/taskbar-widget-weather.wh.cpp` around line 211 (the existing
`#include <memory>` line):

```cpp
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
```

(Only the `#include <map>` line is new — inserted alphabetically between `<cwchar>` and
`<memory>`, matching this block's existing alphabetical ordering.)

- [ ] **Step 2: Add the `StyleSettings` section to `==WindhawkModSettings==`**

Modify the settings block: insert a new top-level section immediately after `PanelSettings`'s
closing `$name: Panel` line and before `- ClickActionSettings:`. Read the file first to find the
exact current line (it was line ~129, `$name: Click actions` follows `- ClickActionSettings:` — the
new section goes right after `PanelSettings`'s own `$name:` line, wherever that currently falls,
by searching for the literal text `$name: Panel` immediately preceding `- ClickActionSettings:`).

Insert:

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

- [ ] **Step 3: Add the engine block**

Add this new block immediately before `Grid BuildNowView() {` (the file's first `Build*View`
function, currently right after the `kIconFontSize`/`kTempFontSize`/`kConditionFontSize`/
`kForecastCellMinWidth`/`kMixedForecastCellMinWidth` constants block):

```cpp
// Style constants engine (2026-09-22): lets the user restyle a curated set
// of this widget's visual properties from Windhawk's own settings screen,
// using the same raw theme entries they already maintain for Windows 11
// Taskbar Styler. Two settings: StyleSettings.styleConstants (the raw
// theme, copy-pasted verbatim, opaque Key=Value pairs this mod never
// interprets by name) and StyleSettings.styleAliases (SlotName=rawKey,
// the only place that ties a specific widget style slot to a specific
// theme entry). See docs/superpowers/specs/2026-09-22-weather-style-constants-design.md.
std::map<std::wstring, std::wstring> g_styleConstants;
std::map<std::wstring, std::wstring> g_styleAliases;
std::mutex g_styleMutex;

// Windhawk's array-setting convention (verified against Windows 11 Taskbar
// Styler's own published source, which reads its styleConstants array the
// same way): index the same setting name with [%d], and stop at the first
// EMPTY result - Wh_GetStringSetting never returns null for a missing
// array element, it returns a valid pointer to an empty string. Each entry
// is "Key=Value"; the first '=' splits key from value.
std::map<std::wstring, std::wstring> ParseKeyValueArraySetting(
    const std::wstring& settingName) {
    std::map<std::wstring, std::wstring> result;
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
// Injects the two standard namespaces right after the root tag's name,
// same two namespaces this file's own XamlReader::Load usage would need
// (see taskbar-widget-media-player.wh.cpp's GetFluentMediaButtonStyle for
// the same pattern in a sibling mod).
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

// Mirrors taskbar-widget-stack.wh.cpp's ParseRgbColor exactly (not shared -
// mods in this repo have no shared header). Malformed/missing components
// fall back to opaque black via swscanf_s leaving them at their zero-init
// value.
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

- [ ] **Step 4: Wire `g_styleConstants`/`g_styleAliases` into `LoadSettings()`, and invalidate the hover-brush cache**

In `LoadSettings()`, add at the very end of the function (after the existing swap-in of
`g_settings`, so this doesn't change the existing single-assignment-swap behavior for `g_settings`
itself):

```cpp
    {
        auto constants = ParseKeyValueArraySetting(L"StyleSettings.styleConstants");
        auto aliases = ParseKeyValueArraySetting(L"StyleSettings.styleAliases");
        std::lock_guard<std::mutex> lock(g_styleMutex);
        g_styleConstants = std::move(constants);
        g_styleAliases = std::move(aliases);
    }
    // g_weatherHoverBrush/g_weatherPressedBrush/g_weatherPressedBorderBrush
    // (EnsureHoverBrushes, further below) are lazily computed exactly once
    // per process, guarded by "if (!g_weatherHoverBrush)". Wh_ModSettingsChanged
    // calls LoadSettings() in-process (no mod unload/reload) - without this
    // reset, changing HoverBrush/PressedBrush in styleConstants and saving
    // would never take effect until the mod actually reloads.
    g_weatherHoverBrush = nullptr;
    g_weatherPressedBrush = nullptr;
    g_weatherPressedBorderBrush = nullptr;
```

Note: `g_weatherHoverBrush`, `g_weatherPressedBrush`, and `g_weatherPressedBorderBrush` are
declared later in the file (in the "Hover/pressed visual state helpers" section, near
`EnsureHoverBrushes`) than `LoadSettings()` is. Since they're file-scope globals (not function-local
or forward-declared-only), and C++ requires a name to be declared before use in a non-template
function body compiled in translation-unit order, this reset code **cannot** simply be appended to
`LoadSettings()` if `LoadSettings()` is textually above those globals' declarations. Read the file
to confirm the actual order: if `LoadSettings()` precedes the hover-brush globals' declaration, add
this reset as a **new small function** (e.g. `void InvalidateHoverBrushCache()`) declared and
defined right after the hover-brush globals themselves (near `EnsureHoverBrushes`), forward-declare
it (`void InvalidateHoverBrushCache();`) near the top alongside this file's other forward
declarations, and call `InvalidateHoverBrushCache();` from `LoadSettings()` instead of touching the
globals directly. If `LoadSettings()` already comes after those globals in the file, the direct
reset shown above is fine as-is — check before choosing.

- [ ] **Step 5: Bump `@version`**

Modify line 5:

```cpp
// @version         1.24
```

- [ ] **Step 6: Self-check (no compiler available — manual read-back)**

Read back every place touched in this task and confirm: `<map>` is present exactly once; the new
`StyleSettings` YAML section parses as valid YAML (correct indentation matching `PanelSettings`'s
own indentation exactly); every new function name matches what Task 2/Task 3 expect
(`GetStyleNumber`, `GetStyleBrush`, `ParseRgbColor`); the hover-brush reset compiles in whichever
position (inline vs. new function) matches this file's actual declaration order; `@version` reads
`1.24`.

- [ ] **Step 7: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Add style constants engine and settings schema to weather widget"
```

---

### Task 2: Apply number-kind style slots (corner radii, padding, opacity)

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp`

**Interfaces:**
- Consumes: `double GetStyleNumber(const std::wstring& slotName, double fallback)` from Task 1.

Seven slots, all uniform four-way `CornerRadius`/`Padding` values or a single `Opacity`. For every
`{n, n, n, n}` call, all four positions take the **same** `GetStyleNumber` call (call it once into
a local variable, then use that variable four times, rather than calling `GetStyleNumber` four
times per site — avoids four redundant map lookups for one value).

- [ ] **Step 1: `CardCornerRadius` (card, header icon-zone)**

Modify (in the panel-building code, `card.CornerRadius({6, 6, 6, 6});`):

```cpp
    double cardCornerRadius = GetStyleNumber(L"CardCornerRadius", 6.0);
    Grid card;
    card.CornerRadius({cardCornerRadius, cardCornerRadius, cardCornerRadius,
                        cardCornerRadius});
```

And (`headerIconZone.CornerRadius({6, 6, 6, 6});` — reuse the same `cardCornerRadius` local, since
both call sites share the same default and slot per the spec's token table):

```cpp
    headerIconZone.CornerRadius({cardCornerRadius, cardCornerRadius,
                                  cardCornerRadius, cardCornerRadius});
```

(`cardCornerRadius` must be declared before `header`/`headerIconZone` are both built — since `card`
is declared first and `headerIconZone` later in the same function, declaring it right before
`card.CornerRadius(...)` and reusing it at the `headerIconZone` site further down works as long as
it's still in scope; confirm both sites are in the same function body before relying on this.)

- [ ] **Step 2: `PanelCornerRadius` (panel header, panel background)**

Modify `header.CornerRadius({8, 8, 8, 8});`:

```cpp
    double panelCornerRadius = GetStyleNumber(L"PanelCornerRadius", 8.0);
    Grid header;
    header.CornerRadius({panelCornerRadius, panelCornerRadius, panelCornerRadius,
                          panelCornerRadius});
```

Modify `panelBg.CornerRadius({8, 8, 8, 8});` (in `BuildForecastListPanel`, a **different function**
than `header`'s — this needs its own `GetStyleNumber(L"PanelCornerRadius", 8.0)` call, it cannot
reuse the `header` function's local variable across function boundaries):

```cpp
    double panelCornerRadius = GetStyleNumber(L"PanelCornerRadius", 8.0);
    panelBg.CornerRadius({panelCornerRadius, panelCornerRadius, panelCornerRadius,
                           panelCornerRadius});
```

- [ ] **Step 3: `CompactCornerRadius` (both compact-host wrapper instances)**

Modify `background.CornerRadius({5.33, 5.33, 5.33, 5.33});` — this exact line appears **twice**
in the file (the registered-mode wrapper and the standalone-injection wrapper, two separate
functions). Apply the same change at **both** occurrences:

```cpp
    double compactCornerRadius = GetStyleNumber(L"CompactCornerRadius", 5.33);
    Border background;
    background.CornerRadius({compactCornerRadius, compactCornerRadius,
                              compactCornerRadius, compactCornerRadius});
```

- [ ] **Step 4: `HeaderPadding` and `PanelPadding`**

Modify `header.Padding({10, 10, 10, 10});`:

```cpp
    double headerPadding = GetStyleNumber(L"HeaderPadding", 10.0);
    header.Padding({headerPadding, headerPadding, headerPadding, headerPadding});
```

Modify `panelBg.Padding({16, 16, 16, 16});`:

```cpp
    double panelPadding = GetStyleNumber(L"PanelPadding", 16.0);
    panelBg.Padding({panelPadding, panelPadding, panelPadding, panelPadding});
```

- [ ] **Step 5: `MutedTextOpacity` (three text elements, three different functions)**

Modify `conditionText.Opacity(0.7);` (in `BuildNowView`):

```cpp
    conditionText.Opacity(GetStyleNumber(L"MutedTextOpacity", 0.7));
```

Modify `feelsLikeText.Opacity(0.7);` (in the panel header-building code):

```cpp
    feelsLikeText.Opacity(GetStyleNumber(L"MutedTextOpacity", 0.7));
```

Modify `labelText.Opacity(0.7);` (inside the `makeDetailCell` lambda in
`BuildForecastListPanel` or its caller — a plain function call inside a lambda body, no capture
changes needed since `GetStyleNumber` is a free function):

```cpp
        labelText.Opacity(GetStyleNumber(L"MutedTextOpacity", 0.7));
```

- [ ] **Step 6: `SeparatorOpacity`**

Modify `separator.Opacity(0.3);` (in `BuildMixedView`):

```cpp
    separator.Opacity(GetStyleNumber(L"SeparatorOpacity", 0.3));
```

- [ ] **Step 7: Self-check**

Read back all seven slots' call sites. Confirm: every `{n,n,n,n}` site computes its
`GetStyleNumber` call exactly once per function (not once per corner); the two `CardCornerRadius`
sites and the two `CompactCornerRadius` sites both changed; `PanelCornerRadius` has two independent
`GetStyleNumber` calls (different functions); no site was missed (grep the file for the literal
strings `{6, 6, 6, 6}`, `{8, 8, 8, 8}`, `{5.33, 5.33, 5.33, 5.33}`, `{10, 10, 10, 10}`,
`{16, 16, 16, 16}`, `Opacity(0.7)`, `Opacity(0.3)` — none should remain as bare hardcoded literals
at the call sites listed above).

- [ ] **Step 8: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Apply style constants to weather widget corner radius, padding, and opacity"
```

---

### Task 3: Apply brush-kind style slots (backgrounds, border, hover/press)

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp`

**Interfaces:**
- Consumes: `Brush GetStyleBrush(const std::wstring& slotName, Brush const& fallback)` from Task 1.

- [ ] **Step 1: `PanelBackgroundBrush`**

Modify the panel-background block in `BuildForecastListPanel` (the `try { AcrylicBrush acrylic; ...
panelBg.Background(acrylic); } catch (...) { panelBg.Background(SolidColorBrush{...}); }` block).
Build the existing `AcrylicBrush`-or-solid-fallback result exactly as today, store it as `Brush
defaultPanelBackground`, then pass it as `GetStyleBrush`'s fallback:

```cpp
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
    } catch (...) {
        defaultPanelBackground = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0xF0, 0x2B, 0x2B, 0x2B)};
    }
    panelBg.Background(GetStyleBrush(L"PanelBackgroundBrush", defaultPanelBackground));
```

- [ ] **Step 2: `HeaderBackgroundBrush`**

Modify **only** `header.Background(SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(0x20,
0, 0, 0)});` (the panel header's own background — **not** `headerIconZone.Background`, which is a
different default color, `0x20 FFFFFF`, and stays hardcoded per the spec's explicit scope note):

```cpp
    Brush defaultHeaderBackground = SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0x20, 0, 0, 0)};
    header.Background(GetStyleBrush(L"HeaderBackgroundBrush", defaultHeaderBackground));
```

Leave `headerIconZone.Background(SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(0x20,
0xFF, 0xFF, 0xFF)});` untouched.

- [ ] **Step 3: `BorderBrush` (panel border + mixed-view separator)**

Modify `panelBg.BorderBrush(SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(0x18, 0xFF,
0xFF, 0xFF)});`:

```cpp
    Brush defaultPanelBorder = SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0x18, 0xFF, 0xFF, 0xFF)};
    panelBg.BorderBrush(GetStyleBrush(L"BorderBrush", defaultPanelBorder));
```

Modify `separator.Background(SolidColorBrush{winrt::Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF,
0xFF, 0xFF)});` (in `BuildMixedView` — same slot name `BorderBrush`, but its own, different,
opaque-white default, since `separator.Opacity(...)` already applies the translucency separately):

```cpp
    Brush defaultSeparatorBrush = SolidColorBrush{
        winrt::Windows::UI::ColorHelper::FromArgb(0xFF, 0xFF, 0xFF, 0xFF)};
    separator.Background(GetStyleBrush(L"BorderBrush", defaultSeparatorBrush));
```

- [ ] **Step 4: `HoverBrush` and `PressedBrush`**

`g_weatherHoverBrush`/`g_weatherPressedBrush` are declared as `SolidColorBrush` (not the general
`Brush` base type) — see their declaration at `SolidColorBrush g_weatherHoverBrush{nullptr};`.
`GetStyleBrush` returns `Brush`, which must be narrowed back with `.as<SolidColorBrush>()`. If the
user supplies a `styleConstants` fragment that parses to a non-`SolidColorBrush` `Brush` (e.g. an
`AcrylicBrush` or `LinearGradientBrush`) for `HoverBrush`/`PressedBrush`, `.as<SolidColorBrush>()`
throws — the assignment is wrapped in its own `try/catch` that falls back to the plain default
`SolidColorBrush` value on failure, since `background.Background(g_weatherHoverBrush)` elsewhere in
the file requires these globals to stay `SolidColorBrush`-typed and must never be left null after
`EnsureHoverBrushes()` returns.

Modify `EnsureHoverBrushes()`:

```cpp
void EnsureHoverBrushes() {
    if (!g_weatherHoverBrush) {
        SolidColorBrush defaultHover{
            winrt::Windows::UI::ColorHelper::FromArgb(0x14, 0xFF, 0xFF, 0xFF)};
        try {
            g_weatherHoverBrush =
                GetStyleBrush(L"HoverBrush", defaultHover).as<SolidColorBrush>();
        } catch (...) {
            g_weatherHoverBrush = defaultHover;
        }
    }
    if (!g_weatherPressedBrush) {
        SolidColorBrush defaultPressed{
            winrt::Windows::UI::ColorHelper::FromArgb(0x28, 0xFF, 0xFF, 0xFF)};
        try {
            g_weatherPressedBrush =
                GetStyleBrush(L"PressedBrush", defaultPressed).as<SolidColorBrush>();
        } catch (...) {
            g_weatherPressedBrush = defaultPressed;
        }
    }
    if (!g_weatherPressedBorderBrush) {
        g_weatherPressedBorderBrush = SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0x0A, 0xFF, 0xFF, 0xFF)};
    }
}
```

- [ ] **Step 5: Self-check**

Read back all five brush slots. Confirm: `PanelBackgroundBrush`'s fallback is built exactly as
today's `AcrylicBrush`/solid-fallback pair (byte-for-byte same tint/opacity values), just captured
into a variable first; `headerIconZone.Background` was **not** touched; both `BorderBrush` call
sites use the same slot name but each keeps its own distinct default; `HoverBrush`/`PressedBrush`
are wrapped in `try/catch` and never leave `g_weatherHoverBrush`/`g_weatherPressedBrush` null after
`EnsureHoverBrushes()` returns, under any input.

- [ ] **Step 6: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Apply style constants to weather widget background, border, and hover/press brushes"
```
