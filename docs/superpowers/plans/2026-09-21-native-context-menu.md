# Native taskbar context menu integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `taskbar-widget-stack`'s own custom right-click `MenuFlyout` with a "Widget stack" submenu injected directly into Windows 11's native taskbar right-click context menu, so the stack's actions look and feel like a normal part of the taskbar instead of a visually distinct pop-up.

**Architecture:** Windows 11's native "empty taskbar" context menu is itself a WinUI `MenuFlyout`, built inside `Taskbar.View.dll`/`ExplorerExtensions.dll` by `ContextMenus::ShowTaskbarSettingsContextMenu`, with items added via the generic `IVector<MenuFlyoutItemBase>::Append`. Two `WindhawkUtils::SYMBOL_HOOK`s on that module (plus a `LoadLibraryExW` hook to catch it loading late) let this mod inject its own `MenuFlyoutSubItem` into that vector the first time it's appended to during a menu build, using a `thread_local` depth counter to know when a build is in progress. This is the exact technique published by `taskbar-restart-explorer` (Mgrmjp, windhawk.net) - not the classic Win32 `TrackPopupMenu` this mod's own PLAN.md (Incident 23) found crash-prone.

**Tech Stack:** C++/WinRT XAML (`MenuFlyoutSubItem`, `MenuFlyoutItem`, `ToggleMenuFlyoutItem`, `MenuFlyoutSeparator` - all already used elsewhere in this file), `WindhawkUtils::SYMBOL_HOOK`/`HookSymbols` (already used in this same file for `taskbar.dll`, now a second time for a different module), `WindhawkUtils::SetFunctionHook` (for the `LoadLibraryExW` hook).

**Spec:** [docs/superpowers/specs/2026-09-21-native-context-menu-design.md](../specs/2026-09-21-native-context-menu-design.md)

## Global Constraints

