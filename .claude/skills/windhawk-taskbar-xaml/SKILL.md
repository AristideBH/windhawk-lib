---
name: windhawk-taskbar-xaml
description: How to put custom UI into the Windows 11 taskbar from a Windhawk mod and keep Explorer alive. Use for any mod that adds, moves or restyles something on the taskbar or system tray, such as a widget, button, indicator, label or menu entry, and for any taskbar-widget-* mod in this repo. Covers reaching the taskbar's XAML tree, element names and anchors, positioning, injection lifecycle, pointer and wheel input, context menus, multi-monitor, ARM64, and the cross-mod widget ABI. Read it before designing a taskbar feature and before debugging one that doesn't appear or doesn't respond.
---

# Windhawk taskbar XAML

Load `windhawk-mod-development` first. This skill adds what is specific to the taskbar. It is built from about 60 incidents in taskbar-widget-stack/PLAN.md and from two published mods the stack was ported from: taskbar-ai-quota by Cleroth and Taskbar Fluent Media Player by Salyts.

## The one fact that decides the design

The Windows 11 taskbar is a XAML island drawn through DirectComposition. A classic child window placed on it exists, receives messages and is never visible. The stack's first version did that with GDI and drew nothing. See stack Incident 2.

Anything visible on the taskbar is a `Windows.UI.Xaml` element inserted into the taskbar's own live tree. This is system XAML, not WinUI 3. Use the `winrt::Windows::UI::Xaml` namespaces.

## Reaching the tree

There is no public API. The repo's method, ported from taskbar-ai-quota, uses symbol hooks into `taskbar.dll`:

```cpp
WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
    {{LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"}, &CTaskBand_ITaskListWndSite_vftable},
    {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"}, &CTaskBand_GetTaskbarHost_Original},
    {{LR"(public: int __cdecl TaskbarHost::FrameHeight(void)const )"}, &TaskbarHost_FrameHeight_Original},
    {{LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"}, &Std_Ref_Decref_Original},
    {{LR"(public: virtual void __cdecl TrayUI::StartTaskbar(void))"}, &TrayUI_StartTaskbar_Original, TrayUI_StartTaskbar_Hook},
};
```

`TryGetTaskbarElementAbi` walks from the taskbar window to its `TaskbarHost`, then reads a field whose offset it recovers from the compiled start of `TaskbarHost::FrameHeight`. From that it gets the root `FrameworkElement` and a `XamlRoot`.

Copy this block from taskbar-widget-stack.wh.cpp. Don't rewrite it from memory, and keep the attribution comment.

The byte match is the most fragile part of every mod that uses it. On Ari's ARM64 laptop the x64 pattern didn't match, because `taskbar.dll` there is native ARM64 code. The fix tries the x64 pattern and then the ARM64 pattern at runtime. When the match fails, the code logs the function's first 16 bytes. Ask for that line before changing a pattern. See stack Incident 3.

Upstream Fluent Media Player also hooks `CSecondaryTaskBand` and its `GetTaskbarHost` for taskbars on other monitors. The stack doesn't, and only handles the primary taskbar.

## Element names

Confirmed in this repo's source or in upstream 1.7.0. Names vary by Windows build and by which other mods are installed, so look each one up with a fallback and log a miss.

| Element | How to find it |
| --- | --- |
| Taskbar frame | class `Taskbar.TaskbarFrame` |
| Root grid | name `RootGrid`, inside the frame |
| Icon repeater | name `TaskbarFrameRepeater`, inside `RootGrid` |
| Start button | name `StartButton`, `StartMenuButton`, `StartMenuLaunchButton` or `LaunchListButton`, tried in that order. Automation id `StartButton`. |
| Start, Task View | class `Taskbar.ExperienceToggleButton`. Task View was the second one in the repeater. |
| Search | class `Taskbar.TaskbarExtensionElement` |
| Widgets button | name `AugmentedEntryPointButton`, class `Taskbar.AugmentedEntryPointButton` |
| Start button's inner panel | name `ExperienceToggleButtonRootPanel`, class `Taskbar.TaskListButtonPanel` |
| Tray frame | class `SystemTray.SystemTrayFrame` |
| Tray grid | name `SystemTrayFrameGrid`, inside the tray frame |

Two lookups that failed silently:

- `SystemTrayFrameGrid` is not a descendant of `RootGrid`. Search from the taskbar's full XAML content for the class `SystemTray.SystemTrayFrame` first, then by name inside it. See stack Incident 42.
- `TaskbarFrameRepeater` holds every pinned and running app icon, not only Start, Search and Task View. Its right edge is past the icon cluster. Anchoring to it pushed the stack off screen. See stack Incident 6.

Ask for a UWPSpy dump of the branch you plan to touch whenever you add a new anchor. See `windhawk-recon`.

## Where to insert and how to position

Insert as a child of `RootGrid` for anything in the main taskbar area. Insert into `SystemTrayFrameGrid` only for things that belong among the tray items.

Positions that work, in order of risk:

