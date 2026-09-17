// ==WindhawkMod==
// @id              taskbar-widget-stack
// @name            Taskbar Widget Stack
// @description     Stack multiple taskbar widgets vertically in one snap-scrollable pane, iOS-widget-stack style
// @version         0.1.18
// @author          AristideBH
// @github          https://github.com/AristideBH
// @homepage        https://aristide-bh.com/
// @license         MIT
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -luser32 -lcomctl32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Widget Stack

> **Note:** This mod is vibe-coded - built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

> **Prototype status:** this version stacks two **placeholder** panes to
> validate the injection/scroll/snap/indicator mechanics. It does not yet
> host real widget content (media player, AI quota, ...) - see this mod's
> `PLAN.md` in the repo for the roadmap toward a real widget SDK.

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
  $name: Navigation
  $description: Which ways of switching between stacked widgets are active.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <windows.h>
#include <unknwn.h>

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
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <algorithm>
#include <atomic>
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

namespace {

constexpr double kStackWidth = 40.0;
constexpr double kDotsColumnWidth = 10.0;
constexpr int kSnapAnimMs = 180;

enum class WidgetMenuCmd : UINT {
    kToggleBase = 1000,
    kMoveUpBase = 2000,
    kMoveDownBase = 3000,
};

// Prototype placeholder widget. The real SDK will replace this with a
// pointer to externally-registered build/update/click callbacks (see
// PLAN.md).
struct Widget {
    std::wstring id;
    std::wstring label;
    winrt::Windows::UI::Color color;
    bool enabled = true;

    // Crash isolation: a widget that throws while its pane is built gets
    // latched here and is skipped (no pane, no dot) on the next rebuild,
    // per the "host catches and disables" decision in PLAN.md. Placeholder
    // widgets can't realistically throw, but the wrapper/flag exist now so
    // the pattern is already in place for real (less-trusted) widget code
    // later.
    bool crashed = false;
};

std::vector<Widget> g_widgets;

struct {
    bool navWheel = true;
    bool navDots = true;
    bool navDrag = true;
} g_settings;

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
    winrt::event_token manipulationToken;
    StackPanel widgetsPanel{nullptr};
    StackPanel dotsPanel{nullptr};
    CompositeTransform sliderTransform{nullptr};
    int activeIndex = 0;
    winrt::event_token renderingToken;
    bool animating = false;
    ULONGLONG animStartTick = 0;
    double animFromY = 0;
    double animToY = 0;
    bool dragging = false;
    double dragStartY = 0;
    winrt::Windows::UI::Xaml::Input::Pointer dragPointer{nullptr};
    double manipulationAccumY = 0;
};

UiState g_ui;
HWND g_taskbarWnd;
HANDLE g_retryThread;
HANDLE g_injectEvent;
HHOOK g_mouseHook;
bool g_rawInputRegistered;
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

double PaneHeight() {
    return 32.0;
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
        StopSnapAnimation();
    }
}

void ApplySliderTarget(int widgetIndex, bool animate) {
    if (!g_ui.sliderTransform) {
        return;
    }
    double y = -widgetIndex * PaneHeight();
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
    int next = (pos + direction + (int)enabled.size()) % (int)enabled.size();
    GoToWidget(enabled[next]);
}

void ShowContextMenu(HWND hWnd, POINT screenPt);
void RebuildStackContents();

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
            // Deferred via the dispatcher rather than called directly:
            // TrackPopupMenu (inside ShowContextMenu) pumps its own
            // nested Win32 message loop. Calling it synchronously from
            // inside a XAML routed-event callback re-enters the XAML
            // dispatcher's own call stack with a blocking modal loop -
            // confirmed live (2026-09-16, "Incident 10") as the trigger
            // for an Explorer crash right after this handler fires (the
            // Incident 8 dangling-delegate fix was real but didn't
            // account for this separate hazard). Queuing the call
            // instead lets this event handler return and the XAML
            // dispatch fully unwind before the popup menu's own message
            // loop ever starts.
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

    // Two-finger trackpad scroll, separately from `PointerWheelChanged`
    // (confirmed live, 2026-09-16, "Incident 14": a real mouse wheel
    // reliably fires `PointerWheelChanged`/`WM_MOUSEWHEEL`, but a
    // Precision Touchpad's two-finger pan over this element did not -
    // plausibly because the taskbar's own native touchpad-gesture
    // handling consumes it before the OS's generic "synthesize
    // WM_MOUSEWHEEL from an unclaimed touchpad pan" fallback ever runs).
    // `ManipulationDelta` is the WinRT-native channel for touch/touchpad
    // pan gestures, independent of wheel synthesis - requires
    // `ManipulationMode` to declare interest in vertical translation.
    g_ui.root.ManipulationMode(wuxi::ManipulationModes::TranslateY);
    g_ui.manipulationToken = g_ui.root.ManipulationDelta(
        [](winrt::Windows::Foundation::IInspectable const&,
           wuxi::ManipulationDeltaRoutedEventArgs const& args) {
            if (!g_settings.navWheel) {
                return;
            }
            double dy = args.Delta().Translation.Y;
            g_ui.manipulationAccumY += dy;
            double height = PaneHeight();
            while (std::abs(g_ui.manipulationAccumY) > height / 2) {
                StepWidget(g_ui.manipulationAccumY < 0 ? 1 : -1);
                g_ui.manipulationAccumY +=
                    g_ui.manipulationAccumY < 0 ? height / 2 : -(height / 2);
            }
            args.Handled(true);
        });
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
        if (g_ui.manipulationToken) {
            g_ui.root.ManipulationDelta(g_ui.manipulationToken);
        }
    } catch (...) {
    }
}

