# Labels for Explorer Extras, design

Status: draft for approval, 2026-11-17. Nothing is built. Probes that back it: `probes/probe-label-paint.wh.cpp` (0.1 to 0.11) and the findings in `.claude/skills/windhawk-explorer-surfaces/SKILL.md`, section "Drawing on file list rows".

## Intent

Ari wants macOS-style tags in Windows File Explorer, as part of the Explorer Extras mod (one mod, by his choice: shared UX and UI, a custom flyout, and coupling with the Columns view and later the sidebar). A "label" is a coloured dot on a file or folder, optionally with a name. The visible dot, in Explorer's own list and in Columns, is the selling point.

## Goals

- Tag any file or folder with one or more tags. A tag is a name (optional) plus a colour from a fixed palette. Colour-only tags are the quick path.
- Show the dots on Explorer's own list in every view mode (Details, List, Tiles, Content, Icons) and in the Columns view, with the per-view placement below.
- Assign from the details pane and from the right-click menu (our own flyout in Columns; Explorer's own menu in the native list, see Recon).
- Filter the current folder by a tag: hide the rest in Columns, dim the rest in the native list.
- Tags travel with the file on NTFS (copy, move, rename keep them).
- Never leave the user guessing: unsupported locations are greyed out in advance, with a reason.

## Non-goals (this version)

- No drag onto a sidebar tag, no sidebar tag list, no cross-folder "Tagged" view or index. They come later, together, once the sidebar surface is understood.
- No keyboard shortcuts.
- No renaming of a named tag (the name lives in each file).
- No tags on FAT, exFAT or other volumes without named streams, and no fallback store for them.
- No custom colours, no colour picker, no tag management window.

## Anti-goals

- A second source of truth. Tags live in the file only.
- Modal dialogs for notices (they add a modal loop, the cause of earlier unload problems).
- Anything that makes Explorer feel slower: painting must never touch the disk.
- A hard failure when a Windows update changes `dui70.dll`: labels turn off quietly instead.

## Constraints

- Windhawk catalog rules: `@version` bumps (minor), no `Wh_`-named macros, `[[clang::no_destroy]]` on globals that hold WinRT or threads plus explicit release in `Wh_ModUninit`, threads joined before unload, no SEH, `HookSymbols` for symbol hooks (we use exports only for painting). See the research notes of 2026-11-17.
- Our own unload machinery (`StartModThread`, `JoinModThreads`, revokers, `ModalScope`) applies to everything new.
- Settings only on Windhawk's page, in groups; no setting text may contain a colon followed by a space.
- The painting hook runs on every window's UI thread. It must be fast (measured about 10 to 30 microseconds per row) and never block.

## Approaches considered

1. **Native list by hooking `Element::PaintContent` in `dui70.dll` (chosen).** Exports need no symbol file, so it survives the symbol outages after Windows updates. Proven by the probe.
2. **An overlay window over Explorer's list.** Works, but fights Explorer for the z-order and cannot dim or align per row. Rejected.
3. **Columns only.** Cheap, but leaves the native list without labels, which is the selling point. Rejected.

Storage: a named NTFS stream on the file (chosen), versus a sidecar database keyed by file ID, versus both. The stream keeps one source of truth and travels with the file. The database brings an extra store and shutdown work. Both is the most robust and the most code. Row to file mapping: by displayed name (chosen), accepted trade-off, versus exact position matching.

## Decisions (as Ari decided them)

**Model.** A tag is a name (optional) plus a palette colour, many per item. The palette is a fixed set of colours following Windows' colour guidelines, with light and dark variants picked from the current theme. The dot has no hard outline: its edge is the fill slightly lightened (dark theme) or darkened (light theme), and it is anti-aliased. A named tag without a colour uses the neutral gray (so every tag has a dot).

**Names.** A named tag keeps its name in the file and cannot be renamed. A colour-only tag takes its meaning from 7 editable colour names in the mod's settings ("Red = Urgent"). The names stay on this PC and are not stored in files.

**Storage.** One named NTFS stream per file or folder, holding the tags as name plus palette colour index. If a location cannot carry the stream (non-NTFS volumes, some network shares, cloud-only placeholders, read-only or locked files), say so and do nothing.

**Notice.** Prevent first: the volume reports whether it supports named streams, and the tag entries in the menu and in the pane are greyed out with the reason ("Tags need an NTFS drive"; in the menu the reason is part of the label, since a disabled menu item has no tooltip). For a failure at the moment of the action (locked, read-only, cloud-only file), show one inline line under the tag row in the details pane, fading after a few seconds. In Columns without the pane open, the same line goes in our panel's bottom bar. No dialogs, no use of Explorer's status bar.

**Details pane.** Tags section, nothing shown beyond what exists:

```
Tags                          [ + ]
● Urgent   ● Invoice   ● Draft
```

Each chip is a dot plus a name. Clicking a chip filters the folder by that tag (the active chip is outlined; click again to clear). Hovering shows an x to remove it. The [ + ] button opens one flyout: 7 colours plus neutral gray in a row, and a name box (optional) with Enter to add. A colour click with no name adds a colour-only tag and closes the flyout; a name plus Enter adds a named tag in the picked colour, or neutral gray if none. With several items selected a chip is solid when every item has it and dimmed when only some do; clicking a dimmed chip applies it to all.

**Right-click menu.** A "Tags" submenu: the 8 colours as checkable items, "Add tag..." (opens the same flyout), "Clear tags". Columns hosts the shell menu, so the entry is ours. For Explorer's own list see Recon.

**Filter.** Columns hides non-matching rows. The native list dims them: the child elements of a non-matching row are faded toward the real colour behind them (sampled with `GetPixel` before the child paints, then `AlphaBlend`, about 60 percent), which is clean on hover and selection. The filter chip is the active chip in the pane. With the pane closed, Columns shows a one-line strip above its first column, "Showing: Urgent x", only while a filter is on. The native list needs no extra control.

**Dot placement per view**, all anti-aliased, 8 px at 100 percent, scaled by DPI, vertically centred on the line they sit on:

| View | Placement |
|---|---|
| List | In the 28 px gap before the icon, centred on the row, left margin equal to the margin above and below. Hidden while the item check box shows there (hover or select with check boxes on). |
| Details | Right-aligned at the end of the Name column, at the same x on every row. If the name nearly fills the column (estimated), draw on the icon's bottom-left corner instead. |
| Tiles | On the second line, right after the detail text; for items with an empty second line, at the start of that line. |
| Content | On the second line of the text block, left-aligned under the first detail line. |
| Icons (all sizes) | Left of the first text line, so the centred label stays centred. When a name wraps and fills the width, use the icon's bottom-left corner. |
| Columns view | Our own rows: same relative rule as the closest native view (Details style, right of the name). |

Several tags on one item: one dot per colour, up to 3 or 4, then a small "+". Neutral gray counts as a colour.

**Row to file.** By displayed name: `Element::GetAccName(Value**)` returns the displayed name (a `const wchar_t*`; release the returned `Value`). The per-folder cache maps displayed name to tags. Two items that show the same name (hidden extensions) are ambiguous: draw no dot on them and tell the user once in the details pane.

**Reading tags.** A worker thread reads the named stream for each item when a folder opens and fills a cache keyed by displayed name. The paint hook only reads the cache and never touches the disk. The cache refreshes on navigation and when the window regains focus, and updates immediately for changes made by the mod itself. Cost to keep honest: a folder of thousands of items on a slow disk, so reads are chunked and cancelled on navigation.

**Dot drawing.** Pre-rendered anti-aliased dot images per colour, theme and scale, cached and blended with `AlphaBlend`. Do not use GDI+ (its start-up and shutdown are the global-destructor trap in the Windhawk docs). Theme comes from the `AppsUseLightTheme` value, re-read about once a second.

**Settings.** A "Labels" group on Windhawk's page: enable, "Dim items that don't match the tag filter" (default on), and the 7 colour names. About 9 new settings; text rules as above; French strings in the schema.

**Fail safe.** If any needed `dui70.dll` export or the `GetClassInfoW` and `GetName` vtable slots cannot be found, labels switch off and everything else works. The Windhawk log says why.

## Recon results (2026-11-17)

**Timestamps.** Writing a named stream resets the file's and the folder's last-write time, unless the handle that writes is told not to update the times: call `SetFileTime(handle, nullptr, nullptr, &minusOne)` (both words of the FILETIME set to 0xFFFFFFFF) before the write. Verified for files and folders. A copy and a move keep the stream (verified). The build must do this on every stream write.

**Text width.** The names' font fits Segoe UI at about 13 px at 96 DPI (within about 5 percent on a 125 px name). Use it only to decide the Details and Icons fallbacks, scaled by the row's line height rather than by a fixed DPI. In List, Tiles and Content the name cell is exactly as wide as its text, so use the cell width.

**Explorer's own right-click menu.** It is built in `FileExplorerExtensions.dll` by `MenuFlyoutControl::AppendMenuItem(CommandBarFlyout&, MenuItem const&)`, once per command: the icon row (cut, copy, and so on) is the primary commands, the text entries are the secondary commands; submenus ("New", "Open with") come from `MenuFlyoutControl::CreateMenuHelper(MenuFlyout&, IVectorView<MenuItem>)`. Both hook with `WindhawkUtils::HookSymbols` like the rest of the mod. Appending an `AppBarButton` to `SecondaryCommands()` from inside the hook works and the entry shows in the real menu (probe-ctx-hook 0.1, seen by Ari). The last append is not marked, so the build should attach a one-shot `Opening` handler on the flyout (revoked, per the unload rules) and add the "Tags" entry there, at the end of the secondary list or before the legacy "Show more options" entry. The entry must be matched to the selection: the shell items are not in these arguments, so read the current selection through the window's shell view when the menu opens.

## What is left to the builder

- The exact stream name and format (suggested: one stream named for the mod, a version line, then one line per tag with the colour index, a tab and the name; at most 16 tags per item, names at most 40 characters).
- The exact palette values from the Fluent colour guidance, for light and dark (7 hues plus neutral gray, with hover-safe contrast).
- Chunk size and cancellation for reading streams.
- The right-click entry in Explorer's own list: the position and the selection lookup (see recon results).
- Tests: the existing smoke script, plus checks for read and write of the stream, a non-NTFS volume, a locked file, hidden-extension collisions, unload while a flyout is open, and all five view modes in light and dark theme, at two scales.

## Version and release

Minor `@version` bump per build, per Ari's rule. This is a large feature for after the current catalog submission round (the PR is not 1.0.0 yet and awaits its next AI review); do not mix it into that PR.
