// ==WindhawkMod==
// @id              taskbar-widget-stack
// @name            Taskbar Widget Stack
// @description     Stack multiple taskbar widgets vertically in one snap-scrollable pane, iOS-widget-stack style
// @version         0.3.0
// @author          AristideBH
// @github          https://github.com/AristideBH
// @homepage        https://aristide-bh.com/
// @license         MIT
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32 -lcomctl32 -ladvapi32 -ldwmapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Widget Stack

> **Note:** This mod is vibe-coded - built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

> **Prototype status:** this version stacks two **placeholder** panes,
> built against a real internal `IWidget` interface (Create/Tick/
> OnSettingsChanged/Destroy), to validate the injection/scroll/snap/
> indicator mechanics and the widget SDK contract itself. It does not yet
> host real widget content (media player, AI quota, ...) - see this mod's
> `PLAN.md` in the repo for the roadmap toward porting one.

Adds a single area next to the taskbar's system tray that holds multiple
widgets stacked vertically, one visible at a time, switchable like an iOS
widget stack:

- **Snap-scroll** between widgets via mouse wheel, vertical drag, or by
  clicking a dot indicator - each independently toggleable in settings.
- **Dot indicators** on the left edge show how many widgets are enabled and
  which one is active.
- **Right-click** the stack for a menu to jump straight to a widget,
  enable/disable widgets and move them up/down in the stack order, toggle
  the dot indicator, reset the stack's position, or open the full settings.

Only Windows 11, primary taskbar (single monitor) is targeted in this
prototype - not yet verified live, see `PLAN.md`.

## Requirements

- Windows 11, 64-bit
- Windhawk v1.4 or later
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- nav:
  - wheel: true
    $name: Mouse wheel navigation
    $description: Scroll the mouse wheel over the stack to switch widgets.
  - dots: true
    $name: Dot-click navigation
    $description: Click a dot indicator to jump directly to that widget.
  - drag: true
    $name: Drag navigation
    $description: >-
      Vertically drag the stack itself to scrub between widgets, snapping to
      the nearest one on release.
  - wrap: true
    $name: Wrap around
    $description: >-
      Stepping past the last widget goes back to the first (and vice versa).
      Turn off to stop at the ends instead.
  - overscroll: true
    $name: Overscroll bounce
    $description: >-
      When wrap-around is off, bounce slightly when trying to step past the
      first or last widget instead of doing nothing.
  $name: Navigation
  $description: Which ways of switching between stacked widgets are active.
- layout:
  - position: left_edge
    $name: Taskbar position
    $description: >-
      Where the widget stack sits in the taskbar. "Left edge" is this mod's
      original/default placement, flush against the taskbar's own left edge.
      The "Left/right of ..." options track that button live (icons moving
      as the taskbar reflows) rather than sitting at a fixed spot; they fall
      back to "Left edge" if the target button isn't found (hidden, or not
      present on this Windows build).
    $options:
    - left_edge: "Left edge"
    - center_edge: "Center"
    - right_edge: "Right edge (before the system tray)"
    - left_of_start: "Left of Start button"
    - right_of_start: "Right of Start button"
    - left_of_search: "Left of Search"
    - right_of_search: "Right of Search"
    - left_of_taskview: "Left of Task View"
    - right_of_taskview: "Right of Task View"
    - left_of_widgets: "Left of Widgets button"
    - right_of_widgets: "Right of Widgets button"
    - left_of_tray: "Left of the system tray"
    - right_of_tray: "Right of the system tray"
  - edgeGap: 6
    $name: Edge gap
    $description: >-
      Gap (px) between the anchor (the taskbar edge, or a tracked button)
      and the stack's dots.
  - rightPadding: 6
    $name: Trailing padding
    $description: >-
      Empty space (px) reserved past the stack's own content, so it isn't
      flush against whatever comes next - matches "Edge gap" by default so
      the stack has the same breathing room on both sides.
  - minWidth: 80
    $name: Minimum stack width
    $description: >-
      Lower bound, in pixels, for the widget stack's width - every widget
      stretches to at least this wide, even if none of them individually
      need it. Raise this if a narrow widget (or an empty stack) looks too
      cramped next to a wider one.
  - maxWidth: 520
    $name: Maximum stack width
    $description: >-
      Upper bound, in pixels, for how wide the widget stack can grow to fit
      its widest enabled widget's own minimum readable width. Widgets
      narrower than the final width stretch to fill it.
  - paneHeight: 56
    $name: Pane height
    $description: >-
      Height, in pixels, of a single widget's pane in the stack - every
      widget shares this one height. Increase it for a widget with more
      content (e.g. taskbar-widget-system-usage's CPU/RAM/GPU bars need
      more room than a plain label).
  - indicator:
    - visible: true
      $name: Show dot indicator
      $description: >-
        Show the dot indicator column at all. Turn off to reclaim the space
        if you never use it to navigate. Also toggleable from the stack's
        right-click menu.
    - hideWhenSingle: true
      $name: Hide when only one widget
      $description: >-
        Don't show the dot indicator column when there's only one enabled
        widget - it has nothing to indicate.
    - gap: 6
      $name: Gap to widgets (px)
      $description: Space between the dot indicator column and the widgets.
    - onRight: false
      $name: Indicator on the right
      $description: >-
        Show the dot indicator to the right of the widgets instead of the
        left (the default).
    $name: Indicator
    $description: >-
      Dot indicator appearance and behavior. More visual options
      (color/size/shape) are likely to land here later.
  $name: Layout
  $description: Sizing behavior for the widget stack.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <windows.h>
#include <unknwn.h>
#include <dwmapi.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Media;
namespace wuxi = winrt::Windows::UI::Xaml::Input;
namespace wuxs = winrt::Windows::UI::Xaml::Shapes;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;

namespace {

// Floor for the stack's content width (excluding the dots column) so it
// never collapses to ~0 if every widget is disabled/crashed. Also what
// the two placeholder widgets report as their own minimum width, so
// wiring up the dynamic-width machinery below doesn't change today's
// visuals at all - see PLAN.md's "Widget SDK design".
constexpr double kMinContentWidth = 30.0;
constexpr double kDotsColumnWidth = 10.0;
constexpr int kSnapAnimMs = 180;

// Passed to a widget's Create()/OnSettingsChanged() so it can attach its
// own root element and size itself to the pane height without reaching
// into this mod's globals directly.
struct WidgetHost {
    HWND taskbarHwnd;
    Panel parent;  // widgetsPanel - a widget appends its own root here.
    double paneHeight;
};

// Widget SDK contract (design in PLAN.md's "Widget SDK design"). A
// same-file, in-process interface - not a cross-mod/DLL ABI - every
// widget implementation (placeholder or, eventually, a ported real
// mod like AI quota/media player) compiles into this one .wh.cpp
// against. Modeled on the common pattern found in both
// taskbar-ai-quota.wh.cpp and taskbar-fluent-media-player.wh.cpp: a
// single owned root element rebuilt from scratch rather than patched
// in place, self-sizing (no host size negotiation beyond reporting a
// minimum readable width), and full responsibility for revoking its own event
// tokens/timers on Destroy().
class IWidget {
   public:
    virtual ~IWidget() = default;

    virtual std::wstring Id() const = 0;
    virtual std::wstring DisplayName() const = 0;

    // Builds this widget's own root element, attaches it under
    // host.parent, and returns this widget's own MINIMUM readable width
    // (DIPs) - the point below which its content would be truncated or
    // illegible, not a "desired" or preferred size. The host derives
    // the stack's actual shared width from the widest of every enabled
    // widget's reported minimum, clamped to the user's
    // layout.minWidth/layout.maxWidth settings - every widget then
    // stretches to fill that shared width via the default
    // HorizontalAlignment::Stretch, so it must not set its own fixed
    // Width() or override HorizontalAlignment to Left/Center/Right on
    // its own top-level returned element.
    virtual double Create(const WidgetHost& host) = 0;

    // Called on the stack's shared tick signal. No host-side timer
    // drives this yet - deferred until a real ported widget actually
    // needs periodic redraw, rather than running an idle DispatcherTimer
    // with no consumer. A widget needing its own cadence (e.g. a 16ms
    // visualizer, matching taskbar-fluent-media-player) owns that timer
    // internally instead, same as both reference mods already do.
    virtual void Tick() = 0;

    // Re-reads this widget's own settings sub-namespace and rebuilds
    // internally, returning its (possibly new) minimum readable width.
    // The host
    // calls Destroy() then Create() again rather than this directly,
    // matching both reference mods' full Remove-then-Inject-on-change
    // pattern - kept here as part of the contract for a widget that
    // wants to react without a full host-driven rebuild later.
    virtual double OnSettingsChanged() = 0;

    // Revokes every owned event token/timer, then removes its root
    // element from the parent it was given in Create(). Must be safe
    // to call even if Create() was never called or threw partway
    // through.
    virtual void Destroy() = 0;

    // Per-widget settings (Incident 31) - optional, default "none", so
    // existing/simple widgets (the placeholders) don't need to
    // implement anything. A widget that wants its own configuration
    // (which stats to show, refresh rate, color thresholds, etc. -
    // things that don't belong in the stack-wide Navigation/Layout
    // tabs) overrides both: HasSettings() to opt in, and
    // BuildSettingsPanel() to build that config UI on demand. The
    // settings window's Widgets tab shows a gear button next to any
    // widget entry where HasSettings() is true, and swaps the visible
    // content to BuildSettingsPanel()'s result when clicked - the
    // panel is responsible for its own persistence (its own private
    // registry values) and for calling back into the host
    // (RebuildStackContents()) if a change needs to take visible
    // effect immediately.
    virtual bool HasSettings() const { return false; }
    virtual FrameworkElement BuildSettingsPanel() { return nullptr; }
};

// ---------------------------------------------------------------------
// Cross-mod widget ABI (Incident 35 - see PLAN.md's "Cross-mod widget
// integration" design)
//
// A second, separate widget contract for widgets that live in a
// DIFFERENT Windhawk mod (a different DLL, injected independently into
// this same explorer.exe process) - taskbar-widget-system-usage is the
// first consumer. Deliberately NOT the same shape as IWidget above:
// IWidget is a C++ interface, and C++ ABI (vtable layout, name mangling,
// exception handling across a throw/catch boundary) isn't guaranteed
// stable between two separately compiled DLLs, even from the same
// compiler - so this is a plain `extern "C"` struct of function
// pointers instead, the same kind of contract COM/Win32 itself uses for
// cross-module calls. No shared header exists between separately-built
// Windhawk mods (each is a single self-contained .wh.cpp), so this
// struct is duplicated verbatim on the widget-providing mod's side
// (taskbar-widget-system-usage.wh.cpp) - the two copies must be kept in
// sync by hand; the "v1" in the type/export names is there so a future
// breaking change can add a "v2" pair without silently breaking widgets
// still using v1.
//
// XAML elements themselves never cross this boundary as C++/WinRT
// wrapper objects (same ABI-stability reasoning) - only as raw
// `IInspectable*` ABI pointers, re-wrapped locally on each side via
// `winrt::copy_from_abi`/`winrt::get_abi`, the same technique this file
// already uses in `GetTaskbarXamlRoot` to cross the boundary between
// this mod's own code and taskbar.dll's internal XAML tree. Since both
// mods run in the same process, a live COM/WinRT interface pointer
// obtained in one DLL is valid to use directly from the other - no
// marshaling needed, same as any other in-process COM scenario.
//
// Discovery: rather than have a widget-providing mod guess this mod's
// DLL filename (not something Windhawk guarantees stays constant), this
// mod publishes its registration function pointers as window
// properties (`SetPropW`) on the taskbar's own HWND once it's
// successfully injected - both mods already independently find that
// HWND via `FindWindowW(L"Shell_TrayWnd", ...)`, so it's a natural
// rendezvous point, the same idea as this file's own use of
// `GetPropW(hTaskbarWnd, L"TaskbandHWND")` to read taskbar.dll's own
// internal state.
// ---------------------------------------------------------------------

constexpr wchar_t kRegisterWidgetPropName[] =
    L"TaskbarWidgetStack_RegisterWidgetFn_v1";
constexpr wchar_t kUnregisterWidgetPropName[] =
    L"TaskbarWidgetStack_UnregisterWidgetFn_v1";

extern "C" {

// Passed to a remote widget's Create/OnSettingsChanged callback -
// the cross-DLL equivalent of WidgetHost above.
struct WidgetStackHostAbiV1 {
    void* taskbarHwnd;    // Actually an HWND - already a plain
                           // process-wide handle, no ABI-crossing issue.
    void* parentPanelAbi;  // IInspectable* for the host's Panel
                            // (winrt::Windows::UI::Xaml::Controls::Panel) -
                            // re-wrap via winrt::copy_from_abi, don't
                            // take ownership.
    double paneHeight;
};

// A remote widget's own callbacks, provided by the widget-owning mod.
// `context` is opaque to this mod - passed back unchanged to every
// call, typically null (a widget with only one instance, like
// taskbar-widget-system-usage's bars, can just use its own globals and
// ignore it).
//
// Fixed-size output buffers for the two name callbacks (rather than a
// returned pointer) sidestep string-ownership-across-a-DLL-boundary
// entirely - the caller (this mod) owns the buffer, so there's no
// question of who frees what or how long a returned pointer stays
// valid.
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

// Defined outside the anonymous namespace, next to Wh_ModInit et al. -
// forward-declared here so InjectWidgetStackGrid (below) can take their
// address for SetPropW. extern "C" linkage isn't affected by the
// surrounding C++ namespace, so this merges fine with the out-of-namespace
// definitions.
__declspec(dllexport) bool __cdecl WidgetStack_RegisterWidget(
    const WidgetStackWidgetAbiV1* widget);
__declspec(dllexport) void __cdecl WidgetStack_UnregisterWidget(void* context);

}  // extern "C"

// Adapts a registered WidgetStackWidgetAbiV1 to the in-process IWidget
// interface, so remote widgets flow through exactly the same
// RebuildStackContents/dots/nav machinery as local ones (PlaceholderWidget
// et al.) - the host doesn't need to know or care whether a given
// IWidget instance is local C++ or a cross-DLL callback underneath.
class RemoteWidget : public IWidget {
   public:
    explicit RemoteWidget(WidgetStackWidgetAbiV1 abi) : abi_(abi) {}

    std::wstring Id() const override {
        wchar_t buf[64] = {};
        if (abi_.GetId) {
            abi_.GetId(abi_.context, buf, ARRAYSIZE(buf));
        }
        return buf;
    }

    std::wstring DisplayName() const override {
        wchar_t buf[64] = {};
        if (abi_.GetDisplayName) {
            abi_.GetDisplayName(abi_.context, buf, ARRAYSIZE(buf));
        }
        return buf;
    }

    double Create(const WidgetHost& host) override {
        if (!abi_.Create) {
            return 0.0;
        }
        WidgetStackHostAbiV1 hostAbi{};
        hostAbi.taskbarHwnd = (void*)host.taskbarHwnd;
        hostAbi.parentPanelAbi = winrt::get_abi(host.parent);
        hostAbi.paneHeight = host.paneHeight;
        return abi_.Create(abi_.context, &hostAbi);
    }

    void Tick() override {
        if (abi_.Tick) {
            abi_.Tick(abi_.context);
        }
    }

    double OnSettingsChanged() override {
        return abi_.OnSettingsChanged ? abi_.OnSettingsChanged(abi_.context)
                                       : 0.0;
    }

    void Destroy() override {
        if (abi_.Destroy) {
            abi_.Destroy(abi_.context);
        }
    }

    // Used by WidgetStack_UnregisterWidget to find this entry in
    // g_widgets again by the same context pointer the widget-owning mod
    // registered with.
    void* Context() const { return abi_.context; }

   private:
    WidgetStackWidgetAbiV1 abi_;
};

struct WidgetEntry {
    std::unique_ptr<IWidget> widget;
    bool enabled = true;

    // Crash isolation: a widget whose Create()/Tick()/OnSettingsChanged()
    // throws gets latched here and is skipped (no pane, no dot) on this
    // and future rebuilds, per the "host catches and disables" decision
    // in PLAN.md. Every IWidget call from host code is wrapped in
    // try/catch for this reason.
    bool crashed = false;

    // Cached return value of the widget's last Create()/
    // OnSettingsChanged() call - its own minimum readable width, not a
    // "desired" size. The stack's final shared width is derived from
    // the widest of these across all enabled widgets, clamped to
    // layout.minWidth/maxWidth (see RebuildStackContents).
    double minWidth = 0.0;
};

std::vector<WidgetEntry> g_widgets;

struct {
    bool navWheel = true;
    bool navDots = true;
    bool navDrag = true;
    bool navWrap = true;
    bool navOverscroll = true;
    int layoutMinWidth = 80;
    int layoutMaxWidth = 520;
    // Default matches this mod's own history: 32 for plain-text
    // placeholders (Incident 34's revert), 76 while SystemUsageWidget
    // lived in-process (Incident 30) - 56 here is a middle ground picked
    // for a registered widget like taskbar-widget-system-usage's 3 bar
    // rows via the cross-mod ABI (Incident 35), not a value derived from
    // any specific widget's content the way those two were; the whole
    // point of Incident 39 is that this no longer needs to be guessed
    // once in code, the user can just set it.
    int layoutPaneHeight = 56;
    bool layoutIndicatorVisible = true;
    bool layoutHideIndicatorWhenSingle = true;
    int layoutIndicatorGap = 6;
    bool layoutIndicatorOnRight = false;
    // "left_edge" (default - RootGrid's own left edge, unchanged from
    // this mod's original/only behavior), "center_edge", or "right_edge"
    // - see InjectWidgetStackGrid's placement comment (Incident 37).
    std::wstring layoutPosition = L"left_edge";
    // Gap (px) from the anchor (the taskbar's edge, or a tracked
    // element) to the stack's own dots column - was a hardcoded
    // constexpr (kEdgeGap) until Incident 43. Also reused as the gap
    // between the stack and a tracked anchor element (UpdateTrackedPosition)
    // - conceptually the same "how far from the anchor" quantity either
    // way.
    int layoutEdgeGap = 6;
    // Extra empty space (px) reserved at the stack's own trailing edge,
    // past its content - Incident 43: symmetric to layoutEdgeGap, so the
    // whole stack has the same breathing room on both sides instead of
    // ending flush against its content on one side.
    int layoutRightPadding = 6;
} g_settings;

// ---------------------------------------------------------------------
// Private settings store (Incident 26)
//
// Windhawk mods can only READ settings (Wh_GetIntSetting/
// Wh_GetStringSetting) - confirmed by checking both reference mods in
// this repo's own source (taskbar-ai-quota.wh.cpp,
// taskbar-fluent-media-player.wh.cpp): neither writes a setting back,
// only reacts to Wh_ModSettingsChanged() when the user edits one
// through Windhawk's own settings UI. There's no supported way for
// this mod's own settings window (see PLAN.md's "Settings window"
// design) to persist a change into Windhawk's settings.json.
//
// Instead, everything the settings window can edit - nav/layout
// toggles and the widget list's order/enabled state - is persisted in
// this mod's own registry key, which becomes the actual source of
// truth once anything has been saved through it. Windhawk's own
// ==WindhawkModSettings== values are only the seed/default used until
// then; editing them through Windhawk's native settings UI after the
// private store has values will have no visible effect, since
// LoadSettings() below prefers the private store whenever a key
// exists there. This is a real, known inconsistency - flagged rather
// than hidden - accepted as the only way to have a real read/write
// settings surface given the read-only mod-settings API.
// ---------------------------------------------------------------------

constexpr wchar_t kPrivateSettingsKeyPath[] =
    L"Software\\WindhawkMods\\taskbar-widget-stack";

HKEY OpenPrivateSettingsKey(bool writable) {
    HKEY key = nullptr;
    if (writable) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kPrivateSettingsKeyPath, 0,
                             nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key,
                             nullptr) != ERROR_SUCCESS) {
            return nullptr;
        }
    } else if (RegOpenKeyExW(HKEY_CURRENT_USER, kPrivateSettingsKeyPath, 0,
                              KEY_READ, &key) != ERROR_SUCCESS) {
        return nullptr;
    }
    return key;
}