Border BuildWidgetPane(Widget& widget) {
    Border border;
    border.Height(PaneHeight());
    border.Width(kStackWidth - kDotsColumnWidth);
    SolidColorBrush brush{widget.color};
    border.Background(brush);

    TextBlock text;
    text.Text(winrt::hstring(widget.label));
    text.HorizontalAlignment(HorizontalAlignment::Center);
    text.VerticalAlignment(VerticalAlignment::Center);
    text.TextAlignment(TextAlignment::Center);
    text.FontSize(9);
    SolidColorBrush fg{winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255)};
    text.Foreground(fg);
    border.Child(text);

    return border;
}

void RefreshDots() {
    if (!g_ui.dotsPanel) {
        return;
    }
    g_ui.dotsPanel.Children().Clear();
    auto enabled = EnabledIndices();
    for (int idx : enabled) {
        bool active = idx == g_ui.activeIndex;
        double r = active ? 3.0 : 2.0;

        // Back to the original small dot with no enlarged hit target
        // (2026-09-16): the earlier "unclickable dots" symptom turned out
        // to be the broken WindhawkModSettings closing marker (see
        // "Incident 12"), not the dot's own hit-test size - now that
        // clicks are confirmed working, the oversized 10x14 transparent
        // hit box isn't needed and was just making the indicators look
        // bulkier than intended.
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

// (Re)builds the widget panes and dots from the current g_widgets list.
// Crash isolation: each widget's pane is built inside its own try/catch -
// a widget that throws is flagged crashed and gets neither a pane nor a
// dot on this and future rebuilds (matches the "host catches and
// disables" decision in PLAN.md).
void RebuildStackContents() {
    if (!g_ui.widgetsPanel) {
        return;
    }
    StopSnapAnimation();
    g_ui.widgetsPanel.Children().Clear();

    for (auto& widget : g_widgets) {
        if (widget.crashed) {
            continue;
        }
        try {
            g_ui.widgetsPanel.Children().Append(BuildWidgetPane(widget));
        } catch (...) {
            widget.crashed = true;
        }
    }

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
}

void ShowContextMenu(HWND hWnd, POINT screenPt) {
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < (int)g_widgets.size(); i++) {
        UINT flags = MF_STRING | (g_widgets[i].enabled ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, (UINT_PTR)WidgetMenuCmd::kToggleBase + i,
                    (g_widgets[i].label + L" (toggle)").c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < (int)g_widgets.size(); i++) {
        AppendMenuW(menu, MF_STRING, (UINT_PTR)WidgetMenuCmd::kMoveUpBase + i,
                    (L"Move up: " + g_widgets[i].label).c_str());
        AppendMenuW(menu, MF_STRING,
                    (UINT_PTR)WidgetMenuCmd::kMoveDownBase + i,
                    (L"Move down: " + g_widgets[i].label).c_str());
    }

    SetForegroundWindow(hWnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                               screenPt.x, screenPt.y, 0, hWnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 0) {
        return;
    }

    if (cmd >= (UINT)WidgetMenuCmd::kToggleBase &&
        cmd < (UINT)WidgetMenuCmd::kMoveUpBase) {
        int idx = cmd - (UINT)WidgetMenuCmd::kToggleBase;
        if (idx >= 0 && idx < (int)g_widgets.size()) {
            g_widgets[idx].enabled = !g_widgets[idx].enabled;
        }
    } else if (cmd >= (UINT)WidgetMenuCmd::kMoveUpBase &&
               cmd < (UINT)WidgetMenuCmd::kMoveDownBase) {
        int idx = cmd - (UINT)WidgetMenuCmd::kMoveUpBase;
        if (idx > 0) {
            std::swap(g_widgets[idx], g_widgets[idx - 1]);
        }
    } else if (cmd >= (UINT)WidgetMenuCmd::kMoveDownBase) {
        int idx = cmd - (UINT)WidgetMenuCmd::kMoveDownBase;
        if (idx >= 0 && idx + 1 < (int)g_widgets.size()) {
            std::swap(g_widgets[idx], g_widgets[idx + 1]);
        }
    }

    // TODO(SDK milestone): persist reordered/toggled state back to
    // Windhawk settings so it survives Explorer restarts. Not implemented
    // in this prototype. See PLAN.md "Next steps".

    RebuildStackContents();
}

// Named (not an inline lambda) because SetWindowSubclass/RemoveWindowSubclass
// match subclasses by exact function pointer - installing with one lambda
// and removing with a different one (even with identical bodies) silently
// fails to remove anything.
LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam, UINT_PTR) {
    if (msg == WM_INPUT) {
        // Diagnostic (2026-09-17, "Incident 17"): raw HID input from the
        // Precision Touchpad, registered via RegisterRawInputDevices
        // (usage page 0x0D "Digitizer", usage 0x05 "Touch Pad") - the
        // lowest level of touchpad data an application can see, below
        // every OS-level gesture/wheel synthesis already confirmed dead
        // for this gesture (Incidents 9, 14, 15, 16). Only dumping raw
        // bytes for now, not parsing them - there's no reference
        // implementation to port for this one (unlike everything else in
        // this file so far), so confirming *any* data arrives at all
        // comes first, before risking a first-principles HID Digitizer
        // report parse silently misinterpreting it.
        UINT size = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size,
                         sizeof(RAWINPUTHEADER));
        if (size > 0) {
            std::vector<BYTE> buffer(size);
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer.data(),
                                 &size, sizeof(RAWINPUTHEADER)) == size) {
                auto* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
                if (raw->header.dwType == RIM_TYPEHID) {
                    DWORD count = raw->data.hid.dwCount;
                    DWORD sizeHid = raw->data.hid.dwSizeHid;
                    wchar_t hex[128] = {};
                    DWORD bytesToShow = std::min<DWORD>(sizeHid, 20);
                    for (DWORD i = 0; i < bytesToShow; i++) {
                        wchar_t b[4];
                        wsprintfW(b, L"%02X ", raw->data.hid.bRawData[i]);
                        wcscat_s(hex, b);
                    }
                    Wh_Log(L"WM_INPUT HID: count=%u sizeHid=%u bytes=%s",
                           count, sizeHid, hex);
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

// Diagnostic (2026-09-17, "Incident 16"): a system-wide, low-level mouse
// hook sees `WM_MOUSEWHEEL` before Windows dispatches it to a specific
// window - unlike this mod's earlier `WM_MOUSEWHEEL`-on-the-taskbar-HWND
// check (see Incident 7/9), which could only observe messages already
// targeted at this mod's own window. If a Precision Touchpad's
// two-finger scroll produces a wheel message anywhere on the desktop
// during the gesture, even one not routed to this element, this should
// see it - narrowing "nothing is ever synthesized for this gesture" from
// "something is synthesized, but not delivered here."
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        short delta = HIWORD(info->mouseData);
        Wh_Log(L"WH_MOUSE_LL: WM_MOUSEWHEEL at (%d,%d) delta=%d", info->pt.x,
               info->pt.y, delta);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

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
        root.Width(kStackWidth);
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
        clipHost.Width(kStackWidth - kDotsColumnWidth);
        clipHost.VerticalAlignment(VerticalAlignment::Center);
        RectangleGeometry clipGeom;
        clipGeom.Rect({0, 0, (float)(kStackWidth - kDotsColumnWidth),
                       (float)PaneHeight()});
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
        g_ui.sliderTransform = transform;

        WireUpNavigation();
        RebuildStackContents();

        if (!g_ui.windowSubclassed) {
            g_ui.windowSubclassed = WindhawkUtils::SetWindowSubclassFromAnyThread(
                hWnd, TaskbarWindowSubclassProc, 0);
        }

        if (!g_mouseHook) {
            // Installed here rather than in Wh_ModInit: this runs on the
            // taskbar's own message-pumping UI thread already (guaranteed
            // by RunFromWindowThread), which WH_MOUSE_LL's callback
            // delivery requires - see LowLevelMouseProc's comment.
            g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc,
                                             GetModuleHandleW(nullptr), 0);
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
}

void InitPlaceholderWidgets() {
    g_widgets.clear();
    g_widgets.push_back({L"placeholder-a", L"Media\nPlayer",
                          winrt::Windows::UI::ColorHelper::FromArgb(255, 70, 90, 160),
                          true});
    g_widgets.push_back({L"placeholder-b", L"AI\nQuota",
                          winrt::Windows::UI::ColorHelper::FromArgb(255, 90, 150, 90),
                          true});
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
    if (g_injectEvent) {
        SetEvent(g_injectEvent);
    }
    if (g_mouseHook) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
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
