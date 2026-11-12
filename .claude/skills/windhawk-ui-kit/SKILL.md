---
name: windhawk-ui-kit
description: The house standard for what a mod in this repo offers the user and how it looks. Use when adding or reorganizing settings, choosing placement, size, background or theme options, building a mod's own settings window, adding a popup or flyout, persisting state the mod writes itself, or making custom UI match Windows 11. Modeled on Taskbar Fluent Media Player 1.7.0, whose option set Ari wants every new mod to follow. Read it before writing a settings block for a new mod, so option names and behavior stay the same across mods.
---

# Windhawk UI kit

Load `windhawk-mod-development` first for the settings rules: the `#!` marker, array defaults and the silent failures. This skill is about which options a mod offers and how its own UI is built.

The model is Taskbar Fluent Media Player 1.7.0 by Salyts, MIT licensed. Its settings block and settings window are the reference. The repo's fork in taskbar-widget-media-player is based on a much older version and has none of the 1.7 features, so read upstream when you need to see how something is done:
`https://raw.githubusercontent.com/ramensoftware/windhawk-mods/main/mods/taskbar-fluent-media-player.wh.cpp`

Reuse upstream's setting names and value strings exactly. A user who knows one mod then knows the next, and code can be shared between mods without renaming.

## The standard option set

Every mod with visible UI offers these groups, in this order. Leave out a group only when it has no meaning for the mod.

1. View switch
2. Placement: monitor, position, size, margin, offset
3. Content options specific to the mod
4. Appearance: background, corner radius, hover effect, text
5. Behavior: click and wheel actions, hide conditions
6. Popup or flyout placement, if the mod has one
7. Settings window options, if the mod has one

### View switch

One top-level setting controls how much the Windhawk settings page shows. Every advanced setting hides itself unless the view is `full`.

```yaml
- settingsView: simple
  $name: Settings view
  $options:
  - window: Settings window (recommended)
  - full: All settings
  - simple: Simple
```

```yaml
    - playerOffset: "0 0"
      #! $showIf: {settingsView: full}
      $name: Offset (x y)
```

Offer the `window` value only when the mod has its own settings window. With `window` selected, whole groups hide with `#! $hideIf: {settingsView: window}` placed on the group, next to its `$name`.

A condition can list several values, `{backgroundType: [solid, gradient]}`, and several keys at once, `{showAlbumArt: true, settingsView: full}`. Hiding is display only. Windhawk 1.7.3 ignores these lines and shows everything.

### Placement

```yaml
- PlacementSettings:
  - monitor: 1
    $name: Monitor
    $description: Which monitor's taskbar to use, counted the way Windows lists them. -1 hides it.
  - position: "tray_left"
    $name: Position
    $options:
    - "taskbar_left_edge": "Taskbar - Left edge (Overlay)"
    - "taskbar_center_edge": "Taskbar - Center (Overlay)"
    - "taskbar_right_edge": "Taskbar - Right edge (Overlay)"
    - "taskbar_left_start": "Taskbar - Left of Start button"
    - "taskbar_right_start": "Taskbar - Right of Start button"
    - "taskbar_after_search_left": "Taskbar - Left of Search button"
    - "taskbar_after_search_right": "Taskbar - Right of Search button"
    - "taskbar_after_taskview_left": "Taskbar - Left of Task View button"
    - "taskbar_after_taskview_right": "Taskbar - Right of Task View button"
    - "taskbar_after_widgets_left": "Taskbar - Left of Widgets button"
    - "taskbar_after_widgets_right": "Taskbar - Right of Widgets button"
    - "tray_left": "Tray - Far left"
    - "tray_right": "Tray - Far right"
    - "tray_before_clock": "Tray - Left of Clock"
    - "tray_after_clock": "Tray - Right of Clock"
    - "tray_before_omni_left": "Tray - Left of Network/Volume button"
    - "tray_before_omni_right": "Tray - Right of Network/Volume button"
    - "tray_language_left": "Tray - Left of Language button"
    - "tray_language_right": "Tray - Right of Language button"
    - "tray_icons_left": "Tray - Left of Tray Icons"
    - "tray_icons_right": "Tray - Right of Tray Icons"
    - "tray_hidden_icons_left": "Tray - Left of Hidden icons button"
    - "tray_hidden_icons_right": "Tray - Right of Hidden icons button"
    - "tray_after_showdesktop_left": "Tray - Left of Show Desktop"
    - "tray_after_showdesktop_right": "Tray - Right of Show Desktop"
  - width: "0 0"
    $name: Width (min max)
    $description: The first number is the minimum, the second the maximum. 0 means no limit.
  - height: "40 40"
    $name: Height (min max)
  - margin: "0 0"
    $name: Margin (left right)
  - offset: "0 0"
    #! $showIf: {settingsView: full}
    $name: Offset (x y)
    $description: Moves the element without changing the room it takes. Negative numbers move it left or up.
  $name: Placement
```