bool ReadPrivateDword(const wchar_t* name, DWORD& outValue) {
    HKEY key = OpenPrivateSettingsKey(/*writable=*/false);
    if (!key) {
        return false;
    }
    DWORD value = 0, size = sizeof(value), type = 0;
    LSTATUS status =
        RegQueryValueExW(key, name, nullptr, &type, (BYTE*)&value, &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_DWORD) {
        return false;
    }
    outValue = value;
    return true;
}

void WritePrivateDword(const wchar_t* name, DWORD value) {
    HKEY key = OpenPrivateSettingsKey(/*writable=*/true);
    if (!key) {
        return;
    }
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&value,
                   sizeof(value));
    RegCloseKey(key);
}

bool ReadPrivateString(const wchar_t* name, std::wstring& outValue) {
    HKEY key = OpenPrivateSettingsKey(/*writable=*/false);
    if (!key) {
        return false;
    }
    DWORD size = 0, type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) !=
            ERROR_SUCCESS ||
        type != REG_SZ || size == 0) {
        RegCloseKey(key);
        return false;
    }
    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &type,
                                       (BYTE*)buffer.data(), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    while (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    outValue = buffer;
    return true;
}

void WritePrivateString(const wchar_t* name, const std::wstring& value) {
    HKEY key = OpenPrivateSettingsKey(/*writable=*/true);
    if (!key) {
        return;
    }
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value.c_str(),
                   (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

// Saves the widget list's current order and enabled state as
// "id:0or1;id:0or1;..." - called after every toggle/move, from
// whichever UI triggered it (right-click menu or the settings
// window's Widgets tab).
void SaveWidgetOrderState() {
    std::wstring value;
    for (auto& entry : g_widgets) {
        if (!value.empty()) {
            value += L';';
        }
        value += entry.widget->Id();
        value += L':';
        value += entry.enabled ? L'1' : L'0';
    }
    WritePrivateString(L"widgets.order", value);
}

// Reorders/re-toggles g_widgets to match a previously saved
// "widgets.order" string, matching by id. Anything not mentioned
// (e.g. a widget added to InitPlaceholderWidgets since the last save)
// keeps its default position, appended after the restored ones - so
// an old save never silently drops a newer widget.
void LoadWidgetOrderState() {
    std::wstring raw;
    if (!ReadPrivateString(L"widgets.order", raw) || raw.empty()) {
        return;
    }
    std::vector<std::pair<std::wstring, bool>> parsed;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t sep = raw.find(L';', pos);
        std::wstring token = raw.substr(
            pos, sep == std::wstring::npos ? std::wstring::npos : sep - pos);
        size_t colon = token.find(L':');
        if (colon != std::wstring::npos) {
            parsed.emplace_back(token.substr(0, colon),
                                 token.substr(colon + 1) == L"1");
        }
        if (sep == std::wstring::npos) {
            break;
        }
        pos = sep + 1;
    }
    if (parsed.empty()) {
        return;
    }

    std::vector<WidgetEntry> reordered;
    for (auto& [id, enabled] : parsed) {
        auto it = std::find_if(
            g_widgets.begin(), g_widgets.end(),
            [&](WidgetEntry& e) { return e.widget->Id() == id; });
        if (it != g_widgets.end()) {
            it->enabled = enabled;
            reordered.push_back(std::move(*it));
            g_widgets.erase(it);
        }
    }
    for (auto& entry : g_widgets) {
        reordered.push_back(std::move(entry));
    }
    g_widgets = std::move(reordered);
}

// ---------------------------------------------------------------------
// Taskbar XAML Access
//
// Windows 11's taskbar is a XAML island, not a classic Win32 client area:
// a plain WS_CHILD/GDI overlay window can be created successfully next to
// it and simply never be visible, composited behind or unrelated to the
// actual taskbar surface (confirmed live - this mod's first version did
// exactly that). Getting real content to render *in* the taskbar means
// reaching into its live XAML visual tree and inserting real XAML
// elements as children of it.
//
// The functions in this section (RunFromWindowThread excepted, which is
// this mod's own) are ported near-verbatim, with attribution, from
// taskbar-ai-quota.wh.cpp (Cleroth, MIT-licensed, ramensoftware/
// windhawk-mods) - they walk internal, undocumented Taskbar.View.dll/
// taskbar.dll structures (symbol-hooked vtables and a machine-code
// pattern match to recover an internal field offset) to obtain the
// taskbar's XamlRoot and, from there, locate the "SystemTrayFrameGrid"
// node that hosts the tray's own icons. This is the same general
// technique - reaching into Explorer's live Composition/XAML tree via
// symbol hooks - used by this repo's other mod
// (windows-11-start-menu-button) for the Start button icon, and is the
// confirmed-working approach for this category of mod on Windows 11.
// Not independently re-derived here; ported because getting any detail
// of this wrong (a wrong symbol string, a wrong offset-scan byte
// pattern) fails safely (returns null / doesn't hook) rather than
// silently, per the guards already present in the source it's ported
// from.
// ---------------------------------------------------------------------

using CTaskBand_GetTaskbarHost_t = void*(WINAPI*)(void*, void*);
using TaskbarHost_FrameHeight_t = int(WINAPI*)(void*);
using Std_Ref_Decref_t = void(WINAPI*)(void*);

CTaskBand_GetTaskbarHost_t CTaskBand_GetTaskbarHost_Original;
TaskbarHost_FrameHeight_t TaskbarHost_FrameHeight_Original;
Std_Ref_Decref_t Std_Ref_Decref_Original;
void* CTaskBand_ITaskListWndSite_vftable;

using TrayUI_StartTaskbar_t = void(WINAPI*)(void*);
TrayUI_StartTaskbar_t TrayUI_StartTaskbar_Original;

HRESULT TryGetTaskbarElementAbi(HWND hTaskbarWnd, void** result) {
    *result = nullptr;
    void* taskbarHostSharedPtr[2]{};

    auto cleanup = [&]() {
        if (taskbarHostSharedPtr[1] && Std_Ref_Decref_Original) {
            Std_Ref_Decref_Original(taskbarHostSharedPtr[1]);
        }
    };

    HWND hTaskSwWnd = (HWND)GetPropW(hTaskbarWnd, L"TaskbandHWND");
    if (!hTaskSwWnd) {
        return E_HANDLE;
    }

    void* taskBand = (void*)GetWindowLongPtrW(hTaskSwWnd, 0);
    if (!taskBand) {
        return E_POINTER;
    }

    if (!CTaskBand_ITaskListWndSite_vftable || !CTaskBand_GetTaskbarHost_Original) {
        return E_NOINTERFACE;
    }

    void* taskBandForTaskListWndSite = taskBand;
    for (int i = 0; *(void**)taskBandForTaskListWndSite !=
                    CTaskBand_ITaskListWndSite_vftable;
         i++) {
        if (i == 20) {
            return E_NOINTERFACE;
        }
        taskBandForTaskListWndSite = (void**)taskBandForTaskListWndSite + 1;
        if (!taskBandForTaskListWndSite) {
            return E_POINTER;
        }
    }

    CTaskBand_GetTaskbarHost_Original(taskBandForTaskListWndSite,
                                       taskbarHostSharedPtr);
    if (!taskbarHostSharedPtr[0]) {
        cleanup();
        return E_POINTER;
    }

    // TaskbarHost::FrameHeight's prologue moves `this + offset` into
    // rcx/x0 to reach the taskbar element pointer; the offset itself
    // isn't a stable, documented constant, so it's recovered by matching
    // the compiled function's own machine code.
    //
    // This checks both the x64 and ARM64 prologue shapes at *runtime*,
    // unconditionally - not gated by `_M_X64`/`_M_ARM64` (this mod's own
    // compile-time target), because those two things aren't the same
    // thing on Windows 11 on Arm: confirmed live (2026-09-16) that even
    // though Windhawk's local/dev editor compiles this mod as x64
    // (`_M_X64` true) on an ARM64 machine, `TaskbarHost::FrameHeight`'s
    // actual instruction bytes are genuine ARM64 machine code - Explorer
    // there runs as an ARM64EC process, where x64-compiled code (this
    // mod) and native ARM64 system DLL code (taskbar.dll) coexist and
    // call each other through ABI-compatible thunks, so what architecture
    // *this mod* was compiled as says nothing about what architecture
    // the *target function's own code* actually is. An `#if
    // defined(_M_ARM64)` compile-time branch (this mod's first attempt
    // at ARM64 support) can therefore never fire in that scenario, no
    // matter the machine, since the mod itself is always built x64 here.
    size_t taskbarElementIUnknownOffset;
    {
        // x64: 48:83EC 28 | sub rsp,28 / 48:83C1 48 | add rcx,48
        const BYTE* b = (const BYTE*)TaskbarHost_FrameHeight_Original;
        // ARM64: 7f2303d5 pacibsp / fd7bbfa9 stp fp,lr,[sp,#-0x10]! /
        // fd030091 mov fp,sp / 080c41f8 ldr x8,[x0,#0x10]!
        const DWORD* p = (const DWORD*)TaskbarHost_FrameHeight_Original;

        if (b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC && b[4] == 0x48 &&
            b[5] == 0x83 && b[6] == 0xC1 && b[7] <= 0x7F) {
            taskbarElementIUnknownOffset = b[7];
        } else if (p[0] == 0xD503237F && (p[1] & 0xFFC07FFF) == 0xA9807BFD &&
                   p[2] == 0x910003FD &&
                   (p[3] & 0xFFF00FE0) == 0xF8400C00) {
            taskbarElementIUnknownOffset = (p[3] >> 12) & 0xFF;
        } else {
            // Diagnostic: this exact build's compiled prologue doesn't
            // match either expected pattern. Dump the first bytes so a
            // correct pattern/offset can be derived from real data
            // instead of guessed - see PLAN.md's "Incident 3".
            wchar_t hex[64] = {};
            for (int i = 0; i < 16; i++) {
                wchar_t byteStr[4];
                wsprintfW(byteStr, L"%02X ", b[i]);
                wcscat_s(hex, byteStr);
            }
            Wh_Log(L"Unsupported TaskbarHost::FrameHeight, bytes: %s", hex);
            cleanup();
            return E_NOINTERFACE;
        }
    }

    auto* taskbarElementIUnknown = *(IUnknown**)(
        (BYTE*)taskbarHostSharedPtr[0] + taskbarElementIUnknownOffset);
    if (!taskbarElementIUnknown) {
        cleanup();
        return E_POINTER;
    }

    HRESULT hr = taskbarElementIUnknown->QueryInterface(
        winrt::guid_of<winrt::Windows::Foundation::IInspectable>(), result);
    cleanup();
    return hr;
}

XamlRoot GetTaskbarXamlRoot(HWND hTaskbarWnd) {
    if (!CTaskBand_ITaskListWndSite_vftable || !CTaskBand_GetTaskbarHost_Original ||
        !TaskbarHost_FrameHeight_Original) {
        return nullptr;
    }

    void* taskbarElementAbi = nullptr;
    if (FAILED(TryGetTaskbarElementAbi(hTaskbarWnd, &taskbarElementAbi)) ||
        !taskbarElementAbi) {
        return nullptr;
    }

    FrameworkElement taskbarElement{nullptr};
    winrt::attach_abi(taskbarElement, taskbarElementAbi);
    return taskbarElement ? taskbarElement.XamlRoot() : nullptr;
}

FrameworkElement FindChildByName(FrameworkElement const& root,
                                  std::wstring_view name, int depth = 32) {
    if (!root || depth == 0) {
        return nullptr;
    }
    int n = VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < n; ++i) {
        auto child = VisualTreeHelper::GetChild(root, i).try_as<FrameworkElement>();
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

// The taskbar's root Grid (parent of both TaskbarFrameRepeater - pinned/
// running app icons and the Start button - and SystemTrayFrameGrid).
// Ported (with attribution) from taskbar-fluent-media-player.wh.cpp
// (Salyts) - that mod positions itself the same way this one now does
// (an element floating in RootGrid, anchored to the Start button by
// margin, not a column inserted into SystemTrayFrameGrid), which is
// exactly where the user already runs it, immediately left of the
// centered app icons - confirmed to be the right target after the first
// version landed in the wrong place (the system tray, on the right).
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

constexpr const wchar_t* kStartButtonNames[] = {
    L"StartButton",
    L"StartMenuButton",
    L"StartMenuLaunchButton",
    L"LaunchListButton",
};

// The Start button's name varies (depends on Windows build and other
// taskbar mods, e.g. a Start-button-replacing mod) - searches direct
// children of `repeater` first, then one level of grandchildren, per
// the same two-pass approach in taskbar-fluent-media-player.wh.cpp.
FrameworkElement FindStartButton(FrameworkElement const& repeater) {
    if (!repeater) {
        return nullptr;
    }
    int childCount = VisualTreeHelper::GetChildrenCount(repeater);
    for (int i = 0; i < childCount; i++) {
        auto child = VisualTreeHelper::GetChild(repeater, i).try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        for (auto name : kStartButtonNames) {
            if (child.Name() == name) {
                return child;
            }
        }
    }
    for (int i = 0; i < childCount; i++) {
        auto child = VisualTreeHelper::GetChild(repeater, i).try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        int subCount = VisualTreeHelper::GetChildrenCount(child);
        for (int k = 0; k < subCount; k++) {
            auto subChild = VisualTreeHelper::GetChild(child, k).try_as<FrameworkElement>();
            if (!subChild) {
                continue;
            }
            for (auto name : kStartButtonNames) {
                if (subChild.Name() == name) {
                    return subChild;
                }
            }
        }
    }
    return nullptr;
}

// Class-name-based lookups for the other taskbar anchor buttons
// (Incident 38's tracking positions) - unlike the Start button, these
// don't have a stable Name (or none was found), so they're found by
// their internal class instead. Ported (with attribution) from
// taskbar-fluent-media-player.wh.cpp (Salyts): FindElementByClassName/
// FindNthElementByClassName/FindChildByClassName.
FrameworkElement FindElementByClassName(FrameworkElement const& parent,
                                         const wchar_t* className) {
    if (!parent) {
        return nullptr;
    }
    int childCount = VisualTreeHelper::GetChildrenCount(parent);
    for (int i = 0; i < childCount; i++) {
        auto child =
            VisualTreeHelper::GetChild(parent, i).try_as<FrameworkElement>();
        if (child && winrt::get_class_name(child) == className) {
            return child;
        }
    }
    return nullptr;
}

// `index` is 0-based among matches, direct children only - e.g. the
// Task View toggle is the second (index 1) of two
// Taskbar.ExperienceToggleButton children in the repeater on builds
// that also show the Widgets toggle button there.
FrameworkElement FindNthElementByClassName(FrameworkElement const& parent,
                                            const wchar_t* className,
                                            int index) {
    if (!parent) {
        return nullptr;
    }
    int found = 0;
    int childCount = VisualTreeHelper::GetChildrenCount(parent);
    for (int i = 0; i < childCount; i++) {
        auto child =
            VisualTreeHelper::GetChild(parent, i).try_as<FrameworkElement>();
        if (child && winrt::get_class_name(child) == className) {
            if (found == index) {
                return child;
            }
            found++;
        }
    }
    return nullptr;
}

FrameworkElement FindChildByClassName(FrameworkElement const& parent,
                                       const wchar_t* className,
                                       int depth = 32) {
    if (!parent || depth <= 0) {
        return nullptr;
    }
    int childCount = VisualTreeHelper::GetChildrenCount(parent);
    for (int i = 0; i < childCount; i++) {
        auto child =
            VisualTreeHelper::GetChild(parent, i).try_as<FrameworkElement>();
        if (!child) {
            continue;
        }
        if (winrt::get_class_name(child) == className) {
            return child;
        }
        if (auto found = FindChildByClassName(child, className, depth - 1)) {
            return found;
        }
    }
    return nullptr;
}

// `SystemTrayFrameGrid` is NOT a descendant of RootGrid the way
// TaskbarFrameRepeater is (Incident 41's original bug: it was searched
// for as if it were) - confirmed against taskbar-fluent-media-player.wh.cpp's
// own two-step lookup, which finds "SystemTray.SystemTrayFrame" by class
// name from the taskbar's full XamlRoot content first, then
// "SystemTrayFrameGrid" by name inside THAT. `rootElement` here is that
// full content (what InjectWidgetStackGrid calls `rootElement`, one
// level above RootGrid/`taskbarRootGrid`), not RootGrid itself.
FrameworkElement FindSystemTrayFrameGrid(FrameworkElement const& rootElement) {
    auto trayFrame =
        FindChildByClassName(rootElement, L"SystemTray.SystemTrayFrame");
    if (!trayFrame) {
        return nullptr;
    }
    return FindChildByName(trayFrame, L"SystemTrayFrameGrid");
}

// Resolves a `layout.position` tracking value (e.g. "left_of_taskview")
// to the real taskbar element to anchor next to, and which side of it
// `side` should track. Returns null (with `side` untouched) for a
// non-tracking position, or if the target element isn't found in this
// taskbar's current layout (e.g. the search box hidden, no widgets
// button on this Windows build) - callers fall back to left_edge in
// that case rather than injecting anchored to nothing.
//
// `rootElement` (the taskbar's full XamlRoot content, one level above
// RootGrid) is only needed for the tray anchors - SystemTrayFrameGrid
// isn't a descendant of RootGrid at all (see FindSystemTrayFrameGrid's
// comment), so it can't be found by searching inside `repeater` (or
// even inside RootGrid) the way the taskbar-side anchors are.
FrameworkElement ResolveTrackingAnchor(FrameworkElement const& rootElement,
                                        FrameworkElement const& repeater,
                                        const std::wstring& position,
                                        std::wstring& side) {
    if (position == L"left_of_start" || position == L"right_of_start") {
        side = position == L"left_of_start" ? L"left" : L"right";
        return FindStartButton(repeater);
    }
    if (position == L"left_of_search" || position == L"right_of_search") {
        side = position == L"left_of_search" ? L"left" : L"right";
        return FindElementByClassName(repeater,
                                       L"Taskbar.TaskbarExtensionElement");
    }
    if (position == L"left_of_taskview" || position == L"right_of_taskview") {
        side = position == L"left_of_taskview" ? L"left" : L"right";
        return FindNthElementByClassName(
            repeater, L"Taskbar.ExperienceToggleButton", 1);
    }
    if (position == L"left_of_widgets" || position == L"right_of_widgets") {
        side = position == L"left_of_widgets" ? L"left" : L"right";
        auto el = FindChildByName(repeater, L"AugmentedEntryPointButton");
        if (!el) {
            el = FindChildByClassName(repeater,
                                       L"Taskbar.AugmentedEntryPointButton");
        }
        return el;
    }
    if (position == L"left_of_tray" || position == L"right_of_tray") {
        side = position == L"left_of_tray" ? L"left" : L"right";
        // Live-tracked, unlike right_edge's one-time-computed margin
        // (see kEdgeGap's comment) - this is what actually closes that
        // gap: the tray's own width changing later (icons appearing/
        // disappearing) moves the widget stack with it instead of
        // leaving a stale margin.
        return FindSystemTrayFrameGrid(rootElement);
    }
    return nullptr;
}

// Defined further down (it calls into the widget-stack functions declared
// later in this file); forward-declared so HookTaskbarDllSymbols can wire
// it up as the TrayUI::StartTaskbar hook target.
void WINAPI TrayUI_StartTaskbar_Hook(void* pThis);

bool HookTaskbarDllSymbols() {
    HMODULE h = LoadLibraryExW(L"taskbar.dll", nullptr,
                                LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        return false;
    }

    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {{LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"},
         &CTaskBand_ITaskListWndSite_vftable},
        {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"},
         &CTaskBand_GetTaskbarHost_Original},
        {{LR"(public: int __cdecl TaskbarHost::FrameHeight(void)const )"},
         &TaskbarHost_FrameHeight_Original},
        {{LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"},
         &Std_Ref_Decref_Original},
        {{LR"(public: virtual void __cdecl TrayUI::StartTaskbar(void))"},
         &TrayUI_StartTaskbar_Original, TrayUI_StartTaskbar_Hook},
    };
    return WindhawkUtils::HookSymbols(h, taskbarDllHooks,
                                       ARRAYSIZE(taskbarDllHooks));
}

// ---------------------------------------------------------------------
// Marshaling (this mod's own code)
// ---------------------------------------------------------------------

// Runs `task` synchronously on the thread that owns `hWnd`. See PLAN.md's
// "Incident" section: Wh_ModInit/hook callbacks aren't guaranteed to run
// on Explorer's own UI thread, and touching a XAML tree or creating a
// window from the wrong thread caused a real Explorer freeze in this
// mod's first version. Payloads are claimed by ID from a shared,
// mutex-guarded table (not read off the message directly) so two
// concurrent calls targeting the same thread can't double-claim each
// other's payload.
bool RunFromWindowThread(HWND hWnd, const std::function<void()>& task,
                          DWORD timeoutMs = 3000) {
    DWORD tid = GetWindowThreadProcessId(hWnd, nullptr);
    if (!tid) {
        return false;
    }
    if (tid == GetCurrentThreadId()) {
        task();
        return true;
    }

    struct Payload {
        const std::function<void()>* task;
        std::atomic<bool> ran{false};
    };
    static std::mutex pendingMutex;
    static std::vector<std::pair<UINT_PTR, Payload*>> pending;
    static std::atomic<UINT_PTR> nextId{1};

    Payload payload{&task};
    UINT_PTR id = nextId.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(pendingMutex);
        pending.push_back({id, &payload});
    }

    static const UINT kMsg = RegisterWindowMessageW(
        L"Windhawk_taskbar-widget-stack_RunFromWindowThread");
    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM w, LPARAM l) -> LRESULT {
            if (code == HC_ACTION) {
                auto* cwp = reinterpret_cast<const CWPSTRUCT*>(l);
                if (cwp->message == kMsg) {
                    Payload* claimed = nullptr;
                    {
                        std::lock_guard<std::mutex> lk(pendingMutex);
                        auto it = std::find_if(
                            pending.begin(), pending.end(),
                            [id = (UINT_PTR)cwp->wParam](const auto& e) {
                                return e.first == id;
                            });
                        if (it != pending.end()) {
                            claimed = it->second;
                            pending.erase(it);
                        }
                    }
                    if (claimed) {
                        (*claimed->task)();
                        claimed->ran.store(true, std::memory_order_release);
                    }
                }
            }
            return CallNextHookEx(nullptr, code, w, l);
        },
        nullptr, tid);
    if (!hook) {
        std::lock_guard<std::mutex> lk(pendingMutex);
        std::erase_if(pending, [id](const auto& e) { return e.first == id; });
        return false;
    }

    SendMessageTimeoutW(hWnd, kMsg, id, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                         timeoutMs, nullptr);
    UnhookWindowsHookEx(hook);

    {
        std::lock_guard<std::mutex> lk(pendingMutex);
        std::erase_if(pending, [id](const auto& e) { return e.first == id; });
    }
    return payload.ran.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------
// Widget stack UI (this mod's own code)
// ---------------------------------------------------------------------

struct UiState {
    HWND hWnd;
    DWORD ownerThreadId = 0;
    bool windowSubclassed = false;
    Grid injectionParent{nullptr};  // The taskbar's RootGrid (not the tray).
    Grid root{nullptr};
    // Tracking positions only (Incident 38) - the real taskbar element
    // `root` is anchored next to, live, via a LayoutUpdated handler on
    // injectionParent. Null/false for the static "edge" positions.
    FrameworkElement trackedElement{nullptr};
    Thickness trackedElementOriginalMargin{};
    bool hasTrackedElementOriginalMargin = false;
    std::wstring trackSide;  // "left" or "right" of trackedElement.
    winrt::event_token layoutUpdatedToken;
    bool layoutUpdatedWired = false;
    // Pointer-event subscription tokens on `root` - see WireUpNavigation's
    // comment for why these must be captured and explicitly revoked.
    winrt::event_token wheelToken;
    winrt::event_token pressedToken;
    winrt::event_token movedToken;
    winrt::event_token releasedToken;
    winrt::event_token rightTappedToken;
    StackPanel widgetsPanel{nullptr};
    StackPanel dotsPanel{nullptr};
    // Resized at runtime by ApplyStackWidth() as widgets are
    // enabled/disabled/ported - see PLAN.md's "Widget SDK design".
    Border clipHost{nullptr};
    RectangleGeometry clipGeom{nullptr};
    // root's three columns, always in this fixed left-to-right order in
    // Grid.ColumnDefinitions() (column0/column1/column2) - which one is
    // currently playing the "dots" vs. "content" role is decided live in
    // ApplyStackWidth() from layout.indicator.onRight, by writing the
    // Pixel/Star widths into whichever slot needs them and re-pointing
    // dotsPanel/clipHost's Grid::SetColumn to match (Incident 36) -
    // rather than physically moving elements or ColumnDefinition objects
    // between indices, which C++/WinRT's Grid API doesn't support
    // anyway. column0/column2 collapse to 0 together with gapColumn when
    // layout.indicator.hideWhenSingle is on and only one widget is
    // enabled (Incident 25 predates the 3-column split; same idea).
    ColumnDefinition column0{nullptr};
    ColumnDefinition gapColumn{nullptr};
    ColumnDefinition column2{nullptr};
    // Trailing 4th column, empty (no content placed in it) - pure
    // reserved space at root's own trailing edge (Incident 43,
    // layout.rightPadding), symmetric to layout.edgeGap's leading gap.
    // A dedicated column rather than just adding to root's own Width()
    // deliberately avoids growing column2 (content) itself: column2 is
    // Star-weighted, so any extra width added to root without a
    // dedicated column would get absorbed there instead, and clipHost's
    // own explicit Width (already fixed at contentWidth) would then
    // center within that now-wider column rather than staying flush
    // left - a real, easy-to-hit XAML gotcha (explicit Width + default
    // Stretch alignment centers within extra space), not a hypothetical
    // one.
    ColumnDefinition paddingColumn{nullptr};
    CompositeTransform sliderTransform{nullptr};
    int activeIndex = 0;
    // Which widget activeIndex is supposed to point at, by Id() rather
    // than raw position (Incident 40, PLAN.md) - g_widgets can be
    // reordered/erased/pushed from under activeIndex's numeric value
    // (a register/unregister round-trip, a manual move up/down) without
    // this file's own navigation code doing it, so RebuildStackContents
    // resolves activeIndex from this Id whenever possible instead of
    // trusting the stale raw index across such a mutation.
    std::wstring activeWidgetId;
    winrt::event_token renderingToken;
    bool animating = false;
    ULONGLONG animStartTick = 0;
    double animFromY = 0;
    double animToY = 0;
    // Overscroll bounce (Incident 22, nav.overscroll): when set,
    // OnRenderingTick starts a second short animation back to
    // bounceBaseY once the current one (the "out" leg, to an overshoot
    // position) completes, instead of just stopping - see
    // BounceAtBoundary.
    bool bouncePending = false;
    double bounceBaseY = 0;
    bool dragging = false;
    double dragStartY = 0;
    winrt::Windows::UI::Xaml::Input::Pointer dragPointer{nullptr};
    // Cached screen-coordinate bounds of `root`, refreshed by
    // UpdateStackScreenRect() from RebuildStackContents (a safe,
    // known-context call site) - see that function's comment for why
    // this exists (Incident 21).
    RECT stackScreenRect{};
};

UiState g_ui;
HWND g_taskbarWnd;
HANDLE g_retryThread;
HANDLE g_injectEvent;
bool g_rawInputRegistered;

// HID Digitizer touchpad-scroll state (Incident 17). Only contact 1's
// status/X/Y fields (bRawData[1], [2:4], [4:6]) are trusted - the
// multi-contact slot layout for a genuine second finger did not validate
// cleanly against real two-contact sample data, so it's deliberately not
// parsed. Tracking contact 1's Y alone is still enough to detect the
// vertical motion of a two-finger scroll gesture.
bool g_hidContactActive;
LONG g_hidPrevY;
double g_hidAccumY;
ULONGLONG g_hidLastStepTick;

// Incident 19: the step threshold used for drag/ManipulationDelta is
// PaneHeight() (an on-screen DIP measurement of the rendered widget
// pane), but raw HID Y is in the touchpad sensor's own logical unit
// resolution (typically ~0-3052 for a Precision Touchpad) - an
// entirely different, much finer-grained unit space. Reusing
// PaneHeight() as the HID threshold meant a single deliberate swipe's
// raw delta cleared it many times over, firing several steps per
// gesture (the "janky"/skippy behavior reported after Incident 18's
// hover/two-finger gating fix). This is a rough empirical guess, not
// derived from a device descriptor (parsing that was already ruled out
// in Incident 17 as too risky to do blind) - expect this to need
// live-tuning like every other HID constant so far.
constexpr double kHidStepThresholdUnits = 380.0;
constexpr ULONGLONG kHidStepDebounceMs = 220;
std::mutex g_retryThreadMutex;
std::atomic<bool> g_stopRequested{false};

std::vector<int> EnabledIndices() {
    std::vector<int> result;
    for (int i = 0; i < (int)g_widgets.size(); i++) {
        if (g_widgets[i].enabled && !g_widgets[i].crashed) {
            result.push_back(i);
        }
    }
    return result;
}

// User-adjustable (Incident 39, `layout.paneHeight`) rather than the
// fixed 32/76 values earlier Incidents hardcoded and reverted between -
// how tall a single pane needs to be genuinely depends on which widgets
// are enabled (a placeholder's one line of text vs. a registered
// cross-mod widget like taskbar-widget-system-usage's three bar rows),
// and now that widgets can be registered from an entirely different mod
// (Incident 35), this file can't know that in advance the way it could
// when every widget was a local, known-in-advance class. Every pane
// shares this one height (the slider offset math is
// `-widgetIndex * PaneHeight()`).
double PaneHeight() {
    return (double)g_settings.layoutPaneHeight;
}

void StopSnapAnimation() {
    if (g_ui.animating && g_ui.renderingToken) {
        CompositionTarget::Rendering(g_ui.renderingToken);
        g_ui.renderingToken = {};
    }
    g_ui.animating = false;
}

void ApplySliderTarget(int widgetIndex, bool animate);

void OnRenderingTick(winrt::Windows::Foundation::IInspectable const&,
                      winrt::Windows::Foundation::IInspectable const&) {
    if (!g_ui.animating || !g_ui.sliderTransform) {
        StopSnapAnimation();
        return;
    }
    ULONGLONG elapsed = GetTickCount64() - g_ui.animStartTick;
    double t = std::min(1.0, (double)elapsed / kSnapAnimMs);
    double eased = 1.0 - (1.0 - t) * (1.0 - t);  // ease-out
    double y = g_ui.animFromY + (g_ui.animToY - g_ui.animFromY) * eased;
    try {
        g_ui.sliderTransform.TranslateY(y);
    } catch (...) {
        StopSnapAnimation();
        return;
    }
    if (t >= 1.0) {
        if (g_ui.bouncePending) {
            // "Out" leg of an overscroll bounce just finished - start
            // the "back" leg to bounceBaseY, reusing the same
            // animating/renderingToken state rather than a second
            // CompositionTarget::Rendering subscription.
            g_ui.bouncePending = false;
            g_ui.animFromY = y;
            g_ui.animToY = g_ui.bounceBaseY;
            g_ui.animStartTick = GetTickCount64();
            return;
        }
        StopSnapAnimation();
    }
}

void ApplySliderTarget(int widgetIndex, bool animate) {
    if (!g_ui.sliderTransform) {
        return;
    }
    double y = -widgetIndex * PaneHeight();
    g_ui.bouncePending = false;
    if (!animate) {
        StopSnapAnimation();
        try {
            g_ui.sliderTransform.TranslateY(y);
        } catch (...) {
        }
        return;
    }
    g_ui.animFromY = g_ui.sliderTransform.TranslateY();
    g_ui.animToY = y;
    g_ui.animStartTick = GetTickCount64();
    if (!g_ui.animating) {
        g_ui.animating = true;
        g_ui.renderingToken = CompositionTarget::Rendering(OnRenderingTick);
    }
}

// nav.overscroll: gives a small bump-and-settle instead of doing
// nothing when nav.wrap is off and StepWidget is asked to go past the
// first/last widget - two short ease-out legs sharing OnRenderingTick's
// existing animation state (see its bouncePending handling above)
// rather than a separate animation mechanism.
constexpr double kOverscrollPixels = 8.0;

void BounceAtBoundary(int direction) {
    if (!g_ui.sliderTransform) {
        return;
    }
    double base = -g_ui.activeIndex * PaneHeight();
    g_ui.bounceBaseY = base;
    g_ui.bouncePending = true;
    g_ui.animFromY = g_ui.sliderTransform.TranslateY();
    g_ui.animToY = base - direction * kOverscrollPixels;
    g_ui.animStartTick = GetTickCount64();
    if (!g_ui.animating) {
        g_ui.animating = true;
        g_ui.renderingToken = CompositionTarget::Rendering(OnRenderingTick);
    }
}

void RefreshDots();

void GoToWidget(int widgetIndex, bool animate = true) {
    if (widgetIndex < 0 || widgetIndex >= (int)g_widgets.size() ||
        widgetIndex == g_ui.activeIndex) {
        return;
    }
    g_ui.activeIndex = widgetIndex;
    try {
        g_ui.activeWidgetId = g_widgets[widgetIndex].widget->Id();
    } catch (...) {
    }
    ApplySliderTarget(widgetIndex, animate);
    RefreshDots();
}

void StepWidget(int direction) {
    auto enabled = EnabledIndices();
    if (enabled.size() < 2) {
        return;
    }
    auto it = std::find(enabled.begin(), enabled.end(), g_ui.activeIndex);
    int pos = it != enabled.end() ? (int)std::distance(enabled.begin(), it) : 0;
    int rawNext = pos + direction;
    if (rawNext < 0 || rawNext >= (int)enabled.size()) {
        // nav.wrap (Incident 22): stepping past either end used to
        // always wrap around unconditionally - now optional.
        if (g_settings.navWrap) {
            int wrapped = (rawNext + (int)enabled.size()) % (int)enabled.size();
            GoToWidget(enabled[wrapped]);
        } else if (g_settings.navOverscroll) {
            BounceAtBoundary(direction);
        }
        return;
    }
    GoToWidget(enabled[rawNext]);
}

void ShowContextMenu(HWND hWnd, POINT screenPt);
void RebuildStackContents();
bool InjectWidgetStackGrid(HWND hWnd);
void RemoveWidgetStackGrid();
void UnwireTracking(bool restoreMargin);
void UpdateTrackedPosition();
void ResetStackPosition();

// Registers this mod's pointer-event handlers on `g_ui.root` and stores
// each subscription's `event_token` in `UiState` so `UnwireNavigation`
// can explicitly revoke them later. This matters more here than for a
// typical XAML app: `g_ui.root` lives in Explorer's own long-lived
// visual tree, not a window this mod owns and fully controls the
// lifetime of. Registering with `.EventName(handler)` and never revoking
// leaves the event source holding a strong reference to a delegate whose
// invoke thunk lives inside this mod's DLL - if Windhawk unloads that
// DLL (every recompile does this) while the element (or anything else)
// still holds that reference, any later invocation jumps into now-freed
// memory. Confirmed live (2026-09-16): Explorer crashed with exception
// 0xC0000005 (access violation), faulting module "unknown", fault offset
// 0x0 - the textbook signature of a call through a dangling function
// pointer into unmapped memory, not a in-module bug. Matches the same
// general crash class already documented in this repo's other mod
// (windows-11-start-menu-button/PLAN.md's "Crash containment" section),
// this mod's own `CompositionTarget::Rendering` handling already
// accounted for it (see `StopSnapAnimation`) - these five pointer-event
// subscriptions didn't.
void WireUpNavigation() {
    if (!g_ui.root) {
        return;
    }

    g_ui.wheelToken = g_ui.root.PointerWheelChanged(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           wuxi::PointerRoutedEventArgs const& args) {
            if (!g_settings.navWheel) {
                return;
            }
            try {
                auto elem = sender.as<UIElement>();
                int delta =
                    args.GetCurrentPoint(elem).Properties().MouseWheelDelta();
                StepWidget(delta > 0 ? -1 : 1);
                args.Handled(true);
            } catch (...) {
            }
        });

    g_ui.pressedToken = g_ui.root.PointerPressed(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           wuxi::PointerRoutedEventArgs const& args) {
            if (!g_settings.navDrag) {
                return;
            }
            auto elem = sender.as<UIElement>();
            g_ui.dragging = true;
            g_ui.dragPointer = args.Pointer();
            g_ui.dragStartY = args.GetCurrentPoint(elem).Position().Y;
            elem.CapturePointer(args.Pointer());
        });

    g_ui.movedToken = g_ui.root.PointerMoved(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           wuxi::PointerRoutedEventArgs const& args) {
            if (!g_ui.dragging || !g_settings.navDrag) {
                return;
            }
            auto elem = sender.as<UIElement>();
            double y = args.GetCurrentPoint(elem).Position().Y;
            double dy = y - g_ui.dragStartY;
            double height = PaneHeight();
            // Threshold raised from half a pane to a full pane, and
            // direction flipped (2026-09-16): confirmed live as "very
            // sensitive" and "inverted" - dragging up now steps to the
            // previous widget (matches dragging the *content* down to
            // reveal what's above, the usual touch-scroll feel) and
            // needs a full pane's worth of movement per step instead of
            // half.
            if (std::abs(dy) > height) {
                StepWidget(dy < 0 ? -1 : 1);
                g_ui.dragStartY = y;
            }
        });

    g_ui.releasedToken = g_ui.root.PointerReleased(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           wuxi::PointerRoutedEventArgs const& args) {
            if (!g_ui.dragging) {
                return;
            }
            g_ui.dragging = false;
            sender.as<UIElement>().ReleasePointerCapture(args.Pointer());
        });

    g_ui.rightTappedToken = g_ui.root.RightTapped(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           wuxi::RightTappedRoutedEventArgs const& args) {
            POINT pt;
            GetCursorPos(&pt);
            HWND hWnd = g_ui.hWnd;
            // Still deferred via the dispatcher, though ShowContextMenu
            // no longer pumps a nested Win32 message loop (Incident 23
            // replaced TrackPopupMenu with a XAML MenuFlyout) - harmless
            // to keep, and there's no reason left to call it
            // synchronously from inside this routed-event handler.
            try {
                auto dispatcher = sender.as<UIElement>().Dispatcher();
                if (dispatcher) {
                    dispatcher.RunAsync(
                        winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                        [hWnd, pt] { ShowContextMenu(hWnd, pt); });
                } else {
                    ShowContextMenu(hWnd, pt);
                }
            } catch (...) {
            }
            args.Handled(true);
        });

    // Incident 14's two-finger trackpad scroll attempt via
    // `ManipulationDelta` used to live here - confirmed dead for
    // touchpad input (never fired, which is why Incidents 17-22 built
    // the raw-HID mechanism that's now the sole working touchpad path)
    // but, per Incident 29, still very much alive for MOUSE drag,
    // where it fired *alongside* the manual PointerPressed/Moved drag
    // handling above off the same physical gesture - two independent
    // paths calling StepWidget with different thresholds/timing,
    // competing and producing the jitter reported when nav.drag was
    // on (nav.drag off left only this stray path active, which read
    // as "works perfectly" since the manual handler wasn't there to
    // conflict with it). Removed entirely, same as the WH_MOUSE_LL
    // hook's removal once HID replaced it as a touchpad mechanism -
    // it was dead weight for its original purpose and actively harmful
    // for mouse drag.
}

