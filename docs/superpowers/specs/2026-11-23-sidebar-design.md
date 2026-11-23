# Explorer Extras: Sidebar, design

Date: 2026-11-23. Status: draft for Ari's approval.
Mockups: Figma file `zbL0EMITDBAr76OcDBss7y`, node `173752:30704` (two variants: "Single OS Drive +1 item" and "Multiple drives-devices"). The rounded card around the sidebar in the mockups only shows its limits; it is not part of the design.

## Intent

Replace Explorer's navigation pane with a sidebar that follows the Windows 11 look of the mockups and adds what macOS's Finder sidebar has: collapsible sections, drag and drop into Favorites, and Tags that open every file carrying them. It is a new area of Explorer Extras, next to Columns, Details pane, Labels and Toolbar, and builds on Labels.

## Goals

- Three sections, Favorites, Locations and Tags, drawn as in the mockups: 32 px rows, 12 px subheaders, pill selector in accent colour, tag icons in the label colours.
- Sections collapse (accordion). The whole header is clickable; a chevron shows on header hover, on all three headers. Collapsed state is remembered.
- Favorites mirrors Explorer's pinned Quick access folders. Dropping a folder pins it, same as the native "Pin to Quick access" entry. Files get the not-allowed cursor.
- Locations is a fixed set, each entry switched on or off from a "…" menu in its header or from "Hide from sidebar" on its context menu: Home, User, Gallery, Drives and devices, Network, Recycle bin, and cloud roots such as OneDrive. No reordering, no custom folders (those go in Favorites).
- Drives and devices (fixed drives, removable drives, phones, cloud roots) are flat in Locations up to two entries and grouped under "Drives and devices" from three, with tree connectors as in the mockup. The group is expanded by default and remembered. The threshold is fixed.
- Tags shows the user's own tag list (name and colour): create, rename, recolour, show or hide in the sidebar, delete, from a "…" menu in the header. The sidebar shows the most recently used tags, the number being a mod setting (default 4). A tag the index finds on disk that is not in the list is added to it automatically.
- "View all" opens a list of all tags in the file area (colour, name, file count). Opening a tag shows its files.
- Clicking a tag shows every file with it on fixed local drives, in the mod's own flat list (Name, Location, Date modified, Size; sortable), with the same icons, rename, drag and context menus as the columns. One tag per list. Back leaves the list and returns to the previous folder.
- Dropping files on a tag adds the tag to them (no move, no copy). Dropping on a tag a file already has does nothing. Removing stays in the existing label menu.
- Rename and delete of a tag rewrite every file that carries it, through the index, in the background with progress. Delete asks first and states the number of files. Hide touches no file.
- Folder drops on any sidebar folder entry behave as on the native pane (move, copy, link by the shell).
- The entry whose folder contains the current folder gets the pill (closest containing entry). In a tag's list that tag gets it. Nothing contains the folder: nothing is marked.

## Non-goals

- No folder tree. The native pane expanded any folder; this sidebar expands only "Drives and devices". Revisit if missed.
- No reordering of Locations, no custom folders in Locations.
- No combining tags (Ctrl+click for both) in the first version; the index must be able to answer it later.
- No tag drop modifier key to remove a tag.
- No index of removable or network drives, of OneDrive files that exist only online, or of system folders (Windows, Program Files and the like). Tags on those still work in the folder view.

## Anti-goals

- A sidebar that breaks the Navigation pane toggle in the View menu, the splitter, or other Explorer windows.
- A tag list that hides tagged files (hence the automatic addition of unknown tags).
- An index that slows Explorer or the disk visibly (hence the low priority and exclusions).
- Writing files without the last-write time kept (the Labels store already guarantees this; the rename and delete passes must too).

## Constraints

