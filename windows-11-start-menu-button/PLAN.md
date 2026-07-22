# windows-11-start-menu-button — Windhawk mod plan

## Context
Windhawk mod for Win11 taskbar Start button. Goals: swap stock icon for custom image, or recolor stock icon while keeping native animation, with a per-tile depth gradient and a one-shot highlight sweep on press. Target: Windows 11 25H2 only, all taskbar instances (multi-monitor).

**Per-state (default/hover/pressed) background/padding/margin/corner-radius box styling has been REMOVED as out of scope** (2026-07-22) — this mod is now icon-only (custom icon swap, or recolor + depth gradient + press sweep). See "Settings schema (current)" below for what remains.

## File
`windows-11-start-menu-button.wh.cpp` — single-file Windhawk mod, metadata header comment block (standard Windhawk format), placed in this folder.

## Reference mods (ramensoftware/windhawk-mods)
- `taskbar-start-button-corner-fix.wh.cpp` — locates Start button via UI Automation, `AutomationId = "StartButton"`.
- `taskbar-fluent-media-player.wh.cpp` — `RunFromWindowThread()` pattern, nested settings struct, dynamic settings reload.
- `taskbar-start-button-position.wh.cpp` — same hook point (`ExperienceToggleButton::UpdateButtonPadding`) used here, verified pattern for reaching `ExperienceToggleButtonRootPanel`.

## Settings schema (current, reorganized 2026-07-22)
Nested groups (Windhawk supports arbitrary-depth nesting; settings keys are dot-joined per level, e.g. `recolor.gradient.lightenPercent`). No enclosing top-level `icon` group anymore — `mode` is top-level, `customIcon`/`recolor` are top-level groups:

- `mode`: default / customIcon / recolor (**default: `default`** — mod is a no-op out of the box)
- `customIcon` (group)
  - `path`: string, PNG/ICO file path
- `recolor` (group)
  - `color`: hex string, resting/base color
  - `recolor.gradient` (sub-group)
    - `enabled`: bool, master toggle for gradient shading + elevated (hover/press/menu-open) brighten (off = flat single-tone fill, no brighten) — **moved here from a separate top-level toggle**
    - `lightenPercent` / `darkenPercent` (default 18/18)
  - `recolor.shimmer` (sub-group)
    - `enabled`: bool, master toggle for the click-sweep specifically. **Decoupled from `recolor.gradient.enabled` (2026-07-22)** — shimmer can be on with gradient shading off (flat color + sweep) and vice versa. `CreateRecolorBrush` now routes through the gradient-brush code path whenever *either* is on (`params.sweepProgress >= 0.0f` is the shimmer-active signal, already forced to `-1` upstream when `recolor.shimmer.enabled` is false); `CreateDepthGradientBrush` itself branches internally on `g_settings.recolorGradient` to decide flat vs. shaded stop colors, independent of the sweep band logic.
    - `color`: hex string, explicit sweep tint; empty = auto-derive (see next)
    - `autoLightenPercent` (default 30) — only used when `color` is empty: blend-toward-white amount, i.e. equivalent to compositing translucent white over `recolor.color` at this opacity (e.g. `#4Cffffff`). **Always lightens, no adaptive push-away-from-lightness logic** (that was tried and rejected — see "How we got here").
    - `durationMs` (default 250)
    - `bandWidthPercent` (default 500) — usable range roughly 100 (thin line) to 1000 (broad glow); no native min/max slider support in Windhawk's settings schema, so this is enforced only via description text, not validation
  - `recolor.elevate` (sub-group)
    - `lightenBoostPercent` / `darkenReliefPercent` (default 100/100) — extra brighten while hover/press/menu-open
    - `transitionMs` (default 180) — ramp time for the elevated-brighten transition

No `default`/`hover`/`pressed` box-style groups anymore (removed 2026-07-22).

## Icon recolor architecture (current, as of 2026-07-22)