// Revokes everything `WireUpNavigation` registered. Must run before
// `g_ui.root` is discarded/reset - see `WireUpNavigation`'s comment.
void UnwireNavigation() {
    if (!g_ui.root) {
        return;
    }
    try {
        if (g_ui.wheelToken) {
            g_ui.root.PointerWheelChanged(g_ui.wheelToken);
        }
        if (g_ui.pressedToken) {
            g_ui.root.PointerPressed(g_ui.pressedToken);
        }
        if (g_ui.movedToken) {
            g_ui.root.PointerMoved(g_ui.movedToken);
        }
        if (g_ui.releasedToken) {
            g_ui.root.PointerReleased(g_ui.releasedToken);
        }
        if (g_ui.rightTappedToken) {
            g_ui.root.RightTapped(g_ui.rightTappedToken);
        }
    } catch (...) {
    }
}

// Prototype IWidget implementation - a solid-color pane with a centered
// label. Exists to prove the interface end to end before porting a real
// widget onto it (see PLAN.md's "Widget SDK design"): it exercises
// Create()'s host-attach-and-report-width contract and Destroy()'s
// self-removal contract, even though it has no event tokens/timers of
// its own to revoke.
class PlaceholderWidget : public IWidget {
   public:
    PlaceholderWidget(std::wstring id,
                       std::wstring displayName,
                       winrt::Windows::UI::Color color,
                       double minWidth)
        : id_(std::move(id)),
          displayName_(std::move(displayName)),
          color_(color),
          minWidth_(minWidth) {}

