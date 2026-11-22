# Stage 4C — Resize placement parity

## Problem

The touch/HomeNX provider already relocated a resized tile when its requested expanded bounds overlapped another tile or no longer fit at the requested origin. The controller/D-pad operation only checked the original origin and returned failure. This made the same resize gesture behave differently depending on input method.

## Change

- `workspace_ops::resizeSelectedTile` now uses the same placement policy as the provider.
- It preserves the original origin when the resized shape fits.
- When that origin cannot fit, it scans forward in reading order and wraps once for the first valid free origin.
- The current tile's old origin is excluded from fallback, so a full/blocked workspace cannot report a no-op resize as success.
- If no valid position exists, the operation fails without changing tile geometry, position or selection.
- Existing supported size ladder remains unchanged, including 2×3 portrait and rejection of unsupported 3×3.

## Validation

A regression test grows a 1×1 Clock widget to 2×1 while a blocker occupies the adjacent cell. The controller path must relocate it to the next available origin, preserve dimensions, keep focus on the resized widget and avoid overlap. The existing no-room test continues to assert failure.

Native Switch CI and physical device testing are separate acceptance steps; this document does not claim hardware validation.
