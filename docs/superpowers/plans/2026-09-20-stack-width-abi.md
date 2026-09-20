# Cross-mod widget stack width negotiation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace each widget mod's own ad-hoc width tuning with a single stack-owned `layout.minWidth`/`layout.maxWidth` pair that every registered widget stretches to fill, so installing/enabling widgets together no longer requires manually juggling each mod's own width settings against the others.

**Architecture:** `Create()`/`OnSettingsChanged()`'s returned `double` changes meaning from "desired width, widest wins" to "minimum readable width, stack decides the rest" - `taskbar-widget-stack`'s `RebuildStackContents` clamps the widest reported minimum against new `layout.minWidth`/`layout.maxWidth` bounds instead of just capping a max. This only fixes anything visually in combination with a second, related bug this plan also fixes: all three widget-providing mods (`media-player`, `system-usage`, `weather`) explicitly set their registered-mode wrapper's `HorizontalAlignment` to `Left`, which silently overrides `IWidget::Create()`'s own already-documented contract ("a widget narrower than that gets stretched to fill it via the default HorizontalAlignment::Stretch") - without removing that override, changing what `Create()` returns alone would change nothing about how wide a widget actually renders.

**Tech Stack:** C++/WinRT XAML (`Grid`/`StackPanel`/`Border` layout, `HorizontalAlignment`, `ColumnDefinition` Star sizing), Windhawk mod settings (`Wh_GetIntSetting`, the private-store override pattern already used for every other `layout.*` setting in `taskbar-widget-stack`).

**Spec:** [docs/superpowers/specs/2026-09-20-stack-width-abi-design.md](../specs/2026-09-20-stack-width-abi-design.md)

## Global Constraints

- This changes an in-repo breaking contract shared by four `.wh.cpp` files (`taskbar-widget-stack`, `taskbar-widget-media-player`, `taskbar-widget-system-usage`, `taskbar-widget-weather`) - all under one author's control, no third-party mod depends on today's meaning of the returned `double`. No `V2` struct, no compat shim (spec's Non-goals).
- Only the **stack-registered** path changes. Standalone-injected mode (no `taskbar-widget-stack` installed/enabled) keeps each mod's own existing width behavior/settings untouched.
- No automated test framework / no local compiler in this working environment - every mod in this repo is verified by a written manual-verification checklist plus the user's own live Windhawk compile/test pass, not an automated run. Every step below is a code-reading verification, not an execution.
- Widget mods must not set an explicit `Width()`/`HorizontalAlignment::Left` on their registered-mode top-level element - `IWidget::Create()`'s own doc comment already states this contract (`taskbar-widget-stack.wh.cpp:226-232`); this plan is bringing three implementations into compliance with a rule that was already documented but not followed.
- `layout.minWidth` must never exceed the *effective* `layout.maxWidth` at the point they're both used - if a user sets `minWidth > maxWidth`, treat `maxWidth` as `max(minWidth, maxWidth)` rather than producing an inverted clamp range (spec's error-handling section).

---

## File Structure

No new files. Every task modifies one existing mod's single `.wh.cpp`:

```
taskbar-widget-stack/taskbar-widget-stack.wh.cpp           (Task 1)
taskbar-widget-media-player/taskbar-widget-media-player.wh.cpp  (Task 2)
taskbar-widget-system-usage/taskbar-widget-system-usage.wh.cpp  (Task 3)
taskbar-widget-weather/taskbar-widget-weather.wh.cpp        (Task 4)
```

Task 5 updates each mod's own `PLAN.md` (documentation + live-test checklist), touching all four again plus the repo's version-bump convention.

---

### Task 1: Stack owns the shared width (`layout.minWidth` + the new clamp)

**Files:**
- Modify: `taskbar-widget-stack/taskbar-widget-stack.wh.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `g_settings.layoutMinWidth` (int, mirrors the existing `layoutMaxWidth` field/loading pattern), `WidgetEntry::minWidth` (renamed from `desiredWidth`, same type/storage - `double`), the updated `RebuildStackContents` clamp. Tasks 2-4 read `IWidget::Create()`'s updated doc comment (no code dependency - each mod's ABI callback is independently compiled) to know the new contract their own `Create()`/`OnSettingsChanged()` must follow.

- [ ] **Step 1: Add `layout.minWidth` to the settings.yaml block**

In the mod's `==WindhawkModSettings==` block, find the existing `maxWidth` entry:
```yaml
  - maxWidth: 520
    $name: Maximum stack width
    $description: >-
      Upper bound, in pixels, for how wide the widget stack can grow to fit
      its widest enabled widget. Widgets narrower than this stretch to fill
      it.