    std::wstring Id() const override { return id_; }
    std::wstring DisplayName() const override { return displayName_; }

    double Create(const WidgetHost& host) override {
        Border border;
        border.Height(host.paneHeight);
        SolidColorBrush brush{color_};
        border.Background(brush);

        TextBlock text;
        text.Text(winrt::hstring(displayName_));
        text.HorizontalAlignment(HorizontalAlignment::Center);
        text.VerticalAlignment(VerticalAlignment::Center);
        text.TextAlignment(TextAlignment::Center);
        text.FontSize(9);
        SolidColorBrush fg{
            winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255)};
        text.Foreground(fg);
        border.Child(text);

        // Intentionally no border.Width(): it stretches to fill
        // whatever content width the host settles on (see IWidget's
        // Create() comment).
        host.parent.Children().Append(border);
        root_ = border;
        parent_ = host.parent;
        return minWidth_;
    }

    void Tick() override {}

    double OnSettingsChanged() override {
        // No per-widget settings exist for placeholders - nothing to
        // re-read, size stays the same.
        return minWidth_;
    }

    void Destroy() override {
        if (root_ && parent_) {
            try {
                uint32_t index;
                if (parent_.Children().IndexOf(root_, index)) {
                    parent_.Children().RemoveAt(index);
                }
            } catch (...) {
            }
        }
        root_ = nullptr;
        parent_ = nullptr;
    }

   private:
    std::wstring id_;
    std::wstring displayName_;
    winrt::Windows::UI::Color color_;
    double minWidth_;
    Border root_{nullptr};
    Panel parent_{nullptr};
};

// Incident 46 (PLAN.md): with more than a handful of widgets registered
// (e.g. taskbar-widget-stack plus two remote widgets plus placeholders),
// one dot per widget in this fixed-width kDotsColumnWidth column just
// got squished together illegibly - a StackPanel doesn't wrap or
// scroll. Caps the number of dots actually drawn at kMaxVisibleDots,
// sliding a window of that size to keep the active widget's dot inside
// it (centered where the widget list is long enough to allow it,
// clamped at either end) - iOS-style paginated dots, not a hard limit
// on how many widgets can be stacked.
constexpr size_t kMaxVisibleDots = 3;

void RefreshDots() {
    if (!g_ui.dotsPanel) {
        return;
    }
    g_ui.dotsPanel.Children().Clear();
    if (!g_settings.layoutIndicatorVisible) {
        return;
    }
    auto enabled = EnabledIndices();
    if (g_settings.layoutHideIndicatorWhenSingle && enabled.size() <= 1) {
        // The dots column itself is collapsed to 0 width by
        // ApplyStackWidth in this case - nothing to build.
        return;
    }

    size_t total = enabled.size();
    size_t windowStart = 0;
    if (total > kMaxVisibleDots) {
        int activePos = 0;
        for (size_t i = 0; i < total; i++) {
            if (enabled[i] == g_ui.activeIndex) {
                activePos = (int)i;
                break;
            }
        }
        int start = activePos - (int)(kMaxVisibleDots / 2);
        int maxStart = (int)total - (int)kMaxVisibleDots;
        start = std::max(0, std::min(start, maxStart));
        windowStart = (size_t)start;
    }
    size_t windowEnd = std::min(total, windowStart + kMaxVisibleDots);
    bool moreBefore = windowStart > 0;
    bool moreAfter = windowEnd < total;

    for (size_t i = windowStart; i < windowEnd; i++) {
        int idx = enabled[i];
        bool active = idx == g_ui.activeIndex;
        // Same size regardless of active state (2026-09-17) - only the
        // color (white vs. gray) marks the active dot for now. Sizing
        // the active dot bigger too was reverted per user request;
        // more elaborate indicator styling (color/size/shape options)
        // is likely to become its own settings group later rather than
        // hardcoded here - see PLAN.md.
        //
        // Exception (Incident 46): the dot at either end of the visible
        // window is drawn smaller when there are more widgets beyond it
        // in that direction - a lightweight "more this way" cue, same
        // idea as edge dots shrinking in a carousel/page indicator.
        bool edgeCue = (i == windowStart && moreBefore) ||
                       (i == windowEnd - 1 && moreAfter);
        double r = edgeCue ? 1.0 : 2.0;

        // Small dot with no enlarged hit target (2026-09-16): the
        // earlier "unclickable dots" symptom turned out to be the
        // broken WindhawkModSettings closing marker (see "Incident
        // 12"), not the dot's own hit-test size - now that clicks are
        // confirmed working, an oversized transparent hit box isn't
        // needed and was just making the indicators look bulkier than
        // intended.
        wuxs::Ellipse dot;
        dot.Width(r * 2);
        dot.Height(r * 2);
        dot.Margin({0, 2, 0, 2});
        SolidColorBrush brush{winrt::Windows::UI::ColorHelper::FromArgb(
            255, active ? 255 : 140, active ? 255 : 140, active ? 255 : 140)};
        dot.Fill(brush);
        dot.Tapped([idx](winrt::Windows::Foundation::IInspectable const&,
                          wuxi::TappedRoutedEventArgs const&) {
            if (g_settings.navDots) {
                GoToWidget(idx);
            }
        });
        g_ui.dotsPanel.Children().Append(dot);
    }
}

// Resizes the stack's content column (root/clipHost/clipGeom) to
// `contentWidth` DIPs, plus the dots column itself (0 when
// layout.indicator.hideWhenSingle applies - Incident 25) - called from
// RebuildStackContents whenever the set of enabled/crashed widgets or
// their reported minimum widths might have changed. See PLAN.md's
// "Widget SDK design" for why the content-width part exists (real
// ported widgets are wider than the two placeholders' original fixed
// 30px pane).
void ApplyStackWidth(double contentWidth) {
    if (!g_ui.root || !g_ui.clipHost || !g_ui.clipGeom || !g_ui.column0 ||
        !g_ui.gapColumn || !g_ui.column2 || !g_ui.paddingColumn ||
        !g_ui.dotsPanel) {
        return;
    }
    try {
        bool hideIndicator = !g_settings.layoutIndicatorVisible ||
                              (g_settings.layoutHideIndicatorWhenSingle &&
                               EnabledIndices().size() <= 1);
        double dotsWidth = hideIndicator ? 0.0 : kDotsColumnWidth;
        double gapWidth =
            hideIndicator ? 0.0 : (double)g_settings.layoutIndicatorGap;
        double rightPadding = (double)g_settings.layoutRightPadding;

        // Which physical column (0 or 2) currently plays the "dots" role
        // vs. the "content" role - see UiState::column0's comment
        // (Incident 36). Re-decided on every call so toggling
        // layout.indicator.onRight moves the indicator immediately, with
        // no re-injection needed.
        bool onRight = g_settings.layoutIndicatorOnRight;
        GridLength dotsLength{dotsWidth, GridUnitType::Pixel};
        GridLength starLength{1.0, GridUnitType::Star};
        g_ui.column0.Width(onRight ? starLength : dotsLength);
        g_ui.column2.Width(onRight ? dotsLength : starLength);
        g_ui.gapColumn.Width({gapWidth, GridUnitType::Pixel});
        g_ui.paddingColumn.Width({rightPadding, GridUnitType::Pixel});
        Grid::SetColumn(g_ui.dotsPanel, onRight ? 2 : 0);
        Grid::SetColumn(g_ui.clipHost, onRight ? 0 : 2);

        g_ui.root.Width(contentWidth + dotsWidth + gapWidth + rightPadding);
        g_ui.clipHost.Width(contentWidth);
        // Height follows PaneHeight() live too (Incident 39) - unlike
        // width, this isn't re-derived from widgets' own reported sizes,
        // it's a direct read of the layout.paneHeight setting, but it
        // belongs here rather than a separate function since clipHost/
        // clipGeom are exactly the two elements this function already
        // owns updating on every settings-driven rebuild.
        g_ui.clipHost.Height(PaneHeight());
        auto rect = g_ui.clipGeom.Rect();
        rect.Width = (float)contentWidth;
        rect.Height = (float)PaneHeight();
        g_ui.clipGeom.Rect(rect);
    } catch (...) {
    }
}

// Recomputes `g_ui.stackScreenRect` from `root`'s live XAML layout -
// this is the ONLY place `TransformToVisual` is called (Incident 21).
// It used to be called directly from the WM_INPUT handler
// (IsCursorOverWidgetStack, Incident 18) to hit-test the cursor against
// the stack for gating touchpad-scroll - that meant a live XAML/
// composition call running from inside raw input delivery, potentially
// reentrant with a native modal loop (TrackPopupMenu) or otherwise an
// unusual point in the UI thread's state; Explorer kept crashing
// (0xC0000005, faulting module "unknown", offset 0x0 - consistent with
// a fault inside that internal call, not a C++ exception our try/catch
// could have caught at all, since MSVC's default /EHsc doesn't
// translate structured/access-violation exceptions into catchable C++
// ones). Guarding around it (Incident 20) didn't fix it because the
// dangerous call itself was still reachable outside that guard's
// window. Removing the call from the input path entirely - caching the
// result instead, refreshed only from RebuildStackContents, a call
// site on the taskbar's own UI thread with none of those hazards - is
// the actual fix: IsCursorOverWidgetStack below no longer touches XAML
// at all.
void UpdateStackScreenRect() {
    if (!g_ui.root || !g_ui.hWnd) {
        g_ui.stackScreenRect = {};
        return;
    }
    try {
        // Forces measure+arrange to run synchronously before reading
        // ActualWidth/ActualHeight/TransformToVisual (Incident 22): this
        // is called right after RebuildStackContents mutates the tree
        // (resizes root/clipHost, adds/removes widget panes), and
        // without this, a fresh XAML layout pass may not have run yet -
        // ActualWidth/ActualHeight would then still reflect a stale (or
        // zero, on first injection) prior size, giving a degenerate
        // cached rect that IsCursorOverWidgetStack could never match,
        // matching the "touchpad scroll stopped working" report after
        // this caching was introduced.
        g_ui.root.UpdateLayout();
        UINT dpi = GetDpiForWindow(g_ui.hWnd);
        double scale = dpi > 0 ? dpi / 96.0 : 1.0;
        auto transform = g_ui.root.TransformToVisual(nullptr);
        auto topLeft = transform.TransformPoint({0, 0});
        auto bottomRight = transform.TransformPoint(
            {(float)g_ui.root.ActualWidth(), (float)g_ui.root.ActualHeight()});
        POINT tl{(LONG)std::lround(topLeft.X * scale),
                 (LONG)std::lround(topLeft.Y * scale)};
        POINT br{(LONG)std::lround(bottomRight.X * scale),
                 (LONG)std::lround(bottomRight.Y * scale)};
        ClientToScreen(g_ui.hWnd, &tl);
        ClientToScreen(g_ui.hWnd, &br);
        g_ui.stackScreenRect = {tl.x, tl.y, br.x, br.y};
    } catch (...) {
        g_ui.stackScreenRect = {};
    }
}

// (Re)builds the widget panes and dots from the current g_widgets list.
// Every widget is torn down (Destroy()) and rebuilt (Create()) on every
// call - toggle/reorder/settings-change all funnel through here - rather
// than patching the existing tree in place, matching both reference
// mods' own Remove-then-Inject-on-change pattern; this is also what lets
// a widget revoke its own event tokens/timers via Destroy() before
// Create() runs again instead of leaking them across a rebuild. Crash
// isolation: each widget's Create() call is wrapped in its own try/catch
// - a widget that throws is flagged crashed and gets neither a pane nor
// a dot on this and future rebuilds (matches the "host catches and
// disables" decision in PLAN.md).
void RebuildStackContents() {
    if (!g_ui.widgetsPanel) {
        return;
    }
    StopSnapAnimation();

    // Shrink the measurement surface to the floor before any widget's
    // Create()/OnSettingsChanged() runs, so a Stretch-aligned top-level
    // element (every widget's registered-mode wrapper, since the width-
    // ABI change) measures against layout.minWidth instead of whatever
    // wider contentWidth a PREVIOUS rebuild already settled on - without
    // this, ActualWidth() after UpdateLayout() reads back the stack's
    // own last answer, not the widget's actual minimum, and contentWidth
    // can only ever grow, never shrink, across rebuilds (a "ratchet").
    ApplyStackWidth((double)g_settings.layoutMinWidth);

    WidgetHost host{g_ui.hWnd, g_ui.widgetsPanel, PaneHeight()};

    for (auto& entry : g_widgets) {
        try {
            entry.widget->Destroy();
        } catch (...) {
        }
    }

    for (auto& entry : g_widgets) {
        if (entry.crashed) {
            continue;
        }
        try {
            entry.minWidth = entry.widget->Create(host);
        } catch (...) {
            entry.crashed = true;
        }
    }

    // Create() runs for every non-crashed widget above regardless of
    // entry.enabled, not just an oversight - ApplySliderTarget's offset
    // math is a flat `-widgetIndex * PaneHeight()` against the *raw*
    // g_widgets index (see PaneHeight()'s comment), so a disabled
    // widget's pane has to keep occupying its slot in widgetsPanel or
    // every later widget's slot would shift and the offset math would
    // point at the wrong pane. What was missing: nothing then hid that
    // pane again, so a disabled widget only dropped out of
    // EnabledIndices() (dots, navigation target) while its own visual
    // stayed fully opaque and interactive right where it was. Widgets
    // append their root as the Nth child of widgetsPanel in the same
    // order as this loop, one child per non-crashed entry - walk both in
    // lockstep and hide (without removing) whichever ones are disabled.
    {
        auto children = g_ui.widgetsPanel.Children();
        uint32_t childIdx = 0;
        for (auto& entry : g_widgets) {
            if (entry.crashed) {
                continue;
            }
            if (childIdx < children.Size()) {
                auto child = children.GetAt(childIdx);
                child.Opacity(entry.enabled ? 1.0 : 0.0);
                child.IsHitTestVisible(entry.enabled);
            }
            childIdx++;
        }
    }

    double contentWidth = 0.0;
    for (auto& entry : g_widgets) {
        if (entry.enabled && !entry.crashed) {
            contentWidth = std::max(contentWidth, entry.minWidth);
        }
    }
    // A misconfigured minWidth > maxWidth would otherwise invert the
    // clamp range below - widen the effective ceiling to match rather
    // than produce nonsense (spec's error-handling section).
    double effectiveMaxWidth =
        std::max((double)g_settings.layoutMinWidth, (double)g_settings.layoutMaxWidth);
    contentWidth = std::clamp(contentWidth, (double)g_settings.layoutMinWidth,
                               effectiveMaxWidth);
    ApplyStackWidth(contentWidth);

    auto enabled = EnabledIndices();
    if (enabled.empty()) {
        g_ui.activeIndex = -1;
        g_ui.activeWidgetId.clear();
    } else {
        // Resolve by Id() first (Incident 40) - g_widgets may have been
        // reordered/erased/pushed since g_ui.activeIndex was last set
        // (a remote widget's register/unregister round-trip, a manual
        // move up/down), which can make the raw index point at a
        // completely different widget even though it's still "valid"
        // (in range and enabled). Falls back to the raw index only when
        // no Id is tracked yet (first run) or the previously active
        // widget itself is gone/disabled now.
        int resolved = -1;
        if (!g_ui.activeWidgetId.empty()) {
            for (int idx : enabled) {
                try {
                    if (g_widgets[idx].widget->Id() == g_ui.activeWidgetId) {
                        resolved = idx;
                        break;
                    }
                } catch (...) {
                }
            }
        }
        if (resolved < 0) {
            resolved = std::find(enabled.begin(), enabled.end(),
                                  g_ui.activeIndex) != enabled.end()
                           ? g_ui.activeIndex
                           : enabled.front();
        }
        g_ui.activeIndex = resolved;
        try {
            g_ui.activeWidgetId = g_widgets[resolved].widget->Id();
        } catch (...) {
        }
    }
    if (g_ui.activeIndex >= 0) {
        ApplySliderTarget(g_ui.activeIndex, /*animate=*/false);
    }
    RefreshDots();
    UpdateStackScreenRect();
}

