# Settings shell, spec

Drawn up with Ari on 2026-11-14 in a question session. Status: draft for approval.
Nothing is built. References: `.claude/skills/windhawk-ui-kit/SKILL.md` (a mod's own
settings window, where it saves), WinSnip's schema driven settings shell
(`C:\\Users\\arist\\Code\\winsnip`: `SettingsSchema.cs`, `SettingsPageRegistry.cs`,
`docs/features/settings.md`), the roadmap's old notes ("Settings window laid out like
Windows Settings", "Extras settings entry in the ... menu", "one-time tip").

## Why

Explorer Extras is becoming several features (Columns now; labels, a sidebar overhaul,
a preview pane, status bar options later). Windhawk's settings page is one long list and
cannot grow into that. A settings window gives each feature a place, a switch and a
page, and gives the mod a home for status, quick actions and tips. It also makes the
settings a table, so strings and defaults are declared once.

## Goals

- A native looking settings window, opened from Explorer, with a sidebar of Home and
  one page per feature. Each page: a header, a description, then cards of options.
- Every setting declared once in a schema. Windhawk's settings block is generated from
  it, and the window is built from it. Strings sit in the same table, ready for
  translation (the 1.0 strings task becomes part of this).
- Live apply: a control changes the real UI at once and is stored at once.
- The window overrides Windhawk's page (the Taskbar Fluent Media Player model); the
  Windhawk page stays as the default layer and a "window" view hides most of it.
- Room for the next features with no rework: a page, a switch and a schema section.

## Non goals

- Writing to Windhawk's settings page (a mod cannot).
- Search across pages (not chosen; can be added once there are 40 or more settings).
- Translating the strings now. The table makes it possible, the translations are later.
- Building the upcoming features. Their pages are staged placeholders only.

## Decisions (Ari)

| Question | Decision |
| --- | --- |
| Where settings live | The window overrides Windhawk's page: it writes the mod's own storage, read on top of Windhawk's values. A "Reset to Windhawk's settings" action deletes the overrides. |
| Entry point | Entry in the command bar's "..." menu under Options. Untested, needs recon. Fallback if it cannot be done: a Settings item in the View dropdown, next to Columns. |
| Layout | A sidebar for Home and the features (Columns, Labels, Sidebar, Preview panel, ...), a scrolling main area with header, description and options. |
| Apply | Live, with Reset. No Save button. |
| Home page | A card per feature with its on/off switch, status and version, quick actions, what's new and tips. |
| Features not built | Shown as staged pages only behind a flag (a hidden setting, off by default). |
| Schema | One schema; the Windhawk YAML is generated from it. |
| Window | Mica backdrop and a custom title bar. |
| Columns page | Four cards: Layout and look, Behaviour, Drag and drop and memory, Content and advanced. |
| Order | Settings shell first, in five slices, then touch and release prep. |
| Debug switch | Stays on Windhawk's page only, out of the window. |

Calls I made, to confirm: one window only (a second request brings it forward); it
remembers its size, position and last page; no search box.

## The schema

`explorer-extras/dev/settings.schema.json`, one entry per setting, grouped by feature:

```
feature   columns | labels | sidebar | preview | statusbar
id        dragOut           (the stored key stays columns.dragOut, no migration)
page, card
type      bool | int | enum | string | colour
default
min, max, step            (int)
choices                   (enum)
name, description         (English; keys for the strings table)
advanced                  (shown in the Windhawk page only when its view is Full)
windowOnly / windhawkOnly (debug log is Windhawk only)
needs                     (applies now | rebuilds the panel | next window)
```

`build.py` reads it and writes two generated parts that the join already knows how to
carry: the Windhawk settings block (replacing the hand-written one in `00-header.cpp`,
with `settingsView` and `$showIf` as the UI kit asks) and a C++ table of the settings
(`src/…generated.cpp`) that the window and the settings reader use. A check mode fails
the build when the generated parts are out of date, as `--check` does today.

## Where values live

- Windhawk page value: read as today through `Wh_GetIntSetting` or string.
- Override: `Wh_SetIntValue` and `Wh_SetStringValue` under a stable prefix (`ui.<id>`),
  written by the window.
