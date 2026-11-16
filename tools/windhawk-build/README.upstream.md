# Build scripts

Compile a mod from this repository with Windhawk's own compiler and deploy it
to the installed Windhawk, without opening the Windhawk editor. Compiler errors
are printed in the terminal, and the running mod is reloaded right away.

| File | Purpose |
| --- | --- |
| [`../build.bat`](../build.bat) | Entry point. Opens the mod menu, or builds the mod you name. |
| [`build.ps1`](build.ps1) | Compiles and deploys one mod. |
| [`menu.ps1`](menu.ps1) | Arrow-key menu that lists the mods and runs `build.ps1` on the one you pick. |
| [`build.ini`](build.ini) | Settings: where Windhawk is installed. |
| [`build-task-uninstall.ps1`](build-task-uninstall.ps1) | Removes the elevated deploy task (see [Deploying without UAC prompts](#deploying-without-uac-prompts)). |

## Requirements

- Windhawk **installed** (the portable version is not supported: it keeps mod
  settings in `.ini` files instead of the registry). Tested with 1.7.x.
- Windows PowerShell 5.1 or PowerShell 7+. `build.bat` uses `pwsh` when it is
  installed and `powershell` otherwise.

## Setup

Set the Windhawk install folder (the one with `windhawk.exe` and
`windhawk.ini`) in [`build.ini`](build.ini):

```
[Windhawk]
WindhawkPath=C:\Software\Windhawk
```

Environment variables such as `%ProgramFiles%` are expanded, and quotes around
the path are allowed. Lines starting with `;` or `#` are comments.

`build.ps1` looks for Windhawk in this order and uses the first folder that
has a `windhawk.exe`:

1. the `-WindhawkPath` argument
2. the `WINDHAWK_PATH` environment variable
3. `WindhawkPath` in `build.ini`
4. `%ProgramFiles%\Windhawk`
5. the install location in Windhawk's uninstall registry entry

So if Windhawk is in the default location, `build.ini` can be left out.

## Usage

From the repository root:

```
build.bat                                   :: pick a mod from the menu
build.bat -CompileOnly                      :: menu; options are passed to build.ps1
build.bat explorer-command-bar              :: build that mod directly
build.bat explorer-command-bar -Release
```

Or call the PowerShell script directly:

```powershell
.\build\build.ps1 explorer-command-bar
.\build\build.ps1 explorer-command-bar -CompileOnly
.\build\build.ps1 -Source C:\path\to\some-mod.wh.cpp
```

Run without a mod name, `build.ps1` prints the list of mods it can build.

### The menu

`build.bat` without a mod name (or with only `-Options`) opens `menu.ps1`. It
lists every folder in the repository root that holds a `*.wh.cpp`, with the
mod's version and name from its metadata.

| Key | Action |
| --- | --- |
| Up / Down, `k` / `j` | Move the selection |
| Home / End | First / last mod |
| `1`-`9` | Jump to that mod |
| Enter | Build the selected mod |
| Esc / `q` | Quit |

When the menu is used, the window stays open after the build so a
double-clicked `build.bat` doesn't close before you can read the output.

### Options

| Option | Description |
| --- | --- |
| `<mod-folder>` | Folder name in the repo root. `explorer-command-bar` builds `explorer-command-bar\explorer-command-bar.wh.cpp` (or the folder's only `*.wh.cpp`). |
| `-Source <file>` | Build this `.wh.cpp` file instead; overrides the mod folder. |
| `-WindhawkPath <dir>` | Windhawk install folder; overrides `build.ini`. |
| `-ModId <id>` | Registry/mod ID. Defaults to `local@<@id>`, the same ID the Windhawk editor uses. |
| `-CompileOnly` | Only check that the mod compiles; don't deploy. |
| `-Release` | Optimize with `-O2` like Windhawk does. Without it builds use `-O0`, which compiles about twice as fast. |
| `-NoPch` | Compile without the cached precompiled header (slower). |
| `-ShowDefaults` | Print the default settings a new mod entry would get, then exit. |
| `-InstallDeployTask` | Register or update the elevated deploy task (one UAC prompt). |
| `-UninstallDeployTask` | Remove the deploy task. |

## How it works

### 1. Read the mod metadata

`build.ps1` reads the `// ==WindhawkMod==` block of the `.wh.cpp` file: `@id`,
`@version`, `@include` / `@exclude`, `@architecture` and `@compilerOptions`.
With no `@architecture`, the mod is built for `x86` and `x86-64`.

### 2. Compile

It finds Windhawk's folders (app data, engine, compiler and registry key)
from `windhawk.ini` in the install folder, then compiles one `mod.dll` per
architecture with Windhawk's bundled clang, using the same flags as the
Windhawk editor, links it against the engine's `windhawk.lib`, and adds the
mod's `@compilerOptions`. Output goes to `%TEMP%\windhawk-build\<mod-id>\`.

To speed up repeated builds:

- **Precompiled header.** `windhawk_api.h` and the mod's own system/library
  `#include`s (where almost all of the compile time goes, e.g. the C++/WinRT
  XAML headers) are compiled once into a precompiled header, cached in
  `%LOCALAPPDATA%\windhawk-build\pch`. It is rebuilt only when the includes,
  the flags or the compiler change. Unused cache entries are removed after 7
  days.
- **Large-address-aware clang.** Windhawk's clang is a 32-bit program limited
  to 2 GB of memory, which is too little to write the precompiled header. The
  script keeps a copy, `clang-NN-laa.exe`, with the large-address-aware flag
  set (4 GB) next to the original. Windhawk's own files are not changed, and
  the copy is refreshed when Windhawk updates its compiler. If the copy can't
  be created, the build continues without the precompiled header.

### 3. Deploy

Deploying does what **Compile Mod** in the Windhawk editor does:

1. Copies each DLL to `<AppData>\Engine\Mods\<32|64|arm64>\` under a new file
   name, `<mod-id>_<version>_<random>.dll`.
2. Creates or updates the mod's registry key under
   `<RegistryKey>\Engine\Mods\<mod-id>` (`Version`, `Include`, `Exclude`,
   `Architecture`).
3. If the mod has no saved settings yet, writes the defaults from its
   `==WindhawkModSettings==` block, like installing it from the Windhawk UI.
   Saved settings are never overwritten.
4. Writes `LibraryFileName` last. The engine in every injected process watches
   this value and reloads the mod as soon as it changes.
5. Copies the source to `<AppData>\ModsSource\` so the editor shows the same
   code.
6. Removes older DLLs of the mod. DLLs still loaded by a process are skipped
   and removed on a later build.

A newly created mod entry may not show up in the Windhawk UI until Windhawk is
restarted.

### Deploying without UAC prompts

Deploying writes to `HKLM` and `%ProgramData%`, so it needs admin rights:

- From an **elevated** terminal, the script deploys directly.
- Otherwise, it re-launches itself elevated just for the deploy step, which
  shows one UAC prompt per build.
- After `build.ps1 -InstallDeployTask` has been run once, deploys go through a
  scheduled task named `WindhawkModDeploy` instead, with no prompt.

The task runs elevated as your user. It runs a copy of `build.ps1` kept in
`%ProgramData%\WindhawkModDeploy`, a folder only administrators can write to,
so the task can't be used to run other code elevated. A normal build writes
what to deploy to `%LOCALAPPDATA%\WindhawkModDeploy\request.json` and starts
the task. The task only accepts builds from `%TEMP%\windhawk-build`.

Because the task uses its own copy, run `build.ps1 -InstallDeployTask` again
after changing `build.ps1`. Until you do, builds print *"the deploy task uses
an older build.ps1"*. The Windhawk path is passed to the task with each
request, so changing `build.ini` doesn't require that.

To remove the task, run `build.ps1 -UninstallDeployTask` or
`build-task-uninstall.ps1`.