// Set while a context menu is open (from just before ShowAt to the
// flyout's Closed event) - stops touchpad nav from stepping widgets
// while the menu is up.
bool g_contextMenuOpen = false;

// Keeps the open MenuFlyout alive - the local variable that creates it
// in ShowContextMenu goes out of scope as soon as that function
// returns, but showing a flyout adds it to XAML's own open-popup
// bookkeeping, which is expected to keep it alive on its own; this is
// pure extra insurance given this file's history with WinRT object
// lifetime bugs (Incident 8). Cleared in the Closed handler.
MenuFlyout g_contextMenuFlyout{nullptr};

void ToggleWidgetEnabled(int idx) {
    if (idx >= 0 && idx < (int)g_widgets.size()) {
        g_widgets[idx].enabled = !g_widgets[idx].enabled;
    }
    SaveWidgetOrderState();
    RebuildStackContents();
}

void MoveWidget(int idx, int delta) {
    int other = idx + delta;
    if (idx >= 0 && idx < (int)g_widgets.size() && other >= 0 &&
        other < (int)g_widgets.size()) {
        std::swap(g_widgets[idx], g_widgets[other]);
    }
    SaveWidgetOrderState();
    RebuildStackContents();
}

// ---------------------------------------------------------------------
// Settings window (Incident 26)
//
// A separate top-level Win32 window hosting its own XAML island
// (DesktopWindowXamlSource) - a genuinely different technique from
// everything else in this file, which only ever attaches content
// into Explorer's OWN pre-existing XAML island (the taskbar's). This
// creates a brand new one from scratch on the same thread (Explorer's
// taskbar UI thread already pumps messages for windows it owns, so no
// separate message loop is needed here). Neither reference mod in
// this repo (taskbar-ai-quota.wh.cpp, taskbar-fluent-media-player.wh.cpp)
// hosts a separate window at all, so there's no prior art to port -
// this is first-principles, based on the publicly documented XAML
// Islands hosting API (WindowsXamlManager/DesktopWindowXamlSource/
// IDesktopWindowXamlSourceNative), never exercised inside an
// explorer.exe-hosted Windhawk mod before in this codebase. Real risk
// this doesn't work on the first try - flagged per the user's own
// "grill me" request before building it, see PLAN.md's "Settings
// window" design for the fuller reasoning and the persistence-layer
// decision above.
// ---------------------------------------------------------------------

struct SettingsWindowState {
    HWND hWnd{nullptr};
    HWND xamlIslandHwnd{nullptr};
    wuxh::WindowsXamlManager manager{nullptr};
    wuxh::DesktopWindowXamlSource source{nullptr};
    winrt::com_ptr<IDesktopWindowXamlSourceNative> sourceNative;
};

SettingsWindowState g_settingsWindow;

constexpr wchar_t kSettingsWindowClassName[] =
    L"TaskbarWidgetStackSettingsWindow";

// Reads the same registry value Windows' own Settings > Personalization
// page writes, so this window's title bar and content follow whatever
// the user has picked, light or dark - not just "matches the taskbar"
// (this window isn't hosted in the taskbar's own island, so it doesn't
// inherit that for free).
bool IsSystemDarkModeActive() {
    DWORD value = 1;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                       L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                       L"Themes\\Personalize",
                       0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD size = sizeof(value), type = 0;
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                          (BYTE*)&value, &size);
        RegCloseKey(key);
    }
    return value == 0;
}

// Colors the window's own non-client frame (title bar) dark or light -
// DwmSetWindowAttribute, not anything XAML controls. Attribute 20 is
// DWMWA_USE_IMMERSIVE_DARK_MODE on Windows 10 2004+ and Windows 11;
// some earlier 1903/1909 builds used 19 instead - tried as a fallback
// since passing an unsupported attribute id is documented to just fail
// harmlessly rather than crash.
void ApplyTitleBarTheme(HWND hWnd, bool dark) {
    BOOL value = dark ? TRUE : FALSE;
    if (DwmSetWindowAttribute(hWnd, 20, &value, sizeof(value)) != S_OK) {
        DwmSetWindowAttribute(hWnd, 19, &value, sizeof(value));
    }
}

void CloseSettingsWindow();

