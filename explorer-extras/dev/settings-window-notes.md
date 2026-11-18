# The settings window of Explorer Extras (removed in 0.66)

Explorer Extras 0.55 to 0.65 had its own settings window, in addition to Windhawk's settings page. It was removed
in 0.66 for the catalog review: two places to change the same settings (the window wrote overrides, so Windhawk's
page could show values that were not in effect), about 1,350 lines, and a second XAML island with its own unload
risks. This note keeps what was learned, so it can be rebuilt if it is wanted again.

**The code:** commit `ab3852a` (0.65.0). `git show ab3852a:explorer-extras/dev/src/10b-settings-window.cpp`, and the
override layer in `src/02a-settings.cpp`. The page, card and French text tables came from `settings.schema.json`
(`cards`, `whatsNew`, `fr.cards`, `fr.settings`, and the `page`, `card`, `needs` fields of each setting).

## How it was built

- **One top-level window on the UI thread of the Explorer window that opened it**, with a `DesktopWindowXamlSource`
  island over it. A XAML island needs a thread with a dispatcher queue, and an Explorer window's thread has one. A
  thread of the mod's own would need its own queue and would add a thread to join at unload.
- **Mica** behind the island, as in Windows Settings: `DwmSetWindowAttribute` with 38 (`DWMWA_SYSTEMBACKDROP_TYPE`)
  set to 2 (Mica), and 20 (`DWMWA_USE_IMMERSIVE_DARK_MODE`) from the content's theme. The island's backgrounds are
  transparent, `WM_ERASEBKGND` returns 1, and the island is moved with `MoveAndResize` on `WM_SIZE`. A rectangle of
  the island's first size showed through the Mica until the first layout had run.
- **Layout:** a sidebar of pages (Overview, Columns, Details pane) and a scrolling page with a header, a description
  and cards. Each card is a rounded border with rows: a name and a description on the left, a control on the right.
  The sidebar had two modes by the window's width: full names, or a rail of icons with tooltips and a button that
  opens the names over the page (a press on the page closes it again).
- **Window details:** minimum size scaled by DPI (`WM_GETMINMAXINFO`), `WM_DPICHANGED` moves the window to the
  suggested rectangle, the position, size and page were kept in `ui.window` as `x,y,w,h,page`. The icon is the
  ExploitProtection glyph of Segoe Fluent Icons drawn into a bitmap in the colour of the caption, again when the
  theme changes. Closing the window only hid it: the island stayed, so reopening was instant.
- **The table:** `build.py` wrote `kSettings` from the schema (id, Windhawk key, kind, default, limits, name,
  description, page, card, options, and a `rebuild` flag). The rest of the mod read a setting by id, never by
  Windhawk's key.
- **Overrides:** a change in the window was stored as `ui.<id>` in the mod's storage, plus `ui.seen.<id>`, the
  Windhawk value seen when the override was made. If Windhawk's value later differed from `seen`, Windhawk's page
  was taken to have been edited and won, and the override was cleared. A button cleared every override ("Reset to
  Windhawk's values"). Windhawk has no call to write a setting, which is why this layer existed.
- **Applying a change:** the `needs` field of a setting said "now" (read on the next use) or "rebuild" (the widths
  that the user had not set were fitted again, and the folders were read again, in every window, on each window's
  thread).
- **Other things on the pages:** Copy diagnostics (version, Windows build, the settings with a `*` on the
  overridden ones), a status block (hooks found or not), a one-time tip, an About block with the repository link.

## What went wrong with it

- A window on an Explorer window's thread must go when that window goes. Releasing its XAML objects after the
  Explorer window was destroyed crashed Explorer, so they were let go and leaked; at unload they were released
  properly, on the window's own thread, with `SendMessage(WM_CLOSE)`.
- The XAML handlers of its controls were released late by XAML after the mod was unmapped (0.57 to 0.58): every
  handler is now a revoker, and the first crash after a disable was a `Toggled` handler of this window.
- The window class was registered with `GetModuleHandleW(nullptr)` (explorer.exe's instance) until 0.56; use the
  mod's own module.
- The reviewer's objection: "two places to edit the same settings". A window that only showed the values (read
  only) would not have had the override layer.

## If it comes back

Smaller is better: a read-only or optional window, no override layer, and settings written to Windhawk's page only.
The pages and cards above, the Mica setup and the nav modes can be taken from `ab3852a` as they were.
