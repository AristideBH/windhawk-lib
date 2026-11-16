# Details pane

One pane on the right of an Explorer window, in place of Explorer's Details and Preview panes. It works in Columns and in every other view. It shows a preview on top and the details below, and it crossfades from one selection to the next.

## What it shows

| Selection | Preview | Details |
| --- | --- | --- |
| A file | The real preview, below | Name, type, size, dates, location and the properties that fit the type (dimensions, duration, author and so on), with "More details" for the rest. Click a value to copy it. |
| A folder, or nothing selected | The folder icon | Item count, dates, location, and its total size, contents and types, filled in by a background count. |
| Several items | A fanned stack of thumbnails, the newest on top | Counts, total size, types, and the size of the folders among them. |

An action row holds Open, Copy path and Properties.

## Previews

- **PDF.** Drawn by the pane with Windows' own PDF reader. Scrolls with the wheel.
- **Text and code, Markdown.** Text files show their start. Markdown is interpreted (headings, lists, quotes, code, bold, italic, links).
- **Images.** Opened at full size. Wheel, pinch and double-tap zoom; a drag pans. Animated GIFs play. Images over 40 megapixels keep the thumbnail until a button on the card opens them.
- **Video and sound.** A small player with its own controls, which start paused. A sound shows its cover, title and artist. Volume is remembered.
- **Office and other types.** Explorer's own preview handler, in a native window, when the file type has one. These cannot scroll.

A corner button on the card switches between the thumbnail and the preview. Previews can be set to open by themselves, or only on that button.

## Showing and hiding

Explorer's Details and Preview entries in the View menu are replaced by one entry, with the navigation pane entry next to it. The toolbar's Details button is replaced by ours. **Alt+P** and **Alt+Shift+P** both toggle the pane, so Explorer's own Preview pane does not open.

## Settings

See [`settings.md`](settings.md), page "Details pane": the pane itself, the Preview pane redirect, animation, which previews run, and their size limits.

The design notes and what the probes found are in [`../SPEC-details-pane.md`](../SPEC-details-pane.md).