LRESULT CALLBACK SettingsWindowProc(HWND hWnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            if (g_settingsWindow.xamlIslandHwnd) {
                RECT rc;
                GetClientRect(hWnd, &rc);
                SetWindowPos(g_settingsWindow.xamlIslandHwnd, nullptr, 0, 0,
                             rc.right - rc.left, rc.bottom - rc.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            g_settingsWindow.sourceNative = nullptr;
            try {
                if (g_settingsWindow.source) {
                    g_settingsWindow.source.Close();
                }
            } catch (...) {
            }
            g_settingsWindow.source = nullptr;
            g_settingsWindow.manager = nullptr;
            g_settingsWindow.xamlIslandHwnd = nullptr;
            g_settingsWindow.hWnd = nullptr;
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

TextBlock MakePlaceholderTab(std::wstring text) {
    TextBlock block;
    block.Text(winrt::hstring(text));
    block.Margin({16, 16, 16, 16});
    block.TextWrapping(TextWrapping::Wrap);
    return block;
}

// Shared by the Navigation and Layout tabs (Incident 28) - a labeled
// ToggleSwitch whose Toggled handler both updates g_settings and
// persists to the private store directly, since neither of these two
// tabs needs a full rebuild-on-change like the Widgets tab does.
// `onChanged` does whatever else that specific setting needs beyond
// the g_settings/private-store write (e.g. layout settings also call
// RebuildStackContents(); nav settings don't need anything further,
// since every nav.* check already reads g_settings live).
ToggleSwitch MakeSettingsToggle(std::wstring header,
                                bool initial,
                                std::function<void(bool)> onChanged) {
    ToggleSwitch toggle;
    toggle.Header(winrt::box_value(winrt::hstring(header)));
    toggle.IsOn(initial);
    toggle.Toggled(
        [onChanged](winrt::Windows::Foundation::IInspectable const& sender,
                     RoutedEventArgs const&) {
            onChanged(sender.as<ToggleSwitch>().IsOn());
        });
    return toggle;
}

FrameworkElement BuildNavigationTab() {
    StackPanel panel;
    panel.Orientation(Orientation::Vertical);
    panel.Margin({16, 16, 16, 16});
    panel.Spacing(12);

    panel.Children().Append(MakeSettingsToggle(
        L"Mouse wheel navigation", g_settings.navWheel, [](bool on) {
            g_settings.navWheel = on;
            WritePrivateDword(L"nav.wheel", on ? 1 : 0);
        }));
    panel.Children().Append(MakeSettingsToggle(
        L"Dot-click navigation", g_settings.navDots, [](bool on) {
            g_settings.navDots = on;
            WritePrivateDword(L"nav.dots", on ? 1 : 0);
        }));
    panel.Children().Append(MakeSettingsToggle(
        L"Drag navigation", g_settings.navDrag, [](bool on) {
            g_settings.navDrag = on;
            WritePrivateDword(L"nav.drag", on ? 1 : 0);
        }));
    panel.Children().Append(MakeSettingsToggle(
        L"Wrap around", g_settings.navWrap, [](bool on) {
            g_settings.navWrap = on;
            WritePrivateDword(L"nav.wrap", on ? 1 : 0);
        }));
    panel.Children().Append(MakeSettingsToggle(
        L"Overscroll bounce", g_settings.navOverscroll, [](bool on) {
            g_settings.navOverscroll = on;
            WritePrivateDword(L"nav.overscroll", on ? 1 : 0);
        }));

    return panel;
}

FrameworkElement BuildLayoutTab() {
    StackPanel panel;
    panel.Orientation(Orientation::Vertical);
    panel.Margin({16, 16, 16, 16});
    panel.Spacing(16);

    StackPanel positionGroup;
    positionGroup.Orientation(Orientation::Vertical);
    positionGroup.Spacing(4);

    TextBlock positionLabel;
    positionLabel.Text(L"Taskbar position");
    positionGroup.Children().Append(positionLabel);

    // Order must match kPositionValues below - index N in one is index N
    // in the other. `static` so the SelectionChanged lambda below can
    // index into them without capturing (a non-static local constexpr
    // array indexed with a runtime value still ODR-uses the array
    // object, which would otherwise require an explicit capture).
    static constexpr const wchar_t* kPositionLabels[] = {
        L"Left edge",        L"Center",            L"Right edge",
        L"Left of Start",    L"Right of Start",    L"Left of Search",
        L"Right of Search",  L"Left of Task View",  L"Right of Task View",
        L"Left of Widgets",  L"Right of Widgets",   L"Left of tray",
        L"Right of tray"};
    static constexpr const wchar_t* kPositionValues[] = {
        L"left_edge",       L"center_edge",       L"right_edge",
        L"left_of_start",   L"right_of_start",    L"left_of_search",
        L"right_of_search", L"left_of_taskview",  L"right_of_taskview",
        L"left_of_widgets", L"right_of_widgets",  L"left_of_tray",
        L"right_of_tray"};
    ComboBox positionCombo;
    int selectedIndex = 0;
    for (int i = 0; i < ARRAYSIZE(kPositionValues); i++) {
        ComboBoxItem item;
        item.Content(winrt::box_value(winrt::hstring(kPositionLabels[i])));
        positionCombo.Items().Append(item);
        if (g_settings.layoutPosition == kPositionValues[i]) {
            selectedIndex = i;
        }
    }
    positionCombo.SelectedIndex(selectedIndex);
    positionCombo.SelectionChanged(
        [](winrt::Windows::Foundation::IInspectable const& sender,
           SelectionChangedEventArgs const&) {
            int index = sender.as<ComboBox>().SelectedIndex();
            if (index < 0 || index >= ARRAYSIZE(kPositionValues)) {
                return;
            }
            g_settings.layoutPosition = kPositionValues[index];
            WritePrivateString(L"layout.position", g_settings.layoutPosition);
            // Position is only applied at injection time (see
            // Wh_ModSettingsChanged's comment) - remove and re-inject to
            // apply it live from this control too.
            if (g_ui.hWnd) {
                RemoveWidgetStackGrid();
                InjectWidgetStackGrid(g_taskbarWnd);
            }
        });
    positionGroup.Children().Append(positionCombo);
    panel.Children().Append(positionGroup);

    StackPanel edgeGapGroup;
    edgeGapGroup.Orientation(Orientation::Vertical);
    edgeGapGroup.Spacing(4);

    TextBlock edgeGapLabel;
    edgeGapLabel.Text(winrt::hstring(
        L"Edge gap: " + std::to_wstring(g_settings.layoutEdgeGap) + L"px"));
    edgeGapGroup.Children().Append(edgeGapLabel);

    Slider edgeGapSlider;
    edgeGapSlider.Minimum(0);
    edgeGapSlider.Maximum(48);
    edgeGapSlider.StepFrequency(1);
    edgeGapSlider.Value(g_settings.layoutEdgeGap);
    edgeGapSlider.ValueChanged(
        [edgeGapLabel](winrt::Windows::Foundation::IInspectable const&,
                        winrt::Windows::UI::Xaml::Controls::Primitives::
                            RangeBaseValueChangedEventArgs const& args) {
            int value = (int)args.NewValue();
            g_settings.layoutEdgeGap = value;
            WritePrivateDword(L"layout.edgeGap", (DWORD)value);
            edgeGapLabel.Text(
                winrt::hstring(L"Edge gap: " + std::to_wstring(value) + L"px"));
            // Static edge positions only apply this at injection time
            // (see Wh_ModSettingsChanged's comment) - tracking positions
            // pick it up live on their own via UpdateTrackedPosition.
            if (g_ui.hWnd) {
                RemoveWidgetStackGrid();
                InjectWidgetStackGrid(g_taskbarWnd);
            }
        });
    edgeGapGroup.Children().Append(edgeGapSlider);
    panel.Children().Append(edgeGapGroup);

    StackPanel rightPaddingGroup;
    rightPaddingGroup.Orientation(Orientation::Vertical);
    rightPaddingGroup.Spacing(4);

    TextBlock rightPaddingLabel;
    rightPaddingLabel.Text(
        winrt::hstring(L"Trailing padding: " +
                       std::to_wstring(g_settings.layoutRightPadding) + L"px"));
    rightPaddingGroup.Children().Append(rightPaddingLabel);

    Slider rightPaddingSlider;
    rightPaddingSlider.Minimum(0);
    rightPaddingSlider.Maximum(48);
    rightPaddingSlider.StepFrequency(1);
    rightPaddingSlider.Value(g_settings.layoutRightPadding);
    rightPaddingSlider.ValueChanged(
        [rightPaddingLabel](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Controls::Primitives::
                RangeBaseValueChangedEventArgs const& args) {
            int value = (int)args.NewValue();
            g_settings.layoutRightPadding = value;
            WritePrivateDword(L"layout.rightPadding", (DWORD)value);
            rightPaddingLabel.Text(winrt::hstring(
                L"Trailing padding: " + std::to_wstring(value) + L"px"));
            RebuildStackContents();
        });
    rightPaddingGroup.Children().Append(rightPaddingSlider);
    panel.Children().Append(rightPaddingGroup);

    StackPanel minWidthGroup;
    minWidthGroup.Orientation(Orientation::Vertical);
    minWidthGroup.Spacing(4);

    TextBlock minWidthLabel;
    minWidthLabel.Text(winrt::hstring(L"Minimum stack width: " +
                                       std::to_wstring(g_settings.layoutMinWidth) +
                                       L"px"));
    minWidthGroup.Children().Append(minWidthLabel);

    Slider minWidthSlider;
    minWidthSlider.Minimum(20);
    minWidthSlider.Maximum(1000);
    minWidthSlider.StepFrequency(10);
    minWidthSlider.Value(g_settings.layoutMinWidth);
    minWidthSlider.ValueChanged(
        [minWidthLabel](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Controls::Primitives::
                RangeBaseValueChangedEventArgs const& args) {
            int value = (int)args.NewValue();
            g_settings.layoutMinWidth = value;
            WritePrivateDword(L"layout.minWidth", (DWORD)value);
            minWidthLabel.Text(winrt::hstring(
                L"Minimum stack width: " + std::to_wstring(value) + L"px"));
            RebuildStackContents();
        });
    minWidthGroup.Children().Append(minWidthSlider);
    panel.Children().Append(minWidthGroup);

    StackPanel maxWidthGroup;
    maxWidthGroup.Orientation(Orientation::Vertical);
    maxWidthGroup.Spacing(4);

    TextBlock maxWidthLabel;
    maxWidthLabel.Text(winrt::hstring(L"Maximum stack width: " +
                                       std::to_wstring(g_settings.layoutMaxWidth) +
                                       L"px"));
    maxWidthGroup.Children().Append(maxWidthLabel);

    Slider maxWidthSlider;
    maxWidthSlider.Minimum(100);
    maxWidthSlider.Maximum(1000);
    maxWidthSlider.StepFrequency(10);
    maxWidthSlider.Value(g_settings.layoutMaxWidth);
    maxWidthSlider.ValueChanged(
        [maxWidthLabel](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Controls::Primitives::
                RangeBaseValueChangedEventArgs const& args) {
            int value = (int)args.NewValue();
            g_settings.layoutMaxWidth = value;
            WritePrivateDword(L"layout.maxWidth", (DWORD)value);
            maxWidthLabel.Text(winrt::hstring(
                L"Maximum stack width: " + std::to_wstring(value) + L"px"));
            RebuildStackContents();
        });
    maxWidthGroup.Children().Append(maxWidthSlider);
    panel.Children().Append(maxWidthGroup);

    StackPanel paneHeightGroup;
    paneHeightGroup.Orientation(Orientation::Vertical);
    paneHeightGroup.Spacing(4);

    TextBlock paneHeightLabel;
    paneHeightLabel.Text(winrt::hstring(
        L"Pane height: " + std::to_wstring(g_settings.layoutPaneHeight) +
        L"px"));
    paneHeightGroup.Children().Append(paneHeightLabel);

    Slider paneHeightSlider;
    paneHeightSlider.Minimum(20);
    paneHeightSlider.Maximum(120);
    paneHeightSlider.StepFrequency(2);
    paneHeightSlider.Value(g_settings.layoutPaneHeight);
    paneHeightSlider.ValueChanged(
        [paneHeightLabel](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Controls::Primitives::
                RangeBaseValueChangedEventArgs const& args) {
            int value = (int)args.NewValue();
            g_settings.layoutPaneHeight = value;
            WritePrivateDword(L"layout.paneHeight", (DWORD)value);
            paneHeightLabel.Text(winrt::hstring(
                L"Pane height: " + std::to_wstring(value) + L"px"));
            RebuildStackContents();
        });
    paneHeightGroup.Children().Append(paneHeightSlider);
    panel.Children().Append(paneHeightGroup);

    panel.Children().Append(MakeSettingsToggle(
        L"Show dot indicator", g_settings.layoutIndicatorVisible,
        [](bool on) {
            g_settings.layoutIndicatorVisible = on;
            WritePrivateDword(L"layout.indicator.visible", on ? 1 : 0);
            RebuildStackContents();
        }));

    panel.Children().Append(MakeSettingsToggle(
        L"Hide indicator with one widget",
        g_settings.layoutHideIndicatorWhenSingle, [](bool on) {
            g_settings.layoutHideIndicatorWhenSingle = on;
            WritePrivateDword(L"layout.indicator.hideWhenSingle",
                               on ? 1 : 0);
            RebuildStackContents();
        }));

    StackPanel gapGroup;
    gapGroup.Orientation(Orientation::Vertical);
    gapGroup.Spacing(4);

    TextBlock gapLabel;
    gapLabel.Text(winrt::hstring(L"Indicator-to-widgets gap: " +
                                  std::to_wstring(g_settings.layoutIndicatorGap) +
                                  L"px"));
    gapGroup.Children().Append(gapLabel);

    Slider gapSlider;
    gapSlider.Minimum(0);
    gapSlider.Maximum(24);
    gapSlider.StepFrequency(1);
    gapSlider.Value(g_settings.layoutIndicatorGap);
    gapSlider.ValueChanged(
        [gapLabel](winrt::Windows::Foundation::IInspectable const&,
                    winrt::Windows::UI::Xaml::Controls::Primitives::
                        RangeBaseValueChangedEventArgs const& args) {
            int value = (int)args.NewValue();
            g_settings.layoutIndicatorGap = value;
            WritePrivateDword(L"layout.indicator.gap", (DWORD)value);
            gapLabel.Text(winrt::hstring(L"Indicator-to-widgets gap: " +
                                          std::to_wstring(value) + L"px"));
            RebuildStackContents();
        });
    gapGroup.Children().Append(gapSlider);
    panel.Children().Append(gapGroup);

    panel.Children().Append(MakeSettingsToggle(
        L"Indicator on the right", g_settings.layoutIndicatorOnRight,
        [](bool on) {
            g_settings.layoutIndicatorOnRight = on;
            WritePrivateDword(L"layout.indicator.onRight", on ? 1 : 0);
            RebuildStackContents();
        }));

    return panel;
}

// Rebuilt (not patched) on every toggle/move, same rebuild-on-change
// idiom used everywhere else in this file - simpler than diffing, and
// this list is short. Reuses ToggleWidgetEnabled/MoveWidget directly,
// the same functions the right-click menu calls, so both surfaces
// stay in sync through the one SaveWidgetOrderState() path.
ScrollViewer g_widgetsTabScroller{nullptr};

FrameworkElement BuildWidgetsTab();

// Shown in place of the widget list (Incident 31) when a widget's own
// gear button is clicked - a "back" button plus that widget's
// IWidget::BuildSettingsPanel() output. Swapping g_widgetsTabScroller's
// own Content back and forth between this and BuildWidgetsTab() is
// simpler than a real nested-navigation stack for what's currently
// just one level deep.
FrameworkElement BuildWidgetSettingsView(int idx) {
    StackPanel panel;
    panel.Orientation(Orientation::Vertical);

    Button backButton;
    backButton.Content(winrt::box_value(winrt::hstring(L"< Back to widgets")));
    backButton.Margin({16, 16, 16, 0});
    backButton.Click(
        [](winrt::Windows::Foundation::IInspectable const&,
           RoutedEventArgs const&) {
            if (g_widgetsTabScroller) {
                g_widgetsTabScroller.Content(BuildWidgetsTab());
            }
        });
    panel.Children().Append(backButton);

    if (idx >= 0 && idx < (int)g_widgets.size()) {
        try {
            auto settingsPanel = g_widgets[idx].widget->BuildSettingsPanel();
            if (settingsPanel) {
                panel.Children().Append(settingsPanel);
            }
        } catch (...) {
        }
    }

    return panel;
}

FrameworkElement BuildWidgetsTab() {
    StackPanel panel;
    panel.Orientation(Orientation::Vertical);
    panel.Margin({16, 16, 16, 16});
    panel.Spacing(6);

    for (int i = 0; i < (int)g_widgets.size(); i++) {
        std::wstring label = g_widgets[i].widget->DisplayName();
        std::replace(label.begin(), label.end(), L'\n', L' ');

        StackPanel row;
        row.Orientation(Orientation::Horizontal);
        row.Spacing(8);

        CheckBox enabledBox;
        enabledBox.Content(winrt::box_value(winrt::hstring(label)));
        enabledBox.IsChecked(g_widgets[i].enabled);
        enabledBox.MinWidth(160);
        auto toggleHandler =
            [i](winrt::Windows::Foundation::IInspectable const&,
                RoutedEventArgs const&) {
                ToggleWidgetEnabled(i);
                if (g_widgetsTabScroller) {
                    g_widgetsTabScroller.Content(BuildWidgetsTab());
                }
            };
        enabledBox.Checked(toggleHandler);
        enabledBox.Unchecked(toggleHandler);
        row.Children().Append(enabledBox);

        Button upButton;
        upButton.Content(winrt::box_value(winrt::hstring(L"Up")));
        upButton.IsEnabled(i > 0);
        upButton.Click([i](winrt::Windows::Foundation::IInspectable const&,
                            RoutedEventArgs const&) {
            MoveWidget(i, -1);
            if (g_widgetsTabScroller) {
                g_widgetsTabScroller.Content(BuildWidgetsTab());
            }
        });
        row.Children().Append(upButton);

        Button downButton;
        downButton.Content(winrt::box_value(winrt::hstring(L"Down")));
        downButton.IsEnabled(i < (int)g_widgets.size() - 1);
        downButton.Click([i](winrt::Windows::Foundation::IInspectable const&,
                              RoutedEventArgs const&) {
            MoveWidget(i, 1);
            if (g_widgetsTabScroller) {
                g_widgetsTabScroller.Content(BuildWidgetsTab());
            }
        });
        row.Children().Append(downButton);

        if (g_widgets[i].widget->HasSettings()) {
            Button gearButton;
            // Plain Unicode glyph rather than a Segoe MDL2 FontIcon
            // codepoint - Incident 31's Move up/down icons showed
            // those codepoints aren't reliably guessable against this
            // SDK (Symbol::Down didn't even exist), so a literal
            // character is the lower-risk choice here.
            gearButton.Content(winrt::box_value(winrt::hstring(L"⚙")));
            gearButton.Click(
                [i](winrt::Windows::Foundation::IInspectable const&,
                    RoutedEventArgs const&) {
                    if (g_widgetsTabScroller) {
                        g_widgetsTabScroller.Content(
                            BuildWidgetSettingsView(i));
                    }
                });
            row.Children().Append(gearButton);
        }

        panel.Children().Append(row);
    }

    // Add/remove: stubbed disabled (user decision, 2026-09-17) - no
    // widget catalog exists yet to add from. See PLAN.md "Next steps".
    Button addButton;
    addButton.Content(winrt::box_value(winrt::hstring(L"Add widget...")));
    addButton.IsEnabled(false);
    addButton.Margin({0, 8, 0, 0});
    panel.Children().Append(addButton);

    return panel;
}

bool OpenSettingsWindow() {
    if (g_settingsWindow.hWnd) {
        SetForegroundWindow(g_settingsWindow.hWnd);
        return true;
    }

    try {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SettingsWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kSettingsWindowClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        // Fails harmlessly with ERROR_CLASS_ALREADY_EXISTS on a second
        // open after a first close - ignored.
        RegisterClassW(&wc);

        HWND hWnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME, kSettingsWindowClassName,
            L"Taskbar Widget Stack Settings", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 480, 420, nullptr, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!hWnd) {
            Wh_Log(L"OpenSettingsWindow: CreateWindowExW failed");
            return false;
        }
        // Stored immediately - if anything below throws, the catch
        // block's CloseSettingsWindow() needs this to already be set,
        // or it wouldn't know to destroy this HWND at all.
        g_settingsWindow.hWnd = hWnd;

        bool darkMode = IsSystemDarkModeActive();
        ApplyTitleBarTheme(hWnd, darkMode);

        auto manager = wuxh::WindowsXamlManager::InitializeForCurrentThread();
        wuxh::DesktopWindowXamlSource source;
        auto sourceNative = source.as<IDesktopWindowXamlSourceNative>();
        winrt::check_hresult(sourceNative->AttachToWindow(hWnd));
        HWND xamlIslandHwnd = nullptr;
        sourceNative->get_WindowHandle(&xamlIslandHwnd);
        if (!xamlIslandHwnd) {
            Wh_Log(L"OpenSettingsWindow: no XAML island HWND");
            DestroyWindow(hWnd);
            return false;
        }
        RECT rc;
        GetClientRect(hWnd, &rc);
        SetWindowPos(xamlIslandHwnd, nullptr, 0, 0, rc.right - rc.left,
                     rc.bottom - rc.top, SWP_SHOWWINDOW);

        // NavigationView with PaneDisplayMode::Top (Incident 27,
        // replacing the first pass's Pivot) - Pivot's header strip is
        // the older, phone-hub-era look (large text, underline
        // indicator); NavigationView's top mode renders a segmented
        // pill-style tab row closer to Windows 11's own look, and -
        // unlike TabView - ships in the OS's own Windows.UI.Xaml
        // (no WinUI 2/3 NuGet package needed, matching the "same UWP
        // toolkit, no new dependency" decision above). NavigationView
        // has no per-item Content property like Pivot/TabView, so tab
        // switching is done by hand in SelectionChanged, swapping
        // contentHost's Content based on the selected item's Tag.
        NavigationView navView;
        navView.PaneDisplayMode(NavigationViewPaneDisplayMode::Top);
        navView.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        navView.IsSettingsVisible(false);
        navView.RequestedTheme(darkMode ? ElementTheme::Dark
                                         : ElementTheme::Light);

        ContentControl contentHost;
        contentHost.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        contentHost.VerticalContentAlignment(VerticalAlignment::Stretch);

        ScrollViewer widgetsScroller;
        widgetsScroller.Content(BuildWidgetsTab());
        g_widgetsTabScroller = widgetsScroller;

        // Navigation/Layout wrapped in their own ScrollViewer too
        // (Incident 44) - Layout in particular has grown enough sliders/
        // combo boxes (position, edge gap, trailing padding, max width,
        // pane height, indicator gap, ...) to overflow the settings
        // window's fixed height without one; matches the Widgets tab's
        // own existing pattern above rather than introducing a new one.
        ScrollViewer navScroller;
        navScroller.Content(BuildNavigationTab());
        ScrollViewer layoutScroller;
        layoutScroller.Content(BuildLayoutTab());
        auto aboutPlaceholder = MakePlaceholderTab(
            L"Taskbar Widget Stack - see this mod's README and PLAN.md "
            L"in the repo for status and roadmap.");

        NavigationViewItem widgetsNavItem;
        widgetsNavItem.Content(winrt::box_value(winrt::hstring(L"Widgets")));
        widgetsNavItem.Tag(winrt::box_value(winrt::hstring(L"widgets")));
        navView.MenuItems().Append(widgetsNavItem);

        NavigationViewItem navNavItem;
        navNavItem.Content(winrt::box_value(winrt::hstring(L"Navigation")));
        navNavItem.Tag(winrt::box_value(winrt::hstring(L"navigation")));
        navView.MenuItems().Append(navNavItem);

        NavigationViewItem layoutNavItem;
        layoutNavItem.Content(winrt::box_value(winrt::hstring(L"Layout")));
        layoutNavItem.Tag(winrt::box_value(winrt::hstring(L"layout")));
        navView.MenuItems().Append(layoutNavItem);

        NavigationViewItem aboutNavItem;
        aboutNavItem.Content(winrt::box_value(winrt::hstring(L"Help/About")));
        aboutNavItem.Tag(winrt::box_value(winrt::hstring(L"about")));
        navView.MenuItems().Append(aboutNavItem);

        navView.SelectionChanged(
            [contentHost, widgetsScroller, navScroller, layoutScroller,
             aboutPlaceholder](
                NavigationView const&,
                NavigationViewSelectionChangedEventArgs const& args) {
                auto item = args.SelectedItem().try_as<NavigationViewItem>();
                if (!item) {
                    return;
                }
                auto tag = winrt::unbox_value_or<winrt::hstring>(
                    item.Tag(), winrt::hstring{});
                if (tag == L"widgets") {
                    contentHost.Content(widgetsScroller);
                } else if (tag == L"navigation") {
                    contentHost.Content(navScroller);
                } else if (tag == L"layout") {
                    contentHost.Content(layoutScroller);
                } else if (tag == L"about") {
                    contentHost.Content(aboutPlaceholder);
                }
            });

        navView.Content(contentHost);
        navView.SelectedItem(widgetsNavItem);
        contentHost.Content(widgetsScroller);

        source.Content(navView);

        g_settingsWindow.hWnd = hWnd;
        g_settingsWindow.xamlIslandHwnd = xamlIslandHwnd;
        g_settingsWindow.manager = manager;
        g_settingsWindow.source = source;
        g_settingsWindow.sourceNative = sourceNative;

        ShowWindow(hWnd, SW_SHOW);
        SetForegroundWindow(hWnd);
        Wh_Log(L"OpenSettingsWindow: opened");
        return true;
    } catch (...) {
        Wh_Log(L"OpenSettingsWindow: exception");
        CloseSettingsWindow();
        return false;
    }
}

void CloseSettingsWindow() {
    if (g_settingsWindow.hWnd) {
        DestroyWindow(g_settingsWindow.hWnd);
    }
    g_settingsWindow = {};
}

// Right-click menu, as a XAML MenuFlyout rather than a native
// TrackPopupMenu (Incident 23 - see PLAN.md). TrackPopupMenu pumps its
// own nested Win32 message loop, and right-click kept crashing Explorer
// (0xC0000005, faulting module "unknown", offset 0x0) with the crash
// site logging added in Incident 22 pointing at "inside TrackPopupMenu
// itself, before it returns" - after two unrelated hypotheses (Incidents
// 20, 21) had already been fixed and ruled out as the cause. Rather than
// keep guessing at what specifically collides with a XAML-island-hosting
// window's native modal popup loop, this replaces the mechanism
// entirely with a MenuFlyout - a real XAML control, shown and dismissed
// through the same Composition/dispatcher machinery as everything else
// in this file, with no nested Win32 message loop at all. Prior art:
// user pointed at `taskbar-icon-separators` (windhawk.net) as a mod that
// already does WinUI-style taskbar context menus this way.
//
// Structure (Incident 24, 2026-09-17, per user-supplied mockup): one
// row per widget, each a submenu (`MenuFlyoutSubItem`) holding "Show
// widget" (a checkable toggle) and "Move up"/"Move down", followed by
// a separator and a "Stack settings" row - matching the native
// Windows 11 taskbar's own per-icon right-click menu style. Replaces
// the flat toggle-then-separator-then-all-moves layout from the first
// MenuFlyout pass.
//
// Incident 50 (2026-09-21, user request): added three more entries so
// the menu isn't just "manage widgets" + "open settings" - a "Go to
// widget" submenu of flat, one-click rows (jump straight to a widget
// without the dots/scroll), a "Show indicator dots" quick toggle (the
// same setting as the settings window's own toggle, mirrored here so a
// frequent flip doesn't need the whole window), and "Reset position"
// (forces the stack's on-screen position to be recomputed - a real fix
// for right_edge's trayGap going stale if the tray's width changed
// since injection, see ResetStackPosition's own comment).
void ShowContextMenu(HWND, POINT) {
    if (!g_ui.root) {
        return;
    }
    try {
        MenuFlyout flyout;

        // "Go to widget" - flat, one row per enabled widget, name only,
        // jumps directly there on click. Deliberately separate from the
        // per-widget management submenus below (which stay focused on
        // show/hide + reorder) rather than merging a third action into
        // each of those - this one is about quick access to a widget
        // that's already visible, not managing the set.
        auto enabledForGoTo = EnabledIndices();
        if (enabledForGoTo.size() > 1) {
            MenuFlyoutSubItem goToItem;
            goToItem.Text(L"Go to widget");
            FontIcon goToIcon;
            goToIcon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
            goToIcon.Glyph(L"\uE8A7");  // forward/jump-to glyph
            goToItem.Icon(goToIcon);
            for (int idx : enabledForGoTo) {
                std::wstring label = g_widgets[idx].widget->DisplayName();
                std::replace(label.begin(), label.end(), L'\n', L' ');
                MenuFlyoutItem goToWidgetItem;
                goToWidgetItem.Text(winrt::hstring(label));
                goToWidgetItem.IsEnabled(idx != g_ui.activeIndex);
                goToWidgetItem.Click(
                    [idx](winrt::Windows::Foundation::IInspectable const&,
                          RoutedEventArgs const&) { GoToWidget(idx); });
                goToItem.Items().Append(goToWidgetItem);
            }
            flyout.Items().Append(goToItem);

            MenuFlyoutSeparator goToSeparator;
            flyout.Items().Append(goToSeparator);
        }

        for (int i = 0; i < (int)g_widgets.size(); i++) {
            // The pane's own label uses an embedded newline for a
            // two-line fit (e.g. "Media\nPlayer") - not appropriate for
            // a single-line menu row.
            std::wstring label = g_widgets[i].widget->DisplayName();
            std::replace(label.begin(), label.end(), L'\n', L' ');

            MenuFlyoutSubItem widgetItem;
            widgetItem.Text(winrt::hstring(label));

            ToggleMenuFlyoutItem showItem;
            showItem.Text(L"Show widget");
            showItem.IsChecked(g_widgets[i].enabled);
            showItem.Click([i](winrt::Windows::Foundation::IInspectable const&,
                                RoutedEventArgs const&) {
                ToggleWidgetEnabled(i);
            });
            widgetItem.Items().Append(showItem);

            MenuFlyoutSeparator innerSeparator;
            widgetItem.Items().Append(innerSeparator);

            // No icons on these two (user request, 2026-09-17) -
            // text only. Disabled (grayed out, non-clickable) at
            // either end of g_widgets - MoveWidget's own bounds check
            // already made clicking a no-op there, but leaving them
            // clickable-looking was misleading.
            MenuFlyoutItem up;
            up.Text(L"Move up");
            up.IsEnabled(i > 0);
            up.Click([i](winrt::Windows::Foundation::IInspectable const&,
                          RoutedEventArgs const&) { MoveWidget(i, -1); });
            widgetItem.Items().Append(up);

            MenuFlyoutItem down;
            down.Text(L"Move down");
            down.IsEnabled(i < (int)g_widgets.size() - 1);
            down.Click([i](winrt::Windows::Foundation::IInspectable const&,
                            RoutedEventArgs const&) { MoveWidget(i, 1); });
            widgetItem.Items().Append(down);

            flyout.Items().Append(widgetItem);
        }

        MenuFlyoutSeparator separator;
        flyout.Items().Append(separator);

        // Same setting/mechanism as the settings window's own "Show dot
        // indicator" toggle - mirrored here since it's a frequent flip
        // that doesn't warrant opening the whole window.
        ToggleMenuFlyoutItem showDotsItem;
        showDotsItem.Text(L"Show indicator dots");
        showDotsItem.IsChecked(g_settings.layoutIndicatorVisible);
        showDotsItem.Click(
            [](winrt::Windows::Foundation::IInspectable const&,
               RoutedEventArgs const&) {
                g_settings.layoutIndicatorVisible =
                    !g_settings.layoutIndicatorVisible;
                WritePrivateDword(L"layout.indicator.visible",
                                   g_settings.layoutIndicatorVisible ? 1 : 0);
                RebuildStackContents();
            });
        flyout.Items().Append(showDotsItem);

        MenuFlyoutItem resetPositionItem;
        resetPositionItem.Text(L"Reset position");
        FontIcon resetPositionIcon;
        resetPositionIcon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
        resetPositionIcon.Glyph(L"\uE777");  // refresh/realign glyph
        resetPositionItem.Icon(resetPositionIcon);
        resetPositionItem.Click(
            [](winrt::Windows::Foundation::IInspectable const&,
               RoutedEventArgs const&) { ResetStackPosition(); });
        flyout.Items().Append(resetPositionItem);

        MenuFlyoutSeparator settingsSeparator;
        flyout.Items().Append(settingsSeparator);

        // Opens the settings window (Incident 26) - was a no-op stub
        // before that existed.
        MenuFlyoutItem settings;
        settings.Text(L"Stack settings");
        FontIcon settingsIcon;
        settingsIcon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
        settingsIcon.Glyph(L"\uE713");  // gear/settings glyph
        settings.Icon(settingsIcon);
        settings.Click([](winrt::Windows::Foundation::IInspectable const&,
                           RoutedEventArgs const&) { OpenSettingsWindow(); });
        flyout.Items().Append(settings);

        flyout.Closed([](winrt::Windows::Foundation::IInspectable const&,
                          winrt::Windows::Foundation::IInspectable const&) {
            g_contextMenuOpen = false;
            g_contextMenuFlyout = nullptr;
        });
        g_contextMenuFlyout = flyout;
        g_contextMenuOpen = true;
        flyout.ShowAt(g_ui.root);
    } catch (...) {
        g_contextMenuOpen = false;
        g_contextMenuFlyout = nullptr;
    }
}

// Hit-tests the live cursor position against the widget stack's own
// bounds (Incident 18: raw HID input is registered against the whole
// touchpad device, not scoped to any window/element, so WM_INPUT fires
// no matter where the cursor is - gating on this is what makes the
// touchpad-scroll handler below only react while actually over the
// stack, instead of hijacking every touchpad touch anywhere on the
// taskbar). Pure Win32 (GetCursorPos + PtInRect) against a rect cached
// by UpdateStackScreenRect() - deliberately does NOT call into XAML
// itself; see that function's comment (Incident 21) for why calling
// TransformToVisual from here specifically was crashing Explorer.
bool IsCursorOverWidgetStack() {
    POINT pt;
    if (!GetCursorPos(&pt)) {
        return false;
    }
    return PtInRect(&g_ui.stackScreenRect, pt);
}

// Named (not an inline lambda) because SetWindowSubclass/RemoveWindowSubclass
// match subclasses by exact function pointer - installing with one lambda
// and removing with a different one (even with identical bodies) silently
// fails to remove anything.
LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam, UINT_PTR) {
    if (msg == WM_INPUT && g_settings.navWheel && !g_contextMenuOpen) {
        // Raw HID input from the Precision Touchpad (Incident 17),
        // registered via RegisterRawInputDevices (usage page 0x0D
        // "Digitizer", usage 0x05 "Touch Pad") - the lowest level of
        // touchpad data an application can see, needed because every
        // OS-level gesture/wheel synthesis is confirmed dead for this
        // gesture (Incidents 9, 14, 15, 16).
        //
        // Byte layout confirmed from live captures (report ID 0x04,
        // sizeHid=40): bRawData[1] = contact 1 status (bit0 tip-switch,
        // bit1 confidence - 0x03 while touching, 0x00 when contact 1 is
        // up), bRawData[2:4]/[4:6] = contact 1 X/Y as little-endian
        // uint16, bRawData[38] = contact count. The slot layout for
        // contacts 2+ did NOT validate cleanly against a real
        // two-contact sample (a naive 5-bytes-per-slot hypothesis put an
        // inactive status where a second contact should be), so it's
        // deliberately not parsed here - only contact 1's Y is tracked,
        // which is enough to see the vertical motion of a two-finger
        // scroll (both fingers move together) without risking a
        // misparsed slot layout.
        //
        // Incident 18 fixes, from live feedback on the first pass:
        // (1) it fired on a single finger and anywhere on the taskbar,
        // because raw input isn't scoped to a window/element at all -
        // now gated on byte[38] (contact count) >= 2 AND the cursor
        // actually being over the stack (IsCursorOverWidgetStack), so a
        // plain one-finger cursor move never steps a widget; (2) the
        // stepped direction was backwards - flipped below.
        UINT size = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size,
                         sizeof(RAWINPUTHEADER));
        if (size > 0) {
            std::vector<BYTE> buffer(size);
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer.data(),
                                 &size, sizeof(RAWINPUTHEADER)) == size) {
                auto* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
                if (raw->header.dwType == RIM_TYPEHID &&
                    raw->data.hid.dwSizeHid >= 39) {
                    const BYTE* bytes = raw->data.hid.bRawData;
                    BYTE status = bytes[1];
                    BYTE contactCount = bytes[38];
                    bool touching =
                        (status & 0x03) != 0 && contactCount >= 2 &&
                        IsCursorOverWidgetStack();
                    LONG y = (LONG)(WORD)(bytes[4] | (bytes[5] << 8));

                    if (!touching) {
                        // Contact lifted, dropped below two fingers, or
                        // cursor left the stack - drop tracking so the
                        // next qualifying touch doesn't see a bogus jump
                        // from whatever Y/state applied before.
                        g_hidContactActive = false;
                    } else if (!g_hidContactActive) {
                        // Fresh qualifying touch (or the "clutch"
                        // re-grip seen in captures): start tracking from
                        // here instead of diffing against a stale
                        // previous position.
                        g_hidContactActive = true;
                        g_hidPrevY = y;
                    } else {
                        LONG dy = y - g_hidPrevY;
                        g_hidPrevY = y;
                        g_hidAccumY += dy;
                        // Incident 19: step at most once per debounce
                        // window, in HID sensor units rather than
                        // PaneHeight()'s on-screen DIPs (see
                        // kHidStepThresholdUnits above) - a burst of
                        // WM_INPUT samples during one continuous swipe
                        // used to clear the old (wrong-unit) threshold
                        // several times over, firing multiple rapid
                        // steps for a single gesture.
                        ULONGLONG now = GetTickCount64();
                        if (std::abs(g_hidAccumY) > kHidStepThresholdUnits) {
                            if (now - g_hidLastStepTick >
                                kHidStepDebounceMs) {
                                StepWidget(g_hidAccumY < 0 ? 1 : -1);
                                g_hidLastStepTick = now;
                                g_hidAccumY = 0;
                            } else {
                                // Still within the debounce window -
                                // clamp instead of letting a held swipe
                                // accumulate unboundedly, so the very
                                // next eligible tick steps once, not
                                // several times back-to-back.
                                g_hidAccumY = g_hidAccumY < 0
                                                  ? -kHidStepThresholdUnits
                                                  : kHidStepThresholdUnits;
                            }
                        }
                    }
                }
            }
        }
    }
    if (msg == WM_NCDESTROY) {
        // The XAML tree is already dying - don't touch trayGrid's
        // children/columns (matches the approach in
        // taskbar-ai-quota.wh.cpp's own WM_NCDESTROY handling) - but the
        // CompositionTarget::Rendering subscription is a separate, live
        // per-process callback that must still be explicitly revoked
        // here, or it survives into g_ui's reset with no way left to find
        // and revoke it - the same orphaned-callback crash class
        // documented in this repo's other mod
        // (windows-11-start-menu-button/PLAN.md's "Crash containment"
        // section).
        StopSnapAnimation();
        UnwireNavigation();
        // Tree is dying - unhook the LayoutUpdated token (a live
        // per-element subscription, same orphaned-callback class as
        // CompositionTarget::Rendering above) but don't try to write
        // trackedElement's Margin back; it's part of the same dying
        // tree, matching this handler's existing "don't touch trayGrid's
        // children/columns" restraint.
        UnwireTracking(/*restoreMargin=*/false);
        g_ui = {};
    } else if (msg == WM_DISPLAYCHANGE && !g_stopRequested) {
        StopSnapAnimation();
        UnwireNavigation();
        UnwireTracking(/*restoreMargin=*/true);
        g_ui = {};
        // Re-poll for the (possibly recreated) taskbar and
        // SystemTrayFrameGrid rather than assuming this exact HWND
        // survives a display change.
        if (g_injectEvent) {
            SetEvent(g_injectEvent);
        }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// Edge positions (Incident 6/37): flush against one of the taskbar's own
// static edges, no element tracking - two earlier attempts at anchoring
// to a specific element instead (the Start button alone, then the whole
// TaskbarFrameRepeater) both put the stack somewhere wrong, since
// TaskbarFrameRepeater in particular also contains every pinned/running
// app icon, not just the leading buttons. Tracking positions (Incident
// 38, below) anchor to a *specific* button element instead of the whole
// repeater, which is what makes tracking actually work this time. The
// gap itself used to be a hardcoded constexpr (kEdgeGap) - now
// layout.edgeGap (Incident 43). UpdateTrackedPosition reads it fresh on
// every layout pass, so a tracking position picks up a change live; the
// two static edge positions (left_edge/right_edge) only set their
// Margin once, at injection, so Wh_ModSettingsChanged treats an
// edgeGap/rightPadding change the same as a layoutPosition change - an
// explicit remove-then-inject to actually apply it for those.

// Unhooks the tracking-position LayoutUpdated subscription (if wired) and
// optionally restores trackedElement's margin to what it was before this
// mod pushed it aside - see UiState's tracking fields and
// UpdateTrackedPosition below. Safe to call unconditionally (no-op if no
// tracking position is/was active). `restoreMargin` is false only when
// the taskbar's own XAML tree is being torn down anyway (WM_NCDESTROY) -
// see that call site's comment.
void UnwireTracking(bool restoreMargin) {
    if (g_ui.layoutUpdatedWired && g_ui.injectionParent) {
        try {
            g_ui.injectionParent.LayoutUpdated(g_ui.layoutUpdatedToken);
        } catch (...) {
        }
        g_ui.layoutUpdatedWired = false;
    }
    if (restoreMargin && g_ui.trackedElement &&
        g_ui.hasTrackedElementOriginalMargin) {
        try {
            g_ui.trackedElement.Margin(g_ui.trackedElementOriginalMargin);
        } catch (...) {
        }
    }
    g_ui.trackedElement = nullptr;
    g_ui.hasTrackedElementOriginalMargin = false;
}

// Live LayoutUpdated handler body for a tracking position (Incident 38).
// Ported (with attribution, simplified - no far_left variant, no
// Start-button-mod width adjustment) from taskbar-fluent-media-player.wh.cpp
// (Salyts). Runs on every layout pass of injectionParent once wired -
// two things it keeps in sync:
//  1. Pushes trackedElement's own Margin (Left or Right, matching
//     trackSide) out by root's current width + a gap, so the two don't
//     overlap - recomputed from trackedElementOriginalMargin each time
//     (not incrementally adjusted) so repeated calls can't drift.
//  2. Reads trackedElement's live on-screen position via
//     TransformToVisual and sets root's own Margin.Left to sit flush
//     against it (left of it, or right of it, per trackSide).
// Every write is gated on a >1px change from the previous value - two
// XAML elements' LayoutUpdated handlers both writing Margin on every
// single pass, even when nothing actually moved, risks a layout
// feedback loop; the threshold breaks it, matching the reference mod's
// own guard.
void UpdateTrackedPosition() {
    if (!g_ui.root || !g_ui.trackedElement || !g_ui.injectionParent) {
        return;
    }
    try {
        double edgeGap = (double)g_settings.layoutEdgeGap;
        double desiredGap = g_ui.root.ActualWidth() + edgeGap;

        auto margin = g_ui.hasTrackedElementOriginalMargin
                          ? g_ui.trackedElementOriginalMargin
                          : g_ui.trackedElement.Margin();
        if (g_ui.trackSide == L"left") {
            margin.Left = desiredGap;
        } else {
            margin.Right = desiredGap;
        }
        auto currentMargin = g_ui.trackedElement.Margin();
        if (std::abs(currentMargin.Left - margin.Left) > 1.0 ||
            std::abs(currentMargin.Right - margin.Right) > 1.0) {
            g_ui.trackedElement.Margin(margin);
        }

        auto transform =
            g_ui.trackedElement.TransformToVisual(g_ui.injectionParent);
        auto point = transform.TransformPoint({0, 0});
        double leftPos = g_ui.trackSide == L"left"
                              ? point.X - desiredGap + edgeGap
                              : point.X + g_ui.trackedElement.ActualWidth() +
                                    edgeGap;
        auto rootMargin = g_ui.root.Margin();
        if (std::abs(rootMargin.Left - leftPos) > 1.0) {
            g_ui.root.Margin({leftPos, 0, 0, 0});
        }
    } catch (...) {
    }
}

// Right-click "Reset position" - forces the stack's on-screen position
// to be recomputed from scratch instead of waiting for whatever would
// normally trigger it. For a tracking position this just calls
// UpdateTrackedPosition() early/on-demand (it self-corrects every
// layout pass anyway, so this is mostly for immediate user-visible
// feedback after, e.g., the taskbar visibly glitched). For the static
// edge positions this actually fixes something real: right_edge's
// trayGap is computed once at injection and goes stale if the system
// tray's own width changes later (icons added/removed) - a documented
// limitation in InjectWidgetStackGrid's own comment - so recomputing it
// here from the tray's current width is the whole point, not just a
// no-op refresh. center_edge needs no margin (alignment alone centers
// it) so it's left untouched.
void ResetStackPosition() {
    if (!g_ui.root) {
        return;
    }
    try {
        if (g_ui.trackedElement) {
            UpdateTrackedPosition();
            return;
        }
        double edgeGap = (double)g_settings.layoutEdgeGap;
        if (g_settings.layoutPosition == L"right_edge") {
            double trayGap = edgeGap;
            if (g_ui.injectionParent) {
                auto rootElement =
                    VisualTreeHelper::GetParent(g_ui.injectionParent)
                        .try_as<FrameworkElement>();
                if (rootElement) {
                    if (auto trayFrame = FindSystemTrayFrameGrid(rootElement)) {
                        trayGap += trayFrame.ActualWidth();
                    }
                }
            }
            g_ui.root.Margin({0, 0, trayGap, 0});
        } else if (g_settings.layoutPosition != L"center_edge") {
            g_ui.root.Margin({edgeGap, 0, 0, 0});
        }
    } catch (...) {
    }
}

// Builds the full widget-stack element (dots column + clipped, slidable
// widget panes) and adds it as a floating child of the taskbar's
// RootGrid, positioned per layout.position (an edge, or tracking a
// specific taskbar button - see the comments above kEdgeGap and
// UpdateTrackedPosition). Must run on the taskbar's own
// UI thread (same requirement as touching any XAML element - see
// PLAN.md's "Incident" section).
bool InjectWidgetStackGrid(HWND hWnd) {
    if (g_ui.root) {
        return true;  // Already injected for this taskbar instance.
    }

    auto xamlRoot = GetTaskbarXamlRoot(hWnd);
    if (!xamlRoot) {
        return false;
    }
    auto rootElement = xamlRoot.Content().try_as<FrameworkElement>();
    if (!rootElement) {
        return false;
    }
    auto taskbarRootGrid = FindTaskbarRootGrid(rootElement);
    if (!taskbarRootGrid) {
        return false;
    }
    // Used only as a readiness check (confirms the taskbar's own content
    // - not just RootGrid itself - has been realized in the visual tree)
    // before we inject; positioning no longer depends on it.
    auto repeater = FindChildByName(taskbarRootGrid, L"TaskbarFrameRepeater");
    auto startButton = FindStartButton(repeater);
    if (!repeater || !startButton) {
        return false;
    }

    // Tracking positions (Incident 38) anchor to one specific element
    // found by ResolveTrackingAnchor - resolved before the try block so
    // a not-found anchor (element missing on this Windows build/taskbar
    // config) falls back to left_edge's static placement below rather
    // than injecting anchored to nothing.
    std::wstring trackSide;
    FrameworkElement trackAnchor = ResolveTrackingAnchor(
        rootElement, repeater, g_settings.layoutPosition, trackSide);
    if (!trackSide.empty() && !trackAnchor) {
        Wh_Log(L"ResolveTrackingAnchor: target not found for %s, falling "
               L"back to left_edge",
               g_settings.layoutPosition.c_str());
    }

    try {
        Grid root;
        root.VerticalAlignment(VerticalAlignment::Stretch);
        // Full-bounds invisible hit area (Incident 47; same reasoning as
        // Incident 4's dot-wrapper fix, applied at the root level): a
        // Grid with no Background at all isn't hit-testable in its own
        // right - only already-hit-testable
        // descendants (a widget's own painted Border/bar) are. That left
        // the gaps between bars, the space around the dots, and the
        // padding columns as dead zones where wheel-scroll/right-click
        // silently did nothing, even though the cursor was still well
        // within the stack's bounds. An alpha-0 Background makes the
        // entire root rectangle hit-testable without changing how it
        // looks, the same trick taskbar-widget-media-player's own
        // "Full-height invisible hit area" setting uses.
        root.Background(SolidColorBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)});
        // Edge placement (Incident 37) - static HorizontalAlignment +
        // Margin only, computed once here. Tracking placement (Incident
        // 38, below, after root is added to the tree) anchors instead to
        // a specific element (Start/Search/Task View/Widgets button),
        // live, via UpdateTrackedPosition on every layout pass -
        // possible now because it targets one specific button rather
        // than an entire container the way Incidents 4-6's earlier,
        // abandoned tracking attempts did (TaskbarFrameRepeater as a
        // whole also contains every pinned/running icon, not just the
        // leading buttons, which is what actually went wrong then).
        double edgeGap = (double)g_settings.layoutEdgeGap;
        if (trackAnchor) {
            // Left-aligned with Margin.Left computed by
            // UpdateTrackedPosition once this is in the tree and a
            // first layout pass has run; edgeGap here is just a
            // placeholder until then.
            root.HorizontalAlignment(HorizontalAlignment::Left);
            root.Margin({edgeGap, 0, 0, 0});
        } else if (g_settings.layoutPosition == L"center_edge") {
            root.HorizontalAlignment(HorizontalAlignment::Center);
        } else if (g_settings.layoutPosition == L"right_edge") {
            root.HorizontalAlignment(HorizontalAlignment::Right);
            // Static, computed once here rather than tracked live -
            // avoids overlapping the system tray/clock, which sits at
            // the taskbar's actual right edge. If the tray's own width
            // changes later (icons added/removed) this margin goes
            // stale until the next injection (Explorer restart, display
            // change, or toggling this setting) - a known limitation,
            // consistent with every other edge position here being
            // static rather than live-tracked.
            double trayGap = edgeGap;
            // Incident 41's fix: SystemTrayFrameGrid isn't a descendant
            // of taskbarRootGrid (this lookup used to search there and
            // silently never find it - see FindSystemTrayFrameGrid's
            // comment), so this gap was actually always just edgeGap in
            // practice, not edgeGap + the tray's width as intended.
            if (auto trayFrame = FindSystemTrayFrameGrid(rootElement)) {
                trayGap += trayFrame.ActualWidth();
            }
            root.Margin({0, 0, trayGap, 0});
        } else {
            root.HorizontalAlignment(HorizontalAlignment::Left);
            root.Margin({edgeGap, 0, 0, 0});
        }
        // Placeholder width - RebuildStackContents (called below) applies
        // the real width immediately, once widgets have reported their
        // minimum widths.
        root.Width(kMinContentWidth + kDotsColumnWidth +
                   g_settings.layoutIndicatorGap +
                   g_settings.layoutRightPadding);
        // Four columns, always in this order (0/1/2/3) - which of 0/2
        // plays the "dots" vs. "content" role, and column1's gap width,
        // are decided live in ApplyStackWidth() from the current
        // settings (see UiState::column0's comment) rather than fixed
        // here; the initial widths just need to be non-degenerate before
        // that first call. Column 3 is the empty trailing padding column
        // (see UiState::paddingColumn's comment).
        ColumnDefinition column0;
        column0.Width({kDotsColumnWidth, GridUnitType::Pixel});
        ColumnDefinition gapCol;
        gapCol.Width({(double)g_settings.layoutIndicatorGap,
                       GridUnitType::Pixel});
        ColumnDefinition column2;
        column2.Width({1.0, GridUnitType::Star});
        ColumnDefinition paddingCol;
        paddingCol.Width({(double)g_settings.layoutRightPadding,
                           GridUnitType::Pixel});
        root.ColumnDefinitions().Append(column0);
        root.ColumnDefinitions().Append(gapCol);
        root.ColumnDefinitions().Append(column2);
        root.ColumnDefinitions().Append(paddingCol);

        StackPanel dotsPanel;
        dotsPanel.VerticalAlignment(VerticalAlignment::Center);
        dotsPanel.HorizontalAlignment(HorizontalAlignment::Center);
        Grid::SetColumn(dotsPanel, g_settings.layoutIndicatorOnRight ? 2 : 0);
        root.Children().Append(dotsPanel);

        Border clipHost;
        clipHost.Height(PaneHeight());
        clipHost.Width(kMinContentWidth);
        clipHost.VerticalAlignment(VerticalAlignment::Center);
        RectangleGeometry clipGeom;
        clipGeom.Rect(
            {0, 0, (float)kMinContentWidth, (float)PaneHeight()});
        clipHost.Clip(clipGeom);
        Grid::SetColumn(clipHost, g_settings.layoutIndicatorOnRight ? 0 : 2);

        StackPanel widgetsPanel;
        widgetsPanel.Orientation(Orientation::Vertical);
        CompositeTransform transform;
        widgetsPanel.RenderTransform(transform);
        clipHost.Child(widgetsPanel);
        root.Children().Append(clipHost);

        Grid::SetColumn(root, 0);
        Canvas::SetZIndex(root, 1000);
        taskbarRootGrid.Children().Append(root);

        g_ui.hWnd = hWnd;
        g_ui.ownerThreadId = GetCurrentThreadId();
        g_ui.injectionParent = taskbarRootGrid;
        g_ui.root = root;
        g_ui.widgetsPanel = widgetsPanel;
        g_ui.dotsPanel = dotsPanel;
        g_ui.column0 = column0;
        g_ui.gapColumn = gapCol;
        g_ui.column2 = column2;
        g_ui.paddingColumn = paddingCol;
        g_ui.clipHost = clipHost;
        g_ui.clipGeom = clipGeom;
        g_ui.sliderTransform = transform;

        if (trackAnchor) {
            g_ui.trackedElement = trackAnchor;
            g_ui.trackedElementOriginalMargin = trackAnchor.Margin();
            g_ui.hasTrackedElementOriginalMargin = true;
            g_ui.trackSide = trackSide;
            g_ui.layoutUpdatedToken = taskbarRootGrid.LayoutUpdated(
                [](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Windows::Foundation::IInspectable const&) {
                    UpdateTrackedPosition();
                });
            g_ui.layoutUpdatedWired = true;
        }

        WireUpNavigation();
        RebuildStackContents();
        if (trackAnchor) {
            // Applies an initial position immediately rather than
            // waiting for the next natural layout pass to fire
            // LayoutUpdated - avoids a one-frame flash at the
            // placeholder Margin set above.
            UpdateTrackedPosition();
        }

        if (!g_ui.windowSubclassed) {
            g_ui.windowSubclassed = WindhawkUtils::SetWindowSubclassFromAnyThread(
                hWnd, TaskbarWindowSubclassProc, 0);
        }

        if (!g_rawInputRegistered) {
            // Raw HID input from the Precision Touchpad ("Incident 17") -
            // see TaskbarWindowSubclassProc's WM_INPUT comment.
            // RIDEV_INPUTSINK: receive input regardless of foreground
            // focus, matching the fact real mouse wheel already works
            // here without needing focus/click.
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x0D;  // Digitizer
            rid.usUsage = 0x05;      // Touch Pad
            rid.dwFlags = RIDEV_INPUTSINK;
            rid.hwndTarget = hWnd;
            if (RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
                g_rawInputRegistered = true;
            } else {
                Wh_Log(L"RegisterRawInputDevices failed: %lu", GetLastError());
            }
        }

        // Publish the registration functions on the taskbar's own HWND so
        // widget-owning mods (e.g. taskbar-widget-system-usage) can find
        // this mod without guessing its DLL filename - mirrors this
        // file's own GetPropW(hTaskbarWnd, L"TaskbandHWND") read of
        // taskbar.dll's state. Set once per injection; RemoveWidgetStackGrid
        // clears both on teardown.
        SetPropW(hWnd, kRegisterWidgetPropName,
                 (HANDLE)&WidgetStack_RegisterWidget);
        SetPropW(hWnd, kUnregisterWidgetPropName,
                 (HANDLE)&WidgetStack_UnregisterWidget);

        Wh_Log(L"Injected widget stack");
        return true;
    } catch (...) {
        Wh_Log(L"InjectWidgetStackGrid: exception");
        UnwireTracking(/*restoreMargin=*/true);
        g_ui = {};
        return false;
    }
}

