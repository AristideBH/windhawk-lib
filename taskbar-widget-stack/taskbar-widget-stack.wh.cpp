// ==WindhawkMod==
// @id              taskbar-widget-stack
// @name            Taskbar Widget Stack
// @description     Stack multiple taskbar widgets vertically in one snap-scrollable pane, iOS-widget-stack style
// @version         0.1.40
// @author          AristideBH
// @github          https://github.com/AristideBH
// @homepage        https://aristide-bh.com/
// @license         MIT
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32 -lcomctl32 -ladvapi32 -ldwmapi -lpdh
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
- **Right-click** the stack for a menu to enable/disable widgets and move
  them up/down in the stack order.

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
  - maxWidth: 520
    $name: Maximum stack width
    $description: >-
      Upper bound, in pixels, for how wide the widget stack can grow to fit
      its widest enabled widget. Widgets narrower than this stretch to fill
      it.
  - indicator:
    - hideWhenSingle: true
      $name: Hide when only one widget
      $description: >-
        Don't show the dot indicator column when there's only one enabled
        widget - it has nothing to indicate.
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
#include <pdh.h>
#include <pdhmsg.h>

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
// the two placeholder widgets report as their own desired width, so
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
// desired width), and full responsibility for revoking its own event
// tokens/timers on Destroy().
class IWidget {
   public:
    virtual ~IWidget() = default;

    virtual std::wstring Id() const = 0;
    virtual std::wstring DisplayName() const = 0;

    // Builds this widget's own root element, attaches it under
    // host.parent, and returns the width (DIPs) it wants. The host
    // sizes the stack to the widest enabled widget's returned width
    // (capped at the user's layout.maxWidth setting) - a widget
    // narrower than that gets stretched to fill it via the default
    // HorizontalAlignment::Stretch, so it should not set its own fixed
    // Width().
    virtual double Create(const WidgetHost& host) = 0;

    // Called on the stack's shared tick signal. No host-side timer
    // drives this yet - deferred until a real ported widget actually
    // needs periodic redraw, rather than running an idle DispatcherTimer
    // with no consumer. A widget needing its own cadence (e.g. a 16ms
    // visualizer, matching taskbar-fluent-media-player) owns that timer
    // internally instead, same as both reference mods already do.
    virtual void Tick() = 0;

    // Re-reads this widget's own settings sub-namespace and rebuilds
    // internally, returning its (possibly new) desired width. The host
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
    // OnSettingsChanged() call, used to compute the stack's width.
    double desiredWidth = 0.0;
};

std::vector<WidgetEntry> g_widgets;

struct {
    bool navWheel = true;
    bool navDots = true;
    bool navDrag = true;
    bool navWrap = true;
    bool navOverscroll = true;
    int layoutMaxWidth = 520;
    bool layoutHideIndicatorWhenSingle = true;
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
    // Collapsed to 0 width by ApplyStackWidth() when
    // layout.indicator.hideWhenSingle is on and only one widget is
    // enabled - see Incident 25.
    ColumnDefinition dotsColumn{nullptr};
    CompositeTransform sliderTransform{nullptr};
    int activeIndex = 0;
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

// Bumped from 32 to 76 (Incident 30): the new SystemUsageWidget needs
// three readable bar rows, and every pane must share one height (the
// slider offset math is `-widgetIndex * PaneHeight()`), so this
// affects every widget's pane, including the two placeholders (now
// visually tall/empty by comparison - acceptable, they were always
// meant to be temporary).
double PaneHeight() {
    return 76.0;
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
// Shared by the settings window's Navigation/Layout tabs and any
// widget's own BuildSettingsPanel() (Incident 31) - forward-declared
// here since SystemUsageWidget, defined before the settings window
// section, needs it too.
ToggleSwitch MakeSettingsToggle(std::wstring header,
                                 bool initial,
                                 std::function<void(bool)> onChanged);

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
                       double desiredWidth)
        : id_(std::move(id)),
          displayName_(std::move(displayName)),
          color_(color),
          desiredWidth_(desiredWidth) {}

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
        return desiredWidth_;
    }

    void Tick() override {}

    double OnSettingsChanged() override {
        // No per-widget settings exist for placeholders - nothing to
        // re-read, size stays the same.
        return desiredWidth_;
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
    double desiredWidth_;
    Border root_{nullptr};
    Panel parent_{nullptr};
};

