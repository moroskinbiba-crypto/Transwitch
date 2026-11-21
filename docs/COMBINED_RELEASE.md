# Combined Transwitch release

This branch consolidates the current launcher features into one source tree based on the last successful premium-widgets / SteamGridDB baseline. Experimental and superseded branches are not merged blindly: overlapping edits are consolidated by feature and checked through the common build/test workflows.

## Included

- Multi-workspace desktop UI, app/library browsing, favorites and recent launches.
- Installed-game and Homebrew catalog discovery, incremental NRO inventory, safe cache refresh and metadata recovery.
- SteamGridDB cover lookup/cache and custom per-game artwork.
- Nextendo online presence, per-title status and popular-games view.
- Configurable widgets, widget editing, storage/system information and diagnostics.
- Music player, profile/quick-settings feature modules already included by the base release.
- SOCOMA alongside the preserved v0.1 theme, SVG interface icons, adjustable header/Dock/surface/selection styling.
- Custom font file preflight, asynchronous SD-card reads, rollback to the previous font on failure.
- Persistence backup/restore, workspace state and performance-oriented cache limits.

## Build outputs

The main CI workflow builds the standalone native NRO, a complete SD-card package and the HOME library applet. The experimental qlaunch/system-backend bundle remains a separate build target and must not be treated as a production HOME replacement.

CI green confirms compilation and host regression tests. It does not replace smoke testing on a physical Switch, especially suspend/resume, launch/return, SD-card removal and user-font replacement.
