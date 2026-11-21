# Combined Transwitch release

This branch consolidates compatible launcher features from the active feature branches into one tested source tree. Conflicting and superseded experiments are not merged blindly; alternative renderers and the qlaunch replacement remain isolated targets.

## Included launcher features

- Multi-workspace desktop UI, app/library browsing, favorites and recent launches.
- Installed-game and Homebrew catalog discovery, incremental NRO inventory, safe cache refresh and metadata recovery.
- SteamGridDB cover lookup/cache and per-game artwork overrides.
- Nextendo online presence, per-title status and popular-games view.
- Configurable widgets, widget editing, storage/system information and diagnostics.
- Music player, profile carousel and quick-settings feature modules.
- HomeNX grid/workspace adapters, touch gestures, rollback-safe workspace file store and portrait 2×3 game tiles.
- SOCOMA alongside the preserved v0.1 theme, SVG interface icons, adjustable header/Dock/surface/selection styling.
- Custom font preflight validation, asynchronous SD-card reads, rollback to the previous font on failure.
- Persistence backup/restore, workspace state and performance-oriented cache limits.

## Build outputs

The main CI workflow builds the standalone native NRO, a complete SD-card package and the HOME library applet diagnostic artifact. The experimental qlaunch/system-backend bundle remains an independent target and is not a production HOME replacement.

## Separate Nextendo overlay

The Tesla/Ultrahand Nextendo Online overlay is included as an independent target under `overlays/NextendoOnline/`. It remains separate from DeskNX runtime and does not alter launcher startup, UI or process lifecycle. CI builds it into `nextendo-online.ovl` and publishes the standalone `NextendoOnline-ovl` artifact. Install only by copying the generated file to `sdmc:/switch/.overlays/nextendo-online.ovl` with a Tesla-compatible overlay loader.

CI green confirms compilation and host regression tests. It does not replace smoke testing on a physical Switch, especially suspend/resume, launch/return, SD-card removal and user-font replacement.
