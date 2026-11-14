# Menu mockups, what to send and what is ready

For Ari, to read when the mockups are being drawn. The three flyouts are built from
one shared builder since 0.23.0, so the mockups only decide content and numbers.

## What to send

One image per flyout, in `explorer-extras/assets/`:

- `menu-file.png`, `menu-folder.png`, `menu-empty.png`. Also welcome: a fourth for an
  entry inside an archive and a fifth for several items selected.

With each, as text next to the image or in the image:

1. The scale it was drawn at (100 percent, 125, 150 or 200), or the real pixel width
   of the menu, so rows and padding can be measured.
2. Row height, icon size (list and quick row), the width of the icon column, the gap
   before the shortcut hint, text sizes (name, hint, quick row label).
3. Colours or the Windows theme resource each comes from, if they are not the default
   menu colours: hover, pressed, separator, hint text, disabled.
4. The corner radius of the menu, of a row and of a quick button.
5. For the quick row: which actions, their order, whether labels show, and the layout
   when there are more than five.

## What each flyout shows (to confirm)

Items to state for every case, with the condition:

| Case | Items to decide |
| --- | --- |
| File | quick row, Open, Open with, Edit, Copy as path, Pin to Quick access, Properties, Show more options |
| Folder | the same, plus Open in a new tab and New folder with selection |
| Archive file | Extract here, Extract selected here, Open |
| Inside an archive | Copy only, Extract selected here |
| Several items | which entries make no sense (Open with, Rename) and how they look |
| Empty space | Paste, New folder, Refresh, View, Sort, Properties or Personalize on the Desktop |

Also: the order of groups, where the separators go, and whether Properties and
Show more options sit together at the bottom.

## Behaviours to say

- Shortcut hints: on every row that has one, or only some? Which text (Enter, Ctrl+X).
- Disabled rows: hidden or greyed.
- Submenus: arrow on the right, opening to the right; how deep.
- Keyboard: the arrow keys move through rows and the quick row; Enter chooses.
- Hover and press animations, if any.
- Touch (0.24): taller rows for a finger. A second set of numbers, or a multiplier.

## What is ready on my side

- `MenuStyle` in `src/08-menu-edit.cpp` holds every visual constant: min width, root
  margin, row icon, row padding, icon column, arrow column, hint opacity and gap,
  quick icon, quick button width and padding, quick label size, separator margin
  and colour, corner radius.
- `MenuBuilder` has `QuickRow`, `Row`, `Separator`, `Clickable` and `Show`. A section
  title row, a hint column and a different row height are small additions.
- The folder menu uses the same builder, so it follows the item menu's look.

## Not in the menus today

The shell's own newer entries (Compress to, Open in Terminal, the extensions) are
not in `IContextMenu::QueryContextMenu` and come under Show more options or not at
all. If a mockup shows them in the first level, say which, since each needs its own
route.