1. **A fixed edge.** Set a fixed `Margin` on your root element and never touch it again. Left edge is reliable. Center and right edges land on top of icons or the tray unless the user has freed that space.
2. **Next to one specific button.** Resolve one element, such as Start or the Widgets button. Subscribe to `RootGrid.LayoutUpdated`. On each pass, push that button's own margin out by your width and set your `Margin.Left` from its live `TransformToVisual` position. Write a margin only when it differs from the last value by more than 1px, or you create a layout loop. See stack Incident 38.
3. **Next to the tray.** Same tracking, with the tray grid as the anchor.

If an anchor is missing on this build, fall back to the left edge and log once. Never inject anchored to nothing.

Upstream's 25-position list is in `windhawk-ui-kit`. Use its names for new mods so settings read the same across the repo.

## Injection lifecycle

The taskbar is created after Explorer starts and is recreated on some display changes. A mod must handle all of these:

- **Not there yet.** `Wh_ModInit` starts a worker thread that polls `FindWindowW(L"Shell_TrayWnd", nullptr)` every 500 ms for up to about five minutes, then marshals the injection onto the taskbar's thread with `RunFromWindowThread`.
- **Recreated.** Hook `TrayUI::StartTaskbar`. Before resetting your bookkeeping, detach your old elements and unregister from any host. Don't assume the old tree died. The weather widget left a remnant on the taskbar because it only nulled its pointers. See taskbar-widget-weather/PLAN.md Incident 1.
- **Display change.** Subclass the taskbar window and handle `WM_DISPLAYCHANGE` and `WM_NCDESTROY` by tearing down and re-injecting.
- **Unload.** In `Wh_ModBeforeUninit`, stop and join the worker, revoke every event token, remove your elements on the taskbar's thread, and remove any window properties you set. If your stored taskbar window handle was never set, look it up again instead of skipping teardown.

Wrap each removal from a `Children()` collection in `try`/`catch`. A throw during teardown, swallowed by a caller, left a stale element behind and produced a duplicate on the next create.

## Input

**Hit testing.** A panel with no `Background` is not hit-testable. Only its painted descendants are. Give your root element an alpha-0 `SolidColorBrush` so the whole rectangle receives pointer events. This bit the stack twice, see Incidents 4 and 47.

**Clicks on native buttons.** Some native elements never raise `PointerPressed` for a real click. On the Start button, use the `ToggleButton` `Checked` and `Unchecked` events.

**Mouse wheel.** `PointerWheelChanged` on your root works for a mouse wheel once hit testing is right.

**Precision touchpad.** Two-finger scroll did not arrive as wheel events on the taskbar. `ManipulationDelta` was tried and later removed because it fought the drag gesture. The stack now reads raw HID digitizer input through `WM_INPUT` on the subclassed taskbar window. It registers with `RegisterRawInputDevices` for usage page `0x0D`, usage `0x05`, with `RIDEV_INPUTSINK`. Three rules came out of that:

- Gate on the cursor being over your element, using a screen rectangle you cached earlier. Don't call `TransformToVisual` or any XAML API from the `WM_INPUT` path.
- Require two contacts, and debounce.
- Refresh the cached rectangle on every rebuild and layout change, or scroll stops working after the element moves.

See stack Incidents 14 to 21.

**Drag.** Use `PointerPressed`, `PointerMoved`, `PointerReleased` and `CapturePointer`. Measure movement against a static ancestor, not the moving element. The stack's own drag-to-step gesture is still unreliable and not root-caused, see the open issue in its PLAN.md.

## Menus and popups

Don't call `TrackPopupMenu` on the taskbar. Its nested message loop crashed Explorer, and deferring the call through the dispatcher didn't help. See stack Incidents 10 and 20 to 23.

Two things work:

- **A XAML `MenuFlyout`** shown with `ShowAt(element)`. Each item's `Click` handler does its own work. Hold the flyout in a global and clear it from `Closed`.
- **An entry in the taskbar's own right-click menu.** The stack hooks two symbols in `Taskbar.View.dll`, with `ExplorerExtensions.dll` as the fallback module name: `ContextMenus::ShowTaskbarSettingsContextMenu`, and the `Append` method of the `IVector` of `MenuFlyoutItemBase`. It sets a scope flag in the first hook and appends its items from the second.

The `Append` hook is generic and runs for every menu built in the process. It needs a depth counter so appending your own items doesn't re-enter it. The first version recursed and crashed Explorer on the first menu open. If the symbols don't resolve, the feature is off and the mod keeps working. See stack Incident 59.

`Taskbar.View.dll` may not be loaded when the mod starts. The stack hooks `LoadLibraryExW` in kernelbase and installs the symbol hooks when the module appears.

Popups and flyouts anchored to a taskbar element should open upward. Upstream uses `FlyoutPlacementMode::Top` as its default.

## Animation

For per-frame work, subscribe to `CompositionTarget::Rendering`, keep the token, and revoke it on every teardown path and in `Wh_ModBeforeUninit`. The event stops firing when nothing redraws, so a flag checked inside the callback is not a teardown mechanism.

To recolor something Explorer animates itself, such as the Start icon, you have to reapply your brush every frame. A one-time set is overwritten by the native hover and press animations.