The Start icon is a Lottie-based `Microsoft.UI.Xaml.Controls.AnimatedVisualPlayer` (WinUI2/MUX), not a `Windows.UI.Xaml.IconElement`. Recolor works by directly overwriting `CompositionSpriteShape.FillBrush`/`StrokeBrush` on every leaf shape in its Composition tree, every frame (`StartPersistentIconColorMaintenance`, a `CompositionTarget.Rendering` subscription per button, for the button's whole lifetime) — a one-shot set gets fought/reset by Explorer's own hover/press Lottie animation playback, confirmed live.

### Confirmed facts about this specific asset (from a full live tree dump — see "How we got here")
- Only **one of four** `ShapeVisual` copies under `GetElementChildVisual` is actually active; the other three have their root container's raw `.Scale()` reading `(0,0)` (a collapsed/inactive Lottie animation-state layer, not a bug — harmless to touch, just invisible).
- The active group has **4 leaf tile shapes**, distinguished by `TransformMatrix`:
  - identity `[1,0,0,1]` → visually **top-left**
  - 180° rotation `[-1,0,0,-1]` → visually **bottom-right** (confirmed live: darken only ever showed up on this exact tile)
  - two 90°/-90° rotations `[0,-1,1,0]` and `[0,1,-1,0]` → the **anti-diagonal pair** (top-right/bottom-left)
- `shape.Scale()` is **unreliable** — reads `(0,0)` on plain container shapes whose `TransformMatrix` is genuine identity. Don't use it; `TransformMatrix`'s own 2×2 linear part is the trustworthy source.
- Per-tile translations cluster within ~1 unit of the icon's center — the real tile position/size lives in path geometry data that Composition's API doesn't expose a bounds query for. **Absolute pixel positioning of tiles is not computable from this API surface.**

### Current gradient approach: per-tile Relative gradient, rotation-corrected direction, role-based stop colors
Given the above, a single Absolute-mode gradient spanning the whole icon (the original design intent, see "Rejected approach" below) isn't achievable with reliable data. Current implementation instead:

1. Accumulate only a **2×2 rotation/reflection matrix** (`Mat2`, in `windows-11-start-menu-button.wh.cpp`) down the Composition tree — composed from each shape's `TransformMatrix` 2×2 part (visuals contribute their XY `.Scale()` diagonal only; no visual-level rotation is in evidence in this asset). No translation/position is tracked at all — not needed.
2. At each leaf, use `MappingMode::Relative` (0..1 of that shape's own render box) instead of `Absolute`.
3. Compute the gradient's local direction by applying the **inverse** of the shape's accumulated rotation matrix to the global diagonal `(1,1)` (`Mat2Invert` + `Mat2Apply`), so after the shape's own transform renders it, the gradient visually points top-left→bottom-right regardless of that tile's own rotation.
4. Classify each tile's **role** from its rotation matrix trace (`m11+m22`): `>+1` → top-left, `<-1` → bottom-right, else → anti-diagonal pair. Each role gets different stop colors (per explicit user spec, 2026-07-22):
   - top-left: 100% lighten → resting color
   - anti-diagonal pair: 25% lighten → 25% darken
   - bottom-right: resting color → 100% darken

This is an approximation of "one continuous diagonal gradient across the whole icon" using only rotation data (reliable) and no position/size data (unavailable) — not literally continuous, but consistently oriented and shaded per-tile.

### Rejected approach: Absolute-mode position math (2026-07-22, several rounds)
Originally tried accumulating `Float2` offset+scale (translation + per-axis scale) down the tree and using `MappingMode::Absolute` with `StartPoint`/`EndPoint` in the icon's overall pixel space. Failed for two fundamental, not-tunable-away reasons (see "Confirmed facts" above): (a) two tiles use a genuine 90°/-90° rotation, which cannot be decomposed into independent X/Y scale factors no matter how the sign/magnitude extraction is tuned; (b) real tile position/size data isn't queryable from Composition's API. Do not retry this without first finding a way to query actual geometry bounds (unclear if possible at all via `CompositionPathGeometry`).

### Press-sweep trigger: `Checked`/`Unchecked`, NOT `PointerPressed` (confirmed live, 2026-07-22)
`PointerPressed` **never fires** on the Start button's `FrameworkElement` for a real click — confirmed via unambiguous per-event DebugView logging: a real click logs `PointerEntered` → `Checked`/`Unchecked` → `PointerCaptureLost`, with zero `PointerPressed` anywhere in between. The click must be handled by Explorer before it reaches this element's routed pointer events. The sweep is armed from `Controls::Primitives::ToggleButton::Checked`/`Unchecked` instead (every click, whether it opens or closes the Start menu, counts as a "press" for sweep purposes). The `PointerPressed` handler and its arming code are still present in `SetupButtonTracking` as a harmless no-op fallback in case a future Windows build routes it through normally — don't be misled by its presence into thinking it's the active trigger.

Sweep is also **staggered per-tile** (not simultaneous across all 4 tiles): each tile's role (top-left / anti-diagonal pair / bottom-right, same classification as the gradient stop colors) gets its own overlapping window within the overall `sweepProgress` 0..1 range (`roleWindowStart`/`roleWindowEnd` in `CreateDepthGradientBrush`), so the 4 independent per-tile sweeps read as one diagonal wave rather than a synchronized flash.

### Crash containment
The entire per-frame body in `StartPersistentIconColorMaintenance` is wrapped in try/catch (`winrt::hresult_error` + catch-all), logging and skipping the frame instead of letting an exception escape the native `CompositionTarget.Rendering` callback (which could otherwise crash Explorer).

**Root cause of the Explorer-restarts-on-every-reload symptom, confirmed 2026-07-22 (not the benign Windhawk reload-cost previously suspected):** Explorer Event Viewer showed a real crash - Event ID 1000, `Explorer.EXE` faulting in `KERNELBASE.dll`, exception code `0x20474343` (Control Flow Guard fail-fast), occurring a few seconds after the log's last "Icon color maintenance" line, which showed the icon fully idle (`state=0 menuOpen=0 elevated=0.00 sweep=-1.00`). Root cause: `Wh_ModBeforeUninit`/`Wh_ModUninit` never explicitly revoked each button's `CompositionTarget.Rendering` subscription - they relied on the render lambda itself noticing `g_unloading` **on its own next invocation** and self-revoking there. But `CompositionTarget.Rendering` stops firing once nothing needs to redraw (exactly the idle state in the log), so if no further frame fired before Windhawk unloaded the mod's DLL, the still-registered callback's code ended up living in now-unmapped memory - the next time the compositor invoked it, an indirect call into freed memory is exactly what CFG fail-fasts on.

**Fix:** `TrackedButton::renderingToken` now holds the subscription's token, and `StopIconColorMaintenance()` (called synchronously for every tracked button from `Wh_ModBeforeUninit`, before any chance of DLL unload) explicitly revokes it and does the one last Composition-brush restore that used to be attempted - unreliably - inside the render lambda's own `g_unloading` branch. That branch is now just a defensive early-return (no revoke, no brush touch - both are `StopIconColorMaintenance`'s job).

This means at least *some* of the "every recompile/settings-apply causes an Explorer restart" behavior was a real bug, not (only) inherent reload cost - re-test after this fix to see whether restarts still happen at all, and if so, whether they're now clean (no Event ID 1000) vs. still crashing.

### Menu-open detection (spec state D)
`button.try_as<Controls::Primitives::ToggleButton>()` — **confirmed working live** (menuOpen correctly flips in DebugView logs on Start-menu open/close). Subscribes `Checked`/`Unchecked` to set `TrackedButton::menuOpen`, which factors into the "elevated" (brighten) state alongside hover/press.

## How we got here (debugging journey, most recent first)
1. **Full shape-tree dump** (`DumpShapeTreeOnce`, now REMOVED from code after serving its purpose — see git history / conversation if needed again) walked every visual/shape from both `GetElementVisual` and `GetElementChildVisual`, logging raw `Offset`/`Scale`/`TransformMatrix` and folded totals. This is what revealed the collapsed-visual-copies and 90°-rotation facts above. If gradient math needs revisiting, recreating a dump like this first is strongly recommended over guessing from a single-sample debug log again — several earlier rounds of blind patching (signed-scale extraction, zero-vs-identity TransformMatrix assumption) failed because they were based on only one leaf's data per frame, picked arbitrarily by traversal order.
2. Before the full dump, `g_gradientDebugSample` (also now removed) logged only the first-touched leaf's data each frame — insufficient, led to two rounds of incorrect fixes (assuming unset `TransformMatrix` reads as zero-matrix; assuming scale sign-extraction alone would fix mirrored tiles). Both assumptions were partially right but incomplete versus the full-tree ground truth.
3. Original design intent (see old conversation) was a single Absolute-mode gradient literally spanning the whole 2×2 icon, chosen deliberately over "identical gradient per tile" during an early design interview — abandoned once the rotation/position data limitations above were confirmed live. Current per-tile-relative-with-role-based-colors approach is the practical compromise.

## Modes (mutually exclusive, user setting)
1. **Custom icon** — user-supplied PNG/ICO file path, replaces stock icon bitmap directly. Overlay `Image` element inserted into the panel's `Children` collection (panel is `Taskbar.TaskListButtonPanel`, a generic `Panel`, not `Grid`).
2. **Recolor** — see "Icon recolor architecture" above.

## Hook / implementation approach (unchanged from original)
1. Symbol hook on `winrt::Taskbar::implementation::ExperienceToggleButton::UpdateButtonPadding` in `Taskbar.View.dll`, filtered to `AutomationId == "StartButton"`, reaching child `ExperienceToggleButtonRootPanel`.
2. `SetupButtonTracking` per button instance (guarded against re-setup via `FindTrackedButton`), registers pointer event handlers (feed `TrackedButton::lastState`, used only by the icon animation now — no box styling) and the `ToggleButton` Checked/Unchecked handlers (menu-open).
3. `StartPersistentIconColorMaintenance` runs one `CompositionTarget.Rendering` subscription per button for its whole lifetime.

## Out of scope
- Custom animations beyond native hover/press scale/fade (deliberately not reimplemented — risks fighting native `VisualStateManager` animation, same bug class as the brush-override fight this mod already works around for color).
- **Per-state (default/hover/pressed) background/padding/margin/corner-radius styling** — removed 2026-07-22, was in original scope but cut as out of scope for this mod.
- Windows versions other than 25H2.
- SVG icon support (PNG/ICO only).

## Dev workflow note
Explorer restarting on every recompile/settings-apply was long assumed to be inherent to Windhawk's reload mechanism for symbol-hooked DLLs (`Taskbar.View.dll`, loaded once at Explorer startup) and not fixable from mod code - **that assumption turned out to be at least partly wrong** (see "Crash containment" above): a real unload-time crash (CFG fail-fast from an orphaned `CompositionTarget.Rendering` callback) was found and fixed 2026-07-22. Re-test whether restarts still occur post-fix before re-adopting the "inherent, unfixable" framing. To minimize how often a full reload is needed regardless: the `recolor.gradient.*`/`recolor.elevate.*`/`recolor.shimmer.*` tuning constants are deliberately exposed as live-editable settings (not compile-time constants) specifically so visual tuning doesn't require a recompile+reload cycle — only logic changes do.

## Verification
- Load mod in Windhawk on a Win11 25H2 test machine, target `explorer.exe`.
- Test custom icon swap and recolor tint modes.
- **Confirmed live (2026-07-22)**: role-based per-tile gradient colors, and the `Checked`/`Unchecked`-triggered press sweep, both visually working per user report ("ok now everything seems to be working visually").
- **Confirmed live (2026-07-22)**: staggered per-tile sweep wave (vs. all 4 tiles flashing in sync) and the re-exposed `recolor.shimmer.color` setting, both added this session — awaiting next test round to confirm visually (not yet explicitly reported back by the user as of this writing).
- **Not yet visually confirmed**: the settings reorg (flattened top-level `mode`/`customIcon`/`recolor` groups, `recolor.gradient.enabled` toggle moved under gradient, `recolor.shimmer.autoLightenPercent` white-overlay-blend behavior replacing the old adaptive-lightness-push logic, `recolor.shimmer.bandWidthPercent` default raised to 500) — code just written this session, awaiting live test.
- Test multi-monitor: confirm styling applies to Start button on every taskbar instance.
- Test settings reload: change settings in Windhawk UI, confirm mod updates live without needing to fully reason about whether the observed Explorer restart is normal (see "Dev workflow note").
