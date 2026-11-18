# windhawk-lib

A collection of [Windhawk](https://windhawk.net/) mods for Windows.

> **Note:** These mods are vibe-coded — built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

## Mods

| Mod | Status | Description |
| --- | --- | --- |
| [**Explorer Extras**](explorer-extras/README.md) | **Released** (1.0.0), in the catalog review, takes reports | Additions for File Explorer, one switch per area: a Columns layout (Miller columns, drag and drop, touch), a details and preview pane (PDF, text, Markdown, images, video, sound), and colour labels (tags) on files and folders. Its settings are on the mod's Windhawk page. |
| [Windows 11 Start Button Tinter](windows-11-start-menu-button/README.md) | Not released | Recolor (with system accent color, gradient shading, and click shimmer) for the Windows 11 taskbar Start button, animation-preserving. |
| [Taskbar Widget Stack](taskbar-widget-stack/README.md) | Prototype | Stack multiple taskbar widgets vertically in one snap-scrollable pane, iOS-widget-stack style. Live-tested. |
| [Taskbar System Usage](taskbar-widget-system-usage/README.md) | Prototype | CPU/RAM/GPU usage bars in the taskbar. Standalone mod, live-tested; registers itself into Taskbar Widget Stack's pane when that mod is also installed. |
| [Taskbar Widget Media Player](taskbar-widget-media-player/README.md) | Not compiled | Minimal fork of Salyts' Taskbar Fluent Media Player, adding registration into Taskbar Widget Stack's pane - falls back to the original standalone behavior otherwise. |
| [Taskbar Widget: Weather](taskbar-widget-weather/README.md) | Not compiled | Shows current weather or a short forecast strip in the taskbar, with a details panel on click. Registers into Taskbar Widget Stack's pane when installed, falls back to standalone otherwise. |

**Status.** *Released*: in daily use, takes bug reports and suggestions. *Prototype*: works for the author, may change or break. *Not compiled*: written, never built. *Not released*: complete for the author, no release planned yet. The folder `explorer-columns/` is the old name of Explorer Extras' first version and is kept only for its notes.

## Reporting issues and ideas

Use the [issue forms](../../issues/new/choose). Every issue says which mod it is about: Explorer Extras has its own forms and the label `mod: explorer-extras`, and the other mods share one form with `mod: other`. Only Beta mods take bug reports for sure; reports about the others are welcome but may wait. How Explorer Extras reports are handled, and how it is told apart from the rest of the repository (labels, pull request titles, release tags), is in [`explorer-extras/FEEDBACK.md`](explorer-extras/FEEDBACK.md).

## License

Each mod is licensed under the MIT License - see its own folder for details.