A new mod doesn't have to implement all 25 positions on day one. Ship the ones you have tested and keep the value strings from this list. The stack mod uses older names such as `left_of_start`. Map them to these when that mod is next touched.

If the requested monitor isn't connected, step down one monitor at a time until one is. Gray out or skip positions whose anchor doesn't exist on this build, and fall back to a working one with a log line. The positioning code itself is in `windhawk-taskbar-xaml`.

Pairs are stored as one string with two numbers:

```cpp
struct Pair { double a = 0, b = 0; };
Pair ParsePair(PCWSTR text, Pair fallback) {
    Pair p = fallback;
    if (text && swscanf(text, L"%lf %lf", &p.a, &p.b) == 2) return p;
    if (text && swscanf(text, L"%lf", &p.a) == 1) { p.b = p.a; return p; }
    return fallback;
}
```

Clamp every parsed value. A stored value outside any range you declare still reaches the mod.

### Appearance

```yaml
- AppearanceSettings:
  - backgroundType: "none"
    $name: Background type
    $options:
    - "none":     "None (transparent)"
    - "solid":    "Solid color"
    - "gradient": "Gradient"
    - "acrylic":  "Acrylic"
    - "mica":     "Mica"
    - "mica_alt": "Mica Alt"
  - solidColor: "35 35 35"
    #! $showIf: {backgroundType: [solid, acrylic, mica, mica_alt]}
    $name: Background color (RGB)
  - solidColor2: "35 35 35"
    #! $showIf: {backgroundType: gradient}
    $name: Gradient color 1 (RGB)
  - gradientColor2: "128 128 128"
    #! $showIf: {backgroundType: gradient}
    $name: Gradient color 2 (RGB)
  - solidOpacity: 100
    #! $showIf: {backgroundType: [solid, gradient]}
    $name: Solid color opacity (0-100)
  - gradientAngle: 50
    #! $showIf: {backgroundType: gradient}
    $name: Gradient rotation angle (0-360)
  - acrylicTintOpacity: 50
    #! $showIf: {backgroundType: acrylic}
    $name: Acrylic tint opacity (0-100)
  - micaOpacity: 50
    #! $showIf: {backgroundType: [mica, mica_alt]}
    $name: Mica/Mica Alt opacity (0-100)
  - cornerRadius: "4"
    $name: Corner radius
    $description: One value for all corners, or four space-separated values for each corner.
  - hoverEffect: "auto"
    $name: Hover effect
    $options:
    - "auto":  "Auto (theme changes automatically)"
    - "black": "Black"
    - "white": "White"
    - "off":   "Disable hover effect"
  $name: Appearance
```

Colors are stored as `"R G B"`. Upstream reserves `-1 -1 -1` for the system accent or contrast color. Also accept hex with a leading `#`, since Windhawk 2.0's color picker writes that form. The default is `none` so a fresh install looks like part of the taskbar.

How upstream draws acrylic and mica on an element inside the taskbar has not been read for this skill. Read that part of its source before implementing those two values, and ship `none`, `solid` and `gradient` first.

### Behavior

Offer hide conditions as separate booleans, such as hide in fullscreen and hide when there is nothing to show. Bind actions to single click, double click, middle click and wheel through `$options` lists, so the user picks an action per gesture instead of the mod hard-coding one.

### Popup placement

```yaml
- PopupSettings:
  - placementMode: near
    $name: Placement mode
    $options:
    - near: Near the element
    - screen: Placement on the screen
  - horizontalOffsetNear: 0
    #! $showIf: {placementMode: near}
    $name: Horizontal offset (right/left)
  $name: Popup
```

A popup near a taskbar element opens upward by default, with `FlyoutPlacementMode::Top`. The weather widget's panel first opened away from its widget because it had no placement at all, see taskbar-widget-weather/PLAN.md Incident 1.

## Matching the Windows 11 look

