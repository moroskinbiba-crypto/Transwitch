# Unified preview build

This branch is the single hardware-test candidate assembled from the Stage 3B base plus Stages 4A, 4B, 4C and 5A.

## Included work

- Workspace moves onto occupied cells scan forward for a free origin, while off-grid moves remain rejected.
- Controller and touch resize paths share the same collision-aware relocation policy and fail atomically if the grid has no space.
- Artwork prefetch does not restart on cursor movement; bounded artwork and text caches retain recently used resources longer.
- Widgets use six explicit dimensions: 1x1, 2x1, 3x1, 2x2, 3x2 and 2x3. Each supported widget has size-specific content.
- SOCOMA is a separate flat/modular visual system. The existing 0.1 Frosted Glass path remains available.
- Existing SVG assets, SVG cache/fallback and full-cover game/homebrew tiles are preserved.

## Build

The `Build experimental DeskNX HOME backend` workflow is the authoritative Switch build for this preview. A successful run assembles `DeskNX-system-experimental.zip` with the SD deployment tree, HOME applet, hbloader, qlaunch backend and install/uninstall scripts.

Do not install a preview artifact from a failed, cancelled or still-running workflow. The package is intended for controlled hardware testing before any merge to `main`.

## Validation

Host regression tests cover workspace placement, resize relocation, all six widget layouts, SOCOMA/0.1 policy separation, storage persistence/migration, runtime lifecycle and IPC. CI native builds cover the selected libnx/Atmosphere toolchain and target binaries. Physical Nintendo Switch testing is still required.

## Branch safety

This preview targets the Stage 3B work branch through draft PR #69. It does not modify `main`.