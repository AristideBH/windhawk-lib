# Columns Showcase, a folder for the video and for the tests

`make-columns-showcase.ps1` builds `Columns Showcase` on the Desktop: a working
folder as a freelance designer and developer keeps it, about 1 900 files and 625 MB
(most of it zero-filled video and size samples), with real pictures, PDFs, Word and
Excel files, playable tones and real zip archives. Dates run from this hour back five
years, so the date groups are all there. `Columns Demo` (the older script) stays the
small test folder.

    pwsh explorer-extras\make-columns-showcase.ps1 -Force
    pwsh explorer-extras\make-columns-showcase.ps1 -Force -Stress   (adds 6000 files)

Years in names (2024, 2025) are fixed; dates are relative to the day it runs.

## What to show in the video

A path that reads well, deepest first:

1. `Clients > Atelier Lumière > Brand > Guidelines`: four columns, PDF and PNG
   thumbnails, the tooltip with dimensions.
2. `Photos > 2024 > Lisbon`: real picture thumbnails, group by Date taken or Type.
3. `Projects > aurora-site > src > components > ui`: a code tree, six levels.
4. `Archive > Projects 2022.zip` (turn `archives` on): an archive browsed as a
   folder, `Extract here`, and `Backups > backup-2025-01-01.zip` which holds a zip.
5. `Documents > Receipts`: 48 files, Group by Name or Date.
6. `Music > Artists > Luna Harbor > Salt & Static (2023)`: Tones that play.
7. `Inbox`: the mess, duplicates, a partial download, a hidden temp file.

## Edge cases, where each one lives (`Edge cases\`)

| Folder | What it tests |
| --- | --- |
| the very long folder name (and the file inside) | Middle ellipsis, tooltip, column width limits. Path stays under 260 characters. |
| `Names` | Japanese, accents, an emoji folder, `& # ( ) [ ] % ; , ' + = @ ! ~`, no extension, `.tar.gz`, a name starting with a dot, case, many dots, runs of spaces. Rename, copy, drag, type to jump. |
| `Empty` | An empty folder, a folder of only folders, a folder with one zero byte file, no content column to open. |
| `Deep nesting` | Twelve levels: the strip scrolls, the active column follows, Back trims. |
| `Hidden and read-only` | Hidden files follow Explorer's setting (toggle it live), a hidden folder, read-only (delete and move prompts). |
| `Shortcuts and links` | `.lnk` to a folder and a file, a broken shortcut, a junction and a symbolic link to real folders: the link target is opened. |
| `Sizes` | 0 B to 400 MB, the Size grouping. |
| `Dates` | An hour ago to five years ago, the Date grouping. |
| `Types` | One of each kind, the Type grouping and the icons. |
| `Natural sort` | `file 1, 2, 3, 10, 11, 20, 100` sorts as numbers. |
| `Type to jump` | `ap`, `bl`, `cr`: jumping by typing, then Enter. |
| `Rename these` | Select the six, F2 or Rename: one name, numbered. Then Ctrl+Z. |
| `Conflicts` | Same names as in `Drop zones\Shared with team`, different sizes and dates: the replace dialog. |
| `Huge folder (1500 files)` | Rows built as they come into view, scrolling, icons arriving. With `-Stress`: 6 000 files, over the limit of 5 000, the "N more items not shown" row. |

## Drag and drop scenarios

Turn `dragOut` on (it is by default). For each: plain, with Ctrl (copy), with Shift
(move), with Alt (link), and Escape in the middle.

- A file onto a folder row in the next column; onto a column's empty area; onto a
  folder in the same column (not itself); a folder onto itself (refused, the label
  says so).
- Several rows (Ctrl or Shift select): the label shows the count and stacked icons.
- Out to the navigation pane (Home, Bureau, Téléchargements, a drive), to a second
  Explorer window in Details and in Icons view, to the Desktop, onto another app.
- Back into the panel after leaving it: the label comes back.
- In from another window or the Desktop, onto a row and onto empty space; edge
  scroll at the top and bottom of a long column (`Huge folder`) and at the left and
  right of the strip (`Deep nesting`).
- Onto `Drop zones\Locked` over `Do not overwrite.txt` (read-only conflict), onto
  `Shared with team` (the replace dialog), into an archive column (refused).
- A file out of an archive column (refused to start).

## Settings worth switching during the video

`keepStale` (columns of a chosen file stay dimmed, or close), `rememberSort`,
`rememberWidth`, `rememberLayout`, `archives`, `compact scrollbars`, group by on a
column and the per-column memory.
