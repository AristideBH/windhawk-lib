# Spec: the details pane

Status: built in 0.37 to 0.52 (2026-11-15 and 16) and tested by Ari. What was built differs from the first plan in three ways: the native Details pane window is taken over (see Decisions after the probe); shell preview handlers are only a fallback for Office types, since a native window cannot scroll or be overlaid, so PDF, text, Markdown, images, video and sound are drawn in XAML; and Alt+P is caught by a keyboard hook. See ROADMAP.md (0.37 to 0.52) and HANDOFF.md for what it does and what it taught.

## What it is

One pane on the right of a File Explorer window that replaces Explorer's own Details pane and Preview pane. It shows a preview on top and the details below, in WinUI 3 with Explorer's own look, and it slides and fades in and out. It works in Columns and in Explorer's default views.

## Goals

- One pane instead of two, with the preview on top and the details below, one scroll.
- Native feel: Explorer's fonts, colours, spacing and icons. An entrance and exit animation of about 200 ms, slide and fade from the right, that actually moves (the slide of the column rows failed, so the animation is proved first).
- No lag while arrowing through files. Cached results for going back.
- Works in every view, not only in Columns.

## Non-goals

- Editing properties inline, rename from the pane, tags and labels (a later feature).
- Rewriting preview handlers. The shell's own handlers produce the previews.
- Details columns auto-fit, Home page changes.

## What it shows

| Selection | Top | Below |
| --- | --- | --- |
| One file | Big shell thumbnail at once, then the real preview handler for the types allowed | Name, type, size, dates, location, plus a few properties by type: dimensions for images, duration and codec for media, author and pages for documents, from the shell's property system |
| One folder | Folder icon | Item count, size when cheap to get, dates, location |
| Several items | A stack of icons | Count, total size, types |
| Nothing | The current folder's icon | The current folder's summary |

Quick actions in a row at the top, matching the context menu: Open, Share, Copy path, Properties. Clicking a property value copies it.

## Behaviour

- **Toggle.** A custom button replaces Explorer's native details-pane button at the right of the command bar, and Alt+P and Alt+Shift+P toggle the pane. While probing, a View menu entry next to Columns is enough. The View menu's Show > Details pane and Preview pane entries have to be handled so they do not fight the custom button.
- **Native panes.** Explorer's own Details and Preview panes are kept hidden in every view, and ours stands in for both.
- **Remembered.** On or off and the width, kept across windows and restarts. Not per folder.
- **Settings.** Its own page and switch in the settings window: enable, animation on or off, a size limit for previews, and which preview types run.
- **Resizing.** A drag handle on the pane's left edge. In Columns the columns give up the width and the strip scrolls as before.

## Performance

- Selection changes wait a short pause, then load on a background thread. A load that is no longer wanted is cancelled. The old pane stays until the new one is ready.
- The thumbnail comes first from the shell's cache. The preview handler starts only after it, for the allowed types and under the size limit, and it never runs for a file larger than the limit.
- The last 20 or so results (thumbnail, properties) are kept in memory.
- The slide and the fade run on the compositor, not on the UI thread.
- A preview handler that hangs must not hang the window: it runs out of line and is abandoned after a time.

## Approaches considered

1. **Own pane replacing both native panes, in every view (chosen).** One consistent pane, one codepath. Costs a probe of how the native panes are hosted and hidden.
2. Own pane only in Columns, native panes elsewhere. Less risk, but half the feature.
3. A third pane with its own toggle, native panes untouched. Safest, but not the merge.

## Steps

Each step is a version Ari tests.

1. **Probe.** How the native details and preview panes are hosted in 24H2 (DirectUI or XAML, which window), how to hide them without breaking Explorer, which command bar button to replace and its element name, and whether a slide animation on our pane works. A View menu entry stands in for the button. Result written in this file.
2. **The pane in Columns.** The pane, the animation, summaries for the four cases, the thumbnail then preview, the cache, the quick actions, the setting and the page.
3. **Default views.** The same pane beside Explorer's file list, with the width given up by the list, and the toggle in every view.
4. **Polish.** The custom command bar button with the right state, the View menu fix-up, remembered width, preview limits, and the cost of a big folder.

## To find in the probe (open questions)

- Is the native details pane a XAML or a DirectUI element, and can it be collapsed without Explorer relaying out wrongly?
- The element names of the command bar's details-pane button and of the View > Show entries.
- Whether the shell's preview handler can be hosted in our pane (IPreviewHandler needs a window handle: a child window over the XAML island, with its airspace problem for the slide and fade).
- Whether the compositor animation (a visual offset animation, not the layout translation that failed for rows) moves.
- Where the pane lives in default views: inside the same island as Columns' panel, or in a new one.

## Risks

- The preview handler is a native window, so it cannot fade with XAML. Likely answer: the thumbnail fades and slides, and the handler window shows once the slide has ended.
- Hiding the native panes may show in Explorer's own state (the button looks off, Alt+P still toggles the native one).
- Preview handlers run third-party code. They run out of line, under the size limit, and for allowed types only.

