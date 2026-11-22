# Stage 4A — Workspace placement and resource streaming

## Changes
- Dragging or moving a tile prefers the requested origin. If that origin is occupied (while the requested origin is still a valid grid position), placement scans forward in reading order and wraps once. Off-grid requests remain rejected.
- The tile's previous origin is excluded from fallback placement. A truly full workspace returns failure without mutating any tile.
- The production D-pad/touch path and the HomeNX workspace provider use the same placement policy.
- Artwork prefetch context is now tied to workspace/list/catalogue/layout changes, not focus coordinates. Moving selection no longer cancels pending decoder work. Visible tile requests continue to promote matching queued or in-flight work.
- Artwork entries remain in the bounded GPU LRU for up to 120 seconds of inactivity; the existing 64-entry / 32 MiB limits remain unchanged.
- Text raster textures are retained in a bounded 512-entry cache. The selected font file itself is still loaded asynchronously on startup or when the user changes it; it is not reopened for each workspace switch.

## Validation
- Extended grid-edit coverage for collision relocation, board-edge wraparound, D-pad policy parity, full-grid atomic failure, and identity/geometry preservation.
- Extended artwork request policy coverage to assert that focus movement is not part of the prefetch rebuild context.
- Both host and native Switch builds are covered by the repository workflows.

## Scope and limitations
- This increment does not redesign widget content or SOCOMA. Those need separate, larger visual changes and regression review so the 0.1 theme and existing SVG/icon work remain intact.
- The cache remains explicitly bounded to protect Switch memory. Resident counts, texture bytes, queue latency, upload time, text cache hits/misses, and workspace-switch latency are already exposed in Runtime Diagnostics for measurement on hardware.
- No claim of physical Nintendo Switch validation is made here.