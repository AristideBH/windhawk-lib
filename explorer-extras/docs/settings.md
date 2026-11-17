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
| Double-click empty space to go up | off | Goes up one folder. In the first column, the parent folder is added in front. |

### Content

| Setting | Default | What it does |
| --- | --- | --- |
| Folders first | on | Folders are listed before files in each column, whatever the sort. |
| Desktop column shows real items only | off | Hides virtual items (This PC, Recycle Bin, Control Panel…) from the Desktop column. |

### Layout

| Setting | Default | What it does |
| --- | --- | --- |
| Minimum column width | 160 | Columns fit their longest name, but not below this width (in pixels). (80 to 800) |
| Maximum column width | 420 | Columns fit their longest name up to this width (in pixels). Longer names are shortened. (80 to 1200) |
| Shift + double-click to even widths | on | Shift + double-click a column border gives all columns the same width. They stay so until a border is double-clicked alone. |
| Show the focus outline | on | Draws an outline around the item under the keyboard cursor. The selection highlight always shows. |
| Panel background | Explorer colour | Explorer uses its own file list colour. Theme uses the standard dark or light colour. |
| Accent badge on the path chevron | on | The chevron of the folder you came from sits in a round accent badge, instead of a muted one. |
| Alternate row shading | off | Every other row is a shade apart, to follow a line across a wide column. |

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

## Limits

| Setting | Default | What it does |
| --- | --- | --- |
| Maximum items per column | 5000 | A column lists at most this many items. A bigger folder shows the first ones in the sort order. (50 to 20000) |
| Largest document to preview (MB) | 60 | Bigger PDFs and Office files keep the thumbnail. (1 to 1024) |
| Largest image to open at once (megapixels) | 40 | Bigger pictures wait for a click on the card's button. (4 to 400) |

Following the active column, dragging items out of the panel, browsing archives, the marks of cloud, network and device items, opening a folder held under a drag, and a click in the navigation pane making its folder the first column are always on.
