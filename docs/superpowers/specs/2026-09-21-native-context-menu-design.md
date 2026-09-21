# taskbar-widget-stack — Native taskbar context menu integration

Design doc for replacing this mod's own right-click `MenuFlyout` (built
by `ShowContextMenu`, triggered by `g_ui.root.RightTapped`) with an
injected submenu inside Windows 11's own native "empty taskbar"
right-click context menu.

## Context

Today, right-clicking the widget stack shows a custom XAML `MenuFlyout`
this mod builds and shows itself (`ShowContextMenu`, wired on
`g_ui.root.RightTapped` - see `PLAN.md` Incidents 23/24/50/51/56 for
its history). This is visually inconsistent with the rest of the
taskbar: right-clicking empty taskbar space shows Windows' own native
menu (Task Manager, Taskbar settings, ...), while right-clicking the
widget stack shows a completely different-looking flyout.

The user wants the stack's own actions folded into that *native* menu
instead, as a single "Widget stack" submenu - matching how other
taskbar mods (e.g. `taskbar-icon-separators`) already integrate.

## Goals

- Right-clicking anywhere on the taskbar (including over the widget
  stack, when the widget under the cursor has no right-click action of
  its own configured) shows Windows' own native context menu, with a
  "Widget stack" submenu added to it.
- That submenu has exactly three items: **Hide stack**, **Reset
  position**, **Stack settings**.
- This mod's own custom `MenuFlyout` (`ShowContextMenu`) is removed
  entirely - not kept as a parallel or fallback UI.
- A widget with its own configured right-click action (e.g. weather's
  `ClickActionSettings.right`, when set to something other than
  "None") keeps firing that action on a right-click over that widget,
  same as today - the native menu only takes over where nothing widget-
  specific was configured.
- If the required symbols can't be found (unsupported Windows/taskbar
  build), this feature silently does nothing - no menu injection, no
  crash, and critically, no impact on the rest of the mod (the widget
  stack itself must keep working normally either way).

## Non-goals

- No fallback to the old custom `MenuFlyout` if the native hook fails
  - per explicit user decision, an unsupported build simply doesn't
    get a "Widget stack" entry in the native menu at all.
- No "Goto" (jump-to-widget) entry in the new menu - dropped entirely,
  per explicit user decision. Widget navigation stays scroll/dots/drag
  only.
- No indicator type/visibility control in the new menu - the indicator
  type ("dots"/"bars"/"hidden", see `PLAN.md` Incident 56) stays a
  settings-window-only concern. An earlier draft of this design
  included a "Hide indicators" checkbox item; dropped per explicit user
  decision during design review.
- "Hide stack" is not persisted (see below) - no settings.yaml key, no
  private-store key for it in this pass.

## Technique

Confirmed via reading `taskbar-restart-explorer`'s (Mgrmjp, windhawk.net)
published source: Windows 11's native "empty taskbar" context menu is
itself a WinUI `MenuFlyout`, built by
`winrt::Taskbar::implementation::ContextMenus::ShowTaskbarSettingsContextMenu`
inside `Taskbar.View.dll` (or `ExplorerExtensions.dll` on some
builds/versions - check both, same as the reference mod). Items are
added to it via the generic
`winrt::impl::consume_Windows_Foundation_Collections_IVector<...MenuFlyoutItemBase...>::Append`.

This is **not** the classic Win32 popup menu this mod's own PLAN.md
Incident 23 found crash-prone (`TrackPopupMenu`) - it's a real XAML
`MenuFlyout`, the same control family this mod already builds
`MenuFlyoutSubItem`/`ToggleMenuFlyoutItem` content with elsewhere. No
`TrackPopupMenu`, no global mouse hook, no separate flyout window.

Three `WindhawkUtils::SYMBOL_HOOK` hooks, on `Taskbar.View.dll`/
`ExplorerExtensions.dll` (a **different** module than the existing
`taskbar.dll` hooks in `HookTaskbarDllSymbols`, hooked with the exact
same `WindhawkUtils::SYMBOL_HOOK`/`HookSymbols` mechanism already used
in this file - no new hooking technique, just a new target module):

