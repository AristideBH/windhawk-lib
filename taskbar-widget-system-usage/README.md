# Taskbar System Usage

> **Note:** This mod is vibe-coded - built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

> **Extracted prototype, not yet compiled/tested standalone:** this mod
> started as one widget inside [`taskbar-widget-stack`](../taskbar-widget-stack/README.md)'s
> own in-process widget SDK, confirmed working there. It's been pulled out
> into this standalone mod so it can be developed and installed
> independently, but the extraction itself (the injection/lifecycle code
> around the bars, which didn't exist in this exact shape before) hasn't
> been live-tested yet - see [`PLAN.md`](PLAN.md).

Three small horizontal bars injected into the Windows 11 taskbar - CPU,
RAM, and GPU usage, label on the left, live percentage on the right.

## Requirements

- Windows 11, 64-bit
- Windhawk v1.4 or later

## Status

Not yet installed/tested on its own. The bar-building and metric-sampling
code (CPU via `GetSystemTimes`, RAM via `GlobalMemoryStatusEx`, GPU via a
PDH `GPU Engine` counter) is unchanged from its confirmed-working run
inside `taskbar-widget-stack` - what's new and unverified here is the
surrounding injection/removal/settings lifecycle (this mod now injects and
owns its own taskbar element directly, rather than being hosted by another
mod's widget stack).

## Relationship to `taskbar-widget-stack`

Not integrated (yet). This is a separate Windhawk mod - its own DLL,
injected independently into `explorer.exe`. See `PLAN.md` for the planned
integration approach (a cross-mod API, still to be designed) and the
known interim limitation: running this mod and `taskbar-widget-stack` at
the same time will very likely fight over the same taskbar position,
since both currently anchor flush against the left edge of the taskbar's
`RootGrid`.
