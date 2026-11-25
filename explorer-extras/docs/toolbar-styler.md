# Toolbar spacing and corners with Windows 11 File Explorer Styler

Up to version 1.0.21, Explorer Extras had a Toolbar part: tighter navigation and command bar buttons, round address and search boxes, and a search box you could resize. Those are now elsewhere:

- **Spacing and corners:** the [Windows 11 File Explorer Styler](https://windhawk.net/mods/windows-11-file-explorer-styler) mod, with the config below.
- **Resizing the search box:** the [Explorer Search Box Resize](../../explorer-search-box-resize/explorer-search-box-resize.wh.cpp) mod.

## Install the config

1. Install **Windows 11 File Explorer Styler** in Windhawk.
2. Open its **Settings**, switch to the text view, and paste the block below. If you already have control styles, add these entries to your `controlStyles` list instead of replacing it.
3. Save, and open a new Explorer window.

```yaml
theme: ""
controlStyles:
- target: AppBarButton#backButton
  styles:
  - Margin=4,8,4,0
- target: AppBarButton#forwardButton
  styles:
  - Margin=0,8,4,0
- target: AppBarButton#upButton
  styles:
  - Margin=0,8,4,0
- target: AppBarButton#refreshButton
  styles:
  - Margin=0,8,4,0
- target: CommandBar#FileExplorerCommandBar > * > AppBarButton
  styles:
  - Margin=2,0,2,0
- target: FileExplorerExtensions.AddressBarControl > Grid#PART_LayoutRoot
  styles:
  - CornerRadius=8
- target: AutoSuggestBox#PART_AutoSuggestBox
  styles:
  - CornerRadius=8
- target: AutoSuggestBox#PART_AutoSuggestBox > Grid#LayoutRoot > TextBox#TextBox
  styles:
  - CornerRadius=8
- target: AutoSuggestBox#PART_AutoSuggestBox > * > Border#BorderElement
  styles:
  - CornerRadius=8
- target: Grid#NormalModeGrid
  styles:
  - CornerRadius=8
- target: AutoSuggestBox#FileExplorerSearchBox
  styles:
  - CornerRadius=8
- target: AutoSuggestBox#FileExplorerSearchBox > Grid#LayoutRoot > TextBox#TextBox
  styles:
  - CornerRadius=8
- target: AutoSuggestBox#FileExplorerSearchBox > * > Border#BorderElement
  styles:
  - CornerRadius=8
```

## What each entry does

| Entries | Effect | Explorer's own |
| --- | --- | --- |
| `backButton` to `refreshButton` | 4 px between Back, Forward, Up and Refresh. Refresh's right margin of 4, plus a 6 px column Explorer keeps after the buttons, leaves 10 px before the address bar. | `8,8,8,0` on each, 16 px apart |
| `FileExplorerCommandBar > * > AppBarButton` | 4 px between New, Cut, Copy, Paste and the rest. | 8 px |
| `AddressBarControl`, `PART_AutoSuggestBox`, `NormalModeGrid` | Address bar corners of 8 (16 makes the ends fully round). The breadcrumbs and the edit box both need it. | 4 |
| `FileExplorerSearchBox` | Search box corners of 8. | 4 |

A margin is `left,top,right,bottom`: change the numbers to taste.