1. `ContextMenus::ShowTaskbarSettingsContextMenu` - wraps the original
   call in a `thread_local` depth-counter scope guard (RAII, matching
   the reference mod's `ScopedTaskbarSettingsMenuBuild`), marking "a
   native taskbar context menu is currently being built" for the
   duration of the call.
2. `IVector<MenuFlyoutItemBase>::Append` (generic - fires for **any**
   `MenuFlyoutItemBase` vector append across the whole process while
   this hook is installed, not just the taskbar's own menu, hence the
   depth-counter guard from hook 1 above gating when we actually act).
   While inside a `ShowTaskbarSettingsContextMenu` call and injection
   hasn't happened yet for this specific menu build, the **first**
   non-separator, non-our-own item appended triggers our own injection
   first (our submenu, then a separator), then lets the real append
   proceed. A `g_currentMenuInjected` flag (reset per outer call, same
   as the reference mod) ensures this happens at most once per menu
   open.
3. `LoadLibraryExW` - since `Taskbar.View.dll`/`ExplorerExtensions.dll`
   may not be loaded yet when this mod's `Wh_ModInit` runs, this hook
   catches it loading later and installs hooks 1/2 against it at that
   point (mirrors the reference mod's own `LoadLibraryExW_Hook`/
   `HandleLoadedModuleIfTaskbarView`). Does **not** conflict with this
   file's existing direct (non-hooked) `LoadLibraryExW(L"taskbar.dll",
   ...)` call in `HookTaskbarDllSymbols` - that's an ordinary API call
   against a different module, not a hook installation.

## Architecture

New free functions/globals (all in `taskbar-widget-stack.wh.cpp`,
following this file's existing single-file convention - no new file):

- `g_stackHidden` (`bool`, in-memory only, not in `g_settings` - see
  "Hide stack" below).
- `BuildNativeStackSubmenu()` → `MenuFlyoutSubItem` - builds "Widget
  stack" with its three children (Hide stack / Reset position / Stack
  settings), reusing `ResetStackPosition()` and `OpenSettingsWindow()`
  exactly as today's `ShowContextMenu` already does for those two.
- `AppendInjectedItems(void* vectorThis)` - constructs the submenu via
  `BuildNativeStackSubmenu()` and appends it (+ a separator) to the
  native vector, mirroring the reference mod's own function of the
  same name/shape.
- `MenuFlyoutItemBaseVector_Append_Hook` / `_Original` - hook 2 above.
- `ContextMenus_ShowTaskbarSettingsContextMenu_Hook` / `_Original` -
  hook 1 above, using `ScopedTaskbarSettingsMenuBuild` (ported
  verbatim in spirit from the reference mod).
- `HookTaskbarViewDllSymbols(HMODULE)` / `GetTaskbarViewModuleHandle()`
  / `LoadLibraryExW_Hook` / `HandleLoadedModuleIfTaskbarView` - the
  discovery/installation plumbing, ported in spirit from the reference
  mod's functions of the same names/shapes (adapted to this file's own
  logging/error-handling style).

Removed entirely:
- `ShowContextMenu` and its whole `MenuFlyout`-building body (the
  "Goto"/indicator-type-submenu/"Reset position"/"Stack settings"
  content it currently builds).
- `g_ui.root.RightTapped(...)` wiring (`g_ui.rightTappedToken` and its
  subscribe/unsubscribe call sites) - no longer needed; a right-click
  anywhere on the taskbar (including over the stack) now reaches
  Windows' own native context-menu flow untouched by this mod, unless
  a widget's own configured right-click action already handled it (see
  below).
- `g_contextMenuOpen` / `g_contextMenuFlyout` (were only used to
  suppress touchpad-scroll navigation while the old custom flyout was
  open, and to keep it alive - see PLAN.md's "Incident 8" history).
  Whether anything still needs to suppress touchpad nav while the
  *native* menu is open is an open question - see "Risks" below.

## Interaction with per-widget right-click actions

Unchanged: each widget's own `WireUpClickActions`-equivalent still
listens for `RightTapped` and, when a non-"None" action is configured,
handles it and calls `args.Handled(true)` - exactly as today. When
"None" (the default, e.g. weather's `ClickActionSettings.right`), it
does nothing and leaves `args.Handled` false.

**Open technical question, not resolvable without live testing**:
`RightTapped` (a gesture-recognizer event) and `ContextRequested` (the
separate routed event the native menu's own build path presumably
responds to) are two different WinUI events. This design assumes
marking `RightTapped.Handled = true` does **not** also suppress
`ContextRequested` from firing independently - i.e., that a widget's
own configured right-click action and the native menu are naturally
independent, with no code on this mod's side needed to reconcile them.
If live testing shows otherwise (e.g. a widget with a configured
right-click action unexpectedly also suppresses the native menu, or
vice versa), that's a **root-cause investigation to do at that point**,
not something to guess a workaround for now.

## "Hide stack" behavior

- New `g_stackHidden` bool, **not persisted** (no settings.yaml key, no
  private-store key) - explicit user decision: a session-scoped visual
  pause, not a durable setting. Starts `false` on every fresh
  injection (mod enable, Explorer restart).
- Toggling it sets `g_ui.root.Visibility(g_stackHidden ?
  Visibility::Collapsed : Visibility::Visible)`. `Collapsed` removes it
  from layout entirely (the taskbar reclaims the space), matches how
  this mod already uses `Visibility::Collapsed` elsewhere (the dots
  panel, Incident 51). The mod keeps running underneath - widgets keep
  ticking, settings changes keep applying - only the visual
  presence toggles.
- The "Hide stack" menu item is a `ToggleMenuFlyoutItem` (checked when
  `g_stackHidden`) - same interaction pattern as the indicator-type
  radio rows already used elsewhere in this file, just a single
  checkable row instead of a group.
- Re-showing (unchecking "Hide stack" from the same native menu) must
  work even while the stack is `Collapsed` - since the injected menu
  item is built fresh on every native menu open (from
  `AppendInjectedItems`, called inside the `Append` hook every time),
  this isn't an issue: the item's own checked-state and click handler
  are rebuilt from current `g_stackHidden` on every open regardless of
  the stack's own visibility.

## Error handling

- Symbol resolution failure (either module not found, or found but
  missing the expected mangled symbols) → `HookTaskbarViewDllSymbols`
  logs and returns `false`. Nothing else happens - the native menu
  gets no injected item, no fallback UI appears, and **the widget stack
  itself is entirely unaffected** (this must not touch `Wh_ModInit`'s
  own return value - unlike `HookTaskbarDllSymbols`, which already
  fails the *whole* mod load if `taskbar.dll`'s symbols aren't found,
  this is a materially different module doing a materially
  lower-stakes, cosmetic-only feature).
- `MenuFlyoutItemBaseVector_Append_Hook` wraps its own injection
  attempt in `try { } catch (...) { }` (matching the reference mod) -
  an exception while inspecting/injecting must never prevent the
  *original* `Append` call beneath it from still running, or every
  other item in the user's native taskbar menu (Task Manager, Taskbar
  settings, ...) would silently vanish.

## Risks / things to verify live (can't be resolved by static review)

1. The `RightTapped`-vs-`ContextRequested` independence assumption
   above.
2. Whether anything needs to suppress touchpad-scroll navigation while
   the *native* menu is open, the way `g_contextMenuOpen` did for the
   old custom flyout (Incident 8) - the native menu is a different
   window/surface entirely, so this may simply not apply, but should
   be checked live (scroll over the stack while the native menu is
   open; confirm nothing unexpected steps widgets underneath it).
3. Whether `Taskbar.View.dll` is the right module on the tester's
   actual Windows build, or whether it needs the `ExplorerExtensions.dll`
   fallback the reference mod also checks for.
4. Whether the generic `Append` hook's depth-counter guard correctly
   leaves *this mod's own* other `MenuFlyoutSubItem`/
   `ToggleMenuFlyoutItem` construction elsewhere (e.g. inside the
   settings window, or `taskbar-widget-weather`'s own click-action
   menus if any) completely unaffected - by construction it should
   (the guard only activates inside `ShowTaskbarSettingsContextMenu`'s
   own call stack), but worth confirming nothing double-injects or
   misfires during normal use of those other menus.

## Testing

- Right-click empty taskbar space → confirm "Widget stack" submenu
  appears alongside Task Manager/Taskbar settings/etc., with exactly
  three items.
- Right-click directly over a widget with no configured right-click
  action (default) → same native menu with the submenu.
- Right-click directly over a widget *with* a configured right-click
  action (e.g. weather set to "Refresh") → confirm that action fires
  and the native menu does *not* also appear (or, if the
  `RightTapped`/`ContextRequested` independence assumption above turns
  out wrong, document whatever the actual live-tested behavior is).
- Click "Hide stack" → stack disappears, taskbar reclaims the space.
  Right-click taskbar again → "Widget stack" submenu still present,
  "Hide stack" now checked, click it again → stack reappears in its
  previous position/state.
- Click "Reset position" and "Stack settings" from the new submenu →
  confirm identical behavior to today's now-removed custom menu.
- Restart Explorer with the stack hidden → confirm it comes back
  visible (non-persisted, per design).
- (If reproducible) test on a Windows build where symbol resolution is
  expected to fail, or simulate by temporarily breaking a symbol
  pattern → confirm the widget stack itself still works completely
  normally, just without the native submenu.