## Probe results, 2026-11-15 (0.36.8 and 0.36.9, Windows 11 24H2, 175 % scale)

How the native panes are built:

- The Details pane is a Win32 window of class `DetailsPaneHwndHostClass` ("Volet des détails"), a child of a `CtrlNotifySink` inside the view's `DirectUIHWND`, next to the navigation tree and the file view (`SHELLDLL_DefView`). It was 524 px wide (300 DIP) and hosts its own WinUI island (a `DesktopChildSiteBridge` child). Explorer's DirectUI layout gives it its space and its splitter.
- Switching it off hides that `CtrlNotifySink` (vis=0) and the file view grows by the pane's width (1151 to 1680 px). Switching it on reverses both. Nothing is destroyed.
- The Preview pane is another window, "Shell Preview Extension Host", in the same slot. It is 0 x 0 and hidden while off. The two panes are exclusive: Alt+P hid the Details pane.
- Toggling: Alt+Shift+P (Details) and Alt+P (Preview) work when sent as keystrokes to the window. In the View menu both are `RadioMenuFlyoutItem`s with no command ("Volet des détails", "Volet de visualisation"), and a UI Automation toggle on them did nothing. Item names must be matched by position or by id, not by English words.
- The toolbar has a Details button: `AppBarButton` named `PaneToggle`, id `DetailsPaneToggleButton`, label "Détails", in the `CommandBar` named `FileExplorerSecondaryCommandBar`, at the right of the toolbar next to the account button. The Preview pane has no toolbar button.
- The XAML root of the toolbar is reachable from the View button or the toolbar after the entry timer has let go of it.

Animation, tested in a test island on the window:

- `UIElement.TranslationTransition` (a `Vector3Transition` of 200 ms) plus setting `UIElement.Translation` slides the element, and it was the one that visibly slid from the right. `OpacityTransition` is known to fade.
- A `Storyboard` on a `TranslateTransform` also changes its values over time (276 to 0 in about 250 ms).
- A visual Offset animation, a visual Translation animation, a compositor Opacity animation and an implicit Offset animation started without error but showed no motion.

Preview handlers and costs:

- A native child window placed above the island shows a preview handler's content over the XAML. The text preview was created in 1 to 20 ms and shown in 40 to 50 ms. So the airspace problem is solved by putting the handler's window above the island.
- Handlers seen: PowerToys Monaco for `.txt`, `.json`, `.cpp` and `.html`; PowerToys Markdown and SVG; Windows' own for `.csv`; Edge's for `.pdf`; Office's own, out of process, for `.docx`, `.xlsx` and `.pptx`. Images, `.mp4`, `.mp3`, `.zip` and `.exe` have none, so they need a thumbnail.
- A cached 512 px thumbnail takes about 2 ms, the property store of a file about 2.4 ms with 68 properties. Both are cheap enough to load on selection.

What this changes in the design:

- Instead of laying out a pane of our own next to the file list, which Explorer's DirectUI layout does not know about, **take over the native Details pane window**: leave Explorer's pane on, so it keeps reserving the space, the splitter and the toggles (the toolbar button, Alt+Shift+P and the View menu), and place our island over its content, as a child of `DetailsPaneHwndHostClass` so it follows its size and visibility. This works in Columns and in every default view at once, so the "Columns first, default views later" split is not needed.
- The width change when the pane toggles is Explorer's own and instant. The entrance and exit are animated inside the pane: our content fades and slides in with `TranslationTransition` and `OpacityTransition` when the host becomes visible, and out before it hides, where Explorer allows it.
- The custom toolbar button becomes optional: the native Details button works as the toggle.
- Open: what the Preview pane (Alt+P, View > Volet de visualisation) should do, since it takes the same slot. Options: send it to our pane too, or leave Explorer's own preview pane alone.

## Decisions after the probe (Ari, 2026-11-15)

- **The native Details pane is taken over** (approach 1 below, in its new form): Explorer's pane stays on and keeps its space, splitter and toggles, and our island is a child of `DetailsPaneHwndHostClass` placed over the native content. Works in Columns and in every default view from the first version.
- **The Preview pane** (Alt+P, View > Volet de visualisation) is a setting on the pane's settings page: "Open the merged pane for the Preview pane too", on by default. On: choosing the Preview pane shows our pane (the shell's own preview pane window stays hidden). Off: Explorer's own preview pane is left alone and replaces ours while it is on.
- The custom toolbar button is dropped for now: the native Details button is the toggle.

## Revised steps

1. **The pane in the native host (0.37):** our island in `DetailsPaneHwndHostClass`, hidden native island, the four summaries (file, folder, several, nothing), thumbnail first, properties by type, quick actions, click to copy, the small cache, and the fade and slide of the content with `TranslationTransition` and `OpacityTransition`. Own page and switch in the settings window, with the Preview pane setting. Logged heavily, since this is the first run on the device.
2. **The shell preview handler** hosted in a native child window above the island, for the allowed types, with the size limit and a timeout.
3. **Polish:** remembered width is Explorer's own, so the work is the preview pane redirect, the cost of big folders, translations of the strings and the toolbar button's look if wanted.
