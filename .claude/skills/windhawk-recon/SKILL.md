---
name: windhawk-recon
description: How to inspect a target process before writing a Windhawk mod against it. Use at the start of any new mod, any new target app or window, and any time an element doesn't appear, a handler never fires, a symbol doesn't resolve or a byte pattern doesn't match. Covers identifying the UI framework, dumping the real UI tree, finding symbols and exports, calling C++ exports safely, and recording confirmed facts. Use it before guessing at class names, element names or event routing.
---

# Windhawk recon

Every long detour in this repo started with a guess about the target. The toolbar buttons were assumed to be `QToolButton` and were `QPushButton`. A Win32 window was placed inside a taskbar that only composites XAML. A handler waited on an event the Start button never raises. Each guess cost between two and six test rounds, and each was settled in one round by dumping what was really there.

So recon comes first, and its output is a "Confirmed facts" section in the mod's PLAN.md. Code gets written against that section.

## Step 1: which framework draws the thing

One process often mixes several. Explorer alone has four, see `windhawk-explorer-surfaces`. Identify the framework of the exact surface you want to change.

| Evidence | Framework | What works on it |
| --- | --- | --- |
| `Windows.UI.Xaml.dll`, and the surface is not a classic child window | System XAML, for example the taskbar | Insert or restyle XAML elements, see `windhawk-taskbar-xaml` |
| `Microsoft.UI.Xaml.dll` | WinUI 3, for example Explorer's tabs and command bar | XAML diagnostics API, as Windows 11 File Explorer Styler does |
| `Qt5Widgets.dll` or `Qt6Widgets.dll` | Qt Widgets | Call Qt's exported C++ functions, style sheets |
| `dui70.dll`, window class `DirectUIHWND` | DirectUI, for example Explorer's file list | Hooks on internal symbols, overlays |
| Classic window classes such as `SysListView32`, `ToolbarWindow32` | Win32 common controls | Subclassing, custom draw, theme hooks |
| `libcef.dll`, `chrome_elf.dll`, a WebView2 loader | Chromium or WebView2 | Usually out of reach for a mod |

Get the evidence from inside the process. A ten-line diagnostic mod that logs the loaded modules settles it:

```cpp
#include <psapi.h>
void LogModules() {
    HMODULE mods[1024]; DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) return;
    for (DWORD i = 0; i < needed / sizeof(HMODULE); i++) {
        WCHAR path[MAX_PATH];
        if (GetModuleFileNameW(mods[i], path, MAX_PATH)) Wh_Log(L"module: %s", path);
    }
}
```

This worked on Snipaste where looking at the install folder did not. Store apps live under `WindowsApps`, which is locked, but the modules are readable once loaded.

If a framework is statically linked, with no separate DLL, there are no exports to call and that route is closed. Say so early.

## Step 2: dump the real tree

Never write a selector, class name or element name from memory or from another mod's source alone. Windows builds differ.

**XAML surfaces.** UWPSpy, from the author of Windhawk, shows the live XAML tree of a process with names, classes and properties. Ask Ari to open it on the target and send the branch around the element. For File Explorer the Styler readme says to select the `explorer.exe` process and the WinUI 3 target framework. The taskbar uses system XAML, so pick that framework there.

**Composition trees.** When the thing is drawn below XAML, as the Start icon's Lottie animation is, log the whole visual and shape tree once: every node's type, offset, scale and transform matrix. A sample of one node led to two wrong fixes on the Start button mod. The full dump showed four copies of the icon, three of them collapsed.

**Any surface with accessibility support.** Walk UI Automation from inside the process. It gave the real class of every Snipaste button, their count and their rectangles. Link `-luiautomationcore` and `-lole32`. Trigger the dump by polling `GetAsyncKeyState` from a hook that already runs on the UI thread. A registered hotkey won't arrive, see the threading section of the core skill.

**Win32 surfaces.** Spy++ or any window spy gives classes, styles and the parent chain. Record the chain, since child windows of the same class are common.

## Step 3: confirm the events really fire

Before building on an event, log it. On the Start button, a real click produced `PointerEntered`, then `Checked` or `Unchecked`, then `PointerCaptureLost`. `PointerPressed` never fired, because Explorer handles the click before routed events see it.

Qt has the same trap. Style sheet states such as `:hover` and `:checked` depend on flags that Qt's own button code sets. Snipaste does its own mouse handling, so those states never applied even though base colors did. Test one loud rule per state, such as a magenta background, before designing around it.

