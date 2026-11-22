# Transwitch UI component layer — Visual Engine 1.0

## Purpose

The component layer centralizes presentation without moving input handling or application logic into widgets. Hit rectangles, key mappings, workspace edits and launch dispatch remain owned by the launcher controller.

## Components

- **Surface:** theme-token fill, true rounded outline and optional three-layer low-alpha shadow.
- **Widget surface:** shared surface, top accent rail and optional cached radial glow when focused.
- **Button:** Glass, Rail, Minimal, Chip and Solid variants. Active, focused, hovered, pressed and disabled states are resolved by pure presentation rules.
- **Progress bar:** normalized progress with clamping for NaN, infinities and values outside 0..1.
- **Divider:** semantic one-pixel separator.

## Rules

- Components accept bounds and visual state; they never own input or decide actions.
- No component reads SD, calls a system service, waits on a worker or performs a network request.
- All SDL drawing and texture use remains on the render thread.
- A failed optional engine primitive falls back to a direct SDL implementation where a safe fallback exists.
- Existing aggregate initialization of the first four theme token fields remains source-compatible.
- Focus is visible through an outline/ring and accent treatment, not only a color change.
- Pressed/disabled feedback changes opacity only; it does not mutate hitbox geometry.

## Modal safety

Modal backdrops are composited on the normal window target. This layer must not allocate or switch SDL render-target textures for ordinary windows. Z-order and keyboard/touch ownership remain with SurfaceManager and the existing controller.

## Theme compatibility

0.1 and SOCOMA remain unchanged. Components consume semantic tokens supplied by the active palette; a later showcase theme will exercise the full token set without altering saved workspace layouts or application data.

## Tests

Host tests cover token normalization and pure button-state resolution. Geometry tests cover rounded spans. SDL renderer integration is covered by the native NRO/HOME applet builds and still requires physical-console visual validation.