- Explorer Extras conventions apply: bump `@version` on every commit, per-window `WindowState`, unload safety (`ModalScope`, `RunOutsideModal`, `ReleaseClosedWindow`, `RevokeKeptThreads`), no `no_destroy` on containers, catalog build through `build.py`, French and English text.
- XAML in an island: only opacity and translation transitions animate on screen. The chevron therefore flips between its two glyphs with a cross-fade and a section collapses with a fade and slide, not a shrinking height. A probe confirms this before the build.
- Labels live in the NTFS stream `:ExplorerExtras.Labels` (a text `v1`, then a colour digit, a tab and a name per line). Named streams need `FILE_NAMED_STREAMS` on the volume.
- Hover and selected states are not in the Figma frame (rest only). Use WinUI's standard fills: `SubtleFillColorSecondaryBrush` on hover (6% white), the same plus the 3 px accent pill for selected, 9% for selected and hovered, `SubtleFillColorTertiaryBrush` pressed.
- Icons come from the shell (`IShellItemImageFactory`), as in the columns, not from the mockup's PNGs. Tag icons are the label shape in the label colours (`imgSubtract` in the Figma code is the tag glyph).

## Approaches

- **A. XAML panel over the native pane (chosen).** Explorer keeps the space, the splitter and the "Navigation pane" toggle; the native tree stays alive underneath; the mod's `DesktopWindowXamlSource` covers it, as the Details pane does. Panel drops reach an own `IDropTarget`, as the columns panel does. Cost: the native tree still exists behind the panel.
- **B. Hide the native pane and add a sibling panel with its own splitter.** Cleanest on screen but re-creates layout Explorer manages and breaks the View menu toggle. Rejected.
- **C. Restyle the native `SysTreeView32`.** Cannot produce section headers, chevrons, a Tags section or the pill. Rejected.

## Decisions taken in the conversation (Ari)

Background index for tags; results in the mod's own flat list (Back handled by the mod, no shell namespace extension); the tag list as described, with rename and delete changing files; Favorites follow native pinned folders; Locations fixed with toggles; tree dropped for now; "View all" opens the tag list; four recent tags, adjustable in settings; closest containing entry gets the pill; adding a tag on drop only; index scope as above; chevron on all three headers, hover, animated, whole header clickable; grouping from three entries; one tag per list; approach A.

## Calls I made (check these)

- The sidebar is its own area with its own switch in the mod settings, independent of Columns; with it off the native pane is untouched.
- The sidebar uses the width of Explorer's pane (its splitter), minimum as native.
- Favorites reordering by drag is only built if a probe shows Windows allows it; otherwise order follows the shell.
- Context menus on sidebar rows use the same custom flyout as the columns, with the shell's entries plus "Hide from sidebar" (Locations) or the tag actions (Tags).
- Recency of a tag is the time it was last added to a file through the mod, kept in the mod's settings value.
- After a first scan, change notifications keep the index current; a quick rescan at Explorer start catches changes made while Explorer was closed.
- The index lives in the mod's data folder (`%LOCALAPPDATA%\ExplorerExtras`), is rebuildable, and is dropped on corruption.

## Left to the builder

- Index format and the change-notification mechanism (`ReadDirectoryChangesW` per fixed drive root, or the USN journal), within the low-priority, no-visible-slowdown constraint.
- How the panel picks which native entries exist (Home, Gallery, cloud roots) from the shell's known folders.
- Pin and unpin through the shell's pin verbs, and how to observe pin changes made elsewhere.
- The exact pairing of the "Back" interception with Explorer's travel log.
- Settings layout, following `windhawk-ui-kit`.

## Probes to run first

1. Opacity and translation animations for the chevron and the section collapse inside the island.
2. Whether pinned Quick access folders can be listed, pinned, unpinned and reordered from a mod, and whether changes made elsewhere can be observed.
3. A rough scan of a large fixed drive for the `:ExplorerExtras.Labels` stream (time, effect on the disk) to size the index.
4. Covering the native pane with an island: splitter drags, the View menu toggle, new tabs and windows (reuse what the Details pane learned).

## Probe results (2026-11-23, details in `explorer-extras/dev/PLAN.md`)

- Pins: listed, pinned and unpinned from the shell with the verb ids `pintohome` and `unpinfromhome`; a pin change rewrites `f01b4d95cf55d32a.automaticDestinations-ms`, which a directory watch can see. No reordering route was found, so Favorites follow the shell's order and reordering is dropped from the first version.
- Scan: C: (about 998,000 files) scans in 90 s at background priority with the exclusions. A plain walk is enough; no USN journal.
- Animation: opacity transitions show intermediate frames in the island, so the chevron cross-fade and the section fade work. The slide is set but unmeasured.
- Island over the pane: hides the native tree and follows its rectangle. Splitter, View toggle, new tabs and a second window still to be tried by hand.
