# Sidebar

A sidebar that replaces Explorer's navigation pane: **Favorites**, **Locations** and **Tags**. It is part of Explorer Extras (version 1.1.0 and later), is off by default, and has its own group on the mod's page in Windhawk (**Replace the navigation pane**). Off, Explorer's own pane is back at once.

It works with or without Columns, and in every tab and window.

## Sections

- **Favorites.** Your pinned folders (the Quick access pins of Home). A click opens the folder. Right-click for Explorer's own menu, plus **Unpin from sidebar**.
- **Locations.** Home, Gallery, Recent, then **This PC** with its drives and devices (OneDrive and phones included), Network, Linux and the Recycle Bin. This PC's menu has **Map network drive**; the Recycle Bin's has **Empty Recycle Bin**; a removable drive's has **Eject** and **Format**.
- **Tags.** The labels you named, with their colour. A click opens a list of every file that has the tag (see below).

Each header has a chevron to fold the section and a **…** button for its options. A small dot on a folded header shows that the open folder is inside it.

## Pin by drop

Drag a folder onto **Favorites** to pin it. A line shows where it will land. Drag files onto a folder row to copy or move them there (the same keys as anywhere in Explorer), and onto a tag row to give them the tag.

## Order

Favorites and tags can be moved by dragging a row, or with Ctrl+Up and Ctrl+Down on the keyboard. Tags are sorted by recent use, by name, or in your own order (**Order of the tags in the sidebar**). **Tags shown in the sidebar** limits the list; **View all** opens the full list, and -1 shows every tag.

## Tag lists

A tag row opens a page of the files that carry the tag, in the file area, with Name, Location and Size columns (click a title to sort, drag an edge to resize, a click on Location goes to the file's folder). The right-click menu, rename, drag, Delete, the details pane and Back all work as in a folder. **Open in new tab** is on the row's menu. The tab is named after the tag, with its colour and the count.

A tag list is read from an index of the fixed local drives that one Explorer process builds in the background (a progress line shows while it is not ready) and keeps in step by watching the drives. Network drives, removable drives and system folders are not indexed.

On a tag row, **…** or right-click offers **Rename**, **Change colour** and **Delete**. They rewrite the tag in every file that has it, with a progress bar, and keep the files' dates.

## Options

| Setting | Default |
| --- | --- |
| Replace the navigation pane | off |
| Highlight section headers on hover | off |
| Let the Drives and devices group collapse | off |
| List drives without a group | off |
| Keep the keyboard in the sidebar after a click | off |
| Show how many files each tag has | off |
| Show free space under each drive | off |
| Eject button on removable drives | off |
| Order of the tags, Tags shown | recent, 4 |

The full list is in [`settings.md`](settings.md).

## Keyboard

With **Keep the keyboard in the sidebar after a click** on, the arrows and Enter move through the rows; Delete unpins a favorite, F2 renames a tag, Ctrl+Up and Ctrl+Down move a row. Off, the keyboard goes back to the file area after each click.

## Good to know

- The tags are the mod's [labels](labels.md); they stay in the files.
- Changing a sidebar setting rebuilds each window's sidebar at once.
