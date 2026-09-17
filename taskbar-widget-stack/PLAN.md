# taskbar-widget-stack — Windhawk mod plan

## Context
User already runs several taskbar-area mods that each claim the space left of
the system tray: `windows-11-start-menu-styler` (Luminosity Dock theme),
`Taskbar-Fluent-Media-Player` (Salyts), and `taskbar-ai-quota`. Goal: replace
"one widget occupies that space" with a vertically snap-scrollable stack (à la
iOS widget stack), with left-side page-indicator dots, where widgets can be
enabled/disabled/reordered, and — eventually — third-party mods can register
themselves as stack members via a small SDK.

Decisions from the design interview (2026-09-16):
- **Architecture**: host + SDK (a widget-hosting mod exposing a small C ABI
  other mods can call into), not a one-off fork of the two existing mods.
  Chosen deliberately for extensibility despite the extra up-front cost.
- **Scope**: open ecosystem intended eventually (published on windhawk.net
  once stable) — API needs to be versioned/documented, not a throwaway
  personal hack.
- **Navigation**: wheel-scroll, dot-click, and drag should all work, and
  which ones are active should be a user setting (not hardcoded).
- **Crash isolation**: each widget's render/update call is wrapped so a
  misbehaving widget gets auto-disabled rather than risking `explorer.exe`.
- **Config UI**: a custom right-click overlay menu (not just the plain
  Windhawk settings editor) — phase 2 will need real drag & drop; phase 1
  ships enable/disable + move-up/move-down via a native popup menu, which is
  the pragmatic Win32 starting point (`TrackPopupMenu`) before investing in
  a custom-drawn reorder UI.
- **Starting point chosen: minimal prototype first.** Validate that a
  vertically stacked, snap-scrolling overlay with dot indicators actually
  renders and feels right in the real taskbar *before* building out the
  inter-mod SDK or onboarding real widget content. This mod's first version
  therefore stacks two **placeholder** panes (labeled blocks, not real
  media-player/AI-quota content) — see "Out of scope (this version)".

## Why host+SDK instead of forking the two mods
A Windhawk mod is a DLL injected independently into `explorer.exe`. For
widgets to live inside one scrollable stack, something has to own a single
overlay window and composite each widget's content into it. Two options:
1. Fork the existing mods' rendering code directly into this mod. Fast, but
   this mod's author then owns re-porting every upstream change by hand.
2. This mod owns the overlay/scroll/snap/indicator mechanics and exposes a
   minimal **exported-function ABI** other mod DLLs can bind to at runtime
   (`GetProcAddress` against `explorer.exe`'s module list, since Windhawk
   mod DLLs are loaded into the same process) to register a widget: a
   paint callback, a size, an optional click handler, a stable string id.

Option 2 was chosen. It costs more up front (a real ABI, versioning, a
registration handshake) but means future widgets — including a real port of
the media player / AI quota mods, written later — don't require touching
this mod's internals.

**Not implemented yet in this version**: the actual SDK/ABI. This prototype
hardcodes its widget list internally. The SDK is the next milestone once the
overlay mechanics are confirmed to work live (see "Next steps").

## File
`taskbar-widget-stack.wh.cpp` — single-file Windhawk mod (matches this repo's
convention), metadata header in the standard Windhawk `==WindhawkMod==`
format.

## Reference points (this repo)
- `windows-11-start-menu-button/windows-11-start-menu-button.wh.cpp` — this
  repo's only other mod; reused conventions from it: crash containment via
  try/catch around per-frame/per-callback work (see its "Crash containment"
  section — an orphaned callback surviving DLL unload is a real
  Control-Flow-Guard-fail-fast crash risk, confirmed live there), explicit
  teardown of anything registered with the OS (rendering subscriptions,
  window classes, hooks) from `Wh_ModBeforeUninit`/`Wh_ModUninit`, and
  settings exposed as live-editable Windhawk settings rather than compile
  constants so visual tuning doesn't force a reload cycle.
- Known taskbar-area overlay technique used by mods like
  `taskbar-ai-quota`/`Taskbar-Fluent-Media-Player` (not yet read — see
  "Open questions"): locate the tray notification area via the standard
  window chain `Shell_TrayWnd` → `TrayNotifyWnd`, create a `WS_CHILD` overlay
  window parented into that area (or positioned via `SetWindowPos` adjacent
  to it), and reposition it on `WM_SIZE`/DPI-change/taskbar-relayout
  notifications. This prototype uses that same general technique since it's
  the well-established pattern for "add custom content to the taskbar" mods
  on Windows 11 (whose taskbar itself is XAML and not directly extensible).

## Prototype scope (this version)
1. **Overlay window**: one `WS_CHILD` window created and positioned in the
   tray-adjacent area (same general area the user currently reserves for the
   media-player/AI-quota mods), fixed width matching typical taskbar icon
   width, height matching taskbar height.
2. **Widgets**: a hardcoded array of 2 placeholder panes (solid-color block +
   centered label text, GDI-drawn), each carrying `id`, `enabled`,
   `displayOrder`.
3. **Stack rendering**: only the current pane draws at full opacity; a
   snap-scroll transitions vertically between panes (owner-drawn, offscreen
   bitmap + `BitBlt`, eased position over a short animation using a timer —
   matches the "iOS widget stack" feel requested).
4. **Indicators**: small dots/dashes drawn along the left edge of the
   overlay, one per *enabled* widget, current one highlighted; each is
   independently hit-tested for click-to-jump.
5. **Navigation settings** (all independently toggleable, matching the
   "configurable" answer from the interview):
   - `nav.wheel` (bool, default true) — mouse wheel over the stack changes
     pane.
   - `nav.dots` (bool, default true) — click a dot to jump to that pane.
   - `nav.drag` (bool, default true) — vertical drag on the stack itself
     scrubs between panes with snap-back.
6. **Config menu**: right-click on the stack opens a native popup menu
   listing each widget with a checkmark (enable/disable) and "Move up"/"Move
   down" entries; selections persist to Windhawk settings immediately.
