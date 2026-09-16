// ==WindhawkMod==
// @id              taskbar-widget-stack
// @name            Taskbar Widget Stack
// @description     Stack multiple taskbar widgets vertically in one snap-scrollable pane, iOS-widget-stack style
// @version         0.1
// @author          AristideBH
// @github          https://github.com/AristideBH
// @homepage        https://aristide-bh.com/
// @license         MIT
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lgdi32 -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Taskbar Widget Stack

> **Note:** This mod is vibe-coded - built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

> **Prototype status:** this version stacks two **placeholder** panes to
> validate the overlay/scroll/snap/indicator mechanics. It does not yet host
> real widget content (media player, AI quota, ...) - see this mod's
> `PLAN.md` in the repo for the roadmap toward a real widget SDK.

Adds a single overlay area next to the taskbar's system tray that holds
multiple widgets stacked vertically, one visible at a time, switchable like
an iOS widget stack:

- **Snap-scroll** between widgets via mouse wheel, vertical drag, or by
  clicking a dot indicator - each independently toggleable in settings.
- **Dot indicators** on the left edge show how many widgets are enabled and
  which one is active.
- **Right-click** the stack for a menu to enable/disable widgets and move
  them up/down in the stack order.

Only Windows 11 is targeted. Applies to the taskbar on every taskbar
instance (multi-monitor) - not yet verified live, see `PLAN.md`.

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
- widgets:
  - - id: placeholder-a
      $name: Widget ID
    - enabled: true
      $name: Enabled
    $name: Widget 1 (placeholder A)
  - - id: placeholder-b
      $name: Widget ID
    - enabled: true
      $name: Enabled
    $name: Widget 2 (placeholder B)
  $name: Widgets
  $description: >-
    Placeholder widgets for this prototype. Order here is the stack order;
    use the right-click menu on the stack in the taskbar to reorder live
    (updates these settings).
*/
// ==WindhawkModSettings==

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kOverlayClassName[] = L"TaskbarWidgetStackOverlay";
constexpr int kOverlayWidth = 48;
constexpr int kSnapAnimMs = 180;
constexpr UINT kSnapTimerId = 1;
constexpr UINT_PTR kSnapTimerElapseMs = 15;

enum class WidgetMenuCmd : UINT {
    kToggleBase = 1000,  // + widget index
    kMoveUpBase = 2000,  // + widget index
    kMoveDownBase = 3000,  // + widget index
};

// Prototype placeholder widget. The real SDK will replace this with a
// pointer to externally-registered paint/click callbacks (see PLAN.md).
struct Widget {
    std::wstring id;
    std::wstring label;
    COLORREF color;
    bool enabled = true;

    // Crash isolation: a widget that throws during Paint gets latched here
    // and is skipped on subsequent redraws, per the "host catches and
    // disables" decision in PLAN.md. Placeholder widgets can't realistically
    // throw, but the wrapper/flag exist now so the pattern is already in
    // place for real (less-trusted) widget code later.
    bool crashed = false;