- Effective value: the override if it exists, else Windhawk's. On first run nothing is
  copied; the first change creates the override. Later edits on Windhawk's page still
  apply: the last seen page values are kept, and a difference counts as a new edit that
  clears that setting's override. Reset deletes all overrides.
- A single reader `Setting<T>(id)` replaces the direct `Wh_GetIntSetting` calls; the
  existing atomics (`g_dragOut`, ...) are filled from it.

## The window

Per the UI kit's technique: from a click handler, defer through the dispatcher; on that
Explorer window's UI thread register a window class and create a top level window; a
`DesktopWindowXamlSource` attached to it; size the island on `WM_SIZE`;
`WM_GETMINMAXINFO` for a minimum size. Mica (DWM backdrop type) and a custom title bar
that follows the theme and accent. Manager and source in `[[clang::no_destroy]]` globals,
released by hand on that thread; class unregistered and window destroyed in
`Wh_ModUninit`. One window; size, position and last page saved in the mod's storage.

Changes apply to every Explorer window: the schema's `needs` says whether a setting is
an atomic read on the next use, or needs the panel rebuilt. The window asks each window's
state, through its dispatcher queue, to reread settings and rebuild what is flagged.

Controls are system XAML (`ToggleSwitch`, `NumberBox`, `ComboBox`, `Slider`, a
`Button` row for actions) in Windows Settings style cards. The sidebar is built from
simple controls, since `NavigationView` cannot be made to match Windows 11 (UI kit).

## Pages

- Home: a card per feature (name, one line, switch, opens its page); status and version
  (mod version, Windows build, whether the hooks found their symbols, a pointer to the log);
  quick actions (open the log, copy diagnostics, reset to Windhawk's settings); what's
  new and a dismissible tip.
- Columns: four cards, from the existing settings:
  - Layout and look: min and max width, harmonize widths, focus rectangle, compact
    scrollbars, panel background.
  - Behaviour: follow the active column, double-click up, keep stale columns, header
    keyboard, sidebar root, restore on open.
  - Drag and drop and memory: drag out, spring load, remember sort, width and layout.
  - Content and advanced: archives, real Desktop items only, remote marks, max items.
- Upcoming (flag `showUpcoming`, hidden, off): Labels, Sidebar, Preview panel and
  Status bar, each a "Coming soon" page with its planned scope.

"Off means gone" stays the rule: a feature whose switch is off has no menu entry and no
hook work; its page still shows its controls greyed.

## Slices

| Version | Content |
| --- | --- |
| 0.24 | Schema and generator. The Windhawk block is generated; the settings reader `Setting<T>`; the strings table for settings names and descriptions. No visible change. |
| 0.25 | The window shell: class, island, Mica, title bar, sidebar, Home (static) and the Columns page (read only), one window, size and page remembered. Opened from the View dropdown first, to have a way in while the "..." recon is done. |
| 0.26 | Live apply and Reset: controls write the override, every window rereads, rebuild where flagged, the Windhawk page last-seen logic, the Reset action. |
| 0.27 | The "..." menu entry (recon), the status card, quick actions, what's new, the one-time tip on the Columns entry. |
| 0.28 | Upcoming pages behind the flag, and the strings of the window and menus moved into the table. |

Touch (0.29) and the release candidates follow and use the schema.

## Risks and things to check

- The "..." menu entry: the flyout there is a different `MenuFlyout`; recon first, as for
  the View menu. Fallback decided.
- Windhawk compile of a second top level window plus XAML island in the same mod: the
  technique is proven in Ari's taskbar mods, not yet in Explorer. Test with a stub
  window in 0.25 before the pages.
- Storage size limits for strings (UI kit: split into chunks). Settings are small ints
  and short strings, so likely fine; check `windhawk_api.h`.
- A setting changed while a panel rebuild is under way: apply through the window's
  dispatcher queue only, never from the settings window's thread directly.
- The generated YAML must keep the same ids and defaults as today, so users' stored
  values carry over.
- `build.py` grows; keep it one file and readable.

## To decide later

- Exact wording and order of the Home cards, the "what's new" source (a short text in the
  schema file or in `CHANGELOG`).
- Whether the window gets an About page (licence, links, version), as WinSnip's.
- The name of the flag (`showUpcoming` is a placeholder).
