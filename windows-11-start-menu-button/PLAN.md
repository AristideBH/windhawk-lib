# windows-11-start-menu-button — Windhawk mod plan

## Context
Windhawk mod for Win11 taskbar Start button. Goals: swap stock icon for custom image, or recolor stock icon while keeping native animation; style button box (padding/margin/bg/corner-radius) per interaction state (default/hover/pressed). Target: Windows 11 25H2 only, all taskbar instances (multi-monitor).

## File
`windows-11-start-menu-button.wh.cpp` — single-file Windhawk mod, metadata header comment block (standard Windhawk format), placed in this folder.

## Reference mods (ramensoftware/windhawk-mods)
- `taskbar-start-button-corner-fix.wh.cpp` — locates Start button via UI Automation, `AutomationId = "StartButton"`, `FindFirst(TreeScope_Descendants, ...)` on taskbar XAML tree.
- `taskbar-fluent-media-player.wh.cpp` — XAML injection/manipulation pattern: `RunFromWindowThread()` (via `SetWindowsHookExW(WH_CALLWNDPROC)`) to execute on explorer's UI thread, locate Grid/Border elements, apply `FrameworkElement.Margin`, nested settings struct (`Wh_GetIntSetting`/`Wh_GetStringSetting` with dotted keys), dynamic settings reload without restart.

## Modes (mutually exclusive, user setting)
1. **Custom icon** — user-supplied PNG/ICO file path, replaces stock icon bitmap directly.
2. **Recolor** — render-time tint of stock icon (Direct2D/WIC color transform applied at draw time, not a one-shot bitmap capture) — preserves native icon animation/theme-driven redraws.

## Per-state styling
States: `Default`, `Hover`, `Pressed`. Each state configurable:
- Background color
- Optional icon override (falls back to mode's base icon if unset)
- Padding, margin
- Corner radius (bg shape, Fluent-style rounding, default ~4px)

Native hover/press animation (scale/fade) is left untouched — no custom animation added, only static per-state values swapped in as the state transitions.

## Hook / implementation approach
1. On mod init and on new taskbar window creation (multi-monitor), use UI Automation to find `AutomationId = "StartButton"` element in each taskbar's XAML tree.
2. Hook into taskbar window's UI thread (`RunFromWindowThread` pattern) to safely mutate XAML tree / WinRT objects.
3. Apply bg/padding/margin/corner-radius via `FrameworkElement`/`Border` property sets on the located button element (and/or a wrapping Border injected around it, following the fluent-media-player Border/StackPanel injection pattern).
4. Icon handling:
   - Custom icon mode: load user PNG/ICO via WIC, replace the icon `ImageSource`/bitmap used by the button's icon element.
   - Recolor mode: hook the icon's render/draw path, apply Direct2D/WIC color matrix tint using configured color, re-apply on each redraw so animation frames stay tinted.
5. State tracking: hook or listen to the button's visual state transitions (pointer enter/leave, pressed) to swap in the per-state bg/icon/padding/margin/corner-radius values.
6. Settings: nested JSON schema, e.g.:
   ```
   mode: default | customIcon | recolor
   customIcon.path
   recolor.color
   states.default.{bgColor, iconPath, padding, margin, cornerRadius}
   states.hover.{...}
   states.pressed.{...}
   ```
   Loaded via `Wh_GetIntSetting` / `Wh_GetStringSetting`, reloadable on settings change without explorer restart (per fluent-media-player pattern).

## Out of scope
- Custom animations beyond native hover/press (explicitly excluded).
- Windows versions other than 25H2.
- SVG icon support (PNG/ICO only).

## Found it: gradient brush, not solid — .Color() mutation silently no-ops on it
User reported the resting squares are a blue *gradient*, still unaffected,
while shimmer (a separate solid-brush highlight overlay shape, it turns out)
worked fine. Root cause: `try_as<CompositionColorBrush>()` fails silently on
a `CompositionLinearGradientBrush`/`CompositionRadialGradientBrush`, so our
`.Color()`-mutation approach was quietly skipping the actual flag-square
shapes the whole time - it only ever touched the (solid-brushed) highlight
overlay, which is what we mistook for a working shimmer.

Fixed by no longer mutating brush color at all: instead **replace** the
shape's `FillBrush`/`StrokeBrush` (or the visual's `Brush`) outright with a
freshly created solid `CompositionColorBrush` via `Compositor.
CreateColorBrush`, which works regardless of the original brush's type.
Original brush objects (including gradients) are captured by shape/visual
identity before first replacement, so Default mode restores the exact
original look. Not yet visually confirmed by the user.

