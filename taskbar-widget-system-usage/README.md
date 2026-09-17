# Taskbar System Usage

> **Note:** This mod is vibe-coded - built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

Three small horizontal bars injected into the Windows 11 taskbar - CPU,
RAM, and GPU usage, label on the left, live percentage on the right.
Layout, spacing, sizing, and color are all configurable through Windhawk's
own settings UI - show/hide the label, bar, or percentage independently;
font size, bar thickness/width, row and label/percentage gaps; a min/max
width the widget adapts to; and a bar color that's either the Windows
accent color, a custom hex color, or a threshold mode that switches to a
warning/critical color as usage climbs.

## Requirements

- Windows 11, 64-bit
- Windhawk v1.4 or later

## Status

Live-tested standalone (bars render and update correctly). Cross-mod
integration with `taskbar-widget-stack` (below) is implemented but new and
not yet extensively live-tested - see [`PLAN.md`](PLAN.md).

## Relationship to `taskbar-widget-stack`

If [`taskbar-widget-stack`](../taskbar-widget-stack/README.md) is installed
and enabled, this mod's bars register themselves as one of its widgets -
sharing that mod's dots, snap-scroll, and right-click menu instead of
occupying a separate spot on the taskbar. This works across two genuinely
separate Windhawk mods (two DLLs, same `explorer.exe` process) through a
small hand-maintained C ABI that `taskbar-widget-stack` publishes on the
taskbar's own window (see `PLAN.md` for the design).

If `taskbar-widget-stack` isn't installed, isn't enabled, or hasn't
registered yet, this mod falls back to injecting its own standalone taskbar
element, unchanged from its original behavior.