## Running next to other mods

Windhawk gives no load order between mods. Two mods that need each other must find each other at any time.

The stack publishes its register and unregister function addresses as window properties on `Shell_TrayWnd`, named `TaskbarWidgetStack_RegisterWidgetFn_v1` and `TaskbarWidgetStack_UnregisterWidgetFn_v1`. It sets them only after its own injection succeeds and removes them on teardown.

A widget mod therefore:

- Polls for the property with a retry thread, every 500 ms for a few minutes. A single attempt at init lost the race about half the time. See system-usage Incident 3 and media-player Incident 3.
- Keeps looking after falling back to standalone mode, and moves into the stack when the host appears.
- Restarts the polling window from its own `StartTaskbar` hook.
- Guards against being registered and standalone at once.

### The widget ABI

```cpp
struct WidgetStackHostAbiV1 {
    void* taskbarHwnd;     // an HWND
    void* parentPanelAbi;  // IInspectable* of a Panel. Wrap with copy_from_abi, don't take ownership.
    double paneHeight;
};
struct WidgetStackWidgetAbiV1 {
    void* context;
    double(__cdecl* Create)(void* context, const WidgetStackHostAbiV1* host);
    void(__cdecl* Tick)(void* context);
    double(__cdecl* OnSettingsChanged)(void* context);
    void(__cdecl* Destroy)(void* context);
    void(__cdecl* GetId)(void* context, wchar_t* buffer, int bufferSize);
    void(__cdecl* GetDisplayName)(void* context, wchar_t* buffer, int bufferSize);
};
```

Rules for both sides:

- Plain C structs and `__cdecl` function pointers only. No C++ classes, no C++/WinRT wrapper objects and no owned strings cross the boundary.
- The host calls `Destroy()` then `Create()` on every widget during every rebuild, including right after a widget registers. `Destroy()` must not register, unregister, start threads or call the host. See the re-entrancy section of the core skill.
- The host identifies a widget by its `context` pointer and its id, never by list index. Order and the active widget reset on every settings change until that was fixed. See stack Incident 40.
- Registering the same context twice replaces the entry. It doesn't add one.
- In registered mode the widget fills the width the host gives it and ignores its own width settings.
- Every call happens on the taskbar's thread.

The struct is copied by hand into five files. Three hardening items are logged and not done: a leading size field so a mismatched copy is rejected, a thread check in the register function, and real isolation of widget callbacks from access violations. The size field is a breaking change, so all five copies change in one commit.

## Mods that react to another program

A taskbar indicator for an outside program, such as a window manager's workspaces, needs a way to read that program's state. Find that first: a named pipe, a socket, a command-line query or a window message. If the program offers none, the work starts with a change to that program, and no amount of taskbar code replaces it. Read state on a worker thread and marshal only the UI update.

## Before you call it working

Run the checklist in the core skill, plus:

- Start button mods, Taskbar Styler themes and the media player enabled at the same time.
- Taskbar alignment set to left and to center.
- The native Widgets button shown and hidden.
- Right-click on your element and on empty taskbar space next to it.
- Touchpad scroll and mouse wheel over your element and beside it.

## Reaching the taskbar without `taskbar.dll` symbols

Confirmed on 2026-11-12 on Ari's laptop, ARM64, build 26300.9974, with `probes/probe-taskbar-view.wh.cpp`. Use this entry point when `taskbar.dll` symbols are missing, which happens for days after a Windows update.

Hook this function in `Taskbar.View.dll`:

```
public: void __cdecl winrt::Taskbar::implementation::TaskbarFrame::OnXamlRootPointerEntered(struct winrt::Windows::Foundation::IInspectable const &,struct winrt::Windows::UI::Xaml::Input::PointerRoutedEventArgs const &)
```

- It fires on the taskbar's UI thread each time the pointer enters the taskbar. No trip through Settings is needed.
- Declare both arguments as `IInspectable const&`. Query the second for `RoutedEventArgs` and read `OriginalSource()`. It was the `Grid` named `RootGrid`.
- From `RootGrid`, the parent is `Taskbar.TaskbarFrame`, named `TaskbarFrame`. A search down from the top finds `TaskbarFrameRepeater`, `SystemTray.SystemTrayFrame` and the `StackPanel` named `SystemTrayFrameGrid`. The whole tree was 123 elements.
- A `Border` appended to `RootGrid.Children()` shows on the taskbar. `RootGrid` had five children before.

Limits found in the same run:

- The widget only appears after the first pointer entry. A mod that must show at startup needs a second hook. `TaskbarFrame::OnLoaded` and `TaskbarFrame::OnApplyTemplate` exist in the module and are untested.
- `ExperienceToggleButton::UpdateButtonPadding` did not fire when Task view was toggled in Settings on this build.
- `TaskListButton::OnPointerPressed` did not log on one click of an app button. One sample, so treat it as unconfirmed.
- `TaskbarFrame::OnElementPrepared` and `ExperienceToggleButton::OnPointerPressed` resolved but were not exercised.