Default to what Windows does and let settings override it.

- **Theme.** Read `ActualTheme()` from your own element and subscribe to `ActualThemeChanged`, keeping the token. For a tool mod with no XAML, the registry value `SystemUsesLightTheme` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` governs the taskbar and tray, and `AppsUseLightTheme` governs app windows. Validate the value's type and size.
- **Corner radius.** 4 px for controls and 8 px for flyouts and windows.
- **Hover and press.** A faint fill over a transparent rest state. On dark, white at about 6% for hover and 4% for press. On light, black at about 4% and 2%. Fade it in and out.
- **Font.** `Segoe UI Variable`, 14 px body and 12 px caption. Icons from `Segoe Fluent Icons`, with `Segoe MDL2 Assets` as the fallback on older builds.
- **Accent.** Use the system accent for selected and checked states.

These numbers are WinUI's published defaults. The taskbar's own buttons can differ, so check the neighbors of your element in UWPSpy and match those when it sits beside them. The weather widget needed three rounds to match the media player's hover, see its Incidents 4 to 6.

Don't re-implement native animations on native elements. They fight the visual states Explorer already runs.

## A mod's own settings window

Build one when the mod has a list the user edits, more than about 30 settings, or anything that needs drag and drop or a live preview. Both this repo's stack and upstream use the same technique.

1. From a click handler, defer through the dispatcher so the handler returns first.
2. On the taskbar's UI thread, register a window class and create a top-level window. Explorer's own message loop pumps it. No extra loop is needed.
3. Call `WindowsXamlManager::InitializeForCurrentThread()`, create a `DesktopWindowXamlSource`, get `IDesktopWindowXamlSourceNative` from it and call `AttachToWindow`. Size the island's child window to the client area on `WM_SIZE`.
4. Set the XAML content, then show the window.

Keep the manager and the source in `[[clang::no_destroy]]` globals, as upstream does, and release them by hand on that thread.

Details that each cost a round:

- Open one window only. A second request brings the existing window forward.
- Title bar. `DwmSetWindowAttribute` with `DWMWA_USE_IMMERSIVE_DARK_MODE`, value 20, follows the theme. Upstream also sets attribute 35 for the caption color, 36 for the text color and 38 for the backdrop type, which gives the mica or acrylic window background.
- Handle `WM_GETMINMAXINFO` for a minimum size, and wrap each page in a `ScrollViewer`.
- `ListView` drag reorder doesn't work in this kind of window. The rows move on screen and the completion event never fires. Build reorder from pointer events on a drag handle. See stack Incidents 52 to 54.
- Unregister the window class and destroy the window in `Wh_ModUninit`, on the owning thread.
- Apply each change live to the real UI, and persist on Save.

Use system XAML controls so the window looks native without custom drawing. Upstream found the system `NavigationView` couldn't be made to match Windows 11's navigation pane and built its sidebar from simpler controls. The stack's window uses a `Pivot`, which is the older tab style. For a new window, copy upstream's layout: a sidebar of pages on the left, content on the right, Save at the bottom of the sidebar.

## Where the window saves

A mod can't write to Windhawk's settings page. Upstream's model, which new mods follow:

- The settings window writes to Windhawk's per-mod storage with `Wh_SetStringValue` and `Wh_SetIntValue`. This works on portable installs.
- At load, read Windhawk's settings first, then apply stored overrides on top.
- On first run, copy the Windhawk page's values into storage and set a migrated flag.
- Later edits made on the Windhawk page still apply. Keep the last seen page values and treat a difference as a new edit.
- Offer "Reset to Windhawk's settings" in the window. It deletes the stored overrides.
- Say in the readme that the Windhawk page can show older values than the window.

Upstream splits long strings into numbered chunks with a count value, so assume a stored string has a size limit and check `windhawk_api.h` for it.

Give stored keys a stable prefix and never rename one without a migration.

## Readme block

- Open with one sentence and a screenshot or GIF. Images must be on `i.imgur.com` or `raw.githubusercontent.com`.
- Say how to open the settings window, if there is one.
- List positions and features as short bullets.
- End with a "Good to know" section: where settings are kept, how to reset, known limits and which Windows builds were tested.

## Localization

`$name`, `$description` and `$options` take a language suffix, as in `$name:fr-FR`. The 2.0 annotations can't be localized. Ari is French, so add `fr-FR` for mods meant for publishing, and keep the two languages in step in the same commit.
