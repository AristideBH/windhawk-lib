// ==WindhawkMod==
// @id              windows-11-start-menu-button
// @name            Windows 11 Start Button Customizer
// @description     Custom icon, recolor (animation-preserving), and per-state (default/hover/pressed) padding/margin/background/corner-radius for the Windows 11 taskbar Start button
// @version         1.0
// @author          arist
// @github          https://github.com/arist
// @license         MIT
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lshcore
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Windows 11 Start Button Customizer

Customize the Windows 11 taskbar Start button:

- **Custom icon** mode: replace the stock icon with your own PNG/ICO file.
- **Recolor** mode: tint the stock icon (render-time, on `IconElement.Foreground`)
  while keeping its native animation/theme behavior.
- Per-state (**default** / **hover** / **pressed**) background color, padding,
  margin and corner radius for the button box.

Only Windows 11 25H2 is supported. Applies to the Start button on every
taskbar instance (multi-monitor).

## Requirements

- Windows 11 25H2, 64-bit
- Windhawk v1.4 or later
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- icon:
  - mode: default
    $name: Icon mode
    $description: >-
      "default" leaves the stock icon untouched, "customIcon" replaces it
      with your own image, "recolor" tints the stock icon while preserving
      its native animation.
    $options:
    - default: System default
    - customIcon: Custom icon
    - recolor: Recolor
  - customIconPath: ""
    $name: Custom icon path
    $description: PNG or ICO file used when Icon mode is "Custom icon"
  - recolorColor: "#FFFFFF"
    $name: Resting color
    $description: >-
      Hex color (#RRGGBB or #AARRGGBB) applied to the icon while it's at
      rest (not mid hover/press transition). Used when Icon mode is
      "Recolor".
  - recolorShimmerColor: "#FF0000"
    $name: Shimmer color
    $description: >-
      Tint for the one-shot diagonal highlight sweep shown when the button
      is pressed. Set equal to the resting color to auto-derive a whitened
      tint from it instead of specifying one. Used when Icon mode is
      "Recolor".
  - recolorGradient: true
    $name: Depth gradient
    $description: >-
      Derive a diagonal light-to-dark gradient spanning the whole icon from
      the resting color, similar to the shading on the original flag icon,
      instead of filling each shape with one flat tone. Also enables the
      hover/press/menu-open brighten and the press sweep; when off, the
      icon is a flat single-tone fill with no animated states. Used when
      Icon mode is "Recolor".
  $name: Icon
  $description: Icon mode, custom icon path, and recolor tint colors
- default:
  - bgColor: ""
    $name: Background color
    $description: Hex color, empty = leave system background
  - iconPath: ""
    $name: Icon override
    $description: >-
      Optional PNG/ICO override for this state only, empty = use mode's
      icon
  - padding: -1
    $name: Padding
    $description: Uniform padding in pixels, -1 = leave system padding
  - margin: -1
    $name: Margin
    $description: Uniform margin in pixels, -1 = leave system margin
  - cornerRadius: -1
    $name: Corner radius
    $description: Corner radius in pixels, -1 = leave system corner radius
  $name: Default state
  $description: Style applied to the button while at rest
- hover:
  - bgColor: ""
    $name: Background color
    $description: Hex color, empty = leave system background
  - iconPath: ""
    $name: Icon override
    $description: >-
      Optional PNG/ICO override for this state only, empty = use mode's
      icon
  - padding: -1
    $name: Padding
    $description: Uniform padding in pixels, -1 = leave system padding
  - margin: -1
    $name: Margin
    $description: Uniform margin in pixels, -1 = leave system margin
  - cornerRadius: -1
    $name: Corner radius
    $description: Corner radius in pixels, -1 = leave system corner radius
  $name: Hover state
  $description: Style applied to the button while hovering
- pressed:
  - bgColor: ""
    $name: Background color
    $description: Hex color, empty = leave system background
  - iconPath: ""
    $name: Icon override
    $description: >-
      Optional PNG/ICO override for this state only, empty = use mode's
      icon
  - padding: -1
    $name: Padding
    $description: Uniform padding in pixels, -1 = leave system padding
  - margin: -1
    $name: Margin
    $description: Uniform margin in pixels, -1 = leave system margin
  - cornerRadius: -1
    $name: Corner radius
    $description: Corner radius in pixels, -1 = leave system corner radius
  $name: Pressed state
  $description: Style applied to the button while pressed
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#undef GetCurrentTime

#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

// The Start button's icon is a Lottie-based Microsoft.UI.Xaml.Controls.
// AnimatedVisualPlayer (WinUI 2/MUX), not a Windows.UI.Xaml IconElement.
#define WH_WINRT_WINUI2
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

using namespace winrt::Windows::UI::Xaml;

// -----------------------------------------------------------------------
// Settings
// -----------------------------------------------------------------------

enum class IconMode {
    Default,
    CustomIcon,
    Recolor,
};

struct StateSettings {
    std::wstring bgColor;
    std::wstring iconPath;
    int padding = -1;
    int margin = -1;
    int cornerRadius = -1;
};

struct {
    IconMode mode;
    std::wstring customIconPath;
    std::wstring recolorColor;
    std::wstring recolorShimmerColor;
    bool recolorGradient;
    StateSettings states[3];  // indexed by ButtonState
} g_settings;

enum ButtonState {
    kStateDefault = 0,
    kStateHover = 1,
    kStatePressed = 2,
};

std::atomic<bool> g_unloading;
std::atomic<bool> g_taskbarViewDllLoaded;

// -----------------------------------------------------------------------
// Per-button tracked state (declared early: StartPersistentIconColorMaintenance,
// defined further down, looks up TrackedButton state every frame)
// -----------------------------------------------------------------------

struct TrackedButton {
    winrt::weak_ref<FrameworkElement> buttonRef;
    winrt::weak_ref<FrameworkElement> panelRef;
    ButtonState lastState = kStateDefault;
    bool menuOpen = false;  // spec state D: Start menu held open
    // Icon depth-gradient animation state (read/written from the
    // per-frame loop in StartPersistentIconColorMaintenance).
    float iconElevatedAmount = 0.0f;  // 0..1, ramped over kHoverTransitionMs
    ULONGLONG iconLastFrameTimeMs = 0;
    bool iconPressSweepActive = false;
    ULONGLONG iconPressSweepStartMs = 0;
    winrt::Windows::UI::Xaml::Controls::Image customIconOverlay{nullptr};
    winrt::event_token pointerEnteredToken;
    winrt::event_token pointerExitedToken;
    winrt::event_token pointerPressedToken;
    winrt::event_token pointerReleasedToken;
    winrt::event_token pointerCaptureLostToken;
    winrt::event_token checkedToken;
    winrt::event_token uncheckedToken;
};

std::vector<TrackedButton> g_trackedButtons;

TrackedButton* FindTrackedButton(const FrameworkElement& button) {
    for (auto& tracked : g_trackedButtons) {
        auto elem = tracked.buttonRef.get();
        if (elem && elem == button) {
            return &tracked;
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------
// XAML tree helpers (pattern verified against public Windhawk taskbar mods)
// -----------------------------------------------------------------------

FrameworkElement EnumChildElements(
    FrameworkElement element,
    const std::function<bool(FrameworkElement)>& enumCallback) {
    int childrenCount = Media::VisualTreeHelper::GetChildrenCount(element);
    for (int i = 0; i < childrenCount; i++) {
        auto child = Media::VisualTreeHelper::GetChild(element, i)
                         .try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        if (enumCallback(child)) {
            return child;
        }
    }
    return nullptr;
}

FrameworkElement FindChildByName(FrameworkElement element, PCWSTR name) {
    return EnumChildElements(element, [name](FrameworkElement child) {
        return child.Name() == name;
    });
}

FrameworkElement FindChildByClassName(FrameworkElement element,
                                       PCWSTR className) {
    return EnumChildElements(element, [className](FrameworkElement child) {
        return winrt::get_class_name(child) == className;
    });
}

// Recursive descendant search (icon/border elements may be nested a few
// levels deep inside the button's root panel).
FrameworkElement FindDescendant(
    FrameworkElement element,
    const std::function<bool(FrameworkElement)>& predicate,
    int maxDepth = 6) {
    if (maxDepth <= 0) {
        return nullptr;
    }

    FrameworkElement result = nullptr;
    EnumChildElements(element, [&](FrameworkElement child) {
        if (predicate(child)) {
            result = child;
            return true;
        }
        auto nested = FindDescendant(child, predicate, maxDepth - 1);
        if (nested) {
            result = nested;
            return true;
        }
        return false;
    });
    return result;
}

FrameworkElement FindIconElement(FrameworkElement panel) {
    return FindDescendant(panel, [](FrameworkElement child) {
        auto className = winrt::get_class_name(child);
        return className == L"Windows.UI.Xaml.Controls.FontIcon" ||
               className == L"Windows.UI.Xaml.Controls.PathIcon" ||
               className == L"Windows.UI.Xaml.Controls.BitmapIcon" ||
               className == L"Windows.UI.Xaml.Controls.Image" ||
               className == L"Windows.UI.Xaml.Controls.AnimatedIcon" ||
               className == L"Microsoft.UI.Xaml.Controls.AnimatedVisualPlayer";
    });
}

// The Start button icon ships as a Lottie-based AnimatedVisualPlayer
// rendering Windows' classic four-color flag logo (confirmed live: 28
// CompositionSpriteShape fill brushes with genuine distinct RGB values, not
// a single-tone icon). IAnimatedVisualSource2::SetColorProperty("Foreground",
// ...) - the theme-color mechanism used by Microsoft's own published
// LottieGen single-tone icon sources - was confirmed to have zero effect on
// this specific asset even after reasserting every frame for 2 seconds; the
// "Foreground" string found in Taskbar.View.dll evidently belongs to a
// different icon class in the same binary. Since there is no working theme
// knob for this asset, recolor is done by directly overwriting every
// CompositionSpriteShape's FillBrush/StrokeBrush color in its Lottie shape
// tree. Original colors are captured (keyed by brush COM identity) before
// the first override so Default mode can restore the real multi-color flag
// look instead of guessing a fallback color.
//
// Lottie/Lottie-Windows content is a vector shape tree, not sprite-visual
// rectangles: ShapeVisual.Shapes() -> CompositionContainerShape.Shapes()
// (recursively) -> CompositionSpriteShape.FillBrush()/StrokeBrush(). Also,
// AnimatedVisualPlayer hosts this tree as an "element child visual"
// (ElementCompositionPreview.SetElementChildVisual), a separate composition
// hook from the element's own XAML-owned visual, so both GetElementVisual
// and GetElementChildVisual must be checked.
// Mutating an existing brush's .Color() only works for a plain
// CompositionColorBrush - it silently no-ops (try_as fails) on a
// CompositionLinearGradientBrush/CompositionRadialGradientBrush, which is
// what the Start icon's flag squares actually use (confirmed: user reports
// the resting squares are a blue *gradient*, unaffected by our .Color()
// writes, while a separate solid-brush shape - the hover/press highlight
// overlay - did visibly change, which is what read as "shimmer only while
// animating"). Fix: replace the whole FillBrush/StrokeBrush/Visual.Brush
// with a freshly created solid CompositionColorBrush, which works
// regardless of the original brush's type. The original brush object
// (gradient or otherwise) is captured by shape/visual identity before the
// first replacement, so Default mode can restore the exact original -
// including the gradient - rather than guessing a fallback.
std::unordered_map<void*, winrt::Windows::UI::Composition::CompositionBrush>
    g_originalIconBrushes;

template <typename T>
void* Identity(T const& obj) {
    return winrt::get_abi(obj);
}

// Lightness-only adjustment (HSL), so the derived shade keeps the chosen
// color's hue/saturation instead of just blending toward white/black.
winrt::Windows::UI::Color AdjustLightness(winrt::Windows::UI::Color color,
                                           float delta) {
    float r = color.R / 255.0f;
    float g = color.G / 255.0f;
    float b = color.B / 255.0f;

    float maxC = std::max({r, g, b});
    float minC = std::min({r, g, b});
    float l = (maxC + minC) / 2.0f;
    float s = 0.0f;
    float h = 0.0f;

    if (maxC != minC) {
        float d = maxC - minC;
        s = l > 0.5f ? d / (2.0f - maxC - minC) : d / (maxC + minC);
        if (maxC == r) {
            h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        } else if (maxC == g) {
            h = (b - r) / d + 2.0f;
        } else {
            h = (r - g) / d + 4.0f;
        }
        h /= 6.0f;
    }

    l = std::clamp(l + delta, 0.0f, 1.0f);

    auto hueToRgb = [](float p, float q, float t) {
        if (t < 0) {
            t += 1;
        }
        if (t > 1) {
            t -= 1;
        }
        if (t < 1.0f / 6) {
            return p + (q - p) * 6 * t;
        }
        if (t < 1.0f / 2) {
            return q;
        }
        if (t < 2.0f / 3) {
            return p + (q - p) * (2.0f / 3 - t) * 6;
        }
        return p;
    };

    float outR, outG, outB;
    if (s == 0.0f) {
        outR = outG = outB = l;
    } else {
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        outR = hueToRgb(p, q, h + 1.0f / 3);
        outG = hueToRgb(p, q, h);
        outB = hueToRgb(p, q, h - 1.0f / 3);
    }

    auto toByte = [](float v) {
        return (BYTE)std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    };
    return winrt::Windows::UI::Color{color.A, toByte(outR), toByte(outG),
                                      toByte(outB)};
}

// -----------------------------------------------------------------------
// Depth gradient: ONE diagonal gradient conceptually spanning the whole
// icon (all 4 flag tiles), not 4 independent per-tile gradients. Each
// shape's brush gets Absolute-mode StartPoint/EndPoint expressed in *that
// shape's own pre-transform geometry space*:
//   local = (global - accumulatedOffset) / accumulatedScale
// where accumulatedOffset/Scale is this shape's position/scale within the
// icon's overall Composition tree, walked down from the root visual
// (translation + scale only - rotation/TransformMatrix assumed unused by
// this asset's static grid layout; unverified against a live 25H2 build,
// same caveat as the rest of this file). Recomputed every frame (not
// captured once) since Explorer's native hover/press Lottie animation is
// suspected to move/scale these shapes during transitions - same reason
// StartPersistentIconColorMaintenance already reasserts colors every tick
// instead of once.
// "Global" space is just the AnimatedVisualPlayer's own render size,
// assuming the Lottie art fills its own bounds exactly.
// -----------------------------------------------------------------------

using Float2 = winrt::Windows::Foundation::Numerics::float2;
using Float3 = winrt::Windows::Foundation::Numerics::float3;

constexpr float kGradientLightenAmount = 0.18f;
constexpr float kGradientDarkenAmount = 0.18f;
// Extra brighten/shadow-relief while hovered/pressed/menu-open, ramped in
// over kHoverTransitionMs rather than stepped instantly.
constexpr float kElevatedLightenBoost = 0.10f;
constexpr float kElevatedDarkenRelief = 0.06f;
constexpr float kHoverTransitionMs = 180.0f;
// One-shot diagonal highlight sweep triggered on press.
constexpr float kSweepDurationMs = 300.0f;
constexpr float kSweepBandWidth = 0.15f;

struct GradientFrameParams {
    winrt::Windows::UI::Color baseColor{};
    winrt::Windows::UI::Color bandColor{};
    float elevatedAmount = 0.0f;  // 0..1, ramped
    float sweepProgress = -1.0f;  // -1 == inactive, else raw progress 0..1
    Float2 iconSize{0.0f, 0.0f};  // "global" gradient space (player render size)
};

winrt::Windows::UI::Color LerpColor(winrt::Windows::UI::Color a,
                                     winrt::Windows::UI::Color b,
                                     float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto lerpByte = [t](BYTE x, BYTE y) {
        return (BYTE)std::round(x + (y - x) * t);
    };
    return winrt::Windows::UI::Color{lerpByte(a.A, b.A), lerpByte(a.R, b.R),
                                      lerpByte(a.G, b.G), lerpByte(a.B, b.B)};
}

float EaseOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

winrt::Windows::UI::Composition::CompositionBrush CreateDepthGradientBrush(
    winrt::Windows::UI::Composition::Compositor compositor,
    const GradientFrameParams& params,
    Float2 shapeOffset,
    Float2 shapeScale) {
    float lightenDelta =
        kGradientLightenAmount + kElevatedLightenBoost * params.elevatedAmount;
    float darkenDelta = std::max(
        0.0f, kGradientDarkenAmount - kElevatedDarkenRelief * params.elevatedAmount);
    auto lighter = AdjustLightness(params.baseColor, lightenDelta);
    auto darker = AdjustLightness(params.baseColor, -darkenDelta);

    // Color at an arbitrary offset along the base (non-sweep) gradient -
    // used to sandwich the sweep band so it doesn't disturb the rest of
    // the gradient.
    auto colorAtOffset = [&](float t) { return LerpColor(lighter, darker, t); };

    struct Stop {
        float offset;
        winrt::Windows::UI::Color color;
    };
    std::vector<Stop> stops = {{0.0f, lighter}, {1.0f, darker}};

    if (params.sweepProgress >= 0.0f) {
        float eased = EaseOut(params.sweepProgress);
        float bandCenter =
            -kSweepBandWidth / 2.0f + eased * (1.0f + kSweepBandWidth);
        float bandStart = bandCenter - kSweepBandWidth / 2.0f;
        float bandEnd = bandCenter + kSweepBandWidth / 2.0f;

        if (bandEnd > 0.0f && bandStart < 1.0f) {
            float clampedStart = std::clamp(bandStart, 0.0f, 1.0f);
            float clampedCenter = std::clamp(bandCenter, 0.0f, 1.0f);
            float clampedEnd = std::clamp(bandEnd, 0.0f, 1.0f);
            stops.push_back({clampedStart, colorAtOffset(clampedStart)});
            stops.push_back({clampedCenter, params.bandColor});
            stops.push_back({clampedEnd, colorAtOffset(clampedEnd)});
        }
    }

    std::sort(stops.begin(), stops.end(),
              [](const Stop& a, const Stop& b) { return a.offset < b.offset; });

    auto gradientBrush = compositor.CreateLinearGradientBrush();
    gradientBrush.MappingMode(
        winrt::Windows::UI::Composition::CompositionMappingMode::Absolute);

    // Degenerate scale (shape not yet laid out) - fall back to unscaled
    // rather than dividing by ~0.
    Float2 safeScale{shapeScale.x != 0.0f ? shapeScale.x : 1.0f,
                      shapeScale.y != 0.0f ? shapeScale.y : 1.0f};
    gradientBrush.StartPoint((Float2{0.0f, 0.0f} - shapeOffset) / safeScale);
    gradientBrush.EndPoint((params.iconSize - shapeOffset) / safeScale);

    auto colorStops = gradientBrush.ColorStops();
    for (auto& stop : stops) {
        colorStops.Append(
            compositor.CreateColorGradientStop(stop.offset, stop.color));
    }

    return gradientBrush;
}

winrt::Windows::UI::Composition::CompositionBrush CreateRecolorBrush(
    winrt::Windows::UI::Composition::Compositor compositor,
    const GradientFrameParams& params,
    Float2 shapeOffset,
    Float2 shapeScale) {
    if (g_settings.recolorGradient) {
        return CreateDepthGradientBrush(compositor, params, shapeOffset,
                                         shapeScale);
    }
    return compositor.CreateColorBrush(params.baseColor);
}

int ReplaceBrush(
    winrt::Windows::UI::Composition::Compositor compositor,
    void* identity,
    winrt::Windows::UI::Composition::CompositionBrush currentBrush,
    const std::optional<GradientFrameParams>& frameParams,
    Float2 shapeOffset,
    Float2 shapeScale,
    std::function<void(winrt::Windows::UI::Composition::CompositionBrush)> setter) {
    if (!compositor) {
        return 0;
    }

    if (frameParams) {
        if (currentBrush) {
            g_originalIconBrushes.try_emplace(identity, currentBrush);
        }
        setter(CreateRecolorBrush(compositor, *frameParams, shapeOffset,
                                   shapeScale));
    } else {
        auto it = g_originalIconBrushes.find(identity);
        if (it != g_originalIconBrushes.end()) {
            setter(it->second);
        }
    }

    return 1;
}

int RecolorShapeBrushes(winrt::Windows::UI::Composition::CompositionShape shape,
                         const std::optional<GradientFrameParams>& frameParams,
                         Float2 accOffset,
                         Float2 accScale,
                         int depth = 0) {
    if (!shape || depth > 12) {
        return 0;
    }

    int count = 0;

    // CompositionShape's own Offset/Scale place it within its parent -
    // they don't affect its FillBrush's internal UV mapping, but they do
    // affect where this shape (and its children) sit in the icon overall,
    // so accumulate through every shape, leaf or container.
    Float2 totalOffset = accOffset + accScale * shape.Offset();
    Float2 totalScale = accScale * shape.Scale();

    if (auto spriteShape =
            shape.try_as<winrt::Windows::UI::Composition::CompositionSpriteShape>()) {
        auto compositor = spriteShape.Compositor();

        if (spriteShape.FillBrush()) {
            count += ReplaceBrush(
                compositor, Identity(spriteShape), spriteShape.FillBrush(),
                frameParams, totalOffset, totalScale, [spriteShape](auto brush) {
                    spriteShape.FillBrush(brush);
                });
        }
        if (spriteShape.StrokeBrush()) {
            // Distinct identity key from the fill (same shape, different
            // brush slot) - offset the raw shape pointer by 1 byte.
            void* strokeIdentity =
                reinterpret_cast<char*>(Identity(spriteShape)) + 1;
            count += ReplaceBrush(
                compositor, strokeIdentity, spriteShape.StrokeBrush(),
                frameParams, totalOffset, totalScale, [spriteShape](auto brush) {
                    spriteShape.StrokeBrush(brush);
                });
        }
    }

    if (auto containerShape =
            shape.try_as<winrt::Windows::UI::Composition::CompositionContainerShape>()) {
        for (auto child : containerShape.Shapes()) {
            count += RecolorShapeBrushes(child, frameParams, totalOffset,
                                          totalScale, depth + 1);
        }
    }

    return count;
}

int RecolorVisualBrushes(winrt::Windows::UI::Composition::Visual visual,
                          const std::optional<GradientFrameParams>& frameParams,
                          Float3 accOffset,
                          Float3 accScale,
                          int depth = 0) {
    if (!visual || depth > 12) {
        return 0;
    }

    int count = 0;

    Float3 totalOffset = accOffset + accScale * visual.Offset();
    Float3 totalScale = accScale * visual.Scale();
    Float2 totalOffset2{totalOffset.x, totalOffset.y};
    Float2 totalScale2{totalScale.x, totalScale.y};

    if (auto spriteVisual =
            visual.try_as<winrt::Windows::UI::Composition::SpriteVisual>()) {
        if (spriteVisual.Brush()) {
            count += ReplaceBrush(
                spriteVisual.Compositor(), Identity(spriteVisual),
                spriteVisual.Brush(), frameParams, totalOffset2, totalScale2,
                [spriteVisual](auto brush) { spriteVisual.Brush(brush); });
        }
    }

    if (auto shapeVisual =
            visual.try_as<winrt::Windows::UI::Composition::ShapeVisual>()) {
        for (auto shape : shapeVisual.Shapes()) {
            count += RecolorShapeBrushes(shape, frameParams, totalOffset2,
                                          totalScale2, depth + 1);
        }
    }

    if (auto container =
            visual.try_as<winrt::Windows::UI::Composition::ContainerVisual>()) {
        for (auto child : container.Children()) {
            count += RecolorVisualBrushes(child, frameParams, totalOffset,
                                           totalScale, depth + 1);
        }
    }

    return count;
}

int RecolorAnimatedVisualPlayer(
    winrt::Microsoft::UI::Xaml::Controls::AnimatedVisualPlayer player,
    const std::optional<GradientFrameParams>& frameParams) {
    int found = 0;
    Float3 rootOffset{0.0f, 0.0f, 0.0f};
    Float3 rootScale{1.0f, 1.0f, 1.0f};
    if (auto ownVisual =
            Hosting::ElementCompositionPreview::GetElementVisual(player)) {
        found +=
            RecolorVisualBrushes(ownVisual, frameParams, rootOffset, rootScale);
    }
    if (auto childVisual =
            Hosting::ElementCompositionPreview::GetElementChildVisual(player)) {
        found += RecolorVisualBrushes(childVisual, frameParams, rootOffset,
                                       rootScale);
    }
    return found;
}

// -----------------------------------------------------------------------
// Color / settings parsing
// -----------------------------------------------------------------------

std::optional<winrt::Windows::UI::Color> ParseHexColor(std::wstring hex) {
    if (hex.empty()) {
        return std::nullopt;
    }
    if (hex[0] == L'#') {
        hex = hex.substr(1);
    }

    for (wchar_t c : hex) {
        if (!iswxdigit(c)) {
            return std::nullopt;
        }
    }

    BYTE a = 0xFF, r, g, b;
    if (hex.size() == 6) {
        r = (BYTE)wcstoul(hex.substr(0, 2).c_str(), nullptr, 16);
        g = (BYTE)wcstoul(hex.substr(2, 2).c_str(), nullptr, 16);
        b = (BYTE)wcstoul(hex.substr(4, 2).c_str(), nullptr, 16);
    } else if (hex.size() == 8) {
        a = (BYTE)wcstoul(hex.substr(0, 2).c_str(), nullptr, 16);
        r = (BYTE)wcstoul(hex.substr(2, 2).c_str(), nullptr, 16);
        g = (BYTE)wcstoul(hex.substr(4, 2).c_str(), nullptr, 16);
        b = (BYTE)wcstoul(hex.substr(6, 2).c_str(), nullptr, 16);
    } else {
        return std::nullopt;
    }

    return winrt::Windows::UI::Color{a, r, g, b};
}

// Empirically (live on 25H2): a one-shot or briefly-retried brush override
// gets fought/reset by the icon's own native hover/press Lottie animation
// playback - confirmed by the override only visibly appearing WHILE that
// animation is actively playing, then reverting once it settles at rest.
// So this still reasserts every frame for the button's entire lifetime
// (only stopped on unload) - but *which* color/gradient it reasserts is
// now driven by our own tracked interaction state (idle/hover/pressed,
// see TrackedButton::lastState, plus TrackedButton::menuOpen), not
// player.IsPlaying(). IsPlaying() was only ever a proxy for "Explorer's
// animation is fighting us right now" - it can't express hover-brighten
// vs. a one-shot press sweep vs. a sustained menu-open hold, which are
// genuinely different states, not different phases of the same animation.
void StartPersistentIconColorMaintenance(
    winrt::Microsoft::UI::Xaml::Controls::AnimatedVisualPlayer player,
    FrameworkElement button) {
    auto token = std::make_shared<winrt::event_token>();
    auto frameCounter = std::make_shared<int>(0);
    *token = Media::CompositionTarget::Rendering(
        [player, button, token, frameCounter](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::Foundation::IInspectable const&) {
            (*frameCounter)++;
            bool logThisFrame = (*frameCounter % 60) == 0;

            if (g_unloading) {
                RecolorAnimatedVisualPlayer(player, std::nullopt);
                Media::CompositionTarget::Rendering(*token);
                return;
            }

            if (g_settings.mode != IconMode::Recolor) {
                int touched = RecolorAnimatedVisualPlayer(player, std::nullopt);
                if (logThisFrame) {
                    Wh_Log(L"Icon color maintenance: mode!=Recolor, "
                           L"restoring original, touched=%d",
                           touched);
                }
                return;
            }

            auto tracked = FindTrackedButton(button);
            auto baseColor = ParseHexColor(g_settings.recolorColor);
            if (!tracked || !baseColor) {
                int touched = RecolorAnimatedVisualPlayer(player, std::nullopt);
                if (logThisFrame) {
                    Wh_Log(L"Icon color maintenance: no tracked button or "
                           L"unparsable resting color, restoring original, "
                           L"touched=%d",
                           touched);
                }
                return;
            }

            ULONGLONG now = GetTickCount64();
            if (tracked->iconLastFrameTimeMs == 0) {
                tracked->iconLastFrameTimeMs = now;
            }
            float deltaMs = (float)(now - tracked->iconLastFrameTimeMs);
            tracked->iconLastFrameTimeMs = now;

            // "Elevated" = hover, pressed, or menu held open - all three
            // share the same brighten treatment per spec (state D reuses
            // hover's "elevated brightness tone").
            bool elevated =
                tracked->lastState != kStateDefault || tracked->menuOpen;
            float target = elevated ? 1.0f : 0.0f;
            float rampStep = deltaMs / kHoverTransitionMs;
            if (tracked->iconElevatedAmount < target) {
                tracked->iconElevatedAmount =
                    std::min(target, tracked->iconElevatedAmount + rampStep);
            } else if (tracked->iconElevatedAmount > target) {
                tracked->iconElevatedAmount =
                    std::max(target, tracked->iconElevatedAmount - rampStep);
            }

            // One-shot sweep: (re)triggered every time we transition into
            // Pressed, runs for kSweepDurationMs regardless of how long the
            // press is held, then clears itself.
            if (tracked->lastState == kStatePressed &&
                !tracked->iconPressSweepActive) {
                tracked->iconPressSweepActive = true;
                tracked->iconPressSweepStartMs = now;
            }

            float sweepProgress = -1.0f;
            if (tracked->iconPressSweepActive) {
                float elapsed = (float)(now - tracked->iconPressSweepStartMs);
                if (elapsed >= kSweepDurationMs) {
                    tracked->iconPressSweepActive = false;
                } else {
                    sweepProgress = elapsed / kSweepDurationMs;
                }
            }

            // Band tint: explicit override if configured to something other
            // than the resting color, otherwise auto-derive by pushing the
            // resting color toward white.
            auto shimmerColor = ParseHexColor(g_settings.recolorShimmerColor);
            winrt::Windows::UI::Color bandColor =
                (shimmerColor &&
                 g_settings.recolorShimmerColor != g_settings.recolorColor)
                    ? *shimmerColor
                    : AdjustLightness(*baseColor, 0.5f);

            GradientFrameParams params;
            params.baseColor = *baseColor;
            params.bandColor = bandColor;
            params.elevatedAmount = tracked->iconElevatedAmount;
            params.sweepProgress = sweepProgress;
            params.iconSize = {(float)player.ActualWidth(),
                                (float)player.ActualHeight()};

            int touched = RecolorAnimatedVisualPlayer(player, params);
            if (logThisFrame) {
                Wh_Log(
                    L"Icon color maintenance: state=%d menuOpen=%d "
                    L"elevated=%.2f sweep=%.2f touched=%d",
                    (int)tracked->lastState, (int)tracked->menuOpen,
                    tracked->iconElevatedAmount, sweepProgress, touched);
            }
        });
}

FrameworkElement FindBackgroundBorder(FrameworkElement panel) {
    return FindDescendant(
        panel,
        [](FrameworkElement child) {
            return winrt::get_class_name(child) ==
                   L"Windows.UI.Xaml.Controls.Border";
        },
        /*maxDepth=*/2);
}

std::wstring FilePathToFileUri(const std::wstring& path) {
    std::wstring uri = L"file:///";
    for (wchar_t c : path) {
        uri += (c == L'\\') ? L'/' : c;
    }
    return uri;
}

// -----------------------------------------------------------------------
// Style application
// -----------------------------------------------------------------------

void ApplyIconForState(FrameworkElement panel,
                        TrackedButton& tracked,
                        const StateSettings& state) {
    auto iconElement = FindIconElement(panel);
    if (iconElement) {
        Wh_Log(L"Icon element found: class=%s name=%s",
               winrt::get_class_name(iconElement).c_str(),
               iconElement.Name().c_str());
    } else {
        Wh_Log(L"Icon element NOT found under panel (class=%s)",
               winrt::get_class_name(panel).c_str());
        int childCount = Media::VisualTreeHelper::GetChildrenCount(panel);
        for (int i = 0; i < childCount; i++) {
            auto child = Media::VisualTreeHelper::GetChild(panel, i)
                             .try_as<FrameworkElement>();
            if (child) {
                Wh_Log(L"  panel child %d: class=%s name=%s", i,
                       winrt::get_class_name(child).c_str(),
                       child.Name().c_str());
            }
        }
    }

    std::wstring iconPath =
        !state.iconPath.empty() ? state.iconPath : g_settings.customIconPath;

    if (g_settings.mode == IconMode::CustomIcon && !iconPath.empty()) {
        // Hide the native icon and overlay our own Image element. The panel
        // (Taskbar.TaskListButtonPanel) is a Panel, not necessarily a Grid,
        // so insert into its generic Children collection rather than
        // requiring Grid row/column semantics.
        if (iconElement) {
            iconElement.Visibility(Visibility::Collapsed);
        }

        auto panelAsPanel = panel.try_as<Controls::Panel>();
        if (panelAsPanel) {
            if (!tracked.customIconOverlay) {
                Controls::Image image;
                image.Stretch(Media::Stretch::Uniform);
                image.HorizontalAlignment(HorizontalAlignment::Center);
                image.VerticalAlignment(VerticalAlignment::Center);
                if (iconElement) {
                    image.Width(iconElement.ActualWidth() > 0
                                    ? iconElement.ActualWidth()
                                    : iconElement.Width());
                    image.Height(iconElement.ActualHeight() > 0
                                     ? iconElement.ActualHeight()
                                     : iconElement.Height());
                    if (auto grid = panel.try_as<Controls::Grid>()) {
                        Controls::Grid::SetRow(image,
                                                Controls::Grid::GetRow(iconElement));
                        Controls::Grid::SetColumn(
                            image, Controls::Grid::GetColumn(iconElement));
                    }
                }
                panelAsPanel.Children().Append(image);
                tracked.customIconOverlay = image;
            }

            Media::Imaging::BitmapImage bitmap;
            bitmap.UriSource(
                winrt::Windows::Foundation::Uri{FilePathToFileUri(iconPath)});
            tracked.customIconOverlay.Source(bitmap);
            tracked.customIconOverlay.Visibility(Visibility::Visible);
        } else {
            Wh_Log(L"Panel is not a Panel-derived element, cannot overlay icon");
        }
    } else {
        // Not custom-icon mode (or no path set): remove any overlay and
        // restore the native icon.
        if (tracked.customIconOverlay) {
            tracked.customIconOverlay.Visibility(Visibility::Collapsed);
        }
        if (iconElement) {
            iconElement.Visibility(Visibility::Visible);

            // AnimatedVisualPlayer (the Start icon's actual type) is NOT
            // handled here: its Lottie shape-brush colors get fought/reset
            // by native hover/press animation playback, so it needs
            // continuous per-frame enforcement rather than a one-shot
            // property set. See StartPersistentIconColorMaintenance, run
            // once per button for its whole lifetime.
            if (auto iconElementAsIcon =
                    iconElement.try_as<Controls::IconElement>()) {
                if (g_settings.mode == IconMode::Recolor) {
                    auto color = ParseHexColor(g_settings.recolorColor);
                    if (color) {
                        iconElementAsIcon.Foreground(
                            Media::SolidColorBrush{*color});
                    }
                } else {
                    iconElementAsIcon.ClearValue(
                        Controls::IconElement::ForegroundProperty());
                }
            }
        }
    }
}

void ApplyBoxStyleForState(FrameworkElement button,
                            FrameworkElement panel,
                            const StateSettings& state) {
    if (state.margin >= 0) {
        button.Margin(Thickness{(double)state.margin, (double)state.margin,
                                 (double)state.margin, (double)state.margin});
    } else {
        button.ClearValue(FrameworkElement::MarginProperty());
    }

    auto border = FindBackgroundBorder(panel);
    FrameworkElement bgTarget = border ? border : panel;

    if (state.padding >= 0) {
        Thickness padding{(double)state.padding, (double)state.padding,
                           (double)state.padding, (double)state.padding};
        if (auto ctl = bgTarget.try_as<Controls::Control>()) {
            ctl.Padding(padding);
        } else if (auto grid = bgTarget.try_as<Controls::Grid>()) {
            grid.Padding(padding);
        } else if (border) {
            border.as<Controls::Border>().Padding(padding);
        }
    }

    auto color = ParseHexColor(state.bgColor);
    if (color) {
        Media::SolidColorBrush brush{*color};
        if (border) {
            border.as<Controls::Border>().Background(brush);
        } else if (auto panelAsPanel = bgTarget.try_as<Controls::Panel>()) {
            panelAsPanel.Background(brush);
        }
    }

    if (state.cornerRadius >= 0 && border) {
        border.as<Controls::Border>().CornerRadius(
            CornerRadius{(double)state.cornerRadius, (double)state.cornerRadius,
                         (double)state.cornerRadius, (double)state.cornerRadius});
    }
}

void ApplyState(FrameworkElement button,
                 FrameworkElement panel,
                 TrackedButton& tracked,
                 ButtonState newState) {
    if (g_unloading) {
        button.ClearValue(FrameworkElement::MarginProperty());
        auto border = FindBackgroundBorder(panel);
        if (border) {
            auto b = border.as<Controls::Border>();
            b.ClearValue(Controls::Border::BackgroundProperty());
            b.ClearValue(Controls::Border::PaddingProperty());
            b.ClearValue(Controls::Border::CornerRadiusProperty());
        }
        if (tracked.customIconOverlay) {
            tracked.customIconOverlay.Visibility(Visibility::Collapsed);
        }
        auto iconElement = FindIconElement(panel);
        if (iconElement) {
            iconElement.Visibility(Visibility::Visible);
            if (auto icon = iconElement.try_as<Controls::IconElement>()) {
                icon.ClearValue(Controls::IconElement::ForegroundProperty());
            }
        }
        return;
    }

    tracked.lastState = newState;
    const StateSettings& state = g_settings.states[newState];
    ApplyBoxStyleForState(button, panel, state);
    ApplyIconForState(panel, tracked, state);
}

// -----------------------------------------------------------------------
// Pointer state tracking (drives per-state style, native hover/press
// animations are untouched since we only set background/icon/padding on
// our own elements, not the layers the native VisualStateManager owns).
// -----------------------------------------------------------------------

void SetupButtonTracking(FrameworkElement button, FrameworkElement panel) {
    if (FindTrackedButton(button)) {
        Wh_Log(L"Button already tracked, skipping setup");
        return;
    }

    Wh_Log(L"Setting up tracking for new button instance");

    g_trackedButtons.push_back({});
    TrackedButton& tracked = g_trackedButtons.back();
    tracked.buttonRef = button;
    tracked.panelRef = panel;

    auto updateState = [](FrameworkElement button, ButtonState state) {
        auto tracked = FindTrackedButton(button);
        if (!tracked) {
            return;
        }
        auto panel = tracked->panelRef.get();
        if (!panel) {
            return;
        }
        ApplyState(button, panel, *tracked, state);
    };

    tracked.pointerEnteredToken = button.PointerEntered(
        [updateState](winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
            updateState(sender.try_as<FrameworkElement>(), kStateHover);
        });
    tracked.pointerExitedToken = button.PointerExited(
        [updateState](winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
            updateState(sender.try_as<FrameworkElement>(), kStateDefault);
        });
    tracked.pointerPressedToken = button.PointerPressed(
        [updateState](winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
            updateState(sender.try_as<FrameworkElement>(), kStatePressed);
        });
    tracked.pointerReleasedToken = button.PointerReleased(
        [updateState](winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
            updateState(sender.try_as<FrameworkElement>(), kStateHover);
        });
    tracked.pointerCaptureLostToken = button.PointerCaptureLost(
        [updateState](winrt::Windows::Foundation::IInspectable const& sender,
                       auto const&) {
            updateState(sender.try_as<FrameworkElement>(), kStateDefault);
        });

    // Spec state D (menu held open) - unverified guess that
    // Taskbar.ExperienceToggleButton derives from Primitives.ToggleButton
    // (consistent with the "ToggleButton" in its class name), same
    // try-and-log-if-it-fails pattern as the rest of this mod. If the cast
    // fails, state D simply never triggers; everything else still works.
    if (auto toggleButton = button.try_as<Controls::Primitives::ToggleButton>()) {
        tracked.checkedToken = toggleButton.Checked(
            [](winrt::Windows::Foundation::IInspectable const& sender,
               auto const&) {
                if (auto t = FindTrackedButton(sender.try_as<FrameworkElement>())) {
                    t->menuOpen = true;
                }
            });
        tracked.uncheckedToken = toggleButton.Unchecked(
            [](winrt::Windows::Foundation::IInspectable const& sender,
               auto const&) {
                if (auto t = FindTrackedButton(sender.try_as<FrameworkElement>())) {
                    t->menuOpen = false;
                }
            });
    } else {
        Wh_Log(L"Button is not a ToggleButton, menu-open icon state will "
               L"not be detected");
    }

    ApplyState(button, panel, tracked, kStateDefault);

    if (auto iconElement = FindIconElement(panel)) {
        if (auto player =
                iconElement
                    .try_as<winrt::Microsoft::UI::Xaml::Controls::AnimatedVisualPlayer>()) {
            StartPersistentIconColorMaintenance(player, button);
        }
    }
}

void ReapplyAllTrackedButtons() {
    for (auto& tracked : g_trackedButtons) {
        auto button = tracked.buttonRef.get();
        auto panel = tracked.panelRef.get();
        if (button && panel) {
            ApplyState(button, panel, tracked, tracked.lastState);
        }
    }
}

// -----------------------------------------------------------------------
// Hook: ExperienceToggleButton::UpdateButtonPadding
// (symbol verified against public "Start button always on the left" mod,
//  ramensoftware/windhawk-mods, which uses the same hook point to reach
//  the Start button's "ExperienceToggleButtonRootPanel".)
// -----------------------------------------------------------------------

using ExperienceToggleButton_UpdateButtonPadding_t = void(WINAPI*)(void* pThis);
ExperienceToggleButton_UpdateButtonPadding_t
    ExperienceToggleButton_UpdateButtonPadding_Original;
void WINAPI ExperienceToggleButton_UpdateButtonPadding_Hook(void* pThis) {
    Wh_Log(L"UpdateButtonPadding hook fired, pThis=%p", pThis);

    ExperienceToggleButton_UpdateButtonPadding_Original(pThis);

    if (g_unloading) {
        Wh_Log(L"Unloading, skipping");
        return;
    }

    FrameworkElement toggleButtonElement = nullptr;
    ((IUnknown**)pThis)[1]->QueryInterface(winrt::guid_of<FrameworkElement>(),
                                            winrt::put_abi(toggleButtonElement));
    if (!toggleButtonElement) {
        Wh_Log(L"QueryInterface for FrameworkElement failed");
        return;
    }

    auto className = winrt::get_class_name(toggleButtonElement);
    Wh_Log(L"Button class name: %s", className.c_str());
    if (className != L"Taskbar.ExperienceToggleButton") {
        return;
    }

    auto automationId =
        Automation::AutomationProperties::GetAutomationId(toggleButtonElement);
    Wh_Log(L"AutomationId: %s", automationId.c_str());
    if (automationId != L"StartButton") {
        return;
    }

    Wh_Log(L"Found StartButton element, name=%s", toggleButtonElement.Name().c_str());

    auto panelElement =
        FindChildByName(toggleButtonElement, L"ExperienceToggleButtonRootPanel");
    if (!panelElement) {
        Wh_Log(L"ExperienceToggleButtonRootPanel not found among direct children");
        int childCount =
            Media::VisualTreeHelper::GetChildrenCount(toggleButtonElement);
        for (int i = 0; i < childCount; i++) {
            auto child = Media::VisualTreeHelper::GetChild(toggleButtonElement, i)
                             .try_as<FrameworkElement>();
            if (child) {
                Wh_Log(L"  child %d: class=%s name=%s", i,
                       winrt::get_class_name(child).c_str(),
                       child.Name().c_str());
            }
        }
        return;
    }

    Wh_Log(L"Found panel, setting up tracking");
    SetupButtonTracking(toggleButtonElement, panelElement);
}

// -----------------------------------------------------------------------
// Module load / symbol hooking plumbing
// -----------------------------------------------------------------------

bool HookTaskbarViewDllSymbols(HMODULE module) {
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(protected: virtual void __cdecl winrt::Taskbar::implementation::ExperienceToggleButton::UpdateButtonPadding(void))"},
            &ExperienceToggleButton_UpdateButtonPadding_Original,
            ExperienceToggleButton_UpdateButtonPadding_Hook,
        },
    };

    return HookSymbols(module, symbolHooks, ARRAYSIZE(symbolHooks));
}

HMODULE GetTaskbarViewModuleHandle() {
    HMODULE module = GetModuleHandle(L"Taskbar.View.dll");
    if (!module) {
        module = GetModuleHandle(L"ExplorerExtensions.dll");
    }
    return module;
}

void HandleLoadedModuleIfTaskbarView(HMODULE module, LPCWSTR lpLibFileName) {
    if (!g_taskbarViewDllLoaded && GetTaskbarViewModuleHandle() == module &&
        !g_taskbarViewDllLoaded.exchange(true)) {
        Wh_Log(L"Loaded %s", lpLibFileName);
        if (HookTaskbarViewDllSymbols(module)) {
            Wh_ApplyHookOperations();
        }
    }
}

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR lpLibFileName,
                                    HANDLE hFile,
                                    DWORD dwFlags) {
    HMODULE module = LoadLibraryExW_Original(lpLibFileName, hFile, dwFlags);
    if (module) {
        HandleLoadedModuleIfTaskbarView(module, lpLibFileName);
    }
    return module;
}

// -----------------------------------------------------------------------
// Settings load / lifecycle
// -----------------------------------------------------------------------

IconMode ParseMode(PCWSTR value) {
    if (wcscmp(value, L"customIcon") == 0) {
        return IconMode::CustomIcon;
    }
    if (wcscmp(value, L"recolor") == 0) {
        return IconMode::Recolor;
    }
    return IconMode::Default;
}

std::wstring GetStringSetting(PCWSTR name) {
    PCWSTR value = Wh_GetStringSetting(name);
    std::wstring result = value ? value : L"";
    Wh_FreeStringSetting(value);
    return result;
}

void LoadStateSettings(StateSettings& state, PCWSTR groupName) {
    wchar_t key[64];

    swprintf_s(key, L"%s.bgColor", groupName);
    state.bgColor = GetStringSetting(key);

    swprintf_s(key, L"%s.iconPath", groupName);
    state.iconPath = GetStringSetting(key);

    swprintf_s(key, L"%s.padding", groupName);
    state.padding = Wh_GetIntSetting(key);

    swprintf_s(key, L"%s.margin", groupName);
    state.margin = Wh_GetIntSetting(key);

    swprintf_s(key, L"%s.cornerRadius", groupName);
    state.cornerRadius = Wh_GetIntSetting(key);
}

void LoadSettings() {
    auto modeStr = GetStringSetting(L"icon.mode");
    g_settings.mode = ParseMode(modeStr.c_str());
    g_settings.customIconPath = GetStringSetting(L"icon.customIconPath");
    g_settings.recolorColor = GetStringSetting(L"icon.recolorColor");
    g_settings.recolorShimmerColor =
        GetStringSetting(L"icon.recolorShimmerColor");
    g_settings.recolorGradient = Wh_GetIntSetting(L"icon.recolorGradient") != 0;

    LoadStateSettings(g_settings.states[kStateDefault], L"default");
    LoadStateSettings(g_settings.states[kStateHover], L"hover");
    LoadStateSettings(g_settings.states[kStatePressed], L"pressed");
}

BOOL Wh_ModInit() {
    Wh_Log(L"Initializing...");

    LoadSettings();

    if (HMODULE taskbarViewModule = GetTaskbarViewModuleHandle()) {
        g_taskbarViewDllLoaded = true;
        if (!HookTaskbarViewDllSymbols(taskbarViewModule)) {
            return FALSE;
        }
    } else {
        Wh_Log(L"Taskbar view module not loaded yet");
        HMODULE kernelBaseModule = GetModuleHandle(L"kernelbase.dll");
        auto pKernelBaseLoadLibraryExW =
            (decltype(&LoadLibraryExW))GetProcAddress(kernelBaseModule,
                                                        "LoadLibraryExW");
        WindhawkUtils::SetFunctionHook(pKernelBaseLoadLibraryExW,
                                        LoadLibraryExW_Hook,
                                        &LoadLibraryExW_Original);
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_taskbarViewDllLoaded) {
        if (HMODULE taskbarViewModule = GetTaskbarViewModuleHandle()) {
            if (!g_taskbarViewDllLoaded.exchange(true)) {
                Wh_Log(L"Got Taskbar.View.dll");
                if (HookTaskbarViewDllSymbols(taskbarViewModule)) {
                    Wh_ApplyHookOperations();
                }
            }
        }
    }
}

void Wh_ModBeforeUninit() {
    g_unloading = true;
    ReapplyAllTrackedButtons();  // restores native styling (see g_unloading branch)
}

void Wh_ModUninit() {
    g_trackedButtons.clear();
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    ReapplyAllTrackedButtons();
}
