# Labels

Colour labels, like the tags of macOS, on files and folders. They are part of Explorer Extras (version 0.69 and later) and have their own group on the mod's page in Windhawk.

## What a label is

A label is a coloured dot with an optional name. The colours are seven and a neutral gray, in a palette with a light and a dark variant. An item can have up to 16 labels, and a name can be up to 40 characters. A label with no name is called by its colour, and each colour has a name you can edit on Windhawk's page (Labels, Colour names), such as "Red = Urgent". Those names stay on your PC.

## Where they show

- In Explorer's own list, in every view (Details, List, Tiles, Content and the icon sizes), as dots after the name. Several labels are stacked, and a small plus means there are more than four colours.
- In Columns, after the name of a row.
- In the **Tags** section of the details pane, as badges that wrap.

The dots follow the light and the dark theme.

## How you set them

- **The details pane.** The **Tags** section shows the labels of the selection. Click **Add a tag** for the colours and an optional name; click a badge for a menu that removes it. With several items selected, a label that only some of them have is dimmed, and its menu can add it to all.
- **The right-click menu**, in Explorer's own list and in Columns: a **Tags** entry with the colours as checkable items, **Add tag** and **Clear tags**. It acts on the selected items, and an empty space has no entry.

## Sort and group

Explorer's own **Sort by**, **Group by** and the **Tags** column of the Details view use the labels. In the **Group by** menu choose **Tags** (**Étiquettes** in French: the mod renames the entry Windows calls Keywords), and in the **Sort by** menu choose it under **More**. Columns groups the same way: each label is a group of the column, and an item is in the group of each of its labels. Each label is a group, named by its name (or its colour's name), and an item with two labels is in both groups, like macOS tags. The tags a file already has, such as a photo's keywords, are kept and the labels added to them. Items with no tag are in the group Explorer calls unspecified.

A folder is read once when it is shown; when it was not read yet the first time Explorer asks, Explorer is told to read it again as soon as it is. After a label is set, the item moves to its new group. Switch this off with **Sort and group by tags** on Windhawk's page (it takes effect when a folder is shown again).

## Recent tags

The tags you gave a name lately (the last eight, with their colour) are listed under **Recent** in the **Add a tag** flyout, in the details pane and in the right-click menu, so a name is typed once: click one to give it to the selection. They are kept between sessions in the mod's storage, and a colour with no name is never listed since the colours are always there.

## Where they are kept

In a named stream of the file itself (`ExplorerExtras.Labels`), so a label follows the file when it is copied or moved on an NTFS drive, and the file's dates do not change. This needs NTFS: on another file system, or when a file is read-only or in use, the pane and the menu say so and write nothing. A zip, an email or a cloud service may drop the stream.

**They stay in the files.** The stream stays in the file after the mod is disabled or uninstalled. It is also written on a network share that supports named streams. To take the labels off, use **Clear tags** (in the pane or the right-click menu) while the mod is still running.

## Switching them off

Labels are off by default, since they are written into your files: turn on **Show labels** on the mod's page to use them. Off, nothing is drawn and the entries are gone; the labels stay in the files (use **Clear tags** to remove them).

## For developers

The design is in `docs/superpowers/specs/2026-11-17-labels-design.md` and the plan in `docs/superpowers/plans/2026-11-17-labels.md`. What the probes found about Explorer's list and menu is in the `windhawk-explorer-surfaces` skill, section "Labels, as built".
