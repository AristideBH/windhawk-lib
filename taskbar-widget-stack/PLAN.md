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