## Resting color still not applying — instrumenting further
User reports shimmer works during hover/press animation but resting color
never shows on the four squares (stays original flag colors at rest).
Added throttled (1/sec) logging of animating state, chosen color string,
parse success, and brushes-touched count in
`StartPersistentIconColorMaintenance` to determine whether the resting-color
codepath is even executing, and whether `RecolorAnimatedVisualPlayer` finds
brushes to touch at rest (vs. only while animating, which would suggest
Explorer swaps to a different, non-`CompositionColorBrush` rendering
strategy - e.g. a cached bitmap - when idle).

## Pivoted to a deliberate two-color "shimmer" design
User observed live: the direct brush override only visibly took effect
*while the icon's native hover/press animation was actively playing*,
reverting once it settled at rest — and liked the effect enough to make it a
feature rather than a bug to eliminate. Root mechanism: Explorer's Lottie
animation playback fights/resets the shape brushes at rest; fighting that
harder (e.g. persistent per-frame override forever) was the fallback plan,
but embracing it is simpler and better UX.

Redesigned recolor mode around two settings: `recolorColor` (steady resting
color) and `recolorShimmerColor` (shown only while `player.IsPlaying()` is
true, i.e. mid hover/press transition). `StartPersistentIconColorMaintenance`
runs one `CompositionTarget.Rendering` subscription per Start-button instance
for its entire lifetime (stopped only on mod unload, restoring original flag
colors then), picking resting vs. shimmer color every frame based on
`IsPlaying()`. This also sidesteps needing to understand *why* brushes get
reset — continuous reassertion driven by real-time animation state is
correct regardless of the exact internal mechanism.

## Real cause found: it's the multi-color flag logo, not a themeable single-tone icon
Frame-by-frame brush readback (once shapes existed) showed genuine distinct
RGB values (e.g. 102,226,248 and 11,155,254 — cyan/blue flag-pane colors),
completely unrelated to the red we were writing via `SetColorProperty`, even
after 120 frames of reassertion. Conclusion: the Start button icon is
Windows' classic four-color flag logo rendered as a static multi-shape
Lottie asset, not a themeable single-tone icon, and `SetColorProperty
("Foreground", ...)` has no effect on it — that property name evidently
belongs to a different `AnimatedVisuals.*` class sharing the same DLL.