Log a line per event with the element name, for one test round, then remove the logging.

## Step 4: find the functions

**Exported functions.** Walk the module's export table in memory and log every name that contains a keyword. This avoids needing the file on disk and gives the exact decorated names for that build.

```cpp
void DumpExportsContaining(HMODULE mod, const char* needle) {
    auto base = (BYTE*)mod;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return;
    auto exp = (IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    auto names = (DWORD*)(base + exp->AddressOfNames);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        const char* name = (const char*)(base + names[i]);
        if (strstr(name, needle)) Wh_Log(L"export: %S", name);
    }
}
```

Decorated names change between major versions. Qt 5's `fromUtf8` takes an `int` and Qt 6's takes a 64-bit size, so the Qt 5 name never resolved. Treat every decorated name as a guess until the dump shows it, and mark each one in the source as confirmed or guessed.

**Internal functions.** Use the Windhawk Symbol Helper tool to list a module's symbols, then hook with `WindhawkUtils::HookSymbols`. Copy the symbol string exactly, including the trailing space some have.

**Fields with no symbol.** Some mods recover a field offset by matching the first bytes of a function. This is the most fragile technique in the repo. When a match fails, log the first 16 bytes as hex and work from those. Don't propose a new pattern without them. Remember the target may be ARM64 code even when the mod is not, see the architecture section of the core skill.

## Step 5: call C++ exports safely

Calling a C++ member function through `GetProcAddress` means reproducing the compiler's calling rules by hand. For MSVC-built targets on x64:

- A member function takes `this` as a hidden first argument.
- A function that returns a class by value takes a hidden pointer to caller-owned storage as its first argument, and returns that pointer. `QString::fromUtf8` is one. Calling it without the slot crashed Snipaste.
- Give that storage generous size and alignment, such as `alignas(16) unsigned char buf[64]`, when you don't have the real class definition.
- An object you build this way is never destroyed unless you also call its destructor export. A one-time leak is acceptable in a proof of concept and not in code that runs repeatedly.

Prefer a factory function the library exports over constructing an object yourself.

## Step 6: check for prior art

Search windhawk.net and the ramensoftware/windhawk-mods repo for a mod that touches the same surface, and read its source, not only its readme. Both taskbar reference mods used a thread marshal helper and XAML injection. The first stack prototype was modeled on them and used neither, and it froze Explorer and then drew nothing. For a pull request that is still open, read the review comments. They list the fixes that weren't obvious.

Credit ported code in a comment with the author and license.

## Step 7: write it down

Add to the mod's PLAN.md, under "Confirmed facts":

- The framework and how it was identified.
- The relevant branch of the tree, with element names and classes.
- Which events fire and which don't.
- Every symbol or export used, marked confirmed or guessed, with the Windows build or app version it was confirmed on.
- What was tried and ruled out.

## When to stop

Some targets can't be done well, and saying so early is a result. Snipaste's hover and selected states are drawn by its own code and no style sheet reaches them. The honest options were a resting-state restyle, a much heavier paint hook, or a feature request to the author. Give the options with their cost and let Ari choose.

## Probe by dumping, not by guessing (learned on the Explorer Extras toolbar, 0.77)

When a XAML change does nothing, or does half of what was expected, do not try the next likely element. Log the whole branch once from the mod (debug build, once per process, with a budget of lines): for every `FrameworkElement` the class, `Name()`, margin, `ActualWidth`/`ActualHeight`, `Height`, `MinHeight`, padding, spacing, and for Borders, Grids and Controls the `CornerRadius`, whether `Background` and `BorderBrush` are set. Read the dump once and name the element that draws the thing. Two examples of what the guesses missed: the address bar's rounded outline is drawn by a Grid (`PART_LayoutRoot`) around the AutoSuggestBox, not by its TextBox, so setting the radius on the box did nothing; and a toolbar button's gap was half in a margin and half in a 6 DIP spare column of the CommandBar's `ContentRoot`.

Pair the dump with a UI Automation dump of the real window (screen pixels, scale about 1.75 on Ari's laptop) to see what Windows really placed, and with `GetWindow`/`GetWindowRect` on the frame's direct child windows when a size does not follow the XAML: Explorer's toolbar island is sized by the frame, not by its content, so shrinking rows in XAML only leaves blank space. Log first, then decide; one dump answers what three tries do not.