void RemoveWidgetStackGrid() {
    // Tears down every widget (timers/tokens included) regardless of
    // whether the grid itself is currently injected - a widget's
    // Create() may have run in an earlier RebuildStackContents even if
    // g_ui.root was since cleared for some other reason, and Destroy()
    // is safe to call unconditionally either way (Incident 30 - added
    // once SystemUsageWidget became the first widget to actually hold
    // a resource, a DispatcherTimer, that a skipped Destroy() would
    // leak across a Windhawk DLL unload).
    for (auto& entry : g_widgets) {
        try {
            entry.widget->Destroy();
        } catch (...) {
        }
    }

    if (g_ui.hWnd) {
        RemovePropW(g_ui.hWnd, kRegisterWidgetPropName);
        RemovePropW(g_ui.hWnd, kUnregisterWidgetPropName);
    }

    UnwireTracking(/*restoreMargin=*/true);

    if (!g_ui.root || !g_ui.injectionParent) {
        g_ui = {};
        return;
    }
    try {
        StopSnapAnimation();
        UnwireNavigation();
        auto rootGrid = g_ui.injectionParent;
        uint32_t rootIndex;
        if (rootGrid.Children().IndexOf(g_ui.root, rootIndex)) {
            rootGrid.Children().RemoveAt(rootIndex);
        }
    } catch (...) {
        Wh_Log(L"RemoveWidgetStackGrid: exception");
    }
    g_ui = {};
}

