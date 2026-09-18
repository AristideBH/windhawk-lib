# Taskbar Widget Media Player

> **Note:** This mod is vibe-coded - built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

A minimal fork of [Salyts' Taskbar Fluent Media
Player](https://github.com/Salyts/Taskbar-Fluent-Media-Player) (MIT
licensed - see `LICENSE`), modified only to register itself as a widget
in [`taskbar-widget-stack`](../taskbar-widget-stack/README.md)'s pane
when that mod is installed and enabled, alongside
[`taskbar-widget-system-usage`](../taskbar-widget-system-usage/README.md).
Every feature - album art, full playback controls, the audio visualizer,
mini player popup, session switching, scrolling text, and all of the
original mod's extensive settings - is the upstream mod's own,
unmodified. This fork only adds a cross-mod registration path and a
fallback to the original standalone injection when the host isn't
present.

## Requirements

- Windows 11, 64-bit
- Windhawk v1.4 or later
- Only one copy of the player should run at a time - disable the
  original upstream `taskbar-fluent-media-player` mod if you enable this
  fork.

## Status

Not yet compiled or tested - see [`PLAN.md`](PLAN.md) for the design and
known risks. Expect a live-test-and-fix round, same as every other
cross-mod integration in this repo.

## Relationship to `taskbar-widget-stack`

If [`taskbar-widget-stack`](../taskbar-widget-stack/README.md) is
installed and enabled, this mod registers its player UI as one of its
widgets - sharing that mod's dots, snap-scroll, and right-click menu
instead of occupying its own separate spot on the taskbar. If
`taskbar-widget-stack` isn't installed, isn't enabled, or hasn't
registered yet, this mod behaves exactly like the original upstream mod
- injecting itself standalone, at whichever position its own settings
say to.

## Attribution

This mod is a derivative of Salyts' Taskbar Fluent Media Player,
MIT-licensed. See `LICENSE` in this folder for the original copyright
notice, kept intact as the license requires.