Rewrote recolor to directly overwrite every `CompositionSpriteShape`'s
`FillBrush`/`StrokeBrush` `CompositionColorBrush.Color` in the Lottie shape
tree (confirmed real via the 28-brush readback), for both
`GetElementVisual` and `GetElementChildVisual` (AnimatedVisualPlayer hosts
its Lottie content as an "element child visual", a separate composition hook
from the element's own XAML visual). Original per-brush colors are captured
by COM identity (`winrt::get_abi`) before first override, so switching back
to "System default" mode restores the real flag colors instead of guessing a
fallback. Still reasserts every frame for ~2s since shapes can appear late
or get recreated across re-layouts. Custom-icon mode (hide + overlay Image)
is unaffected by any of this — it operates purely at the FrameworkElement
Visibility/overlay level, verified working in the logs (panel/icon lookup
all succeed).

**Not yet visually confirmed** — this is the next thing to test.

## Recolor still not visually landing — readback proved unreliable, switched to brute-force reassert
Found the real shape tree (`ShapeVisual.Shapes()` -> `CompositionSpriteShape.
FillBrush()`, 28 brushes on the Start icon — confirms this is the right
node), but every brush read back as transparent black (A=0) both before and
after `SetColorProperty`. Concluded `CompositionColorBrush.Color()` read from
the UI thread does not reliably reflect a value driven by an active
expression animation on the render thread — it's not a valid "did it work"
signal, only usable as an existence probe. The retry loop was also stopping
at the very first frame shapes existed and unsubscribing immediately, which
could still be before Explorer finishes wiring the theme-property expression
bindings. Changed strategy: reassert `SetColorProperty(Foreground, color)` on
every `CompositionTarget.Rendering` tick for ~120 frames (~2s) instead of
stopping at first detection — cheap, and guarantees our write is the last one
regardless of exactly when Explorer's own theme setup runs.

**Still unconfirmed visually as of this note** — next log from the user is
the actual test of this change.

## Recolor race condition found and fixed
Confirmed via live extraction of `Taskbar.View.dll` strings (UTF-16 scan) that
`"Foreground"` is the real theme-color property name — the source object's
runtime class is `AnimatedVisuals.StartDark` (LottieGen C++/WinRT output),
and both `"Foreground"` and the composition-expression fragment
`"theme.Foreground.X/Y/Z/W"` are literal strings in the DLL, matching
Microsoft's public LottieGen convention exactly.

`SetColorProperty` was being called correctly but too early: at
`UpdateButtonPadding` time the animated visual's composition tree is still
empty (0 `CompositionColorBrush`es, `player.IsPlaying=0`) — Explorer creates
the actual Lottie visual asynchronously afterward and re-seeds the theme
color from its own default, winning the race against our earlier call. Fixed
by retrying on `CompositionTarget.Rendering` until the composition tree is
populated (bounded to ~180 frames / ~3s), then reasserting `SetColorProperty`
once more.

## Root cause found (live-debugged on 25H2)
Logs revealed the actual tree: `ExperienceToggleButtonRootPanel` is class
`Taskbar.TaskListButtonPanel` (a generic `Panel`, not `Grid`), with children
`Border "BackgroundElement"` (confirms the corner-radius/bg target guess) and
`Microsoft.UI.Xaml.Controls.AnimatedVisualPlayer "Icon"` — a **Lottie-based**
icon, not a `FontIcon`/`PathIcon`. That's why the original `IconElement`-only
recolor path (`.Foreground()`) silently did nothing: `AnimatedVisualPlayer`
isn't an `IconElement`.

Fixed by looking up Microsoft's own LottieGen-generated shell icon sources
(`microsoft/microsoft-ui-xaml` on GitHub): recoloring these goes through
`IAnimatedVisualSource2::SetColorProperty(L"Foreground", color)` on
`player.Source()` — confirmed real API (`AnimatedIcon.idl`) and confirmed
`"Foreground"` is the property name Microsoft's own generated sources use for
single-tone theme color. Also fixed the custom-icon overlay path, which
previously required `panel.try_as<Grid>()` and silently no-op'd since the
panel is `Taskbar.TaskListButtonPanel`, not `Grid` — now inserts via the
generic `Panel.Children()` collection instead.

## Debugging
Mod loads and hooks the correct symbol cleanly (confirmed live on 25H2,
Taskbar.View.dll 2605.22000.400.0 — resolved from PDB cache, hook applied,
no crash). Recolor not visibly taking effect; added `Wh_Log` tracing at each
decision point (hook entry, class/AutomationId match, panel lookup w/
child dump fallback, icon element lookup w/ child dump fallback, color
parse + Foreground-apply confirmation) to find where the chain breaks —
most likely candidates: `ExperienceToggleButtonRootPanel` child name/icon
element class name not matching this build's actual tree, or hook simply
not firing yet (needs a layout-triggering event, e.g. taskbar resize/DPI
change/Explorer restart).

## Status
Implemented: `windows-11-start-menu-button.wh.cpp` written, using symbol hook
`winrt::Taskbar::implementation::ExperienceToggleButton::UpdateButtonPadding`
in `Taskbar.View.dll`, filtered to `AutomationId == "StartButton"`, reaching
child `ExperienceToggleButtonRootPanel` — pattern verified against the public
`taskbar-start-button-position.wh.cpp` mod. Per-state styling driven by
Pointer{Entered,Exited,Pressed,Released,CaptureLost} handlers on the button
element rather than native VisualStateManager hooks (unverified for 25H2).
Custom icon mode overlays a WinRT `Image` (loaded via `BitmapImage.UriSource`
file:// URI); recolor mode sets `IconElement.Foreground` directly, so no
symbol offsets were hallucinated for the actual icon draw path.

**Unverified against a live 25H2 build** (no build access in this session):
mangled symbol string, exact `ExperienceToggleButtonRootPanel` structure, and
whether the corner-radius `Border` descendant exists in the current build's
control template. `HookSymbols` failing is a non-fatal, logged no-op per
Windhawk's own symbol-hook contract — worst case the mod loads inert.

## Verification
- Load mod in Windhawk on a Win11 25H2 test machine, target `explorer.exe`.
- Test each mode: custom icon swap, recolor tint, verify native hover/press animation still plays.
- Test per-state bg/padding/margin/corner-radius changes visually apply on hover/press.
- Test multi-monitor: confirm styling applies to Start button on every taskbar instance.
- Test settings reload: change settings in Windhawk UI, confirm mod updates live without explorer restart.