```
Add a `minWidth` entry immediately before it:
```yaml
  - minWidth: 80
    $name: Minimum stack width
    $description: >-
      Lower bound, in pixels, for the widget stack's width - every widget
      stretches to at least this wide, even if none of them individually
      need it. Raise this if a narrow widget (or an empty stack) looks too
      cramped next to a wider one.
  - maxWidth: 520
    $name: Maximum stack width
    $description: >-
      Upper bound, in pixels, for how wide the widget stack can grow to fit
      its widest enabled widget's own minimum readable width. Widgets
      narrower than the final width stretch to fill it.
```
(note the `maxWidth` description is also updated here, from "widest enabled widget" to "widest enabled widget's own minimum readable width" - it now describes a floor being stretched from, not a size being capped from).

- [ ] **Step 2: Add `layoutMinWidth` to the settings struct and `LoadSettings()`**

Find the anonymous settings struct (search `int layoutMaxWidth = 520;`, around line 460) and add a sibling field immediately above it:
```cpp
    int layoutMinWidth = 80;
    int layoutMaxWidth = 520;
```
Find `LoadSettings()` (search `void LoadSettings() {`) and add the mirrored read immediately before the existing `layoutMaxWidth` read:
```cpp
    int minWidth = Wh_GetIntSetting(L"layout.minWidth");
    g_settings.layoutMinWidth = minWidth > 0 ? minWidth : 80;
    int maxWidth = Wh_GetIntSetting(L"layout.maxWidth");
    g_settings.layoutMaxWidth = maxWidth > 0 ? maxWidth : 520;
```
A few lines below, in the same function's private-store override block (search `if (ReadPrivateDword(L"layout.maxWidth", v) && v > 0) {`), add the mirrored override immediately before it:
```cpp
    if (ReadPrivateDword(L"layout.minWidth", v) && v > 0) {
        g_settings.layoutMinWidth = (int)v;
    }
    if (ReadPrivateDword(L"layout.maxWidth", v) && v > 0) {
        g_settings.layoutMaxWidth = (int)v;
    }
```

- [ ] **Step 3: Add a "Minimum stack width" slider to the settings window, mirroring "Maximum stack width"**

Find the `maxWidthGroup`/`maxWidthSlider` block in the settings window's Layout tab builder (search `StackPanel maxWidthGroup;`). Insert a mirrored `minWidthGroup`/`minWidthSlider` immediately before it:
```cpp
    StackPanel minWidthGroup;
    minWidthGroup.Orientation(Orientation::Vertical);
    minWidthGroup.Spacing(4);

    TextBlock minWidthLabel;
    minWidthLabel.Text(winrt::hstring(L"Minimum stack width: " +
                                       std::to_wstring(g_settings.layoutMinWidth) +
                                       L"px"));
    minWidthGroup.Children().Append(minWidthLabel);

    Slider minWidthSlider;
    minWidthSlider.Minimum(20);
    minWidthSlider.Maximum(1000);
    minWidthSlider.StepFrequency(10);
    minWidthSlider.Value(g_settings.layoutMinWidth);
    minWidthSlider.ValueChanged(
        [minWidthLabel](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::UI::Xaml::Controls::Primitives::
                RangeBaseValueChangedEventArgs const& args) {
            int value = (int)args.NewValue();
            g_settings.layoutMinWidth = value;
            WritePrivateDword(L"layout.minWidth", (DWORD)value);
            minWidthLabel.Text(winrt::hstring(
                L"Minimum stack width: " + std::to_wstring(value) + L"px"));
            RebuildStackContents();
        });
    minWidthGroup.Children().Append(minWidthSlider);
    panel.Children().Append(minWidthGroup);
```
Leave the existing `maxWidthGroup`/`maxWidthSlider` block immediately after this, unchanged.

- [ ] **Step 4: Rename `WidgetEntry::desiredWidth` to `minWidth` and update its doc comment**

Find the field (search `double desiredWidth = 0.0;`, around line 449) and its preceding comment:
```cpp
    // Cached return value of the widget's last Create()/
    // OnSettingsChanged() call, used to compute the stack's width.
    double desiredWidth = 0.0;
```
Replace with:
```cpp
    // Cached return value of the widget's last Create()/
    // OnSettingsChanged() call - its own minimum readable width, not a
    // "desired" size. The stack's final shared width is derived from
    // the widest of these across all enabled widgets, clamped to
    // layout.minWidth/maxWidth (see RebuildStackContents).
    double minWidth = 0.0;
```
Then find and update the two other uses of `entry.desiredWidth` in `RebuildStackContents` (Step 5 below covers the second one; the first is the `Create()` assignment):
```cpp
            entry.desiredWidth = entry.widget->Create(host);
```
becomes
```cpp
            entry.minWidth = entry.widget->Create(host);
```

- [ ] **Step 5: Change `RebuildStackContents`'s width computation from "cap the max" to "clamp the max"**

Find this block (search `double contentWidth = kMinContentWidth;`):
```cpp
    double contentWidth = kMinContentWidth;
    for (auto& entry : g_widgets) {
        if (entry.enabled && !entry.crashed) {
            contentWidth = std::max(contentWidth, entry.desiredWidth);
        }
    }
    contentWidth = std::min(contentWidth, (double)g_settings.layoutMaxWidth);
    ApplyStackWidth(contentWidth);
```
Replace with:
```cpp
    double contentWidth = 0.0;
    for (auto& entry : g_widgets) {
        if (entry.enabled && !entry.crashed) {
            contentWidth = std::max(contentWidth, entry.minWidth);
        }
    }
    // A misconfigured minWidth > maxWidth would otherwise invert the
    // clamp range below - widen the effective ceiling to match rather
    // than produce nonsense (spec's error-handling section).
    double effectiveMaxWidth =
        std::max((double)g_settings.layoutMinWidth, (double)g_settings.layoutMaxWidth);
    contentWidth = std::clamp(contentWidth, (double)g_settings.layoutMinWidth,
                               effectiveMaxWidth);
    ApplyStackWidth(contentWidth);
```
`kMinContentWidth` itself (the `constexpr double kMinContentWidth = 30.0;` near the top of the file) is NOT touched by this step - it's still used elsewhere (the root/clipHost's initial pre-content size at construction time) and stays as-is; only this one specific use as the width-accumulator's starting floor is removed, since `layout.minWidth` now owns that job.

- [ ] **Step 6: Update `IWidget::Create()`'s doc comment to state the new contract explicitly**

Find the comment (search `// Builds this widget's own root element, attaches it under`, around line 226):
```cpp
    // Builds this widget's own root element, attaches it under
    // host.parent, and returns the width (DIPs) it wants. The host
    // sizes the stack to the widest enabled widget's returned width
    // (capped at the user's layout.maxWidth setting) - a widget
    // narrower than that gets stretched to fill it via the default
    // HorizontalAlignment::Stretch, so it should not set its own fixed
    // Width().
    virtual double Create(const WidgetHost& host) = 0;
```
Replace with:
```cpp
    // Builds this widget's own root element, attaches it under
    // host.parent, and returns this widget's own MINIMUM readable width
    // (DIPs) - the point below which its content would be truncated or
    // illegible, not a "desired" or preferred size. The host derives
    // the stack's actual shared width from the widest of every enabled
    // widget's reported minimum, clamped to the user's
    // layout.minWidth/layout.maxWidth settings - every widget then
    // stretches to fill that shared width via the default
    // HorizontalAlignment::Stretch, so it must not set its own fixed
    // Width() or override HorizontalAlignment to Left/Center/Right on
    // its own top-level returned element.
    virtual double Create(const WidgetHost& host) = 0;
```
Also update `OnSettingsChanged()`'s doc comment two lines below it (search `// Re-reads this widget's own settings sub-namespace and rebuilds`):
```cpp
    // Re-reads this widget's own settings sub-namespace and rebuilds
    // internally, returning its (possibly new) desired width. The host
```
becomes
```cpp
    // Re-reads this widget's own settings sub-namespace and rebuilds
    // internally, returning its (possibly new) minimum readable width.
    // The host
```
(keep the rest of that comment's remaining lines unchanged).

- [ ] **Step 7: `PlaceholderWidget::Create()` - confirm and relabel, no behavior change**

`PlaceholderWidget` (search `class PlaceholderWidget : public IWidget`) already does the right thing today - no explicit `Width()`, default `HorizontalAlignment::Stretch` (it never overrides it), and its constructor parameter is already effectively a minimum (a placeholder's whole "content" is one line of label text, so its natural/minimum/desired sizes all coincide). Find its constructor parameter and member:
```cpp
    PlaceholderWidget(std::wstring id,
                       std::wstring displayName,
                       winrt::Windows::UI::Color color,
                       double desiredWidth)
        : id_(std::move(id)),
          displayName_(std::move(displayName)),
          color_(color),
          desiredWidth_(desiredWidth) {}
```
and
```cpp
    double desiredWidth_;
```
Rename `desiredWidth`/`desiredWidth_` to `minWidth`/`minWidth_` throughout this class (constructor parameter, member, and its two return sites in `Create()`/`OnSettingsChanged()`) purely for naming consistency with the rest of this task - no behavioral change. Also update the comment right above `host.parent.Children().Append(border);` (search `// Intentionally no border.Width(): it stretches to fill`) if it references "desired" - read it first; if it already says "whatever content width the host settles on" without the word "desired," leave it as-is.

- [ ] **Step 8: Commit**

```bash
git add taskbar-widget-stack/taskbar-widget-stack.wh.cpp
git commit -m "Add layout.minWidth; Create() now reports minimum, not desired, width"
```

---

### Task 2: media-player - stop overriding Stretch, gate playerMinWidth/MaxWidth to standalone

**Files:**
- Modify: `taskbar-widget-media-player/taskbar-widget-media-player.wh.cpp`

**Interfaces:**
- Consumes: Task 1's updated `IWidget::Create()` contract (doc-comment only, no code dependency - the ABI callback function signature is unchanged).
- Produces: nothing other tasks depend on - this mod's own `MediaPlayer_Create`/`BuildPlayerGrid` now report a true minimum and genuinely stretch when registered.

- [ ] **Step 1: Stop clamping to `playerMinWidth`/`playerMaxWidth` inside `BuildPlayerGrid` when stack-registered**

`BuildPlayerGrid()` is shared by both the standalone and registered paths - find this block inside it (search `if (hasTextOrButtons && g_settings.playerMinWidth > 0) {`, the wrapper-level one, not the later one inside the visibility-toggle function):
```cpp
        if (hasTextOrButtons && g_settings.playerMinWidth > 0) {
            wrapper.MinWidth((double)g_settings.playerMinWidth);
        }
        if (g_settings.playerMaxWidth > 0) {
            wrapper.MaxWidth((double)g_settings.playerMaxWidth);
        }
```
Replace with:
```cpp
        // playerMinWidth/playerMaxWidth only apply in standalone mode -
        // once registered into taskbar-widget-stack, the stack's own
        // layout.minWidth/layout.maxWidth own this decision instead
        // (see docs/superpowers/specs/2026-09-20-stack-width-abi-design.md),
        // and this wrapper's natural/unclamped size becomes the
        // "minimum readable width" MediaPlayer_Create reports back.
        if (!g_mpRemoteRegistered) {
            if (hasTextOrButtons && g_settings.playerMinWidth > 0) {
                wrapper.MinWidth((double)g_settings.playerMinWidth);
            }
            if (g_settings.playerMaxWidth > 0) {
                wrapper.MaxWidth((double)g_settings.playerMaxWidth);
            }
        }
```

- [ ] **Step 2: Let the registered-mode wrapper stretch instead of staying `Left`-aligned**

Find `MediaPlayer_Create` (search `double __cdecl MediaPlayer_Create(void*`). It builds a `Border container` and sets:
```cpp
        Border container;
        container.HorizontalAlignment(HorizontalAlignment::Left);
        container.Height(host->paneHeight);
```
Change the alignment line:
```cpp
        Border container;
        container.HorizontalAlignment(HorizontalAlignment::Stretch);
        container.Height(host->paneHeight);
```
Leave `BuildPlayerGrid()`'s own internal `wrapper.HorizontalAlignment(HorizontalAlignment::Left);` (used by both paths, near the top of `BuildPlayerGrid`) untouched - that one governs how the player's own content positions itself *within* whatever width `container` ends up stretched to (left-aligned content within a wider box, matching how the standalone path already expects it to look), not whether `container` itself stretches.

- [ ] **Step 3: Confirm `MediaPlayer_Create`'s returned width still makes sense as "minimum" after Steps 1-2**

Read `MediaPlayer_Create`'s tail (search `container.UpdateLayout();`, a few lines below where Step 2 edited):
```cpp
        container.UpdateLayout();
        double desiredWidth = container.ActualWidth();
        if (desiredWidth <= 0) {
            desiredWidth = 150.0;
        }
```
No change needed here - `container.ActualWidth()` after `UpdateLayout()` still measures the player's natural, un-clamped (per Step 1) content size, which is exactly the "minimum readable width" this task is establishing. The local variable name `desiredWidth` can stay as-is (a local variable, not part of the plan's renamed-field consistency requirement from Task 1) unless you want to rename it to `minWidth` for clarity - optional, not required.

- [ ] **Step 4: Manual verification (code-reading only, no compiler here)**

Confirm by re-reading: with `g_mpRemoteRegistered == true`, `BuildPlayerGrid`'s wrapper Grid gets no `MinWidth`/`MaxWidth` applied regardless of `playerMinWidth`/`playerMaxWidth`'s configured values, and `MediaPlayer_Create`'s `container` now has `HorizontalAlignment::Stretch`. Confirm the standalone path (`ApplySettings`/`InjectPlayerGrid`, which also calls `BuildPlayerGrid()`) is unaffected, since `g_mpRemoteRegistered` is false there.

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-media-player/taskbar-widget-media-player.wh.cpp
git commit -m "Stretch to fill the stack's shared width instead of self-clamping when registered"
```

---

### Task 3: system-usage - report the table's natural width, let its Star bar column fill the stack's actual width

**Files:**
- Modify: `taskbar-widget-system-usage/taskbar-widget-system-usage.wh.cpp`

**Interfaces:**
- Consumes: Task 1's updated contract (doc-comment only).
- Produces: nothing other tasks depend on.

This is the trickiest of the three widget mods: `FinalizeTableWidth` doesn't just clamp a size, it sets `table`'s explicit `Width()` to `[minWidth, maxWidth]`-clamped `natural` (label+percent column size), which is what currently gives the bar's `Star`-weighted column a determinate width to compute its own proportion against (`table`'s own comment: "a Star column needs SOME ancestor with a determinate width to compute a proportion against, and an Auto-sized Grid can't give it one"). An explicit `Width()` also means `table` does NOT respond to a later, larger width `taskbar-widget-stack` might arrange its container to (explicit `Width` overrides `HorizontalAlignment::Stretch`'s effect) - today, if another registered widget needs more room than system-usage's own `[minWidth, maxWidth]`-derived `total`, system-usage's own bars stay pinned at their smaller fixed size instead of genuinely filling the wider shared pane, which is a second flavor of the exact "each mod locks its own width" problem this plan is fixing everywhere else.

- [ ] **Step 1: Report `natural` (not the `[minWidth, maxWidth]`-clamped `total`) as the widget's minimum from `SystemUsage_Create`**

Find `SystemUsage_Create` (search `double __cdecl SystemUsage_Create(void*`). It currently does:
```cpp
        Grid table = BuildTable();
        container.Child(table);
        parent.Children().Append(container);
        FinalizeTableWidth(table);

        g_ui.hWnd = (HWND)host->taskbarHwnd;
        g_ui.remoteParentPanel = parent;
        g_ui.remoteContainer = container;
        g_ui.root = table;

        InitPdh();
        SampleCpu();  // primes the delta baseline, first return unused
        UpdateValues();
        StartTimer();

        // ActualWidth already reflects FinalizeTableWidth's clamp - the
        // host still needs this one concrete number back (its own layout
        // can't understand "auto, with these bounds"), matching
        // IWidget::Create's contract ("should not set its own fixed
        // Width()"): the width was derived from content + settings, not
        // hardcoded, even though a plain double is what crosses the ABI.
        double desiredWidth = table.ActualWidth();
        if (desiredWidth <= 0) {
            desiredWidth = g_settings.minWidth;
        }
```
`FinalizeTableWidth` itself is changed in Step 2 below to behave differently when stack-registered - after that change, `table.ActualWidth()` right after `FinalizeTableWidth(table)` returns will correctly reflect the table's *natural* label+percent width (Step 2 makes `FinalizeTableWidth` skip setting an explicit clamped `Width()` in this mode), so **no change is needed at this specific call site** - the existing `double desiredWidth = table.ActualWidth();` already becomes correct once Step 2 lands. Update only the trailing comment, which currently describes the old clamped-to-settings behavior:
```cpp
        // Natural label+percent width (FinalizeTableWidth skips its own
        // clamped explicit Width() when stack-registered, per
        // docs/superpowers/specs/2026-09-20-stack-width-abi-design.md) -
        // this is genuinely this widget's minimum readable size: below
        // it the label/percent text truncates. The bar's own Star
        // column fills whatever width the host later stretches
        // g_ui.remoteContainer to, via the ordinary Grid Star-sizing
        // this file already uses everywhere else.
        double desiredWidth = table.ActualWidth();
        if (desiredWidth <= 0) {
            desiredWidth = g_settings.minWidth;
        }
```
(the fallback to `g_settings.minWidth` on a zero/negative read stays as a defensive floor, unchanged - it's a fallback for a failed measurement, not part of the clamping behavior being removed).

- [ ] **Step 2: `FinalizeTableWidth` - skip the explicit clamped `Width()` and `Left` alignment when stack-registered, let `table` stretch instead**

Find `FinalizeTableWidth` (search `void FinalizeTableWidth(Grid& table) {`):
```cpp
void FinalizeTableWidth(Grid& table) {
    try {
        table.UpdateLayout();
        double labelWidth = table.ColumnDefinitions().GetAt(0).ActualWidth();
        double percentWidth = table.ColumnDefinitions().GetAt(2).ActualWidth();
        double natural = labelWidth + percentWidth;
        double total = std::clamp(natural, (double)g_settings.minWidth,
                                   (double)g_settings.maxWidth);
        table.Width(total);
        // Re-run so the bar's Star column picks up its real share before
        // anything reads ActualWidth() off `table` again (the registered
        // path does, right after this call, to report a desired width
        // back to the host).
        table.UpdateLayout();
    } catch (...) {
    }
```
Replace with:
```cpp
void FinalizeTableWidth(Grid& table) {
    try {
        table.UpdateLayout();
        double labelWidth = table.ColumnDefinitions().GetAt(0).ActualWidth();
        double percentWidth = table.ColumnDefinitions().GetAt(2).ActualWidth();
        double natural = labelWidth + percentWidth;
        if (g_remoteRegistered) {
            // Stack-registered: don't set an explicit clamped Width()
            // at all - that would override Stretch and pin `table` at
            // today's computed size forever, never responding to a
            // later, wider shared width taskbar-widget-stack decides
            // once every registered widget has reported in (see
            // docs/superpowers/specs/2026-09-20-stack-width-abi-design.md).
            // Left as Auto-sized/Stretch, the Star bar column instead
            // resolves its own share whenever a real layout pass runs
            // against g_ui.remoteContainer's actual arranged width -
            // including ones that happen after this function returns,
            // once the host widens the shared pane.
            table.HorizontalAlignment(HorizontalAlignment::Stretch);
            table.ClearValue(FrameworkElement::WidthProperty());
        } else {
            double total = std::clamp(natural, (double)g_settings.minWidth,
                                       (double)g_settings.maxWidth);
            table.Width(total);
        }
        // Re-run so the bar's Star column picks up its real share before
        // anything reads ActualWidth() off `table` again (the registered
        // path does, right after this call, to report a desired width
        // back to the host).
        table.UpdateLayout();
    } catch (...) {
    }
```
Note: `table.ActualWidth()` measured right after this in the stack-registered branch reflects `natural` (labelWidth + percentWidth) with the Star bar column collapsed to ~0 width at this exact moment (since `table` isn't yet arranged against its final, host-decided width) - this is exactly the "minimum readable" value Step 1 wants reported back. The bar visually filling in happens later, automatically, once the host's own subsequent layout pass (triggered by `ApplyStackWidth` widening `g_ui.clipHost`/`g_ui.remoteContainer`) arranges `table` (now `Stretch`-aligned, no explicit Width) to its real final width.

- [ ] **Step 3: Manual verification (code-reading only, no compiler here)**

Confirm `g_remoteRegistered` (search `bool g_remoteRegistered = false;`) is genuinely readable from `FinalizeTableWidth` (declared earlier in the file - confirm by checking the line number of the `bool g_remoteRegistered` declaration is smaller than `FinalizeTableWidth`'s own line number). Confirm the standalone branch (`else`) is byte-identical to today's existing behavior, so standalone mode is provably unaffected. Confirm `table.ClearValue(FrameworkElement::WidthProperty())` is a real WinRT/XAML API (used elsewhere in this repo already - `taskbar-widget-media-player.wh.cpp` calls `ClearValue(FrameworkElement::MaxWidthProperty())`/`ClearValue(FrameworkElement::WidthProperty())` in its own visibility-toggle code, confirming the pattern compiles in this codebase's actual SDK).

- [ ] **Step 4: Commit**

```bash
git add taskbar-widget-system-usage/taskbar-widget-system-usage.wh.cpp
git commit -m "Report natural table width as the minimum; let the Star bar column stretch-fill when registered"
```

---

### Task 4: weather - stop overriding Stretch on the registered-mode wrapper

**Files:**
- Modify: `taskbar-widget-weather/taskbar-widget-weather.wh.cpp`

**Interfaces:**
- Consumes: Task 1's updated contract (doc-comment only).
- Produces: nothing other tasks depend on.

The simplest of the three - weather has no per-widget width settings to gate (confirmed during this mod's own build earlier this session: no `panelMinWidth`-equivalent exists), and `WeatherWidget_Create`'s returned `wrapper.ActualWidth()` is already a reasonable natural/minimum size (its compact views are short - icon plus 1-2 lines of text, or an N-cell forecast strip - with no optional/collapsible sub-elements to strip for a "tighter" minimum). Only the `HorizontalAlignment::Left` override needs removing.

- [ ] **Step 1: Let the registered-mode wrapper stretch**

Find `WeatherWidget_Create` (search `extern "C" double __cdecl WeatherWidget_Create(void*`):
```cpp
        Button wrapper;
        wrapper.HorizontalAlignment(HorizontalAlignment::Left);
        wrapper.Height(host->paneHeight);
```
Change the alignment line:
```cpp
        Button wrapper;
        wrapper.HorizontalAlignment(HorizontalAlignment::Stretch);
        wrapper.Height(host->paneHeight);
```
Leave `InjectWeatherStandalone` (the standalone-mode equivalent, a separate function) untouched - it builds its own separate `wrapper`/`Button` with its own `HorizontalAlignment::Left`, which is correct for standalone's own edge/tracked-anchor positioning system and outside this plan's scope (Global Constraints: only the stack-registered path changes).

- [ ] **Step 2: Confirm the compact view's own content still looks right when stretched wider than its natural size**

Read `BuildNowView()` and `BuildForecastView()` (both called by `BuildCompactView()`, which `WeatherWidget_Create` uses to build `wrapper`'s content). Confirm neither sets its own `HorizontalAlignment::Left`/`Center` that would fight the new outer `Stretch` (i.e., confirm the icon+text/forecast-strip content will simply have extra empty space to its right when the stack is wider than weather's own minimum, rather than stretching its own text/icons to fill - this is expected and matches how `PlaceholderWidget`'s single-line label already behaves in the same situation, not a defect to fix in this task).

- [ ] **Step 3: Commit**

```bash
git add taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Stretch to fill the stack's shared width instead of staying Left-aligned when registered"
```

---

### Task 5: Documentation + live-test checklist

**Files:**
- Modify: `taskbar-widget-stack/PLAN.md`
- Modify: `taskbar-widget-media-player/PLAN.md`
- Modify: `taskbar-widget-system-usage/PLAN.md`
- Modify: `taskbar-widget-weather/PLAN.md`
- Modify: all four mods' `.wh.cpp` `@version` metadata (per this repo's own standing convention: every commit touching a mod's `.wh.cpp` bumps its version)

**Interfaces:**
- Consumes: the completed changes from Tasks 1-4.
- Produces: nothing - documentation only.

- [ ] **Step 1: Replace `taskbar-widget-stack/PLAN.md`'s "Known follow-up: no shared min/max width negotiation" section with a real Incident entry**

That section (added earlier this session, search `## Known follow-up: no shared min/max width negotiation in the ABI`) described this exact problem as a future idea - now that it's implemented, replace that whole section with a proper dated `## Incident N: shared min/max width negotiation` entry (use the next sequential incident number - check the file's current highest `## Incident` number first) following this file's own established format: **Symptom** (the width-juggling problem as originally reported), **Root cause** (each widget reporting a "desired" width with the widest one winning, AND all three widget mods overriding `HorizontalAlignment::Stretch` to `Left` - both pieces, since fixing only one wouldn't have been enough), **Fix** (summarize Tasks 1-4), **Next retest** (see Step 2 below - copy it in).

- [ ] **Step 2: Add the live-test checklist**

Add this as the "Next retest" content for the Incident entry from Step 1:

> With `taskbar-widget-stack`, `taskbar-widget-media-player`, `taskbar-widget-system-usage`, and `taskbar-widget-weather` all installed and enabled together: confirm the shared pane width lands at a sane value with no per-mod width tuning needed. Open the stack's settings window and drag "Minimum stack width" - confirm every registered widget's pane visibly widens together, including system-usage's bar graphics actually growing (not just empty space appearing next to a fixed-size bar). Drag "Maximum stack width" down below the current natural content width of the widest widget - confirm that widget's content clips/truncates gracefully rather than overflowing. Disable all but one widget - confirm the shared width shrinks to roughly that one widget's own minimum (clamped to `layout.minWidth`), not stuck at some stale multi-widget-derived value. Re-enable `taskbar-widget-media-player` alone in standalone mode (stack disabled) and confirm its own `playerMinWidth`/`playerMaxWidth` settings still work exactly as before this change.

- [ ] **Step 3: Add matching one-paragraph notes to the other three mods' `PLAN.md` files**

For `taskbar-widget-media-player/PLAN.md`, `taskbar-widget-system-usage/PLAN.md`, and `taskbar-widget-weather/PLAN.md`, add a short dated entry (matching each file's own existing Incident-log format) summarizing: "Registered-mode width now comes from taskbar-widget-stack's shared layout.minWidth/maxWidth instead of this mod's own reported size - see docs/superpowers/specs/2026-09-20-stack-width-abi-design.md and taskbar-widget-stack/PLAN.md's own Incident entry for the full design/root cause. [mod-specific one-line summary of what changed in this file - e.g. weather's: 'WeatherWidget_Create's wrapper now stretches instead of staying Left-aligned.']"

- [ ] **Step 4: Bump `@version` in all four mods**

Per this repo's own standing convention (every commit touching a mod's `.wh.cpp` bumps its version - confirmed multiple times already this session, e.g. `taskbar-widget-weather` going `1.6 -> 1.7` for a comparably-scoped fix), bump each of the four mods' `// @version` line by one patch-level increment from its current value. Read each file's current `@version` line first (values will have moved since this plan was written) rather than assuming a specific number.

- [ ] **Step 5: Commit**

```bash
git add taskbar-widget-stack/PLAN.md taskbar-widget-media-player/PLAN.md taskbar-widget-system-usage/PLAN.md taskbar-widget-weather/PLAN.md taskbar-widget-stack/taskbar-widget-stack.wh.cpp taskbar-widget-media-player/taskbar-widget-media-player.wh.cpp taskbar-widget-system-usage/taskbar-widget-system-usage.wh.cpp taskbar-widget-weather/taskbar-widget-weather.wh.cpp
git commit -m "Document the shared width negotiation change; bump versions"
```
