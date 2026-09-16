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
1. Get this prototype confirmed working live (overlay renders, scroll/snap/
   dots/drag feel right, popup menu enable/disable/reorder persists).
2. Read `Taskbar-Fluent-Media-Player` and `taskbar-ai-quota` source to design
   the real widget SDK contract (paint callback signature, size negotiation,
   input forwarding, versioning/ABI-stability story).
3. Port one real widget (likely AI quota, simpler) through the new SDK as
   the SDK's first real consumer, before attempting the media player.
4. Revisit config UI: replace the native popup menu with a custom-drawn
   drag-and-drop reorder panel, per the original request.
5. Write a versioned SDK doc once the ABI stabilizes, ahead of any public
   windhawk.net listing.

## Verification
- Not yet tested — no Windows target available in this session. User to
  load in Windhawk against `explorer.exe` on Windows 11 25H2, verify overlay
  position/size looks correct next to the other taskbar mods, verify all
  three nav modes, verify dot indicators track enabled widgets only, verify
  popup-menu enable/disable/reorder persists across Explorer restarts.

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

**Resolved (2026-09-16)**: the diagnostic hex dump the user reported back
(`7F 23 03 D5 FD 7B BF A9 FD 03 00 91 08 0C 41 F8`) is an exact match for
the ARM64 prologue (`pacibsp` / `stp fp,lr,[sp,#-0x10]!` / `mov fp,sp` /
`ldr x8,[x0,#0x10]!`) that `taskbar-ai-quota.wh.cpp` itself already
handles in a `#elif defined(_M_ARM64)` branch - the user is on an ARM64
Windows machine (Surface/Copilot+ PC class device), and this mod's first
port of "Taskbar XAML Access" (Incident 2) only kept the x64 branch,
dropping the reference mod's existing ARM64 support entirely. That was a
real scope-cutting mistake made without checking the user's hardware, not
a genuine platform limitation - the upstream mod already solved this.
Ported the missing `#elif defined(_M_ARM64)` branch (same byte/word
pattern and offset-extraction bit shift as the reference) alongside the
existing `#if defined(_M_X64)` one, and added a matching diagnostic word
dump for the ARM64 path too, in case this exact pattern also turns out to
need adjusting for some ARM64 Windows builds. Updated `@architecture` in
the mod header from `x86-64` to `x86-64 arm64` to reflect actual
capability (worth noting: the mod compiled and ran successfully on the
user's ARM64 machine even while the header still said `x86-64` only,
suggesting Windhawk doesn't hard-gate local/manually-loaded mods on this
field - not re-verified, just observed).
