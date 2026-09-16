# Taskbar Widget Stack

> **Note:** This mod is vibe-coded - built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

> **Prototype status:** this version stacks two placeholder panes to
> validate the overlay/scroll/snap/indicator mechanics, before real widget
> content and the widget SDK land. See [`PLAN.md`](PLAN.md) for the full
> architecture and roadmap.

Stacks multiple taskbar widgets vertically in one area next to the system
tray, switchable like an iOS widget stack:

- Snap-scroll between widgets via mouse wheel, drag, or dot-click - each
  independently toggleable in settings.
- Dot indicators on the left edge show how many widgets are enabled and
  which is active.
- Right-click the stack to enable/disable widgets and reorder them.

## Requirements

- Windows 11, 64-bit
- Windhawk v1.4 or later

## Status

Not yet tested live (no Windows machine in the development environment this
prototype was written in). Went through two live-tested-and-fixed rounds
already - an Explorer freeze on first activation, then an invisible
overlay that led to a rewrite from a Win32 overlay window to real XAML
injection into the taskbar's own visual tree - both documented in
`PLAN.md`'s "Incident" sections. See `PLAN.md` → "Verification" and "Next
steps" for what's still open.
