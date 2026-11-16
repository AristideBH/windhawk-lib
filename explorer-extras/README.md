# Explorer Extras

A [Windhawk](https://windhawk.net) mod that adds to File Explorer on Windows 11. It is one mod with several areas, each with its own page and switch in a settings window. Switch on what you want; the rest stays out of the way.

Status: public beta, in daily use by its author, not yet in the Windhawk catalog. Tested on Windows 11 build 26100 (24H2), ARM64, in French and English. The x86-64 build compiles and links but has not been run.

## Areas

| Area | What it does |
| --- | --- |
| **Columns** | A Columns layout in the View menu: Miller columns, one per folder level, with drag and drop, touch, a restyled context menu and per folder memory. [More](docs/columns.md) |
| **Details pane** | One pane in place of Explorer's Details and Preview panes: a real preview (PDF, text, Markdown, images, video, sound) and the details, in every view. [More](docs/details-pane.md) |
| **Settings window** | Every option in one place, applied at once, in English or French. Open it from **Extras** in the toolbar's ... menu. |

More areas will come; see [`../ROADMAP.md`](../ROADMAP.md) for what is planned.

![The Columns layout](assets/captures/columns.svg)

## Install

1. Install [Windhawk](https://windhawk.net).
2. In Windhawk choose **Create a new mod**, replace the code with the contents of `explorer-extras.wh.cpp`, and compile it (Ctrl+B).
3. Enable the mod. For Columns, open the View menu of a File Explorer window and choose **Columns**. The details pane is on by default: it shows in place of Explorer's Details pane (Alt+Shift+P, or the toolbar button).

The mod hooks `FileExplorerExtensions.dll`. Windhawk must be able to download Microsoft's symbols for it, which has worked on every build tested. If symbols are missing right after a Windows update, the mod waits and logs it.

## Settings

Each area has its own page, and every area has an off switch, so you can use only the part you like. The full list is in [`docs/settings.md`](docs/settings.md). Settings apply at once, and the same ones are on the mod's page in Windhawk.

![The settings window](assets/captures/settings.svg)

## Feedback

Found a bug, or have an idea? Open an issue: [bug report](https://github.com/AristideBH/windhawk-lib/issues/new?template=explorer-extras-bug.yml) or [feature suggestion](https://github.com/AristideBH/windhawk-lib/issues/new?template=explorer-extras-feature.yml). A bug report is easiest to follow with the mod's version, your Windows build and the log (turn on "Write a debug log", then use **Copy diagnostics** on the Home page). How reports are handled is in [`FEEDBACK.md`](FEEDBACK.md).

## Build from source

The mod is one file for Windhawk but is kept in parts under `src/`, joined by a script:

```
python explorer-extras/build.py            # writes explorer-extras.wh.cpp
python explorer-extras/build.py --check    # exits 1 if the generated file is out of date
```

Edit the parts, never the generated file. A syntax-only check with Windhawk's own clang is described in [`HANDOFF.md`](HANDOFF.md).

## More

- [`CHANGELOG.md`](CHANGELOG.md): what changed in each release.
- [`FEEDBACK.md`](FEEDBACK.md): how issues and suggestions are handled.
- [`HANDOFF.md`](HANDOFF.md): how the work is run, the architecture, and what costs time.
- [`PLAN.md`](PLAN.md): findings from the probes and the decisions behind the design.
- [`../ROADMAP.md`](../ROADMAP.md): what is done, tested and open.

## License

MIT.