// This SDK's first non-placeholder widget (Incident 30) - three
// horizontal bars (CPU/RAM/GPU, label left, percent right). Each
// metric comes from a different WinAPI:
// - CPU: delta of GetSystemTimes() samples between ticks (the
//   standard technique - GetSystemTimes' own "kernel time" already
//   includes idle time, so busy% = (totalDelta - idleDelta) /
//   totalDelta).
// - RAM: GlobalMemoryStatusEx's dwMemoryLoad - already a percentage,
//   no math needed.
// - GPU: PDH's "\GPU Engine(*)\Utilization Percentage" wildcard
//   counter - the same technique Task Manager's own GPU graphs use.
//   The only one of the three that can fail to set up at all (some
//   GPU drivers don't expose these counters) - handled by showing
//   "N/A" on just that row rather than failing the whole widget.
//   Simplification: takes the MAX across all reported engine
//   instances rather than Task Manager's more selective per-engine-
//   type accounting, so this can read a little differently from Task
//   Manager's own GPU% on some systems - close enough for a simple
//   bar, not presented as an exact match.
//
// Owns its own DispatcherTimer for polling, per IWidget::Tick()'s own
// contract ("a widget needing its own cadence owns that timer
// internally") rather than a host-driven tick that doesn't exist yet.
// The Tick() lambda captures `this` by raw pointer - safe only because
// Destroy() (via StopTimer()) revokes that delegate before the widget
// object itself can be destructed, which is why Wh_ModBeforeUninit was
// also changed (this same commit) to call every widget's Destroy() on
// full mod unload - a gap that existed since Incident 5's IWidget
// design but was never exercised until a widget actually held a
// timer.
class SystemUsageWidget : public IWidget {
   public:
    std::wstring Id() const override { return L"system-usage"; }
    std::wstring DisplayName() const override { return L"System\nUsage"; }

    double Create(const WidgetHost& host) override {
        LoadSettingsFields();

        // Incident 32: `root` must be exactly `host.paneHeight` tall
        // (every widget's pane has to be, for the slider's
        // `-widgetIndex * PaneHeight()` offset math to line up), but
        // setting VerticalAlignment on `root` itself doesn't center
        // its own children within that fixed height - VerticalAlignment
        // describes how an element sits in the space *its parent*
        // gives it, not how its own children sit within it. That was
        // the actual bug behind the clipped CPU row (rows top-aligned
        // within the full 76px box, so with the CPU/RAM/GPU block
        // shorter than 76px, everything just sat at the top with dead
        // space below - fine when nothing was hidden, but with CPU
        // cut off it looked like the whole block was pushed up too
        // far). Fix: `root` (fixed height) holds one child, `content`
        // (a plain vertical StackPanel, natural/Auto height), and
        // *that* child's own VerticalAlignment(Center) is what
        // actually centers it within root's fixed height - one level
        // removed from where it was set before.
        Grid root;
        root.Height(host.paneHeight);

        StackPanel content;
        content.Orientation(Orientation::Vertical);
        content.VerticalAlignment(VerticalAlignment::Center);
        content.Padding({4, 0, 4, 0});

        if (showCpu_) {
            BuildRow(content, L"CPU", cpuFillCol_, cpuEmptyCol_,
                     cpuPercentText_);
        }
        if (showRam_) {
            BuildRow(content, L"RAM", ramFillCol_, ramEmptyCol_,
                     ramPercentText_);
        }
        if (showGpu_) {
            BuildRow(content, L"GPU", gpuFillCol_, gpuEmptyCol_,
                     gpuPercentText_);
        }

        root.Children().Append(content);
        host.parent.Children().Append(root);
        root_ = root;
        parent_ = host.parent;

        InitPdh();
        SampleCpu();  // primes the delta baseline, first return unused
        UpdateValues();
        StartTimer();
        return kDesiredWidth;
    }

    void Tick() override {}

    double OnSettingsChanged() override { return kDesiredWidth; }

    bool HasSettings() const override { return true; }

