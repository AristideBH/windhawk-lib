# Explorer Extras, all settings

These are the settings on the mod's page in Windhawk. They apply at once, with no restart.

This file is generated from `settings.schema.json` by `docs/make-settings-doc.py`. Do not edit the tables by hand.

## Columns

| Setting | Default | What it does |
| --- | --- | --- |
| Enable Columns | on | Adds Columns to the View menu. Off removes it. |

### Behaviour

| Setting | Default | What it does |
| --- | --- | --- |
| Open new windows in Columns | on | New Explorer windows open in Columns if it was your last layout. Picking another layout turns this off. |
| Keep columns after choosing a file | on | Columns to the right stay, dimmed, when you choose a file or several items. Off closes them. |
| Keyboard on group headers | off | Arrow keys reach group headers. Left, Right and Enter collapse and expand them. |
| Double-click empty space to go up | on | Goes up one folder. In the first column, the parent folder is added in front. |
| Sidebar sets the first column | on | Clicking Home, Gallery, OneDrive or a pinned folder in the navigation pane makes it the first column. Off, the columns follow it like any other navigation. The tree (This PC, drives, Network) always follows the columns. |

### Content

| Setting | Default | What it does |
| --- | --- | --- |
| Folders first | on | Folders are listed before files in each column, whatever the sort. |
| Desktop column shows real items only | on | Hides virtual items (This PC, Recycle Bin, Control Panel…) from the Desktop column. |
| Notice in an empty column, and while one loads | on | A muted line in the column when the folder is empty, and a small spinner with Loading when a folder takes a moment to open. |

### Layout

| Setting | Default | What it does |
| --- | --- | --- |
| Minimum column width | 160 | Columns fit their longest name, but not below this width (in pixels). (80 to 800) |
| Maximum column width | 420 | Columns fit their longest name up to this width (in pixels). Longer names are shortened. (80 to 1200) |
| Shift + double-click to even widths | on | Shift + double-click a column border gives all columns the same width. They stay so until a border is double-clicked alone. |
| Show the focus outline | off | Draws an outline around the item under the keyboard cursor. The selection highlight always shows. |
| Panel background | Theme colour | Explorer uses its own file list colour. Theme uses the standard dark or light colour. |
| Accent badge on the path chevron | off | The chevron of the folder you came from sits in a round accent badge, instead of a muted one. |
| Alternate row shading | on | Every other row is a shade apart, to follow a line across a wide column. |

### Memory

| Setting | Default | What it does |
| --- | --- | --- |
| Remember sort and grouping | on | Each folder keeps its own sort and grouping. When off, every column uses the first folder’s. |
| Remember column widths | on | A column you resize reopens at that width. |
| Remember Columns per folder | on | Each folder remembers whether it was left in Columns. Opening it turns Columns on or off to match. |

## Details pane

| Setting | Default | What it does |
| --- | --- | --- |
| Details pane | on | Replaces Explorer’s Details and Preview panes with one pane that holds a preview and the details of what is selected. |

### Pane

| Setting | Default | What it does |
| --- | --- | --- |
| Preview pane opens this pane | on | When Explorer’s Preview pane is turned on (Alt+P or the View menu), this pane opens in its place. |
| Animate the pane | on | The content slides and fades in when the selection changes. |
| Open previews by themselves | on | When off, a button on the preview opens it, and switches back to the thumbnail. |
| Count folder sizes by itself | off | A folder’s size, contents and types are counted when it is selected, which can use some CPU on a slow computer. Off, a button counts them when you ask. |

### Previews

| Setting | Default | What it does |
| --- | --- | --- |
| Preview documents and code | on | Shows PDF, text, Markdown and Office files in the pane. |
| Preview images | on | Shows pictures at full size in the pane, with zoom and pan. |
| Preview video and sound | on | Plays video and sound files in the pane. |

## Labels

| Setting | Default | What it does |
| --- | --- | --- |
| Show labels | on | Labels are coloured dots on files and folders, drawn in File Explorer and in Columns. They are kept in the file itself and need an NTFS drive. |
| Sort and group by tags | on | Adds the labels to Explorer’s own Tags property, so Sort by, Group by and the Tags column use them. The tags a file already has are kept. Takes effect when a folder is shown again. |

### Colour names

