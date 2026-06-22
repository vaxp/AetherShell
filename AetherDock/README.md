# AetherDock

A lightweight, customizable dock for Wayland and X11, featuring live configuration updates, dynamic styling, and fluid mathematical animations.

## Configuration

AetherDock can be heavily customized by editing its state file. The file is automatically generated on the first launch and is located at:
`~/.config/vaxp/dock/dock_state.vaxp`

The configuration file uses a standard INI format. The best part is that any changes made to this file are applied **instantly** without needing to restart the dock!

### Example Configuration

```ini
[Dock]
Position=bottom
BackgroundColor=rgba(0, 0, 0, 0.300)
ContextMenuColor=rgba(8, 10, 14, 0.78)
IndicatorColor=#00fcd2
LaunchRingColor=#00fcd2
LaunchAnimation=1
```

### Options Explained

#### 1. Position
Controls where the dock is anchored on the screen.
- **Valid values:** `top`, `bottom`, `left`, `right`.

#### 2. Color Settings
There are two main formats you can use for colors. Understanding these formats is key to getting the look you want:

- **RGBA Format:** `rgba(R, G, B, Alpha)` (e.g., `rgba(0, 0, 0, 0.3)`). 
  - *Best used for backgrounds.* It allows you to control the transparency (Alpha channel) from `0.0` (fully transparent) to `1.0` (fully solid).
- **HEX Format:** `#RRGGBB` (e.g., `#00fcd2`). 
  - *Best used for indicators and highlights.* It provides solid, vibrant colors. 
  - Note: You can also use named colors like `red` or `blue`, though HEX is more precise.

**Color Properties:**
- `BackgroundColor`: The background color of the main dock pane. (Recommended format: RGBA for glass/transparent effects).
- `ContextMenuColor`: The background color of the right-click context menu. (Recommended format: RGBA).
- `IndicatorColor`: The color of the small dot that appears beneath running/open applications.
- `LaunchRingColor`: The color of the mathematical animation that plays around the application icon when it is currently launching.

#### 3. Launch Animations (`LaunchAnimation`)
Controls the visual animation style displayed when clicking an application to open it. All animations are drawn using lightweight, high-performance mathematical geometry rather than external images or GIFs.

- **Valid values:** `1` through `7`

**Available Animations:**
1. **Spinner (Default):** A classic segmented ring with varying opacities that rotates around the icon.
2. **Pulse Ripple:** An expanding sonar-like ripple that grows outwards from the center and fades away.
3. **Orbiting Dots:** Three glowing dots orbiting the application icon in a continuous circular path.
4. **Radar Sweep:** A scanning radar-like slice with a bright leading edge that rotates.
5. **Dashed Chase:** A finely dashed circle where the dashes themselves continuously race around the ring.
6. **Pendulum Bounce:** A glowing dot swinging smoothly back and forth like a pendulum at the bottom edge.
7. **Breathing Halo:** A solid glowing ring that smoothly pulses in thickness and brightness, creating a breathing effect.
