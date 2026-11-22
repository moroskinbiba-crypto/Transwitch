# Transwitch UI component layer — Visual Engine 1.0

## Purpose

The component layer centralizes presentation without moving input handling or application logic into widgets. Hit rectangles, key mappings, workspace edits and launch dispatch remain owned by the launcher controller.

## Components

- **Surface:** theme-token fill, true rounded outline and optional three-layer low-alpha shadow.
- **Widget surface:** shared surface, top accent rail and optional cached radial glow when focused.
- **Artwork tile:** shared elevation/surface and a cached bottom-up gradient scrim. It receives already-measured bounds and does not own game selection or hit testing.
- **Button:** Glass, Rail, Minimal, Chip and Solid variants. Active, focused, hovered, pressed and disabled states are resolved by pure presentation rules.
- **Progress bar:** normalized progress with clamping for NaN, infinities and values outside 0..1.
- **Divider:** semantic one-pixel separator.

## Tile contract

`TileRenderer` handles the visual shell for cover-art tiles. The launcher still owns artwork lookup and the original fit policy (cover/contain), text shaping, badges, selection style and controller/touch hitboxes. The title scrim uses the VisualEngine's cached vertical gradient so that text contrast increases toward the bottom of the cover instead of placing a flat dark rectangle over the whole label band.

`TileVisualRules.hpp` is SDL-independent. It measures the title strip, line offsets, maximum character count and safe corner radius for the actual tile bounds. The host test covers standard, wide and constrained tile sizes.

## Rules

- Components accept bounds and visual state; they never own input or decide actions.
- No component reads SD, calls a system service, waits on a worker or performs a network request.
- All SDL drawing and texture use remains on the render thread.
- A failed optional engine primitive falls back to a direct SDL implementation where a safe fallback exists.
- Existing aggregate initialization of the first four theme token fields remains source-compatible.
- Focus is visible through an outline/ring and accent treatment, not only a color change.
- Pressed/disabled feedback changes opacity only; it does not mutate hitbox geometry.
- Visual migrations must not change the launcher model, workspace persistence format, selected item identity or touch/controller hit rectangles.

## Modal safety

Modal backdrops are composited on the normal window target. This layer must not allocate or switch SDL render-target textures for ordinary windows. Z-order and keyboard/touch ownership remain with SurfaceManager and the existing controller.

## Theme compatibility

0.1 and SOCOMA remain unchanged. Components consume semantic tokens supplied by the active palette; a later showcase theme will exercise the full token set without altering saved workspace layouts or application data.

## Tests

Host tests cover token normalization, pure button-state resolution, rounded geometry, coalesced persistence scheduling and tile label layout. SDL renderer integration is covered by the native NRO/HOME applet build and still requires physical-console visual validation.