| Setting | Default | What it does |
| --- | --- | --- |
| Name of the neutral gray | Gray | What this colour means to you. It is shown for tags that have no name of their own, and it stays on this PC. |
| Name of colour 2 | Red | What this colour means to you. It is shown for tags that have no name of their own, and it stays on this PC. |
| Name of colour 3 | Orange | What this colour means to you. It is shown for tags that have no name of their own, and it stays on this PC. |
| Name of colour 4 | Yellow | What this colour means to you. It is shown for tags that have no name of their own, and it stays on this PC. |
| Name of colour 5 | Green | What this colour means to you. It is shown for tags that have no name of their own, and it stays on this PC. |
| Name of colour 6 | Teal | What this colour means to you. It is shown for tags that have no name of their own, and it stays on this PC. |
| Name of colour 7 | Blue | What this colour means to you. It is shown for tags that have no name of their own, and it stays on this PC. |
| Name of colour 8 | Purple | What this colour means to you. It is shown for tags that have no name of their own, and it stays on this PC. |

## Toolbar

| Setting | Default | What it does |
| --- | --- | --- |
| Change the toolbar | on | Turns on the toolbar settings below. Off leaves Explorer’s own spacing, search box and corners. |
| Gap between the navigation buttons | 4 | In pixels, between the four buttons and before the first. Explorer’s own gap is 16. (0 to 16) |
| Gap between the navigation buttons and the address bar | 4 | In pixels, between Refresh and the address bar. Explorer’s own is 8. (0 to 16) |
| Gap between the command bar buttons | 4 | In pixels, between New, Cut, Copy, Paste and the rest. Explorer’s own gap is 8. (0 to 8) |
| Resize the search box by dragging | on | Drag the edge between the address bar and the search box, with the mouse, a finger or a pen. A double click on it goes back to the width below. |
| Search box width | 0 | In pixels. 0 keeps Explorer’s own proportion. A width you dragged is kept until you change this value. (0 to 800) |
| Corner radius of the address bar | 4 | In pixels. Explorer’s own is 4, 16 makes the ends fully round. (0 to 16) |
| Corner radius of the search box | 4 | In pixels. Explorer’s own is 4, 16 makes the ends fully round. (0 to 16) |

## Sidebar

| Setting | Default | What it does |
| --- | --- | --- |
| Replace the navigation pane | off | Covers Explorer’s navigation pane with a sidebar of Favorites, Locations and Tags. Off leaves the navigation pane as it is. |
| Highlight section headers on hover | off | Gives the Favorites, Locations and Tags headers a background while the pointer is over them. The chevron and the … button have their own. |
| Let the Drives and devices group collapse | off | Adds a chevron to the group so it can be closed. Off keeps it always open, and a click on its row opens This PC. |
| List drives without a group | off | Puts the drives, OneDrive and phones straight in Locations instead of under Drives and devices. |
| Keep the keyboard in the sidebar after a click | off | When on, the arrows, Enter, Delete (unpin a favorite), F2 (rename a tag) and Ctrl+arrows (move a favorite or tag) work on the row you clicked. When off, the keyboard goes back to the file area, so the arrow keys keep moving through the columns. |
| Show how many files each tag has | off | A muted number at the right of each tag row in the sidebar. |
| Show free space under each drive | off | A thin bar along the bottom of a drive's row that shows how full it is. It does not change the row's size. |
| Eject button on removable drives | off | A small button at the right of a removable drive's row, shown while the pointer is over it. |
| Order of the tags in the sidebar | Most recently used | Recent puts the tag you used last on top. My order keeps the order you give by dragging a tag row. |
| Tags shown in the sidebar | 4 | The most recently used tags the sidebar lists, View all shows the others. -1 lists every tag. (-1 to 50) |

## Limits

| Setting | Default | What it does |
| --- | --- | --- |
| Maximum items per column | 5000 | A column lists at most this many items. A bigger folder shows the first ones in the sort order. (50 to 20000) |
| Largest document to preview (MB) | 60 | Bigger PDFs and Office files keep the thumbnail. (1 to 1024) |
| Largest image to open at once (megapixels) | 40 | Bigger pictures wait for a click on the card's button. (4 to 400) |

Following the active column, dragging items out of the panel, browsing archives, the marks of cloud, network and device items, opening a folder held under a drag, and a click in the navigation pane making its folder the first column are always on.