    // Built on demand (Incident 31) - shown when the settings window's
    // Widgets tab gear button for this entry is clicked. Each control
    // persists directly to this widget's own private-store keys (under
    // "widget.system-usage.*", separate from the stack-wide nav/layout
    // keys) and calls RebuildStackContents() to take effect
    // immediately - the same pattern ToggleWidgetEnabled/MoveWidget
    // already use, rather than inventing a second apply path.
    FrameworkElement BuildSettingsPanel() override {
        StackPanel panel;
        panel.Orientation(Orientation::Vertical);
        panel.Margin({16, 16, 16, 16});
        panel.Spacing(12);

        TextBlock heading;
        heading.Text(L"System Usage");
        heading.FontSize(14);
        panel.Children().Append(heading);

        panel.Children().Append(MakeSettingsToggle(
            L"Show CPU", showCpu_, [](bool on) {
                WritePrivateDword(L"widget.system-usage.showCpu",
                                   on ? 1 : 0);
                RebuildStackContents();
            }));
        panel.Children().Append(MakeSettingsToggle(
            L"Show RAM", showRam_, [](bool on) {
                WritePrivateDword(L"widget.system-usage.showRam",
                                   on ? 1 : 0);
                RebuildStackContents();
            }));
        panel.Children().Append(MakeSettingsToggle(
            L"Show GPU", showGpu_, [](bool on) {
                WritePrivateDword(L"widget.system-usage.showGpu",
                                   on ? 1 : 0);
                RebuildStackContents();
            }));

        StackPanel refreshGroup;
        refreshGroup.Orientation(Orientation::Vertical);
        refreshGroup.Spacing(4);
        TextBlock refreshLabel;
        refreshLabel.Text(winrt::hstring(
            L"Refresh every " + std::to_wstring(refreshSeconds_) + L"s"));
        refreshGroup.Children().Append(refreshLabel);
        Slider refreshSlider;
        refreshSlider.Minimum(1);
        refreshSlider.Maximum(5);
        refreshSlider.StepFrequency(1);
        refreshSlider.Value(refreshSeconds_);
        refreshSlider.ValueChanged(
            [refreshLabel](
                winrt::Windows::Foundation::IInspectable const&,
                winrt::Windows::UI::Xaml::Controls::Primitives::
                    RangeBaseValueChangedEventArgs const& args) {
                int seconds = (int)args.NewValue();
                WritePrivateDword(L"widget.system-usage.refreshSeconds",
                                   (DWORD)seconds);
                refreshLabel.Text(winrt::hstring(
                    L"Refresh every " + std::to_wstring(seconds) + L"s"));
                // Takes effect on the next toggle/reorder-triggered
                // rebuild rather than immediately - this widget's own
                // timer interval isn't re-read live, and restarting it
                // here (without a full Destroy()/Create()) would be a
                // second, redundant apply path. Simple and honest
                // about the limitation rather than half-implementing
                // "live" for one setting and not others.
            });
        refreshGroup.Children().Append(refreshSlider);
        panel.Children().Append(refreshGroup);

        TextBlock note;
        note.Text(
            L"Refresh rate applies next time the stack rebuilds "
            L"(e.g. after toggling or reordering a widget).");
        note.FontSize(11);
        note.TextWrapping(TextWrapping::Wrap);
        SolidColorBrush noteBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(255, 160, 160, 160)};
        note.Foreground(noteBrush);
        panel.Children().Append(note);

