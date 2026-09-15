# Windows 11 Start Button Tinter

**Windows 11 Start Button Tinter —** is a [Windhawk](https://windhawk.net/) mod that lets you recolor the Windows 11 taskbar Start button's icon, with a diagonal gradient shade and a click shimmer, all while keeping its native hover/press animation intact.

> **Note:** This mod is vibe-coded — built largely with AI assistance and
> tested manually by the author, without a full independent code audit. Use
> at your own judgment, and please report anything odd via GitHub Issues.

<video src="demo/demo-recolor-silver.mp4" controls width="480">Silver recolor demo</video>
<video src="demo/demo-recolor-flat-shimmer.mp4" controls width="480">Flat color + click shimmer demo</video>

## Installation

1. Install [Windhawk](https://windhawk.net/).
2. Open **Windhawk** and go to **Explore** → **Search**.
3. Enter **Windows 11 Start Button Tinter** in the search field.
4. In the results, select **Windows 11 Start Button Tinter**.
5. Click the "Install" button.

Not published to the Windhawk store yet? Open **Windhawk** → **Explore** → **+** (create a new mod), paste the contents of [`windows-11-start-menu-button.wh.cpp`](windows-11-start-menu-button/windows-11-start-menu-button.wh.cpp), and save.

### Key Features:

- **Two Icon Modes —** System default (untouched) or Recolor (tint the stock icon).
- **System Accent Color —** Optionally follow Windows' current accent color instead of a fixed hex value, updating live when you change it in Settings.
- **Gradient Shading —** A diagonal light-to-dark tint across the icon's four tiles, echoing the original Windows icon's own shading, with independently tunable light-side and dark-side strength.
- **Hover & Press Glow —** The icon brightens on hover, press, or while the Start menu is held open, with configurable boost amount and fade speed.
- **Click Shimmer —** A one-shot highlight sweep plays across the icon on every click, staggered per-tile so it reads as a single diagonal wave rather than a flat flash. Sweep color, duration, and width are all configurable — use a custom color or let it auto-lighten from the icon color.
- **Independent Toggles —** Gradient shading and click shimmer are separate settings — shimmer works on a flat-colored icon, gradient shading works with shimmer off, or run both together.

### Settings Overview:

- **Icon mode** — System default / Recolor.
- **Recolor** — icon color (or "use system accent color" toggle), plus three sub-sections:
  - **Gradient shading** — on/off, light side strength, dark side strength.
  - **Click shimmer** — on/off, shimmer color (or auto-lighten amount), sweep duration, sweep width.
  - **Hover & press glow** — extra brightness, shadow reduction, fade speed.

### Requirements

- Windows 11 25H2, build 26200.9445 or later, 64-bit
- Windhawk v1.4 or later

---

### Report a Bug

If you encounter any issues or have a feature suggestion, please open a report on the project's GitHub page:
👉 **[Report an Issue on GitHub](https://github.com/AristideBH/windhawk-lib/issues)**

Author - [@AristideBH](https://github.com/AristideBH)

## License

This project is licensed under the MIT License - see the LICENSE file for details.