DWORD WINAPI RetryInjectThreadProc(LPVOID) {
    for (int attempt = 0; attempt < 600; attempt++) {
        if (g_stopRequested.load(std::memory_order_acquire)) {
            return 0;
        }
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (tray) {
            g_taskbarWnd = tray;
            bool ok = false;
            RunFromWindowThread(tray, [&] { ok = InjectWidgetStackGrid(tray); });
            if (ok) {
                return 0;
            }
        }
        WaitForSingleObject(g_injectEvent, 500);
        ResetEvent(g_injectEvent);
    }
    Wh_Log(L"Giving up on widget stack injection");
    return 0;
}

void StartRetryInject() {
    std::lock_guard<std::mutex> lk(g_retryThreadMutex);
    if (g_stopRequested.load(std::memory_order_acquire)) {
        return;
    }
    if (g_retryThread &&
        WaitForSingleObject(g_retryThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_retryThread);
        g_retryThread = nullptr;
    }
    if (!g_retryThread) {
        g_retryThread =
            CreateThread(nullptr, 0, RetryInjectThreadProc, nullptr, 0, nullptr);
    } else if (g_injectEvent) {
        SetEvent(g_injectEvent);
    }
}

void WINAPI TrayUI_StartTaskbar_Hook(void* pThis) {
    TrayUI_StartTaskbar_Original(pThis);
    if (g_stopRequested.load(std::memory_order_acquire)) {
        return;
    }
    RunFromWindowThread(g_taskbarWnd ? g_taskbarWnd
                                      : FindWindowW(L"Shell_TrayWnd", nullptr),
                         RemoveWidgetStackGrid);
    StartRetryInject();
}

std::wstring GetStringSetting(const wchar_t* name, const wchar_t* fallback) {
    PCWSTR value = Wh_GetStringSetting(name);
    std::wstring result = value ? value : fallback;
    if (value) {
        Wh_FreeStringSetting(value);
    }
    return result;
}

void LoadSettings() {
    g_settings.navWheel = Wh_GetIntSetting(L"nav.wheel");
    g_settings.navDots = Wh_GetIntSetting(L"nav.dots");
    g_settings.navDrag = Wh_GetIntSetting(L"nav.drag");
    g_settings.navWrap = Wh_GetIntSetting(L"nav.wrap");
    g_settings.navOverscroll = Wh_GetIntSetting(L"nav.overscroll");
    int minWidth = Wh_GetIntSetting(L"layout.minWidth");
    g_settings.layoutMinWidth = minWidth > 0 ? minWidth : 80;
    int maxWidth = Wh_GetIntSetting(L"layout.maxWidth");
    g_settings.layoutMaxWidth = maxWidth > 0 ? maxWidth : 520;
    int paneHeight = Wh_GetIntSetting(L"layout.paneHeight");
    g_settings.layoutPaneHeight = paneHeight > 0 ? paneHeight : 56;
    g_settings.layoutIndicatorVisible =
        Wh_GetIntSetting(L"layout.indicator.visible");
    g_settings.layoutHideIndicatorWhenSingle =
        Wh_GetIntSetting(L"layout.indicator.hideWhenSingle");
    int gap = Wh_GetIntSetting(L"layout.indicator.gap");
    g_settings.layoutIndicatorGap = gap >= 0 ? gap : 6;
    g_settings.layoutIndicatorOnRight =
        Wh_GetIntSetting(L"layout.indicator.onRight");
    g_settings.layoutPosition = GetStringSetting(L"layout.position", L"left_edge");
    int edgeGap = Wh_GetIntSetting(L"layout.edgeGap");
    g_settings.layoutEdgeGap = edgeGap >= 0 ? edgeGap : 6;
    int rightPadding = Wh_GetIntSetting(L"layout.rightPadding");
    g_settings.layoutRightPadding = rightPadding >= 0 ? rightPadding : 6;

    // Private store (Incident 26) overrides the above whenever a key
    // exists there - it's the real source of truth once the settings
    // window has saved anything, since Windhawk's own settings.json
    // can only be read, never written, from mod code.
    DWORD v;
    if (ReadPrivateDword(L"nav.wheel", v)) {
        g_settings.navWheel = v != 0;
    }
    if (ReadPrivateDword(L"nav.dots", v)) {
        g_settings.navDots = v != 0;
    }
    if (ReadPrivateDword(L"nav.drag", v)) {
        g_settings.navDrag = v != 0;
    }
    if (ReadPrivateDword(L"nav.wrap", v)) {
        g_settings.navWrap = v != 0;
    }
    if (ReadPrivateDword(L"nav.overscroll", v)) {
        g_settings.navOverscroll = v != 0;
    }
    if (ReadPrivateDword(L"layout.minWidth", v) && v > 0) {
        g_settings.layoutMinWidth = (int)v;
    }
    if (ReadPrivateDword(L"layout.maxWidth", v) && v > 0) {
        g_settings.layoutMaxWidth = (int)v;
    }
    if (ReadPrivateDword(L"layout.paneHeight", v) && v > 0) {
        g_settings.layoutPaneHeight = (int)v;
    }
    if (ReadPrivateDword(L"layout.indicator.visible", v)) {
        g_settings.layoutIndicatorVisible = v != 0;
    }
    if (ReadPrivateDword(L"layout.indicator.hideWhenSingle", v)) {
        g_settings.layoutHideIndicatorWhenSingle = v != 0;
    }
    if (ReadPrivateDword(L"layout.indicator.gap", v)) {
        g_settings.layoutIndicatorGap = (int)v;
    }
    if (ReadPrivateDword(L"layout.indicator.onRight", v)) {
        g_settings.layoutIndicatorOnRight = v != 0;
    }
    std::wstring pos;
    if (ReadPrivateString(L"layout.position", pos) && !pos.empty()) {
        g_settings.layoutPosition = pos;
    }
    if (ReadPrivateDword(L"layout.edgeGap", v)) {
        g_settings.layoutEdgeGap = (int)v;
    }
    if (ReadPrivateDword(L"layout.rightPadding", v)) {
        g_settings.layoutRightPadding = (int)v;
    }
}

void InitPlaceholderWidgets() {
    g_widgets.clear();

    // 2x kMinContentWidth (user request, 2026-09-17) - a placeholder
    // stand-in for the real media player widget, which will need
    // meaningfully more width than a plain label (album art, controls).
    WidgetEntry mediaPlayer;
    mediaPlayer.widget = std::make_unique<PlaceholderWidget>(
        L"placeholder-a", L"Media\nPlayer",
        winrt::Windows::UI::ColorHelper::FromArgb(255, 70, 90, 160),
        kMinContentWidth * 2);
    g_widgets.push_back(std::move(mediaPlayer));

    WidgetEntry aiQuota;
    aiQuota.widget = std::make_unique<PlaceholderWidget>(
        L"placeholder-b", L"AI\nQuota",
        winrt::Windows::UI::ColorHelper::FromArgb(255, 90, 150, 90),
        kMinContentWidth);
    g_widgets.push_back(std::move(aiQuota));

    LoadWidgetOrderState();
}

}  // namespace

// Cross-mod widget ABI entry points (see the "Cross-mod widget ABI"
// section above for the design). Defined out here, matching how
// Wh_ModInit/Wh_ModAfterInit/Wh_ModSettingsChanged/Wh_ModBeforeUninit are
// already handled in this file - kept outside the anonymous namespace so
// their extern "C" names are unambiguous, while still able to see
// g_widgets/RebuildStackContents/etc. via the implicit using-directive an
// anonymous namespace leaves behind from its point of declaration onward
// in this translation unit (the same mechanism that already lets
// Wh_ModInit below call LoadSettings()/InitPlaceholderWidgets()).
extern "C" bool __cdecl WidgetStack_RegisterWidget(
    const WidgetStackWidgetAbiV1* widget) {
    if (!widget) {
        return false;
    }
    try {
        // Incident 46 (PLAN.md): register a context that's already present
        // used to always push_back a second WidgetEntry for it - visible
        // as a duplicated widget that WidgetStack_UnregisterWidget's
        // find_if (stops at the first match) could only ever remove ONE
        // copy of, so it kept reappearing even after disabling it. Treat
        // a re-registration of an already-known context as "refresh the
        // ABI in place", not "add another one".
        auto existing = std::find_if(
            g_widgets.begin(), g_widgets.end(), [&](const WidgetEntry& e) {
                auto* remote = dynamic_cast<RemoteWidget*>(e.widget.get());
                return remote && remote->Context() == widget->context;
            });
        if (existing != g_widgets.end()) {
            existing->widget = std::make_unique<RemoteWidget>(*widget);
            existing->crashed = false;
            RebuildStackContents();
            Wh_Log(L"WidgetStack_RegisterWidget: context already registered, "
                   L"refreshed instead of duplicating");
            return true;
        }

        WidgetEntry entry;
        entry.widget = std::make_unique<RemoteWidget>(*widget);
        g_widgets.push_back(std::move(entry));
        // Incident 40 (PLAN.md): a remote widget's own settings changing
        // makes its owning mod unregister then immediately re-register
        // it here - without this, the freshly pushed_back entry always
        // lands at the END of g_widgets, silently undoing any order the
        // user set via move up/down before that settings change.
        // LoadWidgetOrderState (already written for Wh_ModInit's initial
        // load) re-sorts g_widgets to match the last-saved order by Id(),
        // which is exactly what a re-registered widget needs too - it's
        // a no-op for a widget that's never been reordered/saved before
        // (falls through to its already-correct end-of-list position).
        LoadWidgetOrderState();
        RebuildStackContents();
        Wh_Log(L"WidgetStack_RegisterWidget: registered a remote widget");
        return true;
    } catch (...) {
        Wh_Log(L"WidgetStack_RegisterWidget: exception");
        return false;
    }
}

extern "C" void __cdecl WidgetStack_UnregisterWidget(void* context) {
    try {
        auto it = std::find_if(
            g_widgets.begin(), g_widgets.end(), [&](const WidgetEntry& e) {
                auto* remote = dynamic_cast<RemoteWidget*>(e.widget.get());
                return remote && remote->Context() == context;
            });
        if (it == g_widgets.end()) {
            return;
        }
        try {
            it->widget->Destroy();
        } catch (...) {
        }
        g_widgets.erase(it);
        RebuildStackContents();
        Wh_Log(L"WidgetStack_UnregisterWidget: unregistered a remote widget");
    } catch (...) {
        Wh_Log(L"WidgetStack_UnregisterWidget: exception");
    }
}

BOOL Wh_ModInit() {
    LoadSettings();
    InitPlaceholderWidgets();

    g_stopRequested = false;
    g_injectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_injectEvent) {
        return FALSE;
    }

    if (!HookTaskbarDllSymbols()) {
        Wh_Log(L"HookTaskbarDllSymbols failed");
        CloseHandle(g_injectEvent);
        g_injectEvent = nullptr;
        return FALSE;
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    StartRetryInject();
}

void Wh_ModSettingsChanged() {
    std::wstring previousPosition = g_settings.layoutPosition;
    int previousEdgeGap = g_settings.layoutEdgeGap;
    LoadSettings();
    // Unlike the other layout settings (indicator gap/onRight/maxWidth/
    // hideWhenSingle/rightPadding), which ApplyStackWidth already
    // re-reads live on every RebuildStackContents, layout.position and
    // the static edge positions' use of layout.edgeGap are only ever
    // applied once, at InjectWidgetStackGrid time (root's
    // HorizontalAlignment/Margin, not something RebuildStackContents
    // touches - edgeGap DOES apply live for the tracking positions,
    // via UpdateTrackedPosition, but not for left_edge/right_edge) - so
    // a change made through Windhawk's own native settings UI (as
    // opposed to this mod's private in-app settings window, whose
    // controls call RebuildStackContents/remove-then-inject themselves)
    // needs an explicit remove-then-inject here to actually take effect.
    if ((g_settings.layoutPosition != previousPosition ||
         g_settings.layoutEdgeGap != previousEdgeGap) &&
        g_ui.hWnd) {
        HWND hWnd = g_ui.hWnd;
        RunFromWindowThread(hWnd, [] {
            RemoveWidgetStackGrid();
            InjectWidgetStackGrid(g_taskbarWnd);
        });
    }
}

void Wh_ModBeforeUninit() {
    g_stopRequested = true;
    // Closed explicitly rather than left for the DLL unload to sort
    // out - matches this file's established discipline (see the
    // widget stack's own teardown below) of never leaving a XAML-
    // hosting HWND or WinRT object referencing this DLL's code alive
    // across a Windhawk reload.
    CloseSettingsWindow();
    if (g_injectEvent) {
        SetEvent(g_injectEvent);
    }
    if (g_rawInputRegistered) {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x0D;
        rid.usUsage = 0x05;
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = nullptr;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        g_rawInputRegistered = false;
    }
    {
        std::lock_guard<std::mutex> lk(g_retryThreadMutex);
        if (g_retryThread) {
            WaitForSingleObject(g_retryThread, 3000);
            CloseHandle(g_retryThread);
            g_retryThread = nullptr;
        }
    }

    if (g_ui.root && g_ui.hWnd) {
        HWND hWnd = g_ui.hWnd;
        if (g_ui.windowSubclassed) {
            WindhawkUtils::RemoveWindowSubclassFromAnyThread(
                hWnd, TaskbarWindowSubclassProc);
        }
        RunFromWindowThread(hWnd, RemoveWidgetStackGrid);
    }
    g_ui = {};

    if (g_injectEvent) {
        CloseHandle(g_injectEvent);
        g_injectEvent = nullptr;
    }
}

void Wh_ModUninit() {}