7. **Crash isolation**: each widget's draw call wrapped in try/catch
   (structured + `std::exception`); a widget that throws is flagged
   disabled for the remainder of the session and skipped on redraw (matches
   the "host catches and disables" decision). This prototype's own
   placeholder widgets can't realistically throw, but the wrapper is in
   place now so the pattern already exists when a real SDK plugin type
   (widget code the host doesn't fully control) enters the picture.

## Out of scope (this version)
- The actual widget-registration SDK/ABI for third-party mods (see "Next
  steps").
- Real media-player/AI-quota content — placeholders only, until their
  source is read and a decision is made on how they'd plug into the SDK.
- Drag-and-drop reorder UI (popup menu move-up/down instead, for now).
- Any settings/behavior related to the native Windows 11 widgets panel
  (weather/stocks) the user has hidden — not part of this mod.
- Multi-monitor-specific testing (single overlay per taskbar instance is the
  intent, following the pattern in this repo's other mod, but not yet
  verified live).

## Open questions / things to verify before/while building
- Haven't read `Taskbar-Fluent-Media-Player`'s or `taskbar-ai-quota`'s
  source yet — needed before designing the actual SDK surface (what a real
  widget's paint/update/click needs look like) and before confirming the
  exact parenting technique they use for their overlay windows (assumed
  above from common practice in this mod category, not yet confirmed
  against their code).
- No Windows machine available in this development environment — this mod
  cannot be compiled or run here. Needs to be tested by the user in
  Windhawk on their Windows 11 25H2 machine, same as the existing mod in
  this repo (see its README's "vibe-coded, tested manually" note).
- Exact insertion point/order relative to the other taskbar mods already
  running (Luminosity Dock start button, etc.) is unverified — may need
  z-order/positioning adjustments once tested live.

## Next steps (not started)
1. ~~Get this prototype confirmed working live~~ - **done, 2026-09-16**:
   injection, positioning, mouse-wheel scroll, dot-click, and right-click's
   menu all confirmed live. Drag (press-and-hold-and-move) not yet
   specifically confirmed; two-finger trackpad scroll confirmed *not*
   working (see "Known open item" above) - lower priority, not blocking.
2. ~~Trim the diagnostic `Wh_Log` calls~~ - **done, 2026-09-17**: the
   per-step traces from Incidents 7-13 were already stripped as each
   root cause was found (see those incidents' own entries). What
   remained after touchpad scroll was confirmed working (Incident 19)
   was the `WH_MOUSE_LL` system-wide low-level mouse hook added in
   Incident 16 purely to rule out a routing theory - it had already
   done its job (confirmed the touchpad gesture never produces
   `WM_MOUSEWHEEL` anywhere on the desktop) and was dead weight once
   the HID route replaced it, plus a real cost (a low-level hook runs
   for every mouse event system-wide). Removed `LowLevelMouseProc`,
   `g_mouseHook`, and its install/teardown entirely. The remaining
   `Wh_Log` calls are all lifecycle/error logs (injection
   success/failure, symbol hook failure, raw-input registration
   failure), not step-by-step traces, and are left in place.
3. ~~Investigate two-finger trackpad scroll~~ - **done, 2026-09-17**:
   confirmed working via raw HID digitizer input, see Incidents 17-19.
4. ~~Read `Taskbar-Fluent-Media-Player` and `taskbar-ai-quota` source to
   design the real widget SDK contract~~ - **done, 2026-09-17**, see
   "Widget SDK design (draft)" below.
5. ~~Implement the `IWidget` interface and host-side lifecycle/crash
   isolation~~ - **done, 2026-09-17**: `WidgetHost`/`IWidget`/
   `WidgetEntry` added, both placeholders ported onto `PlaceholderWidget
   : IWidget`. `RebuildStackContents` now calls `Destroy()` then
   `Create()` on every widget on every rebuild (toggle/reorder/settings
   change), matching the reference mods' Remove-then-Inject pattern, and
   computes the stack's content width as
   `min(max(enabled widget desired widths), layout.maxWidth)` via the
   new `ApplyStackWidth`. Not yet live-tested - not expected to change
   anything visually (both placeholders still report the same
   `kMinContentWidth` they were fixed at before), but this is the first
   real exercise of the new machinery end to end and should be
   confirmed live before porting a real widget onto it.
6. Port one real widget (likely AI quota, simpler) through the new SDK as
   the SDK's first real consumer, before attempting the media player.
7. Revisit config UI: replace the native popup menu with a custom-drawn
   drag-and-drop reorder panel, per the original request. Also removes
   `TrackPopupMenu`'s nested Win32 message loop entirely, which is the
   root cause class behind Incidents 10 and 20 - a real XAML-based menu
   wouldn't reenter the taskbar's WM_INPUT/dispatcher handling the way a
   native modal popup does. User pointed at
   `taskbar-icon-separators` (windhawk.net) as a mod that already builds
   a WinUI-style right-click menu / integrates with the taskbar's own
   context menu - worth reading before designing this, may avoid
   re-deriving the technique from scratch.
8. Write a versioned SDK doc once the ABI stabilizes, ahead of any public
   windhawk.net listing.

## Widget SDK design (draft, 2026-09-17)

Based on reading `taskbar-ai-quota.wh.cpp` and
`taskbar-fluent-media-player.wh.cpp` (both ~9-10k lines). Common
pattern in both, independent of each other: a single named root `Grid`
rebuilt from scratch on settings changes (not patched in place),
injected into a taskbar container found by walking the cached
`XamlRoot`; `DispatcherTimer` as the tick primitive (1min for AI
quota's pace recompute, 16ms/~60fps for the media player's marquee/
visualizer animation) with explicit `event_token` revocation, plus
optional background `CreateThread` workers for slow polling (login,
media session, theme) that marshal back to the UI thread; input
handlers (`Click`/`Tapped`/`Pointer*`) attached directly to owned
elements, tokens tracked and revoked on teardown; neither negotiates
size with a host - each just sets its own explicit pixel
widths/heights; both wrap injection/removal/token-revocation in
pervasive `try{}catch(...){}` so one failure doesn't abort the whole
teardown/rebuild.

This is a same-file, in-process plugin pattern (not a cross-mod/DLL
ABI - out of scope per this file's own "Out of scope" section above),
so the "SDK" is a C++ interface every widget implementation compiles
into this one `.wh.cpp`, not a loadable third-party plugin format yet.

```cpp
struct WidgetHost {
    HWND taskbarHwnd;
    winrt::Windows::UI::Xaml::Controls::Grid parent;  // widget's root attaches here
    double paneHeight;                                 // fixed - see PaneHeight()
};

struct IWidget {
    virtual ~IWidget() = default;
    virtual std::wstring Id() const = 0;
    // Builds and attaches the widget's own root element under
    // host.parent. Returns the widget's desired width in DIPs (see
    // stack-width behavior below) - a widget sizes its own height to
    // host.paneHeight but reports the width it wants.
    virtual double Create(const WidgetHost& host) = 0;
    // Called on the stack's shared tick. A widget needing a different
    // cadence (e.g. a 16ms visualizer) owns its own DispatcherTimer
    // internally, same as the reference mods do - Tick() is just the
    // host's "something may have changed, redraw if needed" signal.
    virtual void Tick() = 0;
    // Widget re-reads its own settings sub-namespace and rebuilds
    // internally (matches both reference mods' Remove+Inject-on-change
    // pattern) - may also change the widget's desired width, so the
    // host re-queries it after this call.
    virtual double OnSettingsChanged() = 0;
    // Revokes every owned event token/timer, then removes its root
    // element from host.parent. Must be safe to call even if Create()
    // partially failed.
    virtual void Destroy() = 0;
};
```

Host-side lifecycle rules (in `taskbar-widget-stack.wh.cpp` itself,
not in each widget): every `IWidget` call (`Create`/`Tick`/
`OnSettingsChanged`/`Destroy`) is wrapped in `try/catch` by the host,
matching the "host catches and disables" decision already made for
this mod and the pervasive try/catch idiom both reference mods use
internally - a widget that throws gets its existing `crashed` flag set
and is skipped for the rest of the session, same as today's stub logic
already anticipates. Each widget gets its own settings sub-namespace
(e.g. `widget.aiQuota.*`, `widget.mediaPlayer.*`) so a ported widget's
existing settings schema can carry over largely unchanged.

**Stack width (per user decision, 2026-09-17)**: `kStackWidth` becomes
dynamic instead of the current fixed `40.0` - the stack sizes itself to
the widest currently-enabled widget's desired width (the `double`
`Create`/`OnSettingsChanged` return), capped at a new setting
`layout.maxWidth` (int, default `520`, user-configurable). Any widget
narrower than the resulting stack width stretches to fill it
(`HorizontalAlignment::Stretch` on its root, full width available).
Recomputing the stack's width only needs to happen when the set of
enabled widgets or their settings change (widget add/remove/reorder/
settings-changed), not per-tick - so `RebuildStackContents` computes
`max(enabledWidgetWidths) capped at layout.maxWidth` once and applies
it to `root.Width()`/`kDotsColumnWidth`-adjusted content width, same
place it already rebuilds `dotsPanel`/`widgetsPanel`.

## Verification
- **2026-09-16, confirmed live**: `InjectWidgetStackGrid` logs "Injected
  widget stack" with no error and no Explorer freeze, on the user's ARM64
  Windows machine (v0.1.3 - runtime dual-arch `TaskbarHost::FrameHeight`
  scan, see "Incident 3"). This is the first live confirmation the mod's
  XAML injection actually runs end to end (symbol hooks resolve, XamlRoot
  reached, `SystemTrayFrameGrid` found, column inserted). Visual
  confirmation (does the stack actually render/look right in the
  taskbar, do the two placeholder widgets show, do dots appear) and nav
  testing (wheel/drag/dot-click/right-click menu) - still pending as of
  this writing, awaiting the user's next report.
- Still to verify: overlay position/size looks correct next to the other
  taskbar mods, all three nav modes work, dot indicators track enabled
  widgets only, popup-menu enable/disable/reorder persists across
  Explorer restarts, multi-monitor (explicitly out of scope for now, see
  "Scope note" above).

## Incident: Explorer froze on first activation (2026-09-16), fixed

**Symptom** (reported live): activating the mod for the first time froze
Explorer completely — no window repainted, taskbar unresponsive — with
nothing in Windhawk's debug log at the time. After manually restarting
`explorer.exe`, a second activation logged `Shell_TrayWnd not found; mod
inactive this session` instead of freezing (this run happened to start
before Explorer had created its taskbar window, so the mod's old code took
its no-op early-return branch and never reached the risky code path at
all — consistent with, not contradicting, the diagnosis below).

**Root cause**: `Wh_ModInit` called `CreateWindowExW(..., g_taskbarWnd,
...)` directly, with no verification of which thread `Wh_ModInit` itself
runs on. `Wh_ModInit` is **not guaranteed to run on Explorer's main UI
thread**. A `WS_CHILD` window's owning thread is whichever thread called
`CreateWindow` — not necessarily its parent's thread — so the overlay
window ended up owned by whatever thread loaded the mod, which never pumps
Windows messages (`GetMessage`/`DispatchMessage` loop). Any message
Explorer's real UI thread needed to deliver to that window via a blocking
`SendMessage` (e.g. parent-notify on resize/theme change) would then never
return, freezing Explorer's entire main thread with no exception thrown —
matches the observed "frozen, nothing in the logs" symptom exactly.

Confirmed by reading the actual source of both real mods referenced in
this mod's design (`taskbar-ai-quota.wh.cpp` and
`taskbar-fluent-media-player.wh.cpp`, both in
`ramensoftware/windhawk-mods`): neither ever calls `CreateWindowExW`/
`DestroyWindow` against a taskbar-owned `HWND` directly from `Wh_ModInit`.
Both implement and use a `RunFromWindowThread()` helper — a
`WH_CALLWNDPROC` hook plus a registered window message — specifically to
marshal such calls onto the target window's owning thread first. This
mod's original PLAN.md draft had already flagged this pattern as a
reference point but the first implementation didn't actually apply it —
that gap is what caused the freeze.

**Fix** (2026-09-16, unverified live — no Windows machine in this dev
session):
1. Ported a `RunFromWindowThread()` helper into
   `taskbar-widget-stack.wh.cpp`, matching the reference mods' technique:
   `WH_CALLWNDPROC` hook + registered message, with payloads claimed by ID
   from a shared, mutex-guarded table (not read directly off the message's
   `lParam`) so two concurrent calls targeting the same thread can't
   double-claim each other's payload — a real hazard once teardown and the
   init-poll worker (next point) can both target the taskbar thread.
2. `Wh_ModInit` no longer creates the overlay inline. It starts a worker
   thread (`InitWorkerProc`) that polls for `Shell_TrayWnd` every 500ms (up
   to ~5 minutes) — fixing the secondary bug the post-restart log line
   exposed, where the mod just gave up permanently if the taskbar wasn't up
   yet at `Wh_ModInit` time instead of waiting for it. Once found, it calls
   `RunFromWindowThread(g_taskbarWnd, CreateOverlayWindowOnTaskbarThread)`
   so `RegisterClassW`/`CreateWindowExW`/positioning all run on Explorer's
   own thread and the resulting window is owned by it (and therefore
   pumped by Explorer's already-running message loop, same as any other
   taskbar UI).
3. `Wh_ModBeforeUninit` now signals the worker to stop and joins it
   (`WaitForSingleObject`) before tearing anything else down, then
   marshals `DestroyWindow` the same way creation was marshaled —
   `DestroyWindow`, like `CreateWindow`, must run on the window's owning
   thread per MSDN ("a thread cannot use DestroyWindow to destroy a window
   created by a different thread"), which the original code also got
   wrong.

**Still not verified live** — this fix is reasoned from (a) the confirmed
Win32 thread-affinity rules for window creation/destruction and (b) two
independently-authored, known-working mods applying the identical pattern
for the identical reason, but this development environment has no Windows
machine to actually load the fixed mod and reproduce/rule out the freeze.
Needs a live retest before this incident is considered closed: re-enable
the mod, confirm Explorer stays responsive from first activation, and
confirm the overlay still appears (the init-poll worker is new code too).

## Incident 2: no freeze, but nothing visible in the taskbar (2026-09-16)

**Symptom**: after the thread-marshal fix above, Explorer stayed
responsive on activation (confirming that fix), but the widget stack
never appeared anywhere in the taskbar, with no error logged.

**Root cause**: architectural, not a bug in the fix above. This mod's
overlay was a classic Win32 `WS_CHILD` window drawn with GDI. The Windows
11 taskbar is a XAML island rendered via DirectComposition - a classic
child `HWND` can exist, own its messages correctly, and still never
actually be visible, because it isn't part of the composited XAML surface
at all. Confirmed by reading the actual source of both real mods this
design was originally modeled on: neither `taskbar-ai-quota.wh.cpp` nor
`taskbar-fluent-media-player.wh.cpp` (both `ramensoftware/windhawk-mods`)
uses a Win32 overlay window for its visible content. Both build real
`winrt::Windows::UI::Xaml` elements (`Grid`, `StackPanel`, `TextBlock`,
...) and insert them as children directly into the taskbar's own live
XAML visual tree, specifically into a node named `SystemTrayFrameGrid`.
This is the same category of technique as this repo's other mod
(`windows-11-start-menu-button`), which manipulates the Start button's
Composition tree directly rather than overlaying a separate window.

**Fix (2026-09-16, unverified live)**: full rewrite of
`taskbar-widget-stack.wh.cpp` (now v0.2) from Win32/GDI to XAML injection:

- **Taskbar XAML Access** (ported near-verbatim, with attribution, from
  `taskbar-ai-quota.wh.cpp`, Cleroth, MIT-licensed): symbol-hooks into
  `taskbar.dll` for `CTaskBand::GetTaskbarHost`, the
  `CTaskBand::`vftable'{for `ITaskListWndSite'}` vtable,
  `TaskbarHost::FrameHeight` (its compiled prologue is pattern-matched to
  recover an internal, undocumented field offset - x64 only, no ARM64
  support, matching the mod it's ported from), and
  `std::_Ref_count_base::_Decref`. `TryGetTaskbarElementAbi` walks these to
  reach the taskbar's root `FrameworkElement` and, from it,
  `GetTaskbarXamlRoot` gets a real `XamlRoot`; `FindChildByName` then
  recursively locates `SystemTrayFrameGrid` in the live visual tree. Not
  independently re-derived - ported because this is exactly the kind of
  reverse-engineered, version-sensitive internal structure where a hand-
  rolled reimplementation risks being subtly wrong in a way that (unlike
  the guarded, fails-safe original) could misinterpret memory. Chose to
  depend on the validated technique from a mod already in real-world use
  rather than reinvent it blind, with no Windows machine to verify either
  way.
- **`InjectWidgetStackGrid`**: inserts a new `Grid` (dots column + a
  clipped, slidable widget-panes column) as trayGrid's leftmost child,
  following the same "insert a new `ColumnDefinition` at 0, shift existing
  children's `Grid.Column` by one" pattern `taskbar-ai-quota` uses for its
  quota bars.
- **Navigation**: real XAML pointer events (`PointerWheelChanged`,
  `PointerPressed`/`PointerMoved`/`PointerReleased` for drag,
  `Tapped` per dot, `RightTapped` for the config menu) instead of raw
  `WM_*` handling - these fire naturally on the taskbar's own UI thread
  since they're XAML-tree events, so the earlier `RunFromWindowThread`
  marshaling isn't needed for them (only for reaching the tree in the
  first place, and for `TrackPopupMenu`'s owning-thread requirement, which
  is satisfied automatically here since `RightTapped` already runs there).
- **Snap animation**: `CompositeTransform.TranslateY` on the widgets
  `StackPanel`, driven by a `CompositionTarget.Rendering` subscription
  (same idiom already used and crash-hardened in this repo's other mod)
  instead of a Win32 timer. Explicitly revoked both on natural completion
  and from `TaskbarWindowSubclassProc`'s `WM_NCDESTROY`/`WM_DISPLAYCHANGE`
  handling *before* resetting the rest of the UI state - resetting state
  first, discovered while reviewing this rewrite, would have silently
  orphaned that subscription with no reference left to revoke it, the
  same crash class documented in `windows-11-start-menu-button/PLAN.md`'s
  "Crash containment" section.
- **Lifecycle**: `TrayUI::StartTaskbar` is now symbol-hooked too (also
  ported from the reference mod) to catch taskbar re-creation (e.g. a
  display change) precisely, in addition to the existing poll-based retry
  loop for the cold-start race where `SystemTrayFrameGrid` isn't realized
  in the tree yet when the mod first attempts injection. The taskbar
  `HWND` is subclassed (`WindhawkUtils::SetWindowSubclassFromAnyThread`)
  for `WM_NCDESTROY` cleanup and `WM_DISPLAYCHANGE`-triggered re-injection
  - installed and removed via the *same named function*
  (`TaskbarWindowSubclassProc`), not two separately-written lambdas with
  identical bodies, since `RemoveWindowSubclass` matches by exact function
  pointer and two distinct lambdas (even identical ones) are two distinct
  pointers - removal would have silently failed to find anything to
  remove. Both of these were caught during review of this rewrite, not
  live - flagging in case either resurfaces as a live symptom (subscription
  never revoked; subclass never actually removed on unload).

**Scope note**: this rewrite targets the primary taskbar
(`Shell_TrayWnd`) only. `taskbar-ai-quota` also supports
`Shell_SecondaryTrayWnd` (secondary monitors) via a parallel set of
symbol hooks (`CSecondaryTaskBand_*`) - deliberately left out here to
keep this rewrite reviewable; multi-monitor remains an open question (see
"Open questions" above), now narrowed to "port the secondary-taskband
symbols too" as the concrete next step if needed.

**Still not verified live** - no Windows machine in this development
environment. This is now a materially riskier prototype than a pure
Win32 overlay would have been: it depends on reverse-engineered internal
Explorer/taskbar.dll structures (the same ones a real, published mod
already depends on, but still version-sensitive) rather than only
documented Win32 APIs. If `HookTaskbarDllSymbols` fails to resolve any of
the symbol strings against the user's exact Windows build, the mod
degrades safely (`Wh_ModInit` returns `FALSE`, logged, nothing injected)
rather than crashing - but that also means "nothing appears" could now
mean either "still broken" or "this exact Windows build's internals
don't match the ported symbol strings," which will need the Windhawk
debug log's specific failure point (which symbol failed to hook, or
which `fail(...)` reason `InjectWidgetStackGrid`/`TryGetTaskbarElementAbi`
hit) to tell apart on the next test.

## Build/link fixes found while getting the rewrite to actually compile (2026-09-16)

Real compiler/linker output from the user's Windhawk install surfaced three
issues the rewrite above didn't catch without an actual build:
1. Several C++/WinRT calls (`PointerPoint::Properties()`/`Position()`,
   `IVector<T>::Append`/`Clear`/`GetAt`/`Size`/`InsertAt`) use WinRT's
   deduced-return-type pattern, whose real definitions only arrive via the
   full `winrt/Windows.UI.Input.h` and `winrt/Windows.Foundation.Collections.h`
   headers - only their forward-declaring "0.h" variants were visible
   before adding those includes.
2. `WindhawkUtils`' `WH_SUBCLASSPROC` is a 5-parameter signature (no
   `dwRefData`), not comctl32's 6-parameter `SUBCLASSPROC` -
   `TaskbarWindowSubclassProc` had the wrong arity.
3. `UIElementCollection` has no `Remove(value)`, only `RemoveAt(index)` -
   switched to `IndexOf` + `RemoveAt` in `RemoveWidgetStackGrid`.
4. Link step failed with undefined `SetWindowSubclass`/
   `RemoveWindowSubclass`/`DefSubclassProc` - missing `-lcomctl32` in
   `@compilerOptions` (present in `taskbar-ai-quota.wh.cpp`'s own
   `@compilerOptions` for the same reason, missed when trimming that list
   down for this mod).

## Incident 3: "Unsupported TaskbarHost::FrameHeight" at runtime (2026-09-16)

**Symptom**: mod now compiles, links, loads, and its symbol hooks resolve
(`HookTaskbarDllSymbols` doesn't fail - if it had, `Wh_ModInit` would log
that and return `FALSE`, which isn't what's happening). But every
injection attempt (the retry-poll loop fires roughly every 500ms) logs
`TryGetTaskbarElementAbi: Unsupported TaskbarHost::FrameHeight` and bails,
so `SystemTrayFrameGrid` is never reached and nothing gets injected.

**Root cause**: the byte-pattern match against `TaskbarHost::FrameHeight`'s
compiled prologue (see "Incident 2" above - this recovers an internal,
undocumented field offset from the function's own machine code, since
there's no other way to get it) doesn't match on this user's exact
Windows build. This is the single most version-fragile piece of the
entire ported "Taskbar XAML Access" layer, by design (a fixed byte
pattern against compiler-generated code) - and it's now confirmed fragile
in practice, not just in theory.

Researched whether `taskbar-ai-quota`'s own upstream (Cleroth/
windhawk-taskbar-ai-quota) has hit and fixed this: no. No GitHub issue on
that repo mentions "FrameHeight" or this exact log line; the repo's most
recent relevant fix (v1.6.3, PR ramensoftware/windhawk-mods#5510, "fix
newer taskbars and quota reporting") widens which XAML panel types it
accepts (`Grid` vs `StackPanel`, via `try_as`) but doesn't touch the
`FrameHeight` byte-pattern scanner at all. So this mod has no known,
published fix to copy - the upstream mod may simply not have been tested
against this exact Windows build yet either.

**Fix attempted (2026-09-16, diagnostic only, not a real fix yet)**: added
a hex dump of `TaskbarHost::FrameHeight`'s first 16 bytes to the log line
on pattern-match failure (`TryGetTaskbarElementAbi`), since guessing a
replacement byte pattern with no ground truth from the user's actual
compiled binary would be irresponsible reverse-engineering. **Next
step once that log line comes back**: use the real bytes to work out (a)
whether it's the same `sub rsp,XX` / `add rcx,XX` shape at a different
byte offset (e.g. a different immediate encoding, or the two instructions
swapped/reordered by a different compiler backend), or (b) a materially
different prologue shape entirely, and adjust the match (and offset
extraction) accordingly. Do not guess further without that data.

**First attempt at a fix was itself wrong (2026-09-16)**: added an
`#elif defined(_M_ARM64)` branch (matching `taskbar-ai-quota.wh.cpp`'s
own ARM64 support) alongside the existing `#if defined(_M_X64)` one.
Re-tested live - same failure, byte-for-byte identical dump, still
logged from the `_M_X64` branch (`"bytes:"`, not the ARM64 branch's
`"words:"`). That ruled out the first theory: the user's mod is still
being compiled with `_M_X64` defined (confirmed by which branch's log
message fired) even on their ARM64 machine, so the ARM64 branch could
never have been reached no matter how correct its pattern was -
Windhawk's local/dev editor evidently always compiles mods here as x64
regardless of `@architecture` or the actual host CPU.

**Real explanation**: `_M_X64` (this mod's own compile-time target) and
"what architecture `TaskbarHost::FrameHeight`'s actual code is" are two
different things on Windows 11 on Arm. Explorer there runs as an
**ARM64EC** process - a hybrid mode where x64-compiled code (this mod,
always, per the above) and genuinely-native-ARM64 system DLL code
(`taskbar.dll`) coexist in the same process and call each other through
ABI-compatible thunks. The x64 mod can call `TaskbarHost::FrameHeight`
just fine through that thunk layer - but the raw instruction bytes *at*
that address are real ARM64 machine code regardless, because that's what
the function actually is. A compile-time `#if defined(_M_ARM64)` branch
in *this mod's own source* can therefore never fire in this scenario on
any machine, since Windhawk builds it as x64 here unconditionally.

**Fix (2026-09-16, unverified live)**: replaced the `#if defined(_M_X64)
/ #elif defined(_M_ARM64)` compile-time branch with a single runtime
check that tries the x64 pattern, then the ARM64 pattern, unconditionally
- the two byte/word shapes are structurally distinct enough not to
false-positive against each other. This works regardless of which
architecture Explorer's ARM64EC process actually executes the target
function as, and regardless of what architecture Windhawk happens to
compile this mod's own code for. Reverted the `@architecture` header
change from the previous (wrong) fix attempt back to `x86-64` - it isn't
the relevant lever here (Windhawk's local editor didn't honor it for
architecture selection either way, per the same live evidence above),
and the runtime dual-check makes it a non-issue regardless of what value
it holds.

## Incident 4: injected in the wrong place, nav unreliable on trackpad (2026-09-16)

**Symptom** (first fully-visible live report, v0.1.3): the stack is
visible and readable in the taskbar, but sits between the running-app
icons and the tray dropdown - not at the far left, next to the Start
button, spaced from it, where the user's other taskbar-area mods
(media player, AI quota) already live. Right-click and its submenu work.
Wheel/drag/dot-click don't, at least not on the trackpad available for
testing right now (no mouse tested yet).

**Root cause (position)**: `InjectWidgetStackGrid` inserted into
`SystemTrayFrameGrid` (the system tray's own column grid, right side of
a centered taskbar) - the correct node for tray icons, wrong node
entirely for "far left, next to Start." Confirmed by reading
`taskbar-fluent-media-player.wh.cpp` (Salyts) - the mod already running
in exactly the position the user wants this one in - which never touches
`SystemTrayFrameGrid` for its own placement: it finds
`Taskbar.TaskbarFrame` → `RootGrid` (the taskbar's actual root, parent of
both the tray and the pinned/running-icon repeater and Start button),
locates the Start button inside `RootGrid`'s `TaskbarFrameRepeater`
(name varies: `StartButton`/`StartMenuButton`/`StartMenuLaunchButton`/
`LaunchListButton`, tried in order - name depends on Windows build and
whether another mod replaces the Start button), and adds itself as a
free-floating child of `RootGrid` with a `Margin.Left` computed from the
Start button's `TransformToVisual` position, kept live via a
`RootGrid.LayoutUpdated` handler so it follows if the Start button's own
position/size changes (DPI change, taskbar realignment, a Start-button
mod resizing it).

**Fix (2026-09-16, unverified live)**: ported this positioning approach
(`FindTaskbarRootGrid`, `FindStartButton`, `RepositionWidgetStack` +
`LayoutUpdated` tracking), attributed, simplified from the reference's
full version - dropped its mutual margin-reservation logic (which pushes
the Start button's own margin to make room) since a centered taskbar
layout already has genuine empty space between the Start button and the
centered icon cluster; this mod's widgets just sit in that existing gap
rather than carving out new space. `UiState::injectionParent` is now
`RootGrid` (not the tray grid) and `ownedColumn`/column-shifting logic is
gone entirely, replaced by `trackedElement` (the Start button) +
`layoutUpdatedToken`.

**Root cause (dots unclickable)**: the dot indicators were bare 4-6px
`Ellipse` elements - a `Fill`-only shape's hit-test region is
essentially just its own small rendered geometry, near-impossible to
land a trackpad pointer on precisely. Fixed by making each dot's visible
`Ellipse` `IsHitTestVisible(false)` and wrapping it in a larger (10×14px)
`Grid` with an explicit `Transparent` `Background` (required - a `Grid`
with no `Background` at all isn't hit-testable, only its
already-hit-testable descendants are, which is also why `RightTapped`
worked when clicking on a widget pane - a `Border` with a real
`Background` - but not when aiming at open/background space) as the
actual `Tapped` target.

**Wheel/drag still unclear**: not fixed yet, because it isn't understood
yet - `PointerPressed`/`PointerMoved`/`PointerWheelChanged` are wired the
same way (bubbled routed events on `g_ui.root`) as `RightTapped`, which
does work, so there's no confirmed root cause the way there was for
position and dot size. Left as-is pending a retest, ideally with an
actual mouse (to isolate whether it's a touchpad-gesture-routing issue
specific to two-finger scroll / press-drag-release timing, vs. a real
bug in the handlers themselves) and more specific reporting (does
scrolling do *nothing at all*, or does drag seem to almost-but-not-quite
register - the current drag threshold requires ~16px of movement before
it commits a step, which a light/short trackpad drag might simply not
reach).

## Incident 5: still overlapping Task View; wheel confirmed broken even with a mouse (2026-09-16)

**Symptom** (v0.1.4 retest): stack now appears near Start, but overlaps
the Task View (virtual desktops) button - a screenshot showed the
widget's "Media Player" label rendered directly on top of it. Dots
untestable in that state. Wheel still didn't respond, and this time
tested with an actual mouse, not just a trackpad - ruling out "trackpad
gesture quirk" as the wheel explanation.

**Root cause (overlap)**: `RepositionWidgetStack` anchored to the Start
button alone, but Task View and (when present) the search box live in
`TaskbarFrameRepeater` immediately after Start, in their own
fixed-position slots - not part of the centered/reflowing icon group.
The real empty gap starts after all of the repeater's fixed leading
content, not right after Start specifically.

**Fix (2026-09-16, unverified live)**: `RepositionWidgetStack` now
tracks the whole `TaskbarFrameRepeater` element (`g_ui.trackedElement`),
not the Start button - positions at `repeater.right + gap` instead of
`startButton.right + gap`. `FindStartButton` is still called during
injection, but now purely as a readiness check (confirms the repeater's
content has actually been realized before we compute a position from
it), not as the position anchor itself.

**Root cause (wheel)**: `PointerWheelChanged`'s handler called
`args.GetCurrentPoint(nullptr).Properties().MouseWheelDelta()` - every
other pointer handler in this mod calls `GetCurrentPoint(elem)` with a
real element (`sender.as<UIElement>()`), never `nullptr`. Suspect the
`nullptr` overload throws in this hosting context (a mod-injected XAML
island inside Explorer, not a normal app window) and the exception gets
silently swallowed by the WinRT event-dispatch boundary before reaching
this mod's own code - consistent with "wheel does literally nothing,
with or without a trackpad" and with `RightTapped`/`PointerPressed`
(which never call `GetCurrentPoint(nullptr)`) working. Not confirmed via
a debugger (none available in this dev environment) - a plausible,
testable theory, not certain.

**Fix (2026-09-16, unverified live)**: changed the wheel handler to call
`args.GetCurrentPoint(elem)` with `elem = sender.as<UIElement>()`,
matching every other handler, and wrapped the body in try/catch (matches
this mod's existing crash-isolation pattern elsewhere, and would at
least stop future silent failures here from being invisible - though if
this theory is right, the WinRT boundary was already swallowing the
exception before it could reach this try/catch, so this addition is
about testability, not the actual fix).

**Still open**: drag and dot-click, unconfirmed either way in this
report (dots were unusable due to the overlap; drag wasn't specifically
retested). Retest all three - wheel, drag, dot-click - once position is
confirmed fixed.

## Incident 6: repeater-anchor fix made the stack disappear entirely (2026-09-16)

**Symptom**: with the Incident 5 fix live (v0.1.5), the stack stopped
rendering anywhere at all - no error, just gone. The user clarified what
"correct position" actually means: flush against the taskbar's *left*
edge, in the exact spot the native Widgets button (weather/stocks)
normally occupies - which the user hides and has always used for their
other taskbar-area mods (see this file's "Context" section). That's to
the *left* of the Start button, not to the right of anything in the
repeater.

**Root cause**: `TaskbarFrameRepeater` isn't just the leading buttons
(Start/search/Task View) - it also repeats every pinned and running app
icon. Its right edge is therefore way out past the centered icon
cluster, not "right after Task View" as assumed in Incident 5's fix.
Anchoring `Margin.Left` to `repeater.right + gap` pushed the stack far
past the visible taskbar bounds, off-screen - which reads as "just
disappeared," not a crash or an error.

**Fix (2026-09-16, unverified live)**: dropped element-tracking for
position entirely - two attempts at it (Start button, then the whole
repeater) both put the stack in the wrong place because both
misjudged what a given taskbar element's bounds actually correspond to.
The requested position doesn't need tracking at all: it's simply
RootGrid's own left edge, which doesn't move. `InjectWidgetStackGrid`
now sets a small fixed `Margin.Left` (`kLeftEdgeGap`, 6px) on `root` at
creation and never touches it again - no `LayoutUpdated` subscription,
no tracked element, no `RepositionWidgetStack` function. Removed
`UiState::trackedElement`/`layoutUpdatedToken` (both now unused) and the
teardown code that revoked the `LayoutUpdated` token. `FindStartButton`
is still called during injection, purely as a signal that the taskbar's
content has been realized (not just `RootGrid` itself existing) before
attempting to inject - not for positioning.

This is simpler and more robust than either tracking attempt, not just
a stopgap: the left edge is the actual place being targeted, so there's
nothing to track relative to in the first place.

## Incident 7: position confirmed fixed; wheel still dead with a diagnostic pending (2026-09-16)

**Symptom**: v0.1.6 retest confirms the left-edge placement is correct.
Wheel scroll still does nothing at all, still with a real mouse
confirmed this round too - so the Incident 5 fix (`GetCurrentPoint(elem)`
instead of `nullptr`) did not resolve it. That rules out the specific
"the nullptr overload throws" theory without more data.

**Decision: stop guessing, get a real signal first.** Two theories fixed
in a row without confirming the actual failure point risks a third wrong
guess. Added two unconditional diagnostic `Wh_Log` calls (ahead of any
early-return, so they fire regardless of settings/state):
1. Inside `PointerWheelChanged`'s handler itself, at entry - confirms
   whether XAML's routed-event system ever delivers the wheel event to
   this mod's injected element at all.
2. On `WM_MOUSEWHEEL` in `TaskbarWindowSubclassProc` (the taskbar HWND
   this mod already subclasses for `WM_NCDESTROY`/`WM_DISPLAYCHANGE`) -
   confirms whether the raw Win32 message even reaches the taskbar
   window while hovering the widget, one layer below XAML.

**What each outcome would mean, for the next round**:
- Neither log fires → Windows isn't delivering `WM_MOUSEWHEEL` to this
  HWND while hovering this exact screen region at all (a Win32-level
  routing issue - default focus-based `WM_MOUSEWHEEL` targeting, or
  another mod's window intercepting it first at that location -
  independent of anything in this mod's own XAML code).
- The Win32 log fires but the XAML one doesn't → the message arrives at
  the taskbar HWND but XAML's own routed-pointer pipeline isn't
  delivering it to this mod's specific injected element (points at
  something about how/where this mod's content is hosted in the visual
  tree, or a hit-testing gap upstream of the handler).
- Both fire → the handler runs; the bug is downstream in this mod's own
  logic (`StepWidget`/`GoToWidget`/the slider transform), not event
  delivery at all - would mean re-examining that path specifically
  instead of the event wiring.

Not a fix yet - purpose-built to make the next report diagnostic rather
than another "still doesn't work."

## Incident 8: Explorer crash from dangling XAML event delegates (2026-09-16)

**Symptom**: v0.1.7 retest - wheel still silent, *no diagnostic log
fired at all* (neither the XAML-side nor the Win32-side one added for
Incident 7), and this time right-click crashed Explorer outright
(auto-restarted, Windhawk's toolbox reopened). Got the crash's real
signature from Windows Event Viewer (Event ID 1000, requested and
provided by the user): `Explorer.EXE` faulted with exception code
`0xC0000005` (`STATUS_ACCESS_VIOLATION`), faulting module **"unknown"**,
fault offset **0x0**.

**Root cause**: an "unknown" faulting module with a `0x0` offset is the
textbook signature of executing a call through a **dangling function
pointer** - jumping to an address that used to contain code but no
longer maps to any loaded module, so the crash handler can't attribute
it to one. `WireUpNavigation` registered five XAML pointer-event
handlers on `g_ui.root` (`PointerWheelChanged`, `PointerPressed`,
`PointerMoved`, `PointerReleased`, `RightTapped`) via
`.EventName(lambda)` and **discarded every returned `event_token`** -
never explicitly revoking any of them. `g_ui.root` isn't a window this
mod owns and fully controls the lifetime of; it's an element living in
Explorer's own long-lived taskbar visual tree. Registering a delegate on
it without ever revoking means the event source can hold a live
reference to a delegate whose invoke thunk lives inside this mod's own
DLL indefinitely - and Windhawk unloads/reloads that DLL on every
recompile (this session's had many by now). If any subscription
survives a reload and later fires, it calls into memory the old DLL used
to occupy but no longer does. This is the *same general crash class*
already documented in this repo's other mod
(windows-11-start-menu-button/PLAN.md's "Crash containment" section) and
already partly handled in this mod for `CompositionTarget::Rendering`
(via `StopSnapAnimation`, revoked on every teardown path) - but these
five pointer-event subscriptions were never covered by that same
discipline.

The "no diagnostic log at all, even at the Win32 level" result from the
same test round is now understood as likely confounded by this crash,
not proof either pipeline stage is broken - if hovering/scrolling near
the widget triggered (or coincided with) the crash before either log
line could be written, "no log" doesn't distinguish "never fires" from
"fired, but we crashed before or during logging it." Needs a clean
retest once this fix lands.

**Fix (2026-09-16, unverified live)**: `UiState` now stores each
subscription's `event_token`
(`wheelToken`/`pressedToken`/`movedToken`/`releasedToken`/
`rightTappedToken`). A new `UnwireNavigation()` explicitly revokes all
five, called from every teardown path that resets `g_ui`:
`RemoveWidgetStackGrid` (normal removal - explicit unload, taskbar
re-creation via the `TrayUI::StartTaskbar` hook) and
`TaskbarWindowSubclassProc`'s `WM_NCDESTROY`/`WM_DISPLAYCHANGE` branches
(matching where `StopSnapAnimation` was already called for the same
reason). Left the per-dot `Tapped` handlers in `RefreshDots` as-is -
those are registered on short-lived `Ellipse`/`Grid` objects recreated
(and the old ones discarded, via `dotsPanel.Children().Clear()`) on
every rebuild, so their own object lifetime - and with it their
delegate's - should unwind cleanly through ordinary WinRT reference
counting without this mod holding any other reference to them; lower
risk than `g_ui.root`, which persists for the whole time the widget
stack is on-screen and directly spans DLL-reload boundaries during dev
iteration.

Kept the `WM_MOUSEWHEEL`-on-taskbar-HWND diagnostic log from Incident 7
for one more round, now that a crash isn't expected to interfere with
it - still useful to confirm whether the wheel problem is Win32-level
routing or something else, once Explorer stays up long enough to test
cleanly.

## Incident 9: crash fixed (no crash this round); wheel AND dot-clicks both silent (2026-09-16)

**Symptom**: v0.1.8 retest - clean injection log, no crash this time
(the Incident 8 fix held). But now *dot clicks also don't respond*, not
just wheel - a new, broader symptom than before (dots were previously
untested due to the Task View overlap, not previously confirmed
non-working). Also: neither the `PointerWheelChanged`-side log nor the
`WM_MOUSEWHEEL`-Win32-side log appeared at all this round, and this
time there's no crash to blame it on - a real result, not a confounded
one.

**Reasoning**: wheel and dot-click use different event types
(`PointerWheelChanged` on `root` vs. `Tapped` on each dot's own hit
target) and different elements - if *both* are silent with no crash to
explain it, that points at something more fundamental than a
per-event-type bug: either no pointer input reaches this mod's part of
the visual tree at all since the reposition to `RootGrid` (Incident 4-6
changed the injection parent from `SystemTrayFrameGrid` to `RootGrid`;
right-click's earlier "works" report was under the old
`SystemTrayFrameGrid` placement and hasn't been confirmed since), or a
hit-testing/z-order issue specific to how this mod's element sits among
`RootGrid`'s other children now.

**Not fixed yet - added broader diagnostics instead of guessing which
of these it is**: `Wh_Log` calls added, unconditionally, at the top of
`PointerPressed`, `PointerReleased`, `RightTapped` (previously only
`PointerWheelChanged` had one) and the dots' `Tapped` handler. Left
`PointerMoved` unlogged (it only matters once `PointerPressed` has
already fired and set `dragging`, so it's downstream of that signal, not
useful for narrowing *whether anything at all* reaches the element).

**Next retest should specifically isolate, not just retry**: press-and-
hold (or a plain click) directly on a widget pane, right-click on a
widget pane, click a dot, and scroll - reported separately with their
individual log lines (or lack of any), not lumped as "still doesn't
work." If literally nothing logs for any of the four, the issue is
"nothing reaches this element at all" (an architectural problem with
where/how this mod's content sits in `RootGrid`) rather than anything
specific to wheel or to dots.

## Incident 9 results: input reaches every handler; two separate bugs isolated (2026-09-16)

The Incident 9 diagnostics fully answered the question: pointer input
does reach this mod's element - `PointerPressed`, `RightTapped`,
`PointerWheelChanged`, and each dot's `Tapped` all logged as firing.
That rules out "nothing reaches RootGrid's child at all." Two separate,
narrower problems remain:

1. **Right-click still crashes Explorer**, immediately after
   `RightTapped` logs. The Incident 8 fix (revoking dangling event
   tokens) was real and worth keeping, but wasn't the whole story for
   this specific crash.
2. **Wheel and dot-click both log correctly but produce no visible
   change** - no snap-scroll, no widget switch. Since the *handlers*
   fire, this is a downstream bug in this mod's own navigation/rendering
   logic, not an event-delivery problem.

## Incident 10: right-click crash - re-entrant modal loop from a XAML event handler (2026-09-16)

**Root cause (theory, unverified live)**: `ShowContextMenu` calls
`TrackPopupMenu`, which pumps its own nested Win32 message loop,
synchronously from inside `RightTapped`'s handler - itself already
running from within XAML's own routed-event dispatch on the same UI
thread. Re-entering with a blocking modal loop while still inside that
dispatch call stack is a known-hazardous pattern for UI frameworks in
general; it plausibly corrupts internal XAML/Composition dispatcher
state rather than crashing directly inside `TrackPopupMenu` itself,
which would explain why the crash's earlier Event Viewer signature
(Incident 8: exception `0xC0000005`, faulting module "unknown") didn't
point at a specific recognizable function.

**Fix (2026-09-16, unverified live)**: `RightTapped`'s handler no longer
calls `ShowContextMenu` directly. It captures the target `HWND` and
click point, then defers the call via
`sender.Dispatcher().RunAsync(CoreDispatcherPriority::Normal, ...)` (the
`Windows::UI::Core::CoreDispatcher` associated with this mod's own
element - the same general "queue it, don't call it inline" pattern
`taskbar-fluent-media-player.wh.cpp` uses elsewhere for its own
dispatcher-queued work). This lets `RightTapped`'s handler return and
XAML's dispatch fully unwind before `TrackPopupMenu`'s nested loop ever
starts, removing the re-entrancy rather than working around its
symptoms.

## Incident 11: navigation handlers fire, but nothing visibly updates (2026-09-16, in progress)

**Symptom**: `StepWidget`/`GoToWidget` are confirmed reached (their
callers' logs fire), but no snap-scroll or widget switch is visible.

**Not fixed yet - diagnostics added instead of guessing**: `Wh_Log`
calls in `ApplySliderTarget` (logs `widgetIndex`, `animate`, and whether
`g_ui.sliderTransform` is non-null - confirms whether this function
receives a sane call and has a live transform to act on) and in
`OnRenderingTick` (logs once, on its very first-ever invocation -
confirms whether `CompositionTarget::Rendering` actually invokes this
mod's callback in this hosting context at all; this exact API is proven
to work in this repo's other mod, but for a different element/scenario,
so it isn't yet confirmed here specifically). Also logs if
`TranslateY()` throws inside the tick.

**What the next result would mean**: if `ApplySliderTarget` logs a
sane `widgetIndex`/`animate`/`hasTransform=1` but `OnRenderingTick`
never logs its first-invocation line, `CompositionTarget::Rendering`
isn't actually driving this mod's animation in this context - would
need a different mechanism (e.g. a plain multimedia/dispatcher timer
instead of relying on the compositor's per-frame callback for injected,
non-native content). If both log as expected but there's still no
visible change, the bug is likely in what's actually being clipped/
transformed (the `widgetsPanel`/`clipHost` structure itself), not in the
animation driver.

## Incident 10 confirmed fixed; Incident 11 narrowed further (2026-09-16)

**Right-click**: retested, no crash. The `RunAsync`-deferred
`ShowContextMenu` fix holds.

**Wheel**: retested with the Incident 11 diagnostics in place -
`PointerWheelChanged fired` and `WM_MOUSEWHEEL on taskbar hwnd` both
logged (twice, for two scroll gestures), but **neither
`ApplySliderTarget` nor `OnRenderingTick` logged at all** - meaning the
call chain breaks somewhere between the wheel handler and
`ApplySliderTarget`, i.e. inside `StepWidget`/`GoToWidget`, or the
handler never gets past its own `navWheel` check / `try` block. The
previous diagnostic wasn't fine-grained enough to tell which.

**Fix (2026-09-16, still diagnostic, not a real fix yet)**: added
`Wh_Log` calls tracing every step of the chain: after the `navWheel`
check passes (with the computed `delta`), inside `StepWidget` (enabled
count, and the computed `pos`/`next`/target index), and inside
`GoToWidget` (`widgetIndex`/`activeIndex`/`widgetsCount`, and whether
its own guard returns early). One of these must be the last line to log
on the next retest - whichever one is tells us exactly which line breaks
the chain, the same way narrowing between `ApplySliderTarget` and
`OnRenderingTick` was meant to (still open, now folded into this same
full trace instead of a separate round).

## Incident 12: found it - `==WindhawkModSettings==` closing marker was wrong (2026-09-16)

**Root cause**: the trace pinpointed it immediately - `PointerWheelChanged:
navWheel setting is off`. The user separately noticed Windhawk's own
settings panel for this mod showed "Aucun paramètre disponible pour ce
mod" (no settings available). The metadata block's closing delimiter was
written as `// ==WindhawkModSettings==` - identical to the *opening*
delimiter, missing the leading `/` that makes it `// ==/WindhawkModSettings==`.
Windhawk couldn't find a valid closing marker, so it silently treated
the whole settings block as absent - every `Wh_GetIntSetting` call
therefore returned Windhawk's default-for-missing-key value (0 /
false), which is exactly why `navWheel`/`navDots`/`navDrag` all read as
off despite each defaulting to `true` in the (never-actually-parsed)
YAML.

This typo has been present since this mod's very first commit - it
predates every other incident in this file and plausibly explains part
of "dots don't respond" too (`navDots` would have read false the same
way), though dots were also blocked by the mispositioning in Incidents
4-6 for most of that time, so it's hard to say how much of the dot
symptom was this bug alone versus overlapping with position.

**Fix (2026-09-16, unverified live)**: corrected the closing marker to
`// ==/WindhawkModSettings==`. Expect this to fix wheel, drag, and dots
all at once, since all three read `g_settings.nav*` flags gated by the
same broken settings block.

**Left the Incident 11 diagnostic logs in place for this retest** (not
stripped yet) - if the settings fix is the true, complete fix, the
chain should now log all the way through
`ApplySliderTarget`/`OnRenderingTick` and the stack should visibly
switch; if something else is *also* wrong, the trace will show exactly
where it stops next. Plan to remove the verbose diagnostic logging once
this is confirmed working end to end.

## Incident 13: fixed marker exposed a second bug - invalid `widgets` YAML (2026-09-16)

**Symptom**: after the Incident 12 fix, Windhawk's settings parser now
actually reads the block - and immediately rejected it: `bad
indentation of a sequence entry (20:5)`, pointing at the `widgets`
group's structure.

**Root cause**: the `widgets` block tried to express "a list of
same-shaped groups" as anonymous nested sequences (`- - id: ...` with
no key naming each widget), by analogy with how `recolor`/`gradient`
work as single named groups in `windows-11-start-menu-button.wh.cpp`.
That reference pattern only covers *one* level of "a named group with a
field list + its own `$name`/`$description`" - it doesn't establish that
*repeating* that shape anonymously (an array of anonymous groups) is
valid syntax, and it turned out not to be: a bare `$name:` key at the
same indentation as its enclosing sequence's own `-` markers doesn't
resolve the way a single named group's trailing `$name:` does.

Checked whether `taskbar-ai-quota.wh.cpp` (which has a genuinely
repeatable list - user-added accounts) solves this same problem: it
doesn't use the YAML settings block for that at all - it has its own
native Settings window instead. That's a real signal Windhawk's
standard YAML settings format isn't well-suited to repeatable/array-of-
groups, which is presumably exactly why a mod that actually needed that
built custom UI instead of fighting the schema.

**Fix (2026-09-16, unverified live)**: removed the `widgets` settings
block entirely rather than reworking its YAML - it was checked and
confirmed to have **no effect on the mod's actual behavior**: the C++
code builds `g_widgets` entirely from hardcoded values in
`InitPlaceholderWidgets()` and never reads a widget's `id`/`enabled`
from Windhawk settings at all (this was already flagged as an
unimplemented TODO - "persist reordered/toggled state back to Windhawk
settings ... Not implemented in this prototype"). So this settings
block was inert even when it parsed - removing it costs nothing
functionally right now and unblocks the `nav` settings (which *are*
real and used) from being blocked by the same parse failure. Proper
per-widget settings (add/remove/reorder/persist) are deferred to the
SDK milestone in "Next steps," where the actual schema needed will be
clearer once real (non-placeholder) widgets exist.

## Prototype mechanics confirmed working live (2026-09-16)

After the settings-marker and settings-YAML fixes (Incidents 12-13),
confirmed live: mouse wheel scroll and dot-click navigation both switch
widgets correctly (snap animation included). Right-click's context menu
(enable/disable/reorder) already confirmed earlier, no crash. This is
the first point in this mod's development where the core "iOS widget
stack" mechanic - injection, positioning, snap-scroll, dots, and two of
the three navigation modes - is confirmed working end to end on real
hardware, closing out the prototype's original goal (see "Prototype
scope" at the top of this file).

**Known open item**: two-finger trackpad scroll doesn't trigger
navigation, even though mouse wheel does. Not yet root-caused - Windows
normally synthesizes `WM_MOUSEWHEEL` from a Precision Touchpad's
two-finger vertical pan for most UI elements, so this could be a driver/
OS-level quirk specific to how this element is hosted (injected via
symbol-hook reflection into Explorer's tree, not a native control) that
doesn't get recognized as a valid scroll target the same way, rather
than a bug of the same kind as the ones just fixed. Not yet
investigated further - lower priority than the crashes/positioning/dead-
navigation bugs already resolved, since mouse wheel and dot-click both
give working alternate paths to the same navigation.

**Diagnostic logs still in the code** (`ApplySliderTarget`,
`OnRenderingTick`, `GoToWidget`, `StepWidget`, the wheel handler's
step-by-step trace, the dots' `Tapped`, `WM_MOUSEWHEEL` on the taskbar
subclass) - left in deliberately per "Incident 12"'s note, to catch
anything unexpected during this confirmation round. Now that the core
mechanics are confirmed working, these should be trimmed back down
before this prototype is considered done - noted in "Next steps."

## Incident 14: two-finger trackpad scroll via ManipulationDelta (2026-09-16)

**Theory**: a real mouse wheel reliably produces
`PointerWheelChanged`/`WM_MOUSEWHEEL`, confirmed live, but a Precision
Touchpad's two-finger pan over the same element did not (not root-caused
with certainty - plausibly the taskbar's own native touchpad-gesture
handling claims the pan for itself, e.g. for its built-in
window-switching gestures, before the OS's generic "synthesize
`WM_MOUSEWHEEL` from an unclaimed touchpad pan" fallback ever runs).

**Fix (2026-09-16, unverified live)**: added a second, independent
navigation channel using `ManipulationDelta` - the WinRT-native
mechanism for touch/touchpad pan gestures, which XAML processes from
the raw pointer stream independently of whether it also gets translated
into a wheel message. Set `root.ManipulationMode(ManipulationModes::TranslateY)`
and accumulate `args.Delta().Translation.Y` across calls, stepping the
widget (`StepWidget`) each time the accumulated distance crosses half a
pane's height, then reducing the accumulator by that amount (not
resetting to zero) so a fast/long gesture can trigger multiple steps
rather than being capped at one. Gated on the existing `navWheel`
setting - conceptually the same "scroll" input class as the mouse wheel,
not a new setting.

**Known overlap risk, not yet tested for**: manipulation events fire for
*any* pointer device, including a plain mouse - a literal single-finger/
mouse click-and-drag could now trigger both this manipulation-based
stepping *and* the existing `PointerPressed`/`PointerMoved`-based drag
logic (gated on `navDrag`) simultaneously, if both settings are on
(both default to `true`). Not confirmed whether this actually
double-steps in practice, or whether manipulation recognition only
kicks in for gestures distinct enough from a normal drag (some slop/
threshold before manipulation events start firing is typical). Flagging
so a reported "wheel/drag feels twitchy when done with a mouse instead
of a touchpad" isn't mistaken for a new, unrelated bug.

Also reverted the dots' hit-target back to the original small `Ellipse`
with a simple margin (no oversized transparent wrapper) per explicit
request - now that dot clicks are confirmed working (Incident 12's
settings-marker fix was the real cause of "unclickable," not the dot's
hit-test size), the enlarged hit target's only remaining effect was
making the indicators look bulkier than intended.

## Incident 15: ManipulationDelta retest - no dedicated log, but that log didn't exist yet (2026-09-16)

**Symptom**: v0.1.14 retest, two-finger trackpad scroll - "no dedicated
log". This isn't yet evidence the fix failed: the Incident 14 code never
actually logged anything from `ManipulationDelta`'s handler, so "no
dedicated log" was expected regardless of whether the handler fired.
Genuinely unknown after this report: whether `ManipulationDelta` fired
at all, and whether `PointerWheelChanged`/`WM_MOUSEWHEEL` (which do have
logs) also stayed silent for this same gesture - not explicitly
reconfirmed for the touchpad case specifically in this round.

**Fix (2026-09-16, diagnostic, not a real fix)**: added `Wh_Log` to
`ManipulationDelta`'s handler (logs `dy` on every call, unconditionally,
ahead of the `navWheel` check) and a new `ManipulationStarted` handler
that logs on its own (confirms whether XAML's gesture recognizer
acknowledges the gesture as a manipulation *at all*, independent of
whether `ManipulationDelta` ever gets called afterward - a stronger,
earlier signal than waiting for delta events).

**Next retest should check all of**: `ManipulationStarted` fired?
`ManipulationDelta` fired (and with what `dy`)? Also
`PointerWheelChanged`/`WM_MOUSEWHEEL` (re-confirm they really don't fire
for touchpad two-finger scroll specifically, not just recalled from the
earlier mouse-wheel test). If none of the four log anything at all for a
two-finger touchpad gesture over this element, that's a stronger signal
this is a genuine OS/driver-level gesture-claiming issue outside what
any XAML-level fix can reach - worth knowing before investing further in
this specific feature.

**Result (2026-09-16): confirmed - none of the four fire.** The user
explicitly checked all four for a two-finger touchpad swipe over the
widget: `ManipulationStarted`, `ManipulationDelta`,
`PointerWheelChanged`, `WM_MOUSEWHEEL` - none logged anything.

**Conclusion: this is an OS/driver-level limitation, not a bug in this
mod's code, and not one further application-level changes can reach.**
The gesture never arrives at this element at any layer checked - not
Win32, not XAML's pointer pipeline, not XAML's gesture recognizer. Since
`ManipulationMode`/`ManipulationDelta` is WinRT's own native mechanism
for exactly this kind of input and still saw nothing, the touchpad
driver (or whatever OS-level component decides which window/region a
two-finger pan should target) is plausibly not recognizing this
symbol-hook-injected element as a valid scroll target at all - unlike a
native taskbar control, or a normal top-level app window. Reaching
lower than this would mean raw HID input interception or low-level
`WM_POINTER` hooking - a disproportionate amount of effort and risk for
one of three navigation methods, when the other two (mouse wheel,
dot-click) are already confirmed working.

**Decision: deprioritized.** Two-finger trackpad scroll is left as a
known, documented limitation rather than pursued further for now. Mouse
wheel and dot-click already give full navigation coverage; drag
(press-and-hold-and-move) remains unconfirmed either way but uses the
same underlying pointer-event mechanism already proven to work for
right-click, so it's not expected to share this specific problem.

## Drag confirmed working; log cleanup (2026-09-16)

**Drag**: confirmed working live, but reported "very sensitive" and
"inverted." Fixed both: raised the step threshold from half a pane's
height to a full pane's height, and flipped the direction so dragging
up steps to the *previous* widget (matches "dragging the content down
to reveal what's above," the usual touch-scroll feel, rather than the
original's opposite mapping).

**Diagnostic logging cleanup**: removed all the `Wh_Log` calls added
across Incidents 7-15 to trace the wheel/crash/settings bugs (the
step-by-step chain through `StepWidget`/`GoToWidget`/
`ApplySliderTarget`/`OnRenderingTick`, the "fired" logs on
`PointerWheelChanged`/`PointerPressed`/`PointerReleased`/`RightTapped`,
`ManipulationStarted`'s entire diagnostic-only subscription, and
`WM_MOUSEWHEEL` on the taskbar subclass) - all served their purpose and
are no longer needed now that those bugs are fixed and confirmed live.
Kept the logs that mirror this repo's other mod's ongoing-diagnostic
conventions: injection success/failure, the `TaskbarHost::FrameHeight`
unsupported-pattern dump (still valuable if a future Windows build
changes that prologue again), `HookTaskbarDllSymbols` failure, and the
retry loop giving up.

This closes out the debugging phase this file's "Incident" log
documents in detail - the mod now does what the original prototype set
out to do (see "Prototype scope" at the top of this file), modulo the
two known, deprioritized gaps: two-finger trackpad scroll (Incident 15)
and the still-unbuilt widget SDK (see "Next steps").

## Incident 16: revisiting trackpad scroll, lighter approach first (2026-09-17)

User asked for a real fix rather than leaving Incident 15's conclusion
as final. Weighed the two remaining options:
1. **Raw HID digitizer parsing** (`RegisterRawInputDevices` for the
   touchpad's HID usage page, decoding multi-touch contact reports
   directly, bypassing Windows' own Precision Touchpad gesture
   synthesis entirely) - a real, used technique (some window managers
   and browsers do this for custom touchpad gestures), but substantial:
   effectively writing a small gesture recognizer from raw contact
   data, untestable without live hardware between iterations.
2. **A lighter diagnostic step first**: a system-wide, low-level mouse
   hook (`WH_MOUSE_LL`) sees `WM_MOUSEWHEEL` messages before Windows
   routes them to a specific window - unlike the earlier
   `WM_MOUSEWHEEL`-on-the-taskbar-HWND check (Incidents 7/9), which
   could only observe messages already targeted at this mod's own
   window. This can distinguish "nothing is ever synthesized for this
   gesture, anywhere" (Incident 15's working theory) from "something
   *is* synthesized, just not delivered to this element" - a
   meaningfully different, more actionable finding if true.

Chose option 2 first, per explicit request to try something lighter
before committing to the HID approach.

**Implemented (2026-09-17, diagnostic only)**: `LowLevelMouseProc`,
installed via `SetWindowsHookExW(WH_MOUSE_LL, ...)` from inside
`InjectWidgetStackGrid` (already guaranteed to run on the taskbar's own
message-pumping UI thread via `RunFromWindowThread` - required for
`WH_MOUSE_LL` callback delivery, since low-level hooks are delivered to
the thread that installed them and that thread must be pumping
messages). Logs every `WM_MOUSEWHEEL` seen anywhere on the desktop,
with its screen coordinates and delta. Removed in
`Wh_ModBeforeUninit` via `UnhookWindowsHookEx`.

**Also worth checking (free, no code)**: whether the earlier successful
mouse-wheel test happened while the taskbar/widget had focus (e.g.
right after a click) versus the touchpad test being tried without ever
focusing it first - Windows' "scroll inactive windows when I hover over
them" setting governs exactly this gap for a real mouse wheel, and while
it's not expected to be the touchpad blocker (that gesture doesn't
appear to produce a wheel message at all, focused or not, per the
`WM_MOUSEWHEEL`-on-taskbar-HWND check already coming up empty in
Incident 9), it costs nothing to rule out before spending more
diagnostic rounds on it.

**Next retest**: swipe two fingers over the widget and check whether
`WH_MOUSE_LL: WM_MOUSEWHEEL at (...)` appears at all. If it does, its
coordinates tell us where the OS actually routes this gesture (which
may not be our widget's screen position) - a real, previously-unknown
data point. If it still doesn't appear, that further confirms Incident
15's conclusion and makes the raw-HID route (option 1 above) the only
remaining lever, at which point it's worth explicitly deciding whether
that investment is still wanted.

**Result (2026-09-17): confirmed, still nothing.** Real mouse wheel
works without ever clicking/focusing the widget first (rules out a
focus-routing explanation and confirms "scroll inactive windows" isn't
the blocker). Two-finger touchpad scroll: no `WH_MOUSE_LL` log at all.
Since this is a *system-wide* hook - it would catch a `WM_MOUSEWHEEL`
synthesized anywhere on the desktop, for any window, not just this
mod's - this is the strongest evidence yet that Windows genuinely
synthesizes no wheel-equivalent message anywhere for this gesture at
this screen location, not merely a delivery/routing problem to this
specific element. Something (plausibly the taskbar's own native
touch-gesture handling) is consuming the gesture before any
message-based mechanism this mod can observe - Win32 message queue at
every level checked, and XAML's own gesture recognizer - ever sees it.

User asked to pursue a real fix despite this, explicitly accepting the
risk that the remaining option might not work either.

## Incident 17: raw HID digitizer input, diagnostic-only first pass (2026-09-17)

**Approach**: `RegisterRawInputDevices` for the touchpad's HID usage
(page `0x0D` "Digitizer", usage `0x05` "Touch Pad") with
`RIDEV_INPUTSINK`, registered from `InjectWidgetStackGrid` alongside the
`WH_MOUSE_LL` hook. This is the lowest level of touchpad data an
application can observe - literally the hardware's own HID reports,
delivered via `WM_INPUT` independent of any OS-level gesture/wheel
synthesis, which is why it's the last remaining lever after Incidents 9,
14, 15, and 16 all came up empty at every higher level.

**Important caveat, stated explicitly because it changes the risk
profile of this specific piece of code**: unlike every other
"reverse-engineered" or "advanced Windows API" piece of this mod so
far - the taskbar XAML access layer, the symbol hooks, the
`RunFromWindowThread` marshaling - which were all ported from real,
working reference source (`taskbar-ai-quota.wh.cpp`,
`taskbar-fluent-media-player.wh.cpp`), **there is no reference
implementation to port for HID Digitizer report parsing**. Precision
Touchpad HID report layouts follow a Microsoft specification but vary
per device/OEM, and this mod's author has no capture of a real report
to develop against. Writing a full parser from spec knowledge alone,
with no live device to verify against between iterations, carries real
risk of silently misinterpreting data (wrong scaling, wrong offsets)
in ways that wouldn't show up as a compile or runtime error - just
wrong behavior that looks like "still doesn't work" without revealing
why.

**Mitigation - this first pass parses nothing.** `TaskbarWindowSubclassProc`'s
`WM_INPUT` handler only calls `GetRawInputData`, confirms
`RIM_TYPEHID`, and logs `dwCount`/`dwSizeHid` plus a hex dump of the
report's first ~20 bytes. This answers the one question that has to be
true before any parsing effort is worth attempting at all: does this
mod's process (running inside Explorer, via a window it subclassed)
receive *any* raw HID data for this device during a two-finger scroll
gesture, given every higher-level mechanism already came up empty?
Un-registered cleanly in `Wh_ModBeforeUninit` via `RIDEV_REMOVE`.

**Next retest**: swipe two fingers over the widget and check for
`WM_INPUT HID: count=... sizeHid=... bytes=...` in the log.
- **If it appears**: real data is reaching this mod, and the actual hex
  bytes are the ground truth needed to write a correct parser against -
  same "diagnose from real data, not guesswork" approach that resolved
  the ARM64 `TaskbarHost::FrameHeight` pattern (Incident 3). Next step
  would be reading those bytes against the Windows Precision Touchpad
  HID report spec to identify contact count/X/Y fields specifically for
  this hardware.
- **If it still doesn't appear**: this would mean even raw HID input is
  being claimed exclusively by something else (plausibly Windows' own
  Precision Touchpad class driver, which may register for this device
  in a way that excludes other raw input consumers) before this mod's
  process ever sees it - at which point there is no lower level left to
  try from application code, and the feature would need to be
  considered genuinely not implementable within a Windhawk mod's
  reach, not merely unsolved yet.

**Result (2026-09-17): real data arrives.** Confirmed live -
`WM_INPUT HID` logs fired continuously during a two-finger scroll, with
`sizeHid=40`, a stable Report ID byte (`0x04`), and 20 bytes of visible
data whose bytes `[2:4]`/`[4:6]` (little-endian) trace a smooth,
continuous X/Y path as the gesture progressed (e.g. `4C 0C 10 07` →
X=3148,Y=1808 drifting to X=3149,Y=1823 over ~20 samples, then a second
burst starting fresh around X=2117,Y=2728 - consistent with a "clutch"
re-grip mid-scroll). One row also showed additional non-zero bytes past
offset 8 (`... 17 00 13 AB 10 22 04 17 ...`), plausibly a second
contact's data appearing when both fingers were briefly down together.
This is real, structured touch data reaching this mod's process - the
raw-HID route (Incident 16/17's option 1) is viable in principle.

**Not done yet**: the diagnostic only dumped the first 20 of the
report's 40 bytes, so the tail - where the Windows Precision Touchpad
spec's Contact Count field and a second contact's full data are
expected to live - was never captured. Widened the dump to the full
report (up to 48 bytes) before attempting to identify the exact field
layout (Report ID / per-contact Confidence+TipSwitch+ContactID / X / Y
/ Scan Time / Contact Count / Button) - guessing the layout from a
half-visible report risks exactly the silent-misinterpretation failure
mode flagged as the core risk of this whole approach.

**Next retest**: two-finger scroll again, capture the full 40-byte
dump this time (especially bytes past offset 20), and share it - that's
the ground truth needed to map real byte offsets to Report ID/contact
fields/contact count, the same "diagnose from real data, not guesswork"
approach that resolved the ARM64 `TaskbarHost::FrameHeight` pattern
(Incident 3).

**Result (2026-09-17), full 40-byte report analyzed:**

Confirmed fields, all validated against dozens of consecutive samples:
- `bRawData[0]` = Report ID, constant `0x04`.
- `bRawData[1]` = contact 1 status byte - `0x03` while actively
  touching, `0x01` transiently near lift-off, `0x00` when contact 1 is
  up. Bits 0/1 read as TipSwitch/Confidence.
- `bRawData[2:4]` (little-endian uint16) = contact 1 X.
- `bRawData[4:6]` (little-endian uint16) = contact 1 Y. Both confirmed
  by smooth, continuous trajectories matching real finger movement
  across many consecutive samples, including a "clutch" re-grip
  (position resets to a new starting point mid-gesture).
- `bRawData[36:38]` (little-endian uint16) = Scan Time - confirmed
  monotonically increasing by ~60 units per ~6-7ms sample interval
  (consistent with 100µs ticks).
- `bRawData[38]` = Contact Count - confirmed `0x01` during single-finger
  stretches and `0x02` exactly during rows where extra non-zero bytes
  appear mid-report (a genuine second contact briefly down).
- `bRawData[39]` = Button state (`0x00` throughout, no click involved).

**Not resolved - contacts 2+**: tried to map the middle bytes as
contiguous 5-byte slots (status+X+Y) per contact, 7 slots to fill
`1 + 7*5 + 4 = 40`. This did not validate against a real two-contact
row (`04 03 B9 0A CD 06 00 00 13 04 08 6F 09 00 ...`): slot 0
(indices 1-5) matched contact 1 correctly (status=`03`, X=2745,
Y=1741), but slot 1 (indices 6-10, expected to be contact 2) showed
status=`00` (inactive) while slot 2 (indices 11-15) showed a
plausible-looking active status with X=0,Y=0 - not a believable real
touch position. So either the slot size/stride is wrong, or contact
IDs aren't in touch order, or both. Given the stated risk of this
whole approach (no reference implementation, no way to iterate
locally), guessing further here was judged not worth it.

**Decision - track contact 1's Y only.** Rather than resolve the
ambiguous multi-contact layout, `TaskbarWindowSubclassProc`'s
`WM_INPUT` handler now uses only the two fields proven solid across
every sample: contact 1's status (to know when a touch starts/ends)
and Y (`bRawData[4:6]`). It tracks a "previous Y" reset on every fresh
touch-down (including a clutch re-grip, which naturally resets tracking
since contact 1 goes inactive between grips), accumulates the
frame-to-frame delta, and steps the active widget via `StepWidget` once
the accumulated delta passes half a pane's height - mirroring the
existing drag/`ManipulationDelta` accumulation pattern already tuned
in Incidents 12/15. This works for a two-finger scroll because both
fingers move together, so contact 1 alone still tracks the gesture's
vertical motion; it also means a *one*-finger drag on the touchpad
(if the OS ever routes it as a raw HID contact-1 move rather than a
cursor move) would trigger the same stepping - acceptable for now,
revisit if it proves to conflict with normal pointer use.

Direction is unverified and, like drag in Incident 12, will likely need
one live-tuning round: currently wired so a decreasing Y (swipe toward
the top of the pad) steps to the previous widget, matching the sign
convention already tuned for drag.

**Next retest**: two-finger scroll again - this time check whether the
active widget actually changes, not just whether logs appear. This is
genuinely unverified code with no way to test it outside real hardware,
consistent with the risk flagged when this HID route was approved.
Direction/sensitivity may need a follow-up correction, same as drag did.

## Incident 18: HID scroll fired on one finger anywhere on the taskbar, wrong direction, junky (2026-09-17)

**Live test result of Incident 17's implementation**: it did step
widgets, so the raw-HID route works end to end, but with three
problems:
1. Direction was backwards.
2. It fired with a single finger, not just a two-finger scroll.
3. It fired while the cursor wasn't even over the widget stack.
4. Motion felt "junky" (erratic/stuttery), a direct consequence of (2)
   and (3): every ordinary one-finger cursor move anywhere on the
   taskbar was being read as a scroll gesture, on top of any genuine
   two-finger scroll.

**Root cause**: two things the first pass never scoped correctly.
- `RegisterRawInputDevices` subscribes to the whole touchpad *device*,
  not to any particular window or on-screen region - `WM_INPUT`
  arrives for every touch on the pad, cursor movement included,
  regardless of where the cursor is. Nothing in Incident 17's handler
  restricted it to (a) actually being a two-finger touch or (b) the
  cursor being over the stack, so it was reading normal one-finger
  pointer movement as scroll input.
- The Y-delta sign was just guessed as "same convention as drag"
  without live verification, and guessed wrong.

**Fix**:
- Added `IsCursorOverWidgetStack(HWND)`: reads the live cursor
  position (`GetCursorPos` + `ScreenToClient`), converts it from
  physical pixels to DIPs via `GetDpiForWindow`, and hit-tests it
  against `g_ui.root`'s actual on-screen bounds via
  `root.TransformToVisual(nullptr)` - the same coordinate space
  pointer-routed XAML events use. The `WM_INPUT` handler now only
  tracks a touch when this returns true.
- Added a contact-count gate using `bRawData[38]` (Contact Count,
  confirmed in Incident 17) - now requires `contactCount >= 2`, so a
  single finger (cursor movement, or the button click that shows up as
  one contact) never triggers a step.
- Flipped the step direction (`g_hidAccumY < 0 ? 1 : -1` instead of
  `-1 : 1`).

Both gates reset `g_hidContactActive` the same way a lifted finger
already did, so dropping to one finger or moving the cursor off the
stack mid-scroll cleanly stops tracking instead of producing a jump
when a qualifying touch resumes.

**Next retest**: two-finger scroll while hovering the widget stack -
confirm direction now feels natural, confirm a single finger or
scrolling elsewhere on the taskbar no longer does anything, and report
whether the motion is smooth or still needs a sensitivity/threshold
adjustment (the `height / 2` step threshold, shared with drag, hasn't
been tuned specifically for HID's raw sensor units, which may not be
1:1 with the pointer-event coordinates drag was tuned against).

## Incident 19: still janky after the hover/two-finger gate - wrong-unit threshold, needed debouncing (2026-09-17)

**Live test result of Incident 18's fix (video capture)**: direction
now correct, no longer fires on one finger or off-stack, but motion is
still janky - a single two-finger swipe skips through several widgets
instead of stepping through them one at a time smoothly.

**Root cause, found on re-reading the flagged risk in Incident 19's
threshold rather than more gating**: the step threshold reused
`PaneHeight() / 2` - an on-screen DIP measurement of the rendered
widget pane (tens of units) - copied straight from the drag/
`ManipulationDelta` handlers, where it's correct because those deal in
pointer-event coordinates that already live in that same DIP space.
Raw HID Y, in contrast, is in the touchpad sensor's own logical unit
resolution (Precision Touchpads commonly report a Y range on the order
of ~0-3052), a completely different and much finer-grained scale. A
single deliberate swipe's raw delta cleared the DIP-sized threshold
several times over within one `WM_INPUT` burst, so the old `while
(...) StepWidget(...)` loop fired multiple steps for what was, on
screen, one continuous gesture - exactly the "several widgets skip"
behavior seen in the video.

**Fix**:
- Introduced `kHidStepThresholdUnits` (380.0, in raw HID Y units) as
  its own constant, separate from `PaneHeight()`, since the two are in
  unrelated unit spaces. This is a rough empirical guess - there's no
  device descriptor read to derive it from (still ruled out per
  Incident 17's risk framing) - so it's a first live-tuning attempt,
  not a derived value.
- Replaced the `while`-loop (which could fire several `StepWidget`
  calls off a single large delta) with a single conditional step plus
  a `kHidStepDebounceMs` (220ms) cooldown tracked via
  `g_hidLastStepTick`/`GetTickCount64()` - this is the debouncing
  requested directly. While within the cooldown, the accumulator is
  clamped to the threshold instead of left to grow unboundedly, so a
  continued hold past one step doesn't queue up extra steps to fire
  the instant the cooldown lifts.

**Next retest**: two-finger scroll again - check whether it now steps
one widget per deliberate swipe instead of skipping several. Both the
threshold value and the debounce window are first guesses and may
still need tuning in either direction (higher threshold if it's still
too eager, lower/shorter debounce if it now feels sluggish or
unresponsive to quick repeated swipes).

## Incident 20: right-click crashing Explorer again (2026-09-17)

**Report**: after HID scroll was confirmed working well and the
`IWidget` interface was ported in, right-click on the stack started
crashing Explorer again - same class of failure as Incidents 8 and 10,
both already fixed once.

**Root cause (reasoned from the code, not yet confirmed against a
Windows Event Viewer capture the way Incidents 8/10 originally were -
flag this for the next round if the fix below doesn't hold)**:
`ShowContextMenu`'s `TrackPopupMenu` call pumps its own nested Win32
message loop on this thread, same as when Incident 10 was fixed - and
Incident 10's fix (deferring the call via the XAML dispatcher) still
holds, that part wasn't touched. What's new since then is Incident 18,
which added `IsCursorOverWidgetStack` to the `WM_INPUT` handler -  it
calls `TransformToVisual` on live XAML elements to hit-test the
cursor. `WM_INPUT` keeps arriving and being dispatched to
`TaskbarWindowSubclassProc` while `TrackPopupMenu`'s nested loop has
control of the thread (a finger still resting on the touchpad right
after the click is enough), so that XAML call can now run reentrant
inside a blocking native modal loop - the exact hazard class Incident
10 already established is unsafe, just via a different code path that
didn't exist yet when Incident 10 was fixed.

**Fix**: added `g_contextMenuOpen`, set `true` for the duration of
`TrackPopupMenu` in `ShowContextMenu` and checked (alongside the
existing `nav.wheel` setting) before the `WM_INPUT` handler in
`TaskbarWindowSubclassProc` does anything - touchpad input is simply
ignored while the context menu is open, rather than trying to make the
XAML hit-test itself safe to call reentrant.

**Next retest**: right-click the stack (ideally with a finger still on
the touchpad right after, to match how the crash was triggered) and
confirm no crash, then confirm the menu's toggle/move actions still
work. If this *doesn't* fix it, the next step is pulling the actual
Windows Event Viewer exception (Event ID 1000, code/faulting
module/offset) the way Incidents 8 and 10 did, rather than reasoning
further from the code alone - this fix is a strong hypothesis, not a
confirmed root cause.

**Also flagged (user, 2026-09-17)**: `taskbar-icon-separators` on
windhawk.net reportedly builds a WinUI-style right-click menu / hooks
into the taskbar's own context menu. Worth reading before doing "Next
steps" item 7 (replacing `TrackPopupMenu` with a custom XAML menu) -
it would remove this whole hazard class by removing the nested Win32
message loop entirely, not just guard around it.

## Incident 21: `g_contextMenuOpen` guard didn't fix it - crash was in the call itself (2026-09-17)

**Result**: crashed again, this time immediately on right-click,
before the menu ever appeared - ruling out Incident 20's guard (it
only protects the window around `TrackPopupMenu`, and this crash
happens before that call is even reached). Event Viewer capture this
time (first real crash data pulled for this bug, unlike Incident 20's
code-only reasoning):

```
Faulting application: Explorer.EXE
Faulting module name: unknown, version 0.0.0.0
Exception code: 0xc0000005
Fault offset: 0x0000000000000000
```

Same signature as Incidents 8/10's original crash - access violation,
faulting module "unknown", offset `0x0`.

**Root cause**: not a dangling delegate this time (Incident 20's
theory) - the actual problem is that `IsCursorOverWidgetStack`
(Incident 18) calls `g_ui.root.TransformToVisual(nullptr)` directly
from the `WM_INPUT` handler, i.e. from inside raw input delivery. This
mod's `try { ... } catch (...) { ... }` around that call only catches
C++ exceptions; MSVC's default `/EHsc` does *not* translate structured
exceptions (access violations) into catchable C++ ones, so if
`TransformToVisual`/`ActualWidth`/`ActualHeight` ever faults at the OS
level when called from this unusual context (a live XAML/composition
call reached from raw HID input delivery, not from XAML's own normal
event dispatch), the `catch (...)` here does nothing to stop it -
explaining why guarding the surrounding window (Incident 20) didn't
help: the dangerous call was still reachable and still unprotected by
anything that could actually catch what it does.

**Fix**: stopped calling `TransformToVisual` from the input path
entirely. Added `UpdateStackScreenRect()`, which does that computation
once and caches the result (converted to absolute screen pixels) in
`g_ui.stackScreenRect`; it's called only from `RebuildStackContents`,
a call site that always runs on the taskbar's own UI thread in a
normal, non-reentrant context (initial injection, and after every
toggle/reorder/settings-change) - never from raw input delivery or a
nested modal loop. `IsCursorOverWidgetStack` is now pure Win32
(`GetCursorPos` + `PtInRect` against the cached rect) with no XAML/COM
calls at all, so there's nothing left in the `WM_INPUT` path that can
reach into XAML/composition internals. Incident 20's
`g_contextMenuOpen` guard is left in place (harmless, still prevents
touchpad nav from stepping widgets while the menu is open) but is no
longer what's relied on for correctness here.

**Trade-off**: the cached rect only updates when `RebuildStackContents`
runs. If the taskbar moves/resizes without triggering that (not
expected in this prototype's single-monitor, primary-taskbar scope -
`WM_DISPLAYCHANGE` already forces a full re-injection, which rebuilds
it), the cached rect could go stale. Acceptable for now; worth
revisiting if multi-monitor/DPI-change support is ever added.

**Next retest**: right-click the stack again (same conditions as
before - finger on the touchpad right after, if that's how the crash
was triggered) and confirm no crash. Also confirm touchpad scroll
still only reacts while hovering the stack (the whole point of
`IsCursorOverWidgetStack`), since its hit-test logic changed even
though its intent didn't.

**Result: still crashed, identical signature.** Retested - same
0xC0000005, faulting module "unknown", offset 0x0, and this time
confirmed it happens immediately on right-click, before the menu ever
appears (ruling out Incident 20's `TrackPopupMenu`-adjacent theory
even more directly than the code reasoning already had). Since
Incident 21 removed the only XAML call (`TransformToVisual`) from the
`WM_INPUT` path entirely and the crash is unchanged, that hypothesis is
now also disproven - two targeted fixes in a row, both wrong. See
Incident 22.

## Incident 22: two failed hypotheses in a row - switched to instrumentation (2026-09-17)

**Decision**: stop guessing from the code. Neither Incident 20's
`g_contextMenuOpen` guard nor Incident 21's removal of
`TransformToVisual` from the `WM_INPUT` path changed anything about
this crash, and nothing else obviously touches XAML from an unusual
context along the `RightTapped` → `ShowContextMenu` →
`TrackPopupMenu` path (unchanged since Incident 10, which did fix a
real crash there once). Added `Wh_Log` calls at each step of that path
(`RightTapped: fired`, `...got dispatcher=`, `...deferred callback
running`, `ShowContextMenu: start`, `...menu built`, `...foreground
set, calling TrackPopupMenu`, `...TrackPopupMenu returned cmd=`) so
the next crash pinpoints exactly which line ran last, the same
diagnose-before-fixing approach that resolved Incident 9's wheel/dot
bug and the HID byte layout - these are temporary and should be
stripped once the actual crash site is found.

**Next retest**: right-click, then check the Windhawk debug log for
the last `RightTapped`/`ShowContextMenu` line printed before the
crash (or share the whole log around that time) - that narrows the
crash to a specific few lines instead of an entire call chain.

## Also this round: touchpad-scroll regression, nav.wrap/nav.overscroll settings (2026-09-17)

**Touchpad scroll stopped reacting at all** after Incident 21's
rect-caching change. Root cause: `UpdateStackScreenRect` is called
from `RebuildStackContents`, right after the tree is mutated
(`root`/`clipHost` resized, widget panes rebuilt) - but
`ActualWidth`/`ActualHeight`/`TransformToVisual` reflect the *last
completed* XAML layout pass, which hadn't necessarily run yet at that
point (especially on the very first call, right after injection,
where it could still be 0). That would cache a degenerate
(possibly zero-size) rect, which `IsCursorOverWidgetStack`'s
`PtInRect` could never match - explaining "stopped working entirely"
rather than just behaving oddly. Fixed by calling
`g_ui.root.UpdateLayout()` (forces measure+arrange synchronously)
before reading any of those values in `UpdateStackScreenRect`.

**User also reported, as separate context**: touchpad scroll
previously only worked "when no app window has focus, only when I
first clicked on the taskbar" - i.e. `RIDEV_INPUTSINK`'s documented
"receives input regardless of foreground focus" doesn't seem to hold
in practice for this device/setup. **Not investigated yet** - noted
here as a known characteristic/limitation rather than guessed at
blind, given three wrong guesses already this session on a related
crash. Worth its own diagnostic round (does `WM_INPUT` actually stop
arriving when another app has focus, or does something else in this
mod's gating suppress it) once the right-click crash is resolved.

**Added `nav.wrap` and `nav.overscroll` settings** (both default
`true`, requested by user): `StepWidget` previously always wrapped
past either end unconditionally; now it only does that when
`nav.wrap` is on. When it's off and `nav.overscroll` is on, stepping
past an end triggers `BounceAtBoundary` - a small (8px) overshoot-and-
settle animation reusing `OnRenderingTick`'s existing ease-out lerp
for both legs (out to the overshoot position, then back to the
current widget's resting position), via a new `bouncePending`/
`bounceBaseY` pair on `UiState` rather than a second animation
mechanism. When both settings are off, stepping past an end does
nothing, matching the pre-existing behavior before `nav.wrap` existed.

**Next retest** (once the right-click crash is separately resolved):
confirm wrap-around can be turned off and the stack then stops at the
first/last widget instead of cycling, and confirm the overscroll bump
feels like a quick bounce rather than a stutter or an unwanted extra
step.

**Result: touchpad scroll confirmed fixed.** Right-click crash
confirmed still present, identical signature, and this time with the
Incident 22 logging captured on the actual crash run:

```
RightTapped: fired
RightTapped: got dispatcher=1
RightTapped: deferred callback running
ShowContextMenu: start, 2 widgets
ShowContextMenu: menu built
ShowContextMenu: foreground set, calling TrackPopupMenu
[[ Explorer restarts here - new PID in the next "Injected widget stack" line ]]
```

The crash site is now pinned down precisely: after `SetForegroundWindow`
succeeds, inside the `TrackPopupMenu` call itself, before it can return
(`ShowContextMenu: TrackPopupMenu returned cmd=...` never printed). See
Incident 23.

## Incident 23: replaced TrackPopupMenu with a XAML MenuFlyout entirely (2026-09-17)

**Given the crash site**: purely native code (`CreatePopupMenu`/
`AppendMenuW`, no XAML) runs fine beforehand; the fault is specifically
inside `TrackPopupMenu`'s own nested Win32 message loop, on a window
that hosts a live XAML island. Both prior targeted fixes (Incidents 20,
21) addressed real, plausible hazards from this file's OWN code
touching XAML reentrant with that loop - and both were confirmed
ineffective, meaning whatever collides with `TrackPopupMenu` here isn't
something this mod's code can obviously see or guard around from the
outside. Rather than attempt a third guess, switched to the fix already
flagged as the long-term plan in "Next steps" item 7: stop using
`TrackPopupMenu` at all.

**Fix**: `ShowContextMenu` now builds and shows a
`winrt::Windows::UI::Xaml::Controls::MenuFlyout` instead of a native
popup menu - `ToggleMenuFlyoutItem` per widget (using its own
`IsChecked` property instead of `MF_CHECKED`) for enable/disable, a
`MenuFlyoutSeparator`, then a `MenuFlyoutItem` pair per widget for move
up/down. `flyout.ShowAt(g_ui.root)` shows it near the stack with no
explicit position (default placement) - no nested Win32 message loop
at all, the whole thing runs through the same Composition/dispatcher
machinery as everything else in this file. Each item's `Click` handler
does its own toggle/move + `RebuildStackContents()` immediately
(`ToggleWidgetEnabled`/`MoveWidget`), replacing the old single
post-`TrackPopupMenu`-return switch statement, since showing a flyout
is fire-and-forget rather than blocking. `g_contextMenuFlyout` (a global
holding the shown flyout) and `g_contextMenuOpen` (still gates the
touchpad `WM_INPUT` handler while the menu is up, though the specific
hazard it originally guarded no longer applies here) are both cleared
from the flyout's `Closed` event. Removed `WidgetMenuCmd`,
`TrackPopupMenu`/`CreatePopupMenu`/`AppendMenuW`/`DestroyMenu`/
`SetForegroundWindow`, and the Incident 22 diagnostic `Wh_Log` calls
(no longer needed - the crash site they were added to find is now
moot, since the code that crashed is gone).

**Prior art**: user pointed at `taskbar-icon-separators` (windhawk.net)
as a mod that already builds WinUI-style taskbar context menus this
way - matches the approach taken here, though its source wasn't read
before making this change (the API shape - MenuFlyout/
ToggleMenuFlyoutItem/ShowAt - is standard WinRT, not something specific
to that mod).

**Not fully investigated**: *why* `TrackPopupMenu` specifically crashed
on a XAML-island-hosting window in this environment remains unknown -
this fix works around it rather than explains it. If a future feature
genuinely needs a native modal loop again, that question would need
revisiting.

**Next retest**: right-click the stack - confirm the menu appears (as
a XAML flyout, so it'll likely look different from the native menu:
no checkmark glyph, just `ToggleMenuFlyoutItem`'s own checked visual),
confirm toggling a widget and moving up/down all work and immediately
update the stack, and confirm no crash. Also worth checking the
flyout's default placement looks reasonable next to the narrow stack -
`ShowAt` positioning can be tuned with `FlyoutShowOptions` if not.

**Result (0.1.29): confirmed working.** One compile error first -
`FlyoutBase::Closed`/`ShowAt` are declared with a deduced `auto`
return type in `Windows.UI.Xaml.Controls.h`'s own header, and the
actual definitions live in `Windows.UI.Xaml.Controls.Primitives.h`,
which wasn't included - added it, fixed the build. After that: no
crash, menu appears, toggle/move-up/move-down all work and update the
stack immediately. Right-click is confirmed stable again.

**Session status as of 0.1.29**: touchpad scroll (Incidents 17-22),
right-click (Incident 23), dots, wheel, drag, and the `IWidget`
interface with both placeholders are all confirmed working live.
`nav.wrap`/`nav.overscroll` (Incident 22) added but not yet
specifically retested. Still open: the "touchpad scroll only reacts
after first clicking the taskbar to focus it" characteristic noted
alongside Incident 22 - not investigated.

## Incident 24: right-click menu restructured to match native taskbar style (2026-09-17)

**Request**: user supplied a mockup of the native Windows 11 taskbar's
own per-icon right-click menu style - one row per item, each opening a
submenu with a checkable "Show widget" toggle plus "Move up"/"Move
down" (with chevron icons), and a "Stack settings" row (with a
layered/stack icon) below a separator at the top level. Incident 23's
first MenuFlyout pass was flatter: a toggle item per widget, one
separator, then a move-up/move-down pair per widget, all at the same
level.

**Change**: `ShowContextMenu` now builds, per widget, a
`MenuFlyoutSubItem` (renders with the ">" chevron that opens a
submenu, matching the mockup) whose own `Items()` holds a
`ToggleMenuFlyoutItem` ("Show widget", checkable exactly as
before - `IsChecked`/`Click` unchanged, just moved into the submenu),
a separator, then "Move up"/"Move down" as `MenuFlyoutItem`s with
`SymbolIcon(Symbol::Up)`/`SymbolIcon(Symbol::Down)`. The widget's own
`DisplayName()` (which embeds a `\n` for the pane's own two-line
label, e.g. "Media\nPlayer") is used as the submenu's row text with
the newline replaced by a space, since a menu row is single-line.
Below all the per-widget submenus: a separator, then a "Stack
settings" `MenuFlyoutItem` with `SymbolIcon(Symbol::Setting)` (a gear
- the mockup's own layered-stack icon wasn't matched to an exact
Segoe Fluent glyph here; revisit if the user wants that specific
icon) - currently a no-op click, since there's no custom settings
surface yet (`ToggleWidgetEnabled`/`MoveWidget` handlers, `Closed`
cleanup, and the overall `flyout.ShowAt` mechanism are all unchanged
from Incident 23).

**Next retest**: right-click, confirm each widget shows as its own
submenu row with the chevron, opening to reveal "Show widget"
(checked when enabled)/"Move up"/"Move down", and confirm "Stack
settings" appears below a separator (even though it doesn't do
anything yet).

**Result: compile error** - `Symbol::Down` doesn't exist in this
SDK's `Windows.UI.Xaml.Controls.Symbol` enum (`Symbol::Up` alone did
compile). Rather than keep guessing at which Symbol enum members
exist, switched both chevrons - and, proactively, the "Stack
settings" gear that used `Symbol::Setting` - to `FontIcon` with
explicit Segoe MDL2 Assets glyphs (``/`` for up/down
chevrons, `` for the settings gear) instead of the `Symbol`
enum, to remove this whole class of "which enum members actually
exist" guesswork.
