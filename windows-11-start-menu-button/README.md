# Windows 11 Start Button Customizer

**Windows 11 Start Button Customizer —** is a [Windhawk](https://windhawk.net/) mod that lets you restyle the Windows 11 taskbar Start button's icon — swap it for your own image, or recolor the stock icon with a diagonal gradient shade and a click shimmer — all while keeping its native hover/press animation intact.

![video](https://i.imgur.com/Q8aFc4p.gif)

## Installation

1. Install [Windhawk](https://windhawk.net/).
2. Open **Windhawk** and go to **Explore** → **Search**.
3. Enter **Windows 11 Start Button Customizer** in the search field.
4. In the results, select **Windows 11 Start Button Customizer**.
5. Click the "Install" button.

Not published to the Windhawk store yet? Open **Windhawk** → **Explore** → **+** (create a new mod), paste the contents of [`windows-11-start-menu-button.wh.cpp`](windows-11-start-menu-button/windows-11-start-menu-button.wh.cpp), and save.

### Key Features:

- **Three Icon Modes —** System default (untouched), Custom icon (your own PNG/ICO), or Recolor (tint the stock icon).
- **Gradient Shading —** A diagonal light-to-dark tint across the icon's four tiles, echoing the original flag icon's own shading, with independently tunable light-side and dark-side strength.
- **Hover & Press Glow —** The icon brightens on hover, press, or while the Start menu is held open, with configurable boost amount and fade speed.
- **Click Shimmer —** A one-shot highlight sweep plays across the icon on every click, staggered per-tile so it reads as a single diagonal wave rather than a flat flash. Sweep color, duration, and width are all configurable — use a custom color or let it auto-lighten from the icon color.
- **Independent Toggles —** Gradient shading and click shimmer are separate settings — shimmer works on a flat-colored icon, gradient shading works with shimmer off, or run both together.

### Settings Overview:

- **Icon mode** — System default / Custom icon / Recolor.
- **Custom icon** — image file path (PNG or ICO).
- **Recolor** — icon color, plus three sub-sections:
  - **Gradient shading** — on/off, light side strength, dark side strength.
  - **Click shimmer** — on/off, shimmer color (or auto-lighten amount), sweep duration, sweep width.
  - **Hover & press glow** — extra brightness, shadow reduction, fade speed.

---

### Report a Bug

If you encounter any issues or have a feature suggestion, please open a report on the project's GitHub page:
👉 **[Report an Issue on GitHub](https://github.com/AristideBH/windhawk-lib/issues)**

Author - [@AristideBH](https://github.com/AristideBH)

## License

This project is licensed under the MIT License - see the LICENSE file for details.
