# Transwitch UI component layer

## Purpose

The UI component layer centralizes presentation without moving input handling or application logic into widgets. Hit rectangles, key mappings, workspace edits and launch dispatch remain owned by main.cpp.

## Components

- Surface: layered fill, inset edge and optional low-cost shadow. The existing opacity setting scales both the surface and its edge consistently.
- Button: Glass, Rail, Minimal, Chip and Solid variants. Active, focused and disabled states are resolved by pure presentation rules.
- Divider: one-pixel themed separator.

Components consume theme tokens (accent, focus and edge) and explicit per-control colors. No component owns SDL_Renderer or textures. All SDL drawing stays on the main render thread, and a direct SDL fallback remains available if a Visual Engine call fails.

## Migrated controls

The component layer is used by the shared soft-card helper and by the main navigation controls, overflow button, desktop tabs, action footer and library filter chips. Coordinates and sizes are unchanged, so their existing touch hit regions and controller navigation remain compatible.

Game artwork remains on the existing asynchronous IconCache and rounded-image mask path. Widget content remains information-first and uses its measured footprint; this migration changes the card/surface presentation, not widget data or action routing.

## Themes

The existing v0.1 and SOCOMA theme palettes remain source data. Components receive the active accent/focus/edge tokens each frame, so changing theme or appearance settings does not recreate the component system or invalidate artwork caches.

## Motion

`include/desknx/UiMotion.hpp` provides bounded cubic easing, wrap-safe transition progress, fade alpha and a low-cost ping-pong pulse. These are time-based helpers; they allocate no memory and create no threads.

The existing screen transition keeps its 150 ms duration but now uses eased alpha. Selected game/NRO tiles now use the existing selection-style setting: outline, accent rail, pulse ring or corner brackets. When motion effects are enabled, the selection pulse is smoothed and remains bounded; when disabled, the selected-state alpha is static.

## Tests

Host tests cover pure button-state resolution: active, focused, disabled and each visual variant. SDL drawing itself remains covered by the native Switch build and requires a hardware visual pass for final confirmation.