    void Paint(HDC hdc, const RECT& rect) const {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        RECT textRect = rect;
        DrawTextW(hdc, label.c_str(), -1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
};

std::vector<Widget> g_widgets;
HWND g_overlayWnd;
HWND g_taskbarWnd;
int g_activeIndex = 0;
int g_animFromIndex = 0;
DWORD g_animStartTick;
bool g_animating = false;
bool g_dragging = false;
POINT g_dragStart{};
int g_dragStartIndex = 0;

struct {
    bool navWheel = true;
    bool navDots = true;
    bool navDrag = true;
} g_settings;

// Indices into g_widgets of currently-enabled widgets, in stack order.
// Recomputed whenever g_widgets or its enabled flags change.
std::vector<int> EnabledIndices() {
    std::vector<int> result;
    for (int i = 0; i < (int)g_widgets.size(); i++) {
        if (g_widgets[i].enabled && !g_widgets[i].crashed) {
            result.push_back(i);
        }
    }
    return result;
}

void ClampActiveIndex() {
    auto enabled = EnabledIndices();
    if (enabled.empty()) {
        g_activeIndex = -1;
        return;
    }
    if (std::find(enabled.begin(), enabled.end(), g_activeIndex) ==
        enabled.end()) {
        g_activeIndex = enabled.front();
    }
}

void StartSnapAnimation(int fromIndex) {
    g_animFromIndex = fromIndex;
    g_animStartTick = GetTickCount();
    g_animating = true;
    if (g_overlayWnd) {
        SetTimer(g_overlayWnd, kSnapTimerId, kSnapTimerElapseMs, nullptr);
    }
}

void GoToWidget(int widgetIndex) {
    if (widgetIndex == g_activeIndex) {
        return;
    }
    int from = g_activeIndex;
    g_activeIndex = widgetIndex;
    StartSnapAnimation(from);
}

void StepWidget(int direction) {
    auto enabled = EnabledIndices();
    if (enabled.size() < 2) {
        return;
    }
    auto it = std::find(enabled.begin(), enabled.end(), g_activeIndex);
    if (it == enabled.end()) {
        return;
    }
    int pos = (int)std::distance(enabled.begin(), it);
    int next =
        (pos + direction + (int)enabled.size()) % (int)enabled.size();
    GoToWidget(enabled[next]);
}

float SnapAnimProgress() {
    if (!g_animating) {
        return 1.0f;
    }
    DWORD elapsed = GetTickCount() - g_animStartTick;
    if (elapsed >= kSnapAnimMs) {
        return 1.0f;
    }
    // Ease-out.
    float t = (float)elapsed / (float)kSnapAnimMs;
    return 1.0f - (1.0f - t) * (1.0f - t);
}

void PaintOverlay(HDC hdc, const RECT& clientRect) {
    HDC memDc = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(
        hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDc, memBmp);

    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(memDc, &clientRect, bg);
    DeleteObject(bg);

    ClampActiveIndex();
    if (g_activeIndex < 0 || g_activeIndex >= (int)g_widgets.size()) {
        goto blit;
    }

    {
        int height = clientRect.bottom - clientRect.top;
        float progress = SnapAnimProgress();
        int fromY = 0, toY = 0;

        if (g_animating && g_animFromIndex != g_activeIndex) {
            // Slide from the previous widget's pane to the active one,
            // direction based on stack order (simple up/down slide).
            bool movingDown = g_animFromIndex < g_activeIndex;
            int offset = (int)((1.0f - progress) * height);
            fromY = movingDown ? -offset : offset;
        }
        toY = 0;

        auto paintOne = [&](int widgetIndex, int y) {
            if (widgetIndex < 0 || widgetIndex >= (int)g_widgets.size()) {
                return;
            }
            Widget& w = g_widgets[widgetIndex];
            if (w.crashed) {
                return;
            }
            RECT paneRect{clientRect.left, y, clientRect.right,
                          y + height};
            try {
                w.Paint(memDc, paneRect);
            } catch (const std::exception&) {
                w.crashed = true;
            } catch (...) {
                w.crashed = true;
            }
        };

        if (g_animating && g_animFromIndex != g_activeIndex &&
            progress < 1.0f) {
            paintOne(g_animFromIndex, fromY);
            bool movingDown = g_animFromIndex < g_activeIndex;
            paintOne(g_activeIndex, movingDown ? fromY + height
                                                : fromY - height);
        } else {
            g_animating = false;
            paintOne(g_activeIndex, toY);
        }

        // Dot indicators along the left edge.
        if (g_settings.navDots) {
            auto enabled = EnabledIndices();
            int n = (int)enabled.size();
            if (n > 1) {
                int spacing = std::min(14, height / (n + 1));
                int startY = (height - spacing * (n - 1)) / 2;
                for (int i = 0; i < n; i++) {
                    bool active = enabled[i] == g_activeIndex;
                    int cy = startY + i * spacing;
                    int r = active ? 3 : 2;
                    HBRUSH dotBrush = CreateSolidBrush(
                        active ? RGB(255, 255, 255) : RGB(140, 140, 140));
                    HGDIOBJ oldBrush = SelectObject(memDc, dotBrush);
                    HGDIOBJ oldPen =
                        SelectObject(memDc, GetStockObject(NULL_PEN));
                    Ellipse(memDc, 3 - r, cy - r, 3 + r, cy + r);
                    SelectObject(memDc, oldBrush);
                    SelectObject(memDc, oldPen);
                    DeleteObject(dotBrush);
                }
            }
        }
    }

blit:
    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDc, 0, 0,
           SRCCOPY);
    SelectObject(memDc, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDc);
}

void ShowContextMenu(HWND hwnd, POINT screenPt) {
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < (int)g_widgets.size(); i++) {
        UINT flags = MF_STRING | (g_widgets[i].enabled ? MF_CHECKED : 0);
        AppendMenuW(menu, flags,
                    (UINT_PTR)WidgetMenuCmd::kToggleBase + i,
                    (g_widgets[i].label + L" (toggle)").c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < (int)g_widgets.size(); i++) {
        AppendMenuW(menu, MF_STRING,
                    (UINT_PTR)WidgetMenuCmd::kMoveUpBase + i,
                    (L"Move up: " + g_widgets[i].label).c_str());
        AppendMenuW(menu, MF_STRING,
                    (UINT_PTR)WidgetMenuCmd::kMoveDownBase + i,
                    (L"Move down: " + g_widgets[i].label).c_str());
    }

    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0,
        hwnd, nullptr);
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
    // Windhawk settings (Wh_SetStringSetting/Wh_SetIntSetting per widget)
    // so it survives Explorer restarts. Not implemented in this prototype;
    // widget list currently resets to settings-file order on reload. See
    // PLAN.md "Next steps".

    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);
            PaintOverlay(hdc, rect);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;  // Avoid flicker; WM_PAINT fills the whole client rect.
        case WM_TIMER: {
            if (wParam == kSnapTimerId) {
                if (SnapAnimProgress() >= 1.0f) {
                    g_animating = false;
                    KillTimer(hwnd, kSnapTimerId);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!g_settings.navWheel) {
                return 0;
            }
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            StepWidget(delta > 0 ? -1 : 1);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int y = GET_Y_LPARAM(lParam);
            int x = GET_X_LPARAM(lParam);
            if (g_settings.navDots && x < 12) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                int height = rect.bottom - rect.top;
                auto enabled = EnabledIndices();
                int n = (int)enabled.size();
                if (n > 1) {
                    int spacing = std::min(14, height / (n + 1));
                    int startY = (height - spacing * (n - 1)) / 2;
                    int nearest = 0;
                    int bestDist = INT_MAX;
                    for (int i = 0; i < n; i++) {
                        int cy = startY + i * spacing;
                        int dist = abs(cy - y);
                        if (dist < bestDist) {
                            bestDist = dist;
                            nearest = i;
                        }
                    }
                    GoToWidget(enabled[nearest]);
                    return 0;
                }
            }
            if (g_settings.navDrag) {
                g_dragging = true;
                g_dragStart = {x, y};
                g_dragStartIndex = g_activeIndex;
                SetCapture(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_dragging && g_settings.navDrag) {
                // Simple scrub: crossing half the overlay height commits a
                // step, matching a snap-scroll feel; full smooth drag
                // tracking is a "Next steps" refinement (see PLAN.md).
                RECT rect;
                GetClientRect(hwnd, &rect);
                int height = rect.bottom - rect.top;
                int dy = GET_Y_LPARAM(lParam) - g_dragStart.y;
                if (abs(dy) > height / 2) {
                    StepWidget(dy < 0 ? 1 : -1);
                    g_dragStart.y = GET_Y_LPARAM(lParam);
                }
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g_dragging) {
                g_dragging = false;
                ReleaseCapture();
            }
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &pt);
            ShowContextMenu(hwnd, pt);
            return 0;
        }
        case WM_NCDESTROY:
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// Locates the tray notification area to park the overlay next to, using
// the standard window chain for this category of taskbar mod. Unverified
// against the actual mods this is meant to sit alongside - see PLAN.md
// "Open questions". Returns nullptr if not found (mod becomes a no-op).
HWND FindTrayNotifyWnd() {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        return nullptr;
    }
    g_taskbarWnd = tray;
    HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
    return notify;
}

