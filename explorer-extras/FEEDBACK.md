# Explorer Extras, feedback and issues

How bug reports, questions and ideas are received and handled. Explorer Extras is a solo project, built with AI assistance and tested by hand, so there is no fixed answer time, but every issue gets a label and a reply.

## Where to report

GitHub Issues of this repository, with one of the forms:

- **Bug report.** Something does not work. Fill in the version, the Windows build, the processor, the steps, and the diagnostics.
- **Feature suggestion.** An idea for an existing area or a new one. Start from the problem, not the solution.

Questions go in an issue too (label `type: question`). Reports about the other mods in the repository use the "Another mod" form; those mods are not released.

## What makes a report easy to act on

1. **The version** of the mod (in Windhawk) and the **Windows build** (Settings, System, About).
2. **Steps.** What you did, and whether by mouse, keyboard or touch.
3. **The log.** Turn on logging in the mod's Advanced tab in Windhawk, repeat the problem and paste the log. For a crash, say what you were doing; the Windows Reliability History entry or the crash dump helps.
4. **Whether it is only one Explorer window, one folder, or one kind of file.**

## What happens to a report

| Step | Label | Meaning |
| --- | --- | --- |
| Received | `status: needs triage` | Not looked at yet. Added by the form. |
| Asked for more | `status: needs info` | The version, steps or log are missing. Closed after two weeks without an answer. |
| Accepted | `status: confirmed` | Reproduced, or a clear idea worth doing. |
| Scheduled | `status: planned` | On the [roadmap](../ROADMAP.md). |
| Underway | `status: in progress` | A branch is open. |
| Done | `status: fixed in next` | Fixed on `main`, in the next release. Closed when released. |
| Declined | `status: wontfix` | With the reason in the thread (for example, Windows already has an option for it). |

Every issue carries `mod: explorer-extras`, an `area:` label (`columns`, `details-pane`, `settings`, `new`) and a `type:` label (`bug`, `crash`, `idea`, `question`). The labels are listed in [`../.github/labels.yml`](../.github/labels.yml).

## How an issue is worked on

1. Read it, add the labels, ask for what is missing.
2. Reproduce from the diagnostics, or from the steps on a test folder (`make-columns-showcase.ps1` builds one).
3. Branch `fix/explorer-extras-<number>-<short-name>` (or `feat/...`).
4. Change the parts under `src/`, bump `@version`, run `python explorer-extras/build.py`, and the syntax check in [`HANDOFF.md`](HANDOFF.md).
5. Try it on a device. Say in the issue what was tried, and on which Windows build and processor.
6. Add a line to [`CHANGELOG.md`](CHANGELOG.md), open a pull request titled `[explorer-extras] ...` with `Fixes #<number>`.
7. After the merge the issue is labelled `status: fixed in next`, and closed with the release.

## Telling Explorer Extras apart from the other mods

The repository holds several mods. Explorer Extras is the one that is released. To keep it apart:

- Issues: the `mod: explorer-extras` label (the forms add it), and the "Explorer Extras" forms. The other mods use `mod: other`.
- Pull requests and commits: the title starts with `[explorer-extras]`.
- Releases: tags are `explorer-extras-v<version>`, for example `explorer-extras-v0.55.0`. A tag of another mod starts with that mod's folder name.
- Notes: this mod's changes are in `explorer-extras/CHANGELOG.md`, nowhere else.
- The root [README](../README.md) lists every mod with its status: Released, Beta, Prototype or Planned. Only Released and Beta mods take bug reports.

## Releases

A release is a tag `explorer-extras-v<version>` with the changelog entry as its notes and the generated `explorer-extras.wh.cpp` attached. The Windhawk catalog is a separate step: a pull request to the windhawk-mods repository, made when the beta has settled.

## What will not be built

Things Windows already does with its own option (the status bar, for one: Folder Options, View, "Show status bar") are declined, and said so in the thread. Anything that needs a patch of Explorer's binary beyond the symbols the mod already uses is judged case by case.
