# Windhawk build script

`build.ps1`, `README.upstream.md` and `LICENSE.upstream` come from
[DanRotaru/windhawk-mods](https://github.com/DanRotaru/windhawk-mods) (`build/`), MIT licence,
copyright 2026 Dan Rotaru. The files are kept as they were taken; see `README.upstream.md` for how
the script works. Local changes, if any, are listed below.

Used here to compile a mod with Windhawk's own compiler and deploy it to the installed Windhawk:

```
pwsh tools/windhawk-build/build.ps1 -Source explorer-extras/explorer-extras.debug.wh.cpp -CompileOnly
pwsh tools/windhawk-build/build.ps1 -Source explorer-extras/explorer-extras.debug.wh.cpp
```

## Local changes

- `build.ps1`: on Windows on ARM, a mod that lists `x86-64` is also built for `arm64`, as Windhawk's
  editor does (Explorer runs as arm64 there, so an x86-64 DLL alone would never load).
