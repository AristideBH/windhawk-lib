# Explorer Extras, all settings

Settings apply at once. Open the settings window from **Extras** at the bottom of the toolbar's ... menu. The same settings are on the mod's page in Windhawk. A change made in the settings window wins until the Windhawk page is edited again, and **Reset** on the Home page goes back to the Windhawk values.

This file is generated from `settings.schema.json` by `docs/make-settings-doc.py`. Do not edit the tables by hand.

## Home

| Setting | Default | What it does |
| --- | --- | --- |
| Enable Columns | on | Adds Columns to the View menu. Off removes it. |
| Details pane | on | Replaces Explorer’s Details pane with a preview and the details of what is selected. |

## Columns

| Setting | Default | What it does |
| --- | --- | --- |
| Open new windows in Columns | on | New Explorer windows open in Columns if it was your last layout. Picking another layout turns this off. |
| Desktop column shows real items only | off | Hides virtual items (This PC, Recycle Bin, Control Panel…) from the Desktop column. |
| Mark remote items | on | Shows a small icon on cloud-only files and on items on a network or a phone. |
| Remember sort and grouping | on | Each folder keeps its own sort and grouping. When off, every column uses the first folder’s. |
| Remember column widths | on | A column you resize reopens at that width. |
| Remember Columns per folder | on | Each folder remembers whether it was left in Columns. Opening it turns Columns on or off to match. |
| Open folders while dragging | on | Hold dragged items on a folder for about 0.7 s to open it in the next column. |
| Border after the last column | off | Draws a separator line on the right of the last column. |
| Accent badge on the path chevron | on | The chevron of the folder you came from sits in a round accent badge, instead of a muted one. |
| Alternate row shading | off | Every other row is a shade apart, to follow a line across a wide column. |
| Folders first | on | Folders are listed before files in each column, whatever the sort. |
| Shift + double-click to even widths | on | Shift + double-click a column border gives all columns the same width. They stay so until a border is double-clicked alone. |
| Keep columns after choosing a file | on | Columns to the right stay, dimmed, when you choose a file or several items. Off closes them. |
| Keyboard on group headers | off | Arrow keys reach group headers. Left, Right and Enter collapse and expand them. |
| Double-click empty space to go up | off | Goes up one folder. In the first column, the parent folder is added in front. |
| Show the focus outline | on | Draws an outline around the item under the keyboard cursor. The selection highlight always shows. |
| Panel background | Explorer colour | Explorer uses its own file list colour. Theme uses the standard dark or light colour. |
| Sidebar sets the first column | off | Clicking Home, Gallery, OneDrive or a pinned folder makes it the first column. The tree (This PC, drives, Network) still follows the columns. |
| Minimum column width | 160 | (80 to 800) |
| Maximum column width | 420 | Columns fit their longest name up to this width. Longer names are shortened. (80 to 1200) |
| Maximum items per column | 5000 | (50 to 20000) |

## Details pane

| Setting | Default | What it does |
| --- | --- | --- |
| Open previews by themselves | on | When off, a button on the preview opens it, and switches back to the thumbnail. |
| Preview pane opens this pane | on | Choosing Explorer’s Preview pane (Alt+P) shows this pane instead. |
| Animate the pane | on | The content slides and fades in when the selection changes. |
| Preview images | on | Shows pictures at full size in the pane, with zoom and pan. |
| Preview video and sound | on | Plays video and sound files in the pane. |
| Largest document to preview (MB) | 60 | Bigger PDFs and Office files keep the thumbnail. (1 to 1024) |
| Largest image to open at once (megapixels) | 40 | Bigger pictures wait for a click on the card's button. (4 to 400) |
| Preview documents and code | on | Shows PDF, text, Markdown and Office files in the pane. |

Following the active column, dragging items out of the panel and browsing archives are always on. The settings window is in English or French, following Windows' display language; Windhawk's own page for the mod is in English.