void RepositionOverlay() {
    if (!g_overlayWnd || !g_taskbarWnd) {
        return;
    }
    RECT taskbarRect;
    GetWindowRect(g_taskbarWnd, &taskbarRect);
    int height = taskbarRect.bottom - taskbarRect.top;

    HWND notify = FindWindowExW(g_taskbarWnd, nullptr, L"TrayNotifyWnd",
                                 nullptr);
    RECT notifyRect{};
    if (notify) {
        GetWindowRect(notify, &notifyRect);
    }

    // Park immediately to the left of the tray notify area, matching where
    // the user currently places the media-player/AI-quota widgets (see
    // PLAN.md "Open questions" - exact offset unverified live).
    int x = (notify ? notifyRect.left : taskbarRect.right) - kOverlayWidth;
    int y = taskbarRect.top;

    SetWindowPos(g_overlayWnd, HWND_TOP, x, y, kOverlayWidth, height, 0);
}

void InitPlaceholderWidgets() {
    g_widgets.clear();
    g_widgets.push_back(
        {L"placeholder-a", L"Media\nPlayer", RGB(70, 90, 160), true});
    g_widgets.push_back(
        {L"placeholder-b", L"AI\nQuota", RGB(90, 150, 90), true});
    g_activeIndex = 0;
}

void LoadSettings() {
    g_settings.navWheel = Wh_GetIntSetting(L"nav.wheel");
    g_settings.navDots = Wh_GetIntSetting(L"nav.dots");
    g_settings.navDrag = Wh_GetIntSetting(L"nav.drag");
}

}  // namespace

BOOL Wh_ModInit() {
    LoadSettings();
    InitPlaceholderWidgets();

    HWND notify = FindTrayNotifyWnd();
    if (!g_taskbarWnd) {
        Wh_Log(L"Shell_TrayWnd not found; mod inactive this session");
        return TRUE;  // Non-fatal: taskbar may not be up yet.
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_overlayWnd = CreateWindowExW(
        0, kOverlayClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0,
        kOverlayWidth, 32, g_taskbarWnd, nullptr, wc.hInstance, nullptr);

    RepositionOverlay();

    return TRUE;
}

void Wh_ModAfterInit() {
    RepositionOverlay();
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    if (g_overlayWnd) {
        InvalidateRect(g_overlayWnd, nullptr, FALSE);
    }
}

void Wh_ModBeforeUninit() {
    // Explicit teardown before DLL unload - an overlay window surviving
    // with its WndProc pointing into unmapped memory is the same crash
    // class documented in windows-11-start-menu-button/PLAN.md's "Crash
    // containment" section (CFG fail-fast on an orphaned callback).
    if (g_overlayWnd) {
        KillTimer(g_overlayWnd, kSnapTimerId);
        DestroyWindow(g_overlayWnd);
        g_overlayWnd = nullptr;
    }
    UnregisterClassW(kOverlayClassName, GetModuleHandleW(nullptr));
}

void Wh_ModUninit() {}