- No automated test framework / no local compiler in this working environment - every mod in this repo is verified by a written manual-verification checklist plus the user's own live Windhawk compile/test pass. Every "verification" step below is a code-reading check, not an execution.
- If the required symbols can't be found (unsupported Windows/taskbar build), this feature must silently do nothing - no crash, no fallback UI, and critically **no impact on `Wh_ModInit`'s own return value** (unlike `HookTaskbarDllSymbols`, whose failure already fails the whole mod load - this is a different, lower-stakes module and must not gate the rest of the mod).
- "Hide stack" is explicitly **not persisted** - no settings.yaml key, no private-store key. A plain in-memory `bool`, reset to `false` (visible) on every fresh injection.
- No "Goto" entry and no indicator-type control in the new menu - both dropped per explicit user decision during design (spec's Non-goals section).
- `MenuFlyoutItemBaseVector_Append_Hook` must wrap its own injection attempt in `try {} catch (...) {}` so an exception there can never suppress the *original* `Append` call beneath it - if it did, every other native taskbar menu item (Task Manager, Taskbar settings, ...) would silently vanish too.

---

## File Structure

No new files. Every task modifies the one existing file:

```
taskbar-widget-stack/taskbar-widget-stack.wh.cpp   (Tasks 1-2)
taskbar-widget-stack/PLAN.md                        (Task 3)
```

---

### Task 1: Add the native menu injection (new code, coexists with the old menu for now)

**Files:**
- Modify: `taskbar-widget-stack/taskbar-widget-stack.wh.cpp`

**Interfaces:**
- Consumes: `ResetStackPosition()` (existing, no-arg, `void`), `OpenSettingsWindow()` (existing, no-arg, returns `bool`), `g_ui.root` (existing `Grid`, the stack's own root element - `IsVisible`/`Visibility` target for "Hide stack").
- Produces: `g_stackHidden` (`bool`, new global), `BuildNativeStackSubmenu()` (returns `MenuFlyoutSubItem`), `HookTaskbarViewDllSymbols(HMODULE)` (returns `bool`) - Task 2 doesn't call any of these directly, but Task 3's documentation references this task's behavior.

This task is purely additive - the old `ShowContextMenu`/`RightTapped` wiring stays untouched and still works, so the native submenu and the old on-widget flyout will both be reachable at the end of this task (independently testable: right-click empty taskbar space to see the new submenu; right-click the widget stack itself to see the still-present old menu). Task 2 removes the old one.

- [ ] **Step 1: Add `g_stackHidden` and the discovery/hook-installation plumbing**

Find `HookTaskbarDllSymbols` (search `bool HookTaskbarDllSymbols() {`, around line 1172) and insert the following immediately **before** it (this ordering matters only for readability - there's no compile-order dependency either way since these don't call each other):

```cpp
// ---------------------------------------------------------------------
// Native taskbar context menu integration (Incident 59, user request)
//
// Windows 11's own "empty taskbar" right-click menu is itself a WinUI
// MenuFlyout - built by ContextMenus::ShowTaskbarSettingsContextMenu in
// Taskbar.View.dll (or ExplorerExtensions.dll on some builds), with
// items added via the generic IVector<MenuFlyoutItemBase>::Append.
// This is a *different* module than taskbar.dll (hooked above/below by
// HookTaskbarDllSymbols) and a *different* mechanism than this mod's
// own now-removed ShowContextMenu (Incident 23's XAML MenuFlyout,
// shown by this mod itself) - confirmed by reading
// taskbar-restart-explorer's (Mgrmjp, windhawk.net) published source,
// which uses exactly this technique. See
// docs/superpowers/specs/2026-09-21-native-context-menu-design.md.
// ---------------------------------------------------------------------

// Not persisted anywhere (explicit user decision) - a session-scoped
// visual pause. Starts false (visible) on every fresh injection.
bool g_stackHidden = false;

thread_local int g_taskbarSettingsMenuDepth = 0;
thread_local bool g_currentMenuInjected = false;

bool IsMenuFlyoutItemBaseNamed(MenuFlyoutItemBase const& baseItem,
                                const wchar_t* name) {
    try {
        if (auto fe = baseItem.try_as<FrameworkElement>()) {
            return fe.Name() == name;
        }
    } catch (...) {
    }
    return false;
}

bool IsMenuFlyoutSeparatorItem(MenuFlyoutItemBase const& baseItem) {
    try {
        return !!baseItem.try_as<MenuFlyoutSeparator>();
    } catch (...) {
    }
    return false;
}

constexpr wchar_t kNativeMenuItemName[] = L"WindhawkWidgetStackItem";
constexpr wchar_t kNativeMenuSeparatorName[] = L"WindhawkWidgetStackSeparator";

// "Widget stack" submenu - Hide stack / Reset position / Stack settings,
// per the design spec's explicit menu content (Non-goals: no "Goto",
// no indicator control here - both settings-window/scroll-only now).
MenuFlyoutSubItem BuildNativeStackSubmenu() {
    MenuFlyoutSubItem root;
    root.Name(kNativeMenuItemName);
    root.Text(L"Widget stack");
    FontIcon rootIcon;
    rootIcon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
    // "Stack"/layered-squares glyph - a best-effort pick (this SDK's
    // codepoints aren't always reliably guessable, per this file's own
    // Incident 31), confirm it actually renders as a stack icon during
    // live testing; if not, swap for a plain Unicode character instead
    // (this file already does that in a few other places rather than
    // risk a wrong/missing glyph).
    rootIcon.Glyph(L"");
    root.Icon(rootIcon);

    ToggleMenuFlyoutItem hideStackItem;
    hideStackItem.Text(L"Hide stack");
    hideStackItem.IsChecked(g_stackHidden);
    hideStackItem.Click(
        [](winrt::Windows::Foundation::IInspectable const&,
           RoutedEventArgs const&) {
            g_stackHidden = !g_stackHidden;
            if (g_ui.root) {
                g_ui.root.Visibility(g_stackHidden ? Visibility::Collapsed
                                                    : Visibility::Visible);
            }
        });
    root.Items().Append(hideStackItem);

    MenuFlyoutItem resetPositionItem;
    resetPositionItem.Text(L"Reset position");
    FontIcon resetPositionIcon;
    resetPositionIcon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
    resetPositionIcon.Glyph(L"");  // refresh/realign glyph
    resetPositionItem.Icon(resetPositionIcon);
    resetPositionItem.Click(
        [](winrt::Windows::Foundation::IInspectable const&,
           RoutedEventArgs const&) { ResetStackPosition(); });
    root.Items().Append(resetPositionItem);

    MenuFlyoutItem settingsItem;
    settingsItem.Text(L"Stack settings");
    FontIcon settingsIcon;
    settingsIcon.FontFamily(FontFamily(L"Segoe MDL2 Assets"));
    settingsIcon.Glyph(L"");  // gear/settings glyph
    settingsItem.Icon(settingsIcon);
    settingsItem.Click([](winrt::Windows::Foundation::IInspectable const&,
                           RoutedEventArgs const&) { OpenSettingsWindow(); });
    root.Items().Append(settingsItem);

    return root;
}

using MenuFlyoutItemBaseVector_Append_t =
    void(__cdecl*)(void* pThis, MenuFlyoutItemBase const& item);
MenuFlyoutItemBaseVector_Append_t MenuFlyoutItemBaseVector_Append_Original;

void AppendInjectedNativeMenuItems(void* vectorThis) {
    auto submenu = BuildNativeStackSubmenu();
    MenuFlyoutSeparator separator;
    separator.Name(kNativeMenuSeparatorName);

    MenuFlyoutItemBaseVector_Append_Original(vectorThis, submenu);
    MenuFlyoutItemBaseVector_Append_Original(vectorThis, separator);

    g_currentMenuInjected = true;
    Wh_Log(L"Injected Widget stack into the native taskbar context menu");
}

void __cdecl MenuFlyoutItemBaseVector_Append_Hook(
    void* pThis,
    MenuFlyoutItemBase const& item) {
    if (g_taskbarSettingsMenuDepth > 0 && !g_currentMenuInjected) {
        try {
            if (!IsMenuFlyoutSeparatorItem(item) &&
                !IsMenuFlyoutItemBaseNamed(item, kNativeMenuItemName) &&
                !IsMenuFlyoutItemBaseNamed(item, kNativeMenuSeparatorName)) {
                AppendInjectedNativeMenuItems(pThis);
            }
        } catch (...) {
            Wh_Log(L"Native taskbar menu append inspection failed; skipping "
                   L"injection");
        }
    }
    MenuFlyoutItemBaseVector_Append_Original(pThis, item);
}

struct ScopedTaskbarSettingsMenuBuild {
    bool outer = false;
    ScopedTaskbarSettingsMenuBuild() {
        outer = g_taskbarSettingsMenuDepth++ == 0;
        if (outer) {
            g_currentMenuInjected = false;
        }
    }
    ~ScopedTaskbarSettingsMenuBuild() { g_taskbarSettingsMenuDepth--; }
};

using ContextMenus_ShowTaskbarSettingsContextMenu_t =
    void(__cdecl*)(FrameworkElement const& target, void* taskbarSettings,
                   wuxi::ContextRequestedEventArgs const& args,
                   unsigned long long options);
ContextMenus_ShowTaskbarSettingsContextMenu_t
    ContextMenus_ShowTaskbarSettingsContextMenu_Original;

void __cdecl ContextMenus_ShowTaskbarSettingsContextMenu_Hook(
    FrameworkElement const& target, void* taskbarSettings,
    wuxi::ContextRequestedEventArgs const& args,
    unsigned long long options) {
    ScopedTaskbarSettingsMenuBuild scopedBuild;
    ContextMenus_ShowTaskbarSettingsContextMenu_Original(
        target, taskbarSettings, args, options);
}

HMODULE GetTaskbarViewModuleHandle() {
    HMODULE module = GetModuleHandleW(L"Taskbar.View.dll");
    if (!module) {
        module = GetModuleHandleW(L"ExplorerExtensions.dll");
    }
    return module;
}

bool HookTaskbarViewDllSymbols(HMODULE module) {
    WindhawkUtils::SYMBOL_HOOK taskbarViewDllHooks[] = {
        {
            {
                LR"(void __cdecl winrt::Taskbar::implementation::ContextMenus::ShowTaskbarSettingsContextMenu(struct winrt::Windows::UI::Xaml::FrameworkElement const &,struct winrt::WindowsUdk::UI::Shell::TaskbarSettings const &,struct winrt::Windows::UI::Xaml::Input::ContextRequestedEventArgs const &,unsigned __int64))",
            },
            &ContextMenus_ShowTaskbarSettingsContextMenu_Original,
            ContextMenus_ShowTaskbarSettingsContextMenu_Hook,
        },
        {
            {
                LR"(public: __cdecl winrt::impl::consume_Windows_Foundation_Collections_IVector<struct winrt::Windows::Foundation::Collections::IVector<struct winrt::Windows::UI::Xaml::Controls::MenuFlyoutItemBase>,struct winrt::Windows::UI::Xaml::Controls::MenuFlyoutItemBase>::Append(struct winrt::Windows::UI::Xaml::Controls::MenuFlyoutItemBase const &)const )",
            },
            &MenuFlyoutItemBaseVector_Append_Original,
            MenuFlyoutItemBaseVector_Append_Hook,
        },
    };
    if (!WindhawkUtils::HookSymbols(module, taskbarViewDllHooks,
                                     ARRAYSIZE(taskbarViewDllHooks))) {
        Wh_Log(L"Native taskbar menu: symbol hook failed, feature disabled");
        return false;
    }
    Wh_Log(L"Native taskbar menu: hooks installed");
    return true;
}

std::atomic<bool> g_taskbarViewModuleHooked = false;

void HandleLoadedModuleIfTaskbarView(HMODULE module) {
    if (!module || g_taskbarViewModuleHooked) {
        return;
    }
    if (GetTaskbarViewModuleHandle() != module) {
        return;
    }
    if (g_taskbarViewModuleHooked.exchange(true)) {
        return;
    }
    if (HookTaskbarViewDllSymbols(module)) {
        Wh_ApplyHookOperations();
    }
}

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_Original;

HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR lpLibFileName, HANDLE hFile,
                                    DWORD dwFlags) {
    HMODULE module = LoadLibraryExW_Original(lpLibFileName, hFile, dwFlags);
    if (module) {
        HandleLoadedModuleIfTaskbarView(module);
    }
    return module;
}

// Best-effort, non-fatal (Global Constraints: this must never affect
// Wh_ModInit's own return value or the rest of the mod). Tries to hook
// immediately if the module is already loaded; otherwise installs a
// LoadLibraryExW hook to catch it loading later.
void InitNativeTaskbarMenuHook() {
    if (HMODULE module = GetTaskbarViewModuleHandle()) {
        g_taskbarViewModuleHooked = true;
        HookTaskbarViewDllSymbols(module);
        return;
    }
    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibraryExW =
        kernelBase ? reinterpret_cast<LoadLibraryExW_t>(
                         GetProcAddress(kernelBase, "LoadLibraryExW"))
                   : nullptr;
    if (!loadLibraryExW) {
        Wh_Log(L"Native taskbar menu: LoadLibraryExW not found, feature "
               L"disabled");
        return;
    }
    if (!WindhawkUtils::SetFunctionHook(loadLibraryExW, LoadLibraryExW_Hook,
                                         &LoadLibraryExW_Original)) {
        Wh_Log(L"Native taskbar menu: LoadLibraryExW hook install failed");
    }
}
```

- [ ] **Step 2: Call `InitNativeTaskbarMenuHook()` from `Wh_ModInit`, without gating its return value**

Find `Wh_ModInit` (search `BOOL Wh_ModInit() {`, around line 4342):
```cpp
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
```
Add the call right before `return TRUE;` - deliberately **not** checked/gated, per the Global Constraints requirement that this feature's failure must never affect the rest of the mod:
```cpp
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

    // Best-effort - never gates the mod's own load (see its own comment
    // and docs/superpowers/specs/2026-09-21-native-context-menu-design.md).
    InitNativeTaskbarMenuHook();

    return TRUE;
}
```

- [ ] **Step 3: Manual verification (code-reading only, no compiler here)**

Confirm every type referenced in Step 1's new code already has a `using`/namespace alias earlier in this file: `MenuFlyoutItemBase`, `MenuFlyoutSubItem`, `MenuFlyoutItem`, `ToggleMenuFlyoutItem`, `MenuFlyoutSeparator`, `FontIcon`, `FontFamily` (all via `using namespace winrt::Windows::UI::Xaml::Controls;`), `wuxi::ContextRequestedEventArgs` (via `namespace wuxi = winrt::Windows::UI::Xaml::Input;`, already used elsewhere in this file for `wuxi::PointerRoutedEventArgs`/`wuxi::RightTappedRoutedEventArgs`). Confirm `WindhawkUtils::SYMBOL_HOOK`/`HookSymbols`/`SetFunctionHook` match the exact call shapes already used by `HookTaskbarDllSymbols` (search that function) - same struct/array/`ARRAYSIZE` pattern, just a different target module and symbol list. Confirm `Visibility::Collapsed`/`Visibility::Visible` are already used elsewhere in this file (search `Visibility::Collapsed` - used on `g_ui.dotsPanel` already) so this isn't a new API surface. Confirm `g_ui.root` is declared before this new code runs (it's a `UiState` member, declared far earlier in the file - the injected click handler only reads it at click-time, long after injection, so there's no ordering issue even though `BuildNativeStackSubmenu` is defined before `UiState`'s own usage sites elsewhere).

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-stack/taskbar-widget-stack.wh.cpp
git commit -m "Add native taskbar context menu injection (Widget stack submenu)"
```

---

### Task 2: Remove the old custom right-click MenuFlyout and its wiring

**Files:**
- Modify: `taskbar-widget-stack/taskbar-widget-stack.wh.cpp`

**Interfaces:**
- Consumes: nothing from Task 1 directly (independent removal).
- Produces: nothing other tasks depend on.

- [ ] **Step 1: Remove `ShowContextMenu`'s forward declaration and body**

Find the forward declaration (search `void ShowContextMenu(HWND hWnd, POINT screenPt);`, around line 1555) and delete that single line.

Find the full function (search `void ShowContextMenu(HWND, POINT) {`, around line 3380) along with its entire preceding doc-comment block (starts at `// Right-click menu, as a XAML MenuFlyout rather than a native`, around line 3331) and delete everything from that comment block's first line through the function's closing `}` (around line 3480) - the whole block, comment and code together.

- [ ] **Step 2: Remove the `RightTapped` wiring in `WireUpNavigation`**

Find this block inside `WireUpNavigation` (search `g_ui.rightTappedToken = g_ui.root.RightTapped(`, around line 1653):
```cpp
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

```
Delete the whole block (including its trailing blank line). Right-clicking the stack's own root now falls through untouched to Windows' own native context-menu handling (Task 1's injected submenu), instead of this mod intercepting it.

- [ ] **Step 3: Remove the now-dead `g_ui.rightTappedToken` unhook call in `UnwireNavigation`**

Find this block (search `if (g_ui.rightTappedToken) {`, around line 1714):
```cpp
        if (g_ui.rightTappedToken) {
            g_ui.root.RightTapped(g_ui.rightTappedToken);
        }
```
Delete it (its surrounding `if (g_ui.wheelToken) {...}` / `if (g_ui.pressedToken) {...}` etc. siblings stay untouched).

- [ ] **Step 4: Remove the `rightTappedToken` field from `UiState`**

Find the field (search `winrt::event_token rightTappedToken;`, around line 1305) and delete that line.

- [ ] **Step 5: Remove `g_contextMenuOpen`/`g_contextMenuFlyout` and update their one remaining reader**

Find and delete these two globals and their preceding comments (search `bool g_contextMenuOpen = false;`, around line 2180, through `MenuFlyout g_contextMenuFlyout{nullptr};`, around line 2188 - read the exact surrounding comment text first, since it explains *why* they existed, and delete that explanation along with the declarations it's attached to).

Find their one remaining reader, in `TaskbarWindowSubclassProc`'s `WM_INPUT` handler (search `if (msg == WM_INPUT && g_settings.navWheel && !g_contextMenuOpen) {`, around line 3506):
```cpp
    if (msg == WM_INPUT && g_settings.navWheel && !g_contextMenuOpen) {
```
Change to:
```cpp
    if (msg == WM_INPUT && g_settings.navWheel) {
```
**This is a known, accepted regression to verify live, not silently paper over** (spec's Risk #2): touchpad-scroll navigation used to pause while this mod's own context menu was open (`g_contextMenuOpen`); there is no equivalent signal for "the *native* taskbar menu is currently open" available to this mod without additional hooks this plan doesn't add. Note this explicitly in Task 3's PLAN.md entry rather than leaving it undocumented.

- [ ] **Step 6: Manual verification (code-reading only, no compiler here)**

Search the whole file for `ShowContextMenu`, `g_contextMenuOpen`, `g_contextMenuFlyout`, and `rightTappedToken` - confirm zero remaining references after Steps 1-4 (a leftover reference to a deleted symbol is a compile error the user's own Windhawk compile pass would catch, but confirming it here first saves that round-trip). Confirm `WireUpNavigation`'s other four token subscriptions (`wheelToken`/`pressedToken`/`movedToken`/`releasedToken`) and `UnwireNavigation`'s matching four unhook calls are untouched by this task - only the fifth (`rightTappedToken`) is removed.

- [ ] **Step 7: Commit**

```bash
git add taskbar-widget-stack/taskbar-widget-stack.wh.cpp
git commit -m "Remove the old custom right-click MenuFlyout, superseded by the native menu injection"
```

---

### Task 3: Documentation + version bump

**Files:**
- Modify: `taskbar-widget-stack/PLAN.md`
- Modify: `taskbar-widget-stack/taskbar-widget-stack.wh.cpp` (`@version` line only)

**Interfaces:**
- Consumes: the completed changes from Tasks 1-2.
- Produces: nothing - documentation only.

- [ ] **Step 1: Add an Incident entry to `PLAN.md`**

Check the file's current highest `## Incident N` number first (values will have moved since this plan was written), then append a new dated entry using the next sequential number, following this file's own established format:

- **Symptom**: right-clicking the widget stack showed a visually distinct custom flyout, inconsistent with right-clicking empty taskbar space (Windows' own native menu) right next to it.
- **Fix**: summarize Task 1 (symbol-hook injection into the native menu, "Widget stack" submenu with Hide stack/Reset position/Stack settings) and Task 2 (old `ShowContextMenu`/its `RightTapped` wiring removed entirely - no fallback, per explicit user decision if the native hook fails on an unsupported build).
- **Known regression** (carry Task 2 Step 5's note forward): touchpad-scroll navigation no longer pauses while a context menu is open - this mod has no signal for "the native taskbar menu is currently open" the way it did for its own now-removed flyout (`g_contextMenuOpen`). Flag as open, not fixed in this pass.
- **Open questions to resolve from live testing** (copy from the spec's own "Risks" section): whether a widget's own configured right-click action (e.g. weather's `ClickActionSettings.right`) and the native menu's `ContextRequested` handling interact cleanly or suppress each other; whether `Taskbar.View.dll` vs `ExplorerExtensions.dll` is the right module on the tester's actual build; whether the `` "Stack" glyph on the submenu actually renders as intended.
- **Next retest**: copy the spec's own "Testing" section verbatim (right-click empty taskbar space / over a widget with no configured action / over a widget with a configured action / Hide stack toggle + persistence-across-Explorer-restart check / Reset position + Stack settings still work).

- [ ] **Step 2: Bump `@version`**

Read the current `// @version` line first (its value will have moved since this plan was written) and bump it by one minor-level increment (per this repo's own standing convention - a behavior change/removal this size, not a one-line tweak, warrants more than a patch bump; consistent with prior minor bumps this session for comparably-scoped changes, e.g. the width-ABI change).

- [ ] **Step 3: Commit**

```bash
git add taskbar-widget-stack/PLAN.md taskbar-widget-stack/taskbar-widget-stack.wh.cpp
git commit -m "Document the native context menu change; bump version"
```