        return panel;
    }

    void Destroy() override {
        StopTimer();
        ClosePdh();
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
    // Reduced from 170 (Incident 31): the internal layout already
    // uses Star-weighted columns for the bar track, so it stretches to
    // fill whatever width the stack ends up at (driven by
    // layout.maxWidth or a wider sibling widget) - this only needs to
    // be wide enough to read comfortably on its own, not to dictate
    // the stack's width.
    static constexpr double kDesiredWidth = 130.0;

    void BuildRow(Panel& content,
                  const wchar_t* label,
                  ColumnDefinition& fillCol,
                  ColumnDefinition& emptyCol,
                  TextBlock& percentText) {
        Grid row;
        // Tight, content-sized columns (Incident 31 - the first pass's
        // 30/32px fixed columns were wider than "CPU"/"100%" need,
        // leaving visible dead space on both sides of the bar) plus a
        // small margin between rows instead of relying on Star-height
        // rows to space them (which left uneven whitespace above/below
        // each row's actual content).
        row.Margin({0, 1, 0, 1});
        ColumnDefinition labelCol;
        labelCol.Width({22, GridUnitType::Pixel});
        ColumnDefinition trackCol;
        trackCol.Width({1.0, GridUnitType::Star});
        ColumnDefinition percentCol;
        percentCol.Width({26, GridUnitType::Pixel});
        row.ColumnDefinitions().Append(labelCol);
        row.ColumnDefinitions().Append(trackCol);
        row.ColumnDefinitions().Append(percentCol);

        TextBlock labelText;
        labelText.Text(winrt::hstring(label));
        labelText.FontSize(9);
        labelText.VerticalAlignment(VerticalAlignment::Center);
        SolidColorBrush textBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(255, 220, 220, 220)};
        labelText.Foreground(textBrush);
        Grid::SetColumn(labelText, 0);
        row.Children().Append(labelText);

        // Halved from 8 to 4 (user request, 2026-09-17).
        Border track;
        track.Height(4);
        track.Margin({4, 0, 4, 0});
        track.CornerRadius({2, 2, 2, 2});
        SolidColorBrush trackBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(255, 70, 70, 70)};
        track.Background(trackBrush);
        track.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(track, 1);

        fillCol = ColumnDefinition();
        fillCol.Width({0.0, GridUnitType::Star});
        emptyCol = ColumnDefinition();
        emptyCol.Width({100.0, GridUnitType::Star});
        Grid fillGrid;
        fillGrid.ColumnDefinitions().Append(fillCol);
        fillGrid.ColumnDefinitions().Append(emptyCol);
        Border fill;
        SolidColorBrush fillBrush{
            winrt::Windows::UI::ColorHelper::FromArgb(255, 90, 170, 230)};
        fill.Background(fillBrush);
        fill.CornerRadius({2, 2, 2, 2});
        Grid::SetColumn(fill, 0);
        fillGrid.Children().Append(fill);
        track.Child(fillGrid);
        row.Children().Append(track);

        percentText = TextBlock();
        percentText.Text(L"0%");
        percentText.FontSize(9);
        percentText.HorizontalAlignment(HorizontalAlignment::Left);
        percentText.VerticalAlignment(VerticalAlignment::Center);
        percentText.Foreground(textBrush);
        Grid::SetColumn(percentText, 2);
        row.Children().Append(percentText);

        content.Children().Append(row);
    }

    void ApplyBar(ColumnDefinition& fillCol,
                   ColumnDefinition& emptyCol,
                   TextBlock& percentText,
                   double percent) {
        percent = std::clamp(percent, 0.0, 100.0);
        try {
            fillCol.Width({percent, GridUnitType::Star});
            emptyCol.Width({100.0 - percent, GridUnitType::Star});
            wchar_t buf[8];
            wsprintfW(buf, L"%d%%", (int)std::lround(percent));
            percentText.Text(buf);
        } catch (...) {
        }
    }

    void UpdateValues() {
        // Only touches the rows actually built (Incident 31 - a
        // hidden metric's ColumnDefinition/TextBlock members stay
        // null, so calling ApplyBar on them would throw - caught, but
        // wasteful - skipping is simpler than relying on the catch).
        if (showCpu_) {
            ApplyBar(cpuFillCol_, cpuEmptyCol_, cpuPercentText_, SampleCpu());
        }
        if (showRam_) {
            ApplyBar(ramFillCol_, ramEmptyCol_, ramPercentText_, SampleRam());
        }
        if (showGpu_) {
            double gpu = pdhOk_ ? SampleGpu() : -1.0;
            if (gpu >= 0.0) {
                ApplyBar(gpuFillCol_, gpuEmptyCol_, gpuPercentText_, gpu);
            } else {
                ApplyBar(gpuFillCol_, gpuEmptyCol_, gpuPercentText_, 0.0);
                try {
                    gpuPercentText_.Text(L"N/A");
                } catch (...) {
                }
            }
        }
    }

    double SampleCpu() {
        FILETIME idleTime, kernelTime, userTime;
        if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
            return lastCpuPercent_;
        }
        auto toULL = [](const FILETIME& ft) -> ULONGLONG {
            ULARGE_INTEGER u;
            u.LowPart = ft.dwLowDateTime;
            u.HighPart = ft.dwHighDateTime;
            return u.QuadPart;
        };
        ULONGLONG idle = toULL(idleTime);
        ULONGLONG kernel = toULL(kernelTime);
        ULONGLONG user = toULL(userTime);

        if (haveCpuSample_) {
            ULONGLONG idleDelta = idle - lastIdle_;
            // GetSystemTimes' kernelTime already includes idle time.
            ULONGLONG totalDelta = (kernel - lastKernel_) + (user - lastUser_);
            if (totalDelta > 0) {
                double busy =
                    (double)(totalDelta - idleDelta) / (double)totalDelta;
                lastCpuPercent_ = std::clamp(busy * 100.0, 0.0, 100.0);
            }
        }
        lastIdle_ = idle;
        lastKernel_ = kernel;
        lastUser_ = user;
        haveCpuSample_ = true;
        return lastCpuPercent_;
    }

    double SampleRam() {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status)) {
            return (double)status.dwMemoryLoad;
        }
        return 0.0;
    }

    void InitPdh() {
        if (PdhOpenQueryW(nullptr, 0, &pdhQuery_) != ERROR_SUCCESS) {
            pdhOk_ = false;
            return;
        }
        if (PdhAddEnglishCounterW(pdhQuery_,
                                   L"\\GPU Engine(*)\\Utilization Percentage",
                                   0, &pdhCounter_) != ERROR_SUCCESS) {
            PdhCloseQuery(pdhQuery_);
            pdhQuery_ = nullptr;
            pdhOk_ = false;
            return;
        }
        // Primes the query - PDH counters need at least one prior
        // sample before a formatted value means anything.
        PdhCollectQueryData(pdhQuery_);
        pdhOk_ = true;
    }

    void ClosePdh() {
        if (pdhQuery_) {
            PdhCloseQuery(pdhQuery_);
            pdhQuery_ = nullptr;
        }
        pdhCounter_ = nullptr;
        pdhOk_ = false;
    }

    double SampleGpu() {
        if (PdhCollectQueryData(pdhQuery_) != ERROR_SUCCESS) {
            return -1.0;
        }
        DWORD bufferSize = 0, itemCount = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(
            pdhCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        if (status != PDH_MORE_DATA || bufferSize == 0) {
            return -1.0;
        }
        std::vector<BYTE> buffer(bufferSize);
        auto* items =
            reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(pdhCounter_, PDH_FMT_DOUBLE,
                                          &bufferSize, &itemCount,
                                          items) != ERROR_SUCCESS) {
            return -1.0;
        }
        double maxUsage = 0.0;
        for (DWORD i = 0; i < itemCount; i++) {
            if (items[i].FmtValue.CStatus == ERROR_SUCCESS) {
                maxUsage = std::max(maxUsage, items[i].FmtValue.doubleValue);
            }
        }
        return std::clamp(maxUsage, 0.0, 100.0);
    }

    void StartTimer() {
        timer_ = DispatcherTimer();
        timer_.Interval(std::chrono::seconds(std::max(1, refreshSeconds_)));
        timerToken_ = timer_.Tick(
            [this](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Windows::Foundation::IInspectable const&) {
                UpdateValues();
            });
        timer_.Start();
    }

    void StopTimer() {
        if (timer_) {
            try {
                if (timerToken_) {
                    timer_.Tick(timerToken_);
                }
                timer_.Stop();
            } catch (...) {
            }
            timer_ = nullptr;
        }
    }

    // Own settings (Incident 31) - separate keys from the stack-wide
    // nav/layout ones, under "widget.system-usage.*". Loaded fresh at
    // the start of every Create() (i.e. every rebuild), matching how
    // g_settings itself is loaded - simpler than trying to patch
    // already-built XAML in place when a value changes.
    void LoadSettingsFields() {
        DWORD v;
        showCpu_ = !ReadPrivateDword(L"widget.system-usage.showCpu", v) ||
                   v != 0;
        showRam_ = !ReadPrivateDword(L"widget.system-usage.showRam", v) ||
                   v != 0;
        showGpu_ = !ReadPrivateDword(L"widget.system-usage.showGpu", v) ||
                   v != 0;
        if (ReadPrivateDword(L"widget.system-usage.refreshSeconds", v) &&
            v >= 1 && v <= 5) {
            refreshSeconds_ = (int)v;
        }
    }

    Grid root_{nullptr};
    Panel parent_{nullptr};

    ColumnDefinition cpuFillCol_{nullptr};
    ColumnDefinition cpuEmptyCol_{nullptr};
    TextBlock cpuPercentText_{nullptr};

    ColumnDefinition ramFillCol_{nullptr};
    ColumnDefinition ramEmptyCol_{nullptr};
    TextBlock ramPercentText_{nullptr};

    ColumnDefinition gpuFillCol_{nullptr};
    ColumnDefinition gpuEmptyCol_{nullptr};
    TextBlock gpuPercentText_{nullptr};

    DispatcherTimer timer_{nullptr};
    winrt::event_token timerToken_;

    ULONGLONG lastIdle_ = 0;
    ULONGLONG lastKernel_ = 0;
    ULONGLONG lastUser_ = 0;
    bool haveCpuSample_ = false;
    double lastCpuPercent_ = 0.0;

    PDH_HQUERY pdhQuery_ = nullptr;
    PDH_HCOUNTER pdhCounter_ = nullptr;
    bool pdhOk_ = false;

    bool showCpu_ = true;
    bool showRam_ = true;
    bool showGpu_ = true;
    int refreshSeconds_ = 1;
};

void RefreshDots() {
    if (!g_ui.dotsPanel) {
        return;
    }
    g_ui.dotsPanel.Children().Clear();
    auto enabled = EnabledIndices();
    if (g_settings.layoutHideIndicatorWhenSingle && enabled.size() <= 1) {
        // The dots column itself is collapsed to 0 width by
        // ApplyStackWidth in this case - nothing to build.
        return;
    }
    for (int idx : enabled) {
        bool active = idx == g_ui.activeIndex;
        // Same size regardless of active state (2026-09-17) - only the
        // color (white vs. gray) marks the active dot for now. Sizing
        // the active dot bigger too was reverted per user request;
        // more elaborate indicator styling (color/size/shape options)
        // is likely to become its own settings group later rather than
        // hardcoded here - see PLAN.md.
        double r = 2.0;

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
// their reported desired widths might have changed. See PLAN.md's
// "Widget SDK design" for why the content-width part exists (real
// ported widgets are wider than the two placeholders' original fixed
// 30px pane).
void ApplyStackWidth(double contentWidth) {
    if (!g_ui.root || !g_ui.clipHost || !g_ui.clipGeom || !g_ui.dotsColumn) {
        return;
    }
    try {
        bool hideIndicator = g_settings.layoutHideIndicatorWhenSingle &&
                              EnabledIndices().size() <= 1;
        double dotsWidth = hideIndicator ? 0.0 : kDotsColumnWidth;
        g_ui.dotsColumn.Width({dotsWidth, GridUnitType::Pixel});
        g_ui.root.Width(contentWidth + dotsWidth);
        g_ui.clipHost.Width(contentWidth);
        auto rect = g_ui.clipGeom.Rect();
        rect.Width = (float)contentWidth;
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
            entry.desiredWidth = entry.widget->Create(host);
        } catch (...) {
            entry.crashed = true;
        }
    }

    double contentWidth = kMinContentWidth;
    for (auto& entry : g_widgets) {
        if (entry.enabled && !entry.crashed) {
            contentWidth = std::max(contentWidth, entry.desiredWidth);
        }
    }
    contentWidth = std::min(contentWidth, (double)g_settings.layoutMaxWidth);
    ApplyStackWidth(contentWidth);

    auto enabled = EnabledIndices();
    if (enabled.empty()) {
        g_ui.activeIndex = -1;
    } else if (std::find(enabled.begin(), enabled.end(), g_ui.activeIndex) ==
               enabled.end()) {
        g_ui.activeIndex = enabled.front();
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

    panel.Children().Append(MakeSettingsToggle(
        L"Hide indicator with one widget",
        g_settings.layoutHideIndicatorWhenSingle, [](bool on) {
            g_settings.layoutHideIndicatorWhenSingle = on;
            WritePrivateDword(L"layout.indicator.hideWhenSingle",
                               on ? 1 : 0);
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

        FrameworkElement navContent = BuildNavigationTab();
        FrameworkElement layoutContent = BuildLayoutTab();
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
            [contentHost, widgetsScroller, navContent, layoutContent,
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
                    contentHost.Content(navContent);
                } else if (tag == L"layout") {
                    contentHost.Content(layoutContent);
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
void ShowContextMenu(HWND, POINT) {
    if (!g_ui.root) {
        return;
    }
    try {
        MenuFlyout flyout;
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
        g_ui = {};
    } else if (msg == WM_DISPLAYCHANGE && !g_stopRequested) {
        StopSnapAnimation();
        UnwireNavigation();
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

// Flush against the taskbar's left edge, where the native Widgets
// button (weather/stocks) normally sits - the user hides that button and
// uses this exact space for taskbar-area mods, confirmed against their
// live setup (see PLAN.md's "Context"). Not tracking any other element's
// position: two rounds of trying that (Start button alone, then the
// whole TaskbarFrameRepeater) both put the stack somewhere wrong -
// TaskbarFrameRepeater in particular turned out to also contain every
// pinned/running app icon, not just the leading buttons, so anchoring to
// its right edge pushed the stack far off to the right, past the visible
// taskbar bounds ("disappeared"). The true left edge needs no tracking
// at all - it's RootGrid's own edge, which doesn't move.
constexpr double kLeftEdgeGap = 6.0;

// Builds the full widget-stack element (dots column + clipped, slidable
// widget panes) and adds it as a floating child of the taskbar's
// RootGrid, flush against its left edge. Must run on the taskbar's own
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

    try {
        Grid root;
        root.VerticalAlignment(VerticalAlignment::Stretch);
        root.HorizontalAlignment(HorizontalAlignment::Left);
        root.Margin({kLeftEdgeGap, 0, 0, 0});
        // Placeholder width - RebuildStackContents (called below) applies
        // the real width immediately, once widgets have reported their
        // desired widths.
        root.Width(kMinContentWidth + kDotsColumnWidth);
        ColumnDefinition dotsCol;
        dotsCol.Width({kDotsColumnWidth, GridUnitType::Pixel});
        ColumnDefinition contentCol;
        contentCol.Width({1.0, GridUnitType::Star});
        root.ColumnDefinitions().Append(dotsCol);
        root.ColumnDefinitions().Append(contentCol);

        StackPanel dotsPanel;
        dotsPanel.VerticalAlignment(VerticalAlignment::Center);
        dotsPanel.HorizontalAlignment(HorizontalAlignment::Center);
        Grid::SetColumn(dotsPanel, 0);
        root.Children().Append(dotsPanel);

        Border clipHost;
        clipHost.Height(PaneHeight());
        clipHost.Width(kMinContentWidth);
        clipHost.VerticalAlignment(VerticalAlignment::Center);
        RectangleGeometry clipGeom;
        clipGeom.Rect(
            {0, 0, (float)kMinContentWidth, (float)PaneHeight()});
        clipHost.Clip(clipGeom);
        Grid::SetColumn(clipHost, 1);

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
        g_ui.dotsColumn = dotsCol;
        g_ui.clipHost = clipHost;
        g_ui.clipGeom = clipGeom;
        g_ui.sliderTransform = transform;

        WireUpNavigation();
        RebuildStackContents();

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

        Wh_Log(L"Injected widget stack");
        return true;
    } catch (...) {
        Wh_Log(L"InjectWidgetStackGrid: exception");
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

void LoadSettings() {
    g_settings.navWheel = Wh_GetIntSetting(L"nav.wheel");
    g_settings.navDots = Wh_GetIntSetting(L"nav.dots");
    g_settings.navDrag = Wh_GetIntSetting(L"nav.drag");
    g_settings.navWrap = Wh_GetIntSetting(L"nav.wrap");
    g_settings.navOverscroll = Wh_GetIntSetting(L"nav.overscroll");
    int maxWidth = Wh_GetIntSetting(L"layout.maxWidth");
    g_settings.layoutMaxWidth = maxWidth > 0 ? maxWidth : 520;
    g_settings.layoutHideIndicatorWhenSingle =
        Wh_GetIntSetting(L"layout.indicator.hideWhenSingle");

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
    if (ReadPrivateDword(L"layout.maxWidth", v) && v > 0) {
        g_settings.layoutMaxWidth = (int)v;
    }
    if (ReadPrivateDword(L"layout.indicator.hideWhenSingle", v)) {
        g_settings.layoutHideIndicatorWhenSingle = v != 0;
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

    // This SDK's first non-placeholder widget (Incident 30) - see
    // SystemUsageWidget's own comment.
    WidgetEntry systemUsage;
    systemUsage.widget = std::make_unique<SystemUsageWidget>();
    g_widgets.push_back(std::move(systemUsage));

    LoadWidgetOrderState();
}

}  // namespace

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
    LoadSettings();
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
