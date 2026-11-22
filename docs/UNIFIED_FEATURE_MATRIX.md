# Unified Transwitch feature matrix

**Audit baseline:** `recovered/unified-best-20261122`  
**Audited commit:** `888898f70aec3cd7973e35912bebb0fc3ae0494b`  
**Default branch:** `main` (`6498f9b26760479e0935f745caf649f44a3fe3bc`)  
**Date:** 22 November 2026

This matrix distinguishes code that is in the production path from code that merely exists as a helper, prototype, test fixture or roadmap item. "Integrated" means the current native production path contains the feature; it does not imply physical-console validation.

Status key:

- **Implemented** — present in the current production path and covered by relevant host/CI checks.
- **Partial** — useful code exists, but the end-to-end feature or architectural unification is incomplete.
- **Prototype/domain** — data model/helper/tests exist; not a complete production-facing feature.
- **Not implemented** — explicitly absent or still planned.
- **Hardware gate** — compilation/tests exist, but Switch runtime acceptance is outstanding.

## Core launcher

| Area | Current state | Evidence / origin | Decision |
|---|---|---|---|
| Native standalone NRO | Implemented | Root `Makefile` / `Makefile.switch`; `source/main.cpp`; NRO asset validation | Keep as the primary release path. No WebKit/React runtime in this target. |
| HOME library applet build | Implemented as a build target; runtime gate remains | `projects/DeskNXMenu/Makefile` selects production `source/main.cpp`; `scripts/check-home-applet-entrypoint.sh` guards the selection | Preserve; distinguish the diagnostic applet artifact from an install-ready HOME replacement. |
| Experimental qlaunch / ECS backend | Partial; hardware gate | `projects/sSystem`, `projects/hbloader`, `projects/common`; dedicated system workflow | Keep isolated. Do not install on a daily-use console before full recovery testing. |
| Game launch / return lifecycle | Partial; hardware gate | `projects/sSystem/source/Application.cpp`, `projects/sSystem/source/main.cpp`, IPC state machine | Audit transitions and verify on device: launch, HOME, return, repeat, sleep/wake, failures. |
| User/profile handoff | Partial; title-policy logic present | Native launcher and backend; NACP `StartupUserAccount` handling described in HOME integration docs | Host tests are not proof of game handoff. Test real titles and preserve the `None` account mode. |
| HOME donor-title Application Mode | Not implemented | `docs/HOME_INTEGRATION.md` explicitly says donor-title Application Mode is disabled | Separate future project; no implied support in release notes. |

## Catalog, scanning and metadata

| Area | Current state | Evidence / origin | Decision |
|---|---|---|---|
| Installed title enumeration | Implemented | `source/catalog.cpp`; NS records, views, NACP and icon reads; SLauncher enumeration history | Keep current production path; no wholesale replacement with older NS branches. |
| Homebrew NRO scan | Implemented with bounded incremental inventory | `include/desknx/NroScanner.hpp`, `source/catalog.cpp`, `tests/nro_scanner_test.cpp` | Preserve incomplete-scan semantics and last-known-good cache. Hardware-test large SD cards and `DT_UNKNOWN`. |
| Incremental catalogue cache | Implemented; newer than older perf branch | Inventory/directory signatures, NRO metadata cache, manifest-completeness checks, limits and truncation guards in `source/catalog.cpp` | The four unique commits in `perf-stability-iteration` are functionally present or superseded. Do not copy its older catalogue implementation. |
| Early installed-game visibility | Implemented | Catalogue seed is published before full SD metadata scan; `source/catalog.cpp` and native UI worker | Preserve; verify cold start and transient SD failure on console. |
| NACP compressed title blocks | Implemented | `include/desknx/NacpTitleBlock.hpp`, tests and catalogue integration | Preserve and keep regression tests. |
| Installed version / storage metadata | Implemented | `ApplicationSize.hpp`, `GameRecord::updateStatusAvailable`, display-version and installed-size UI | Verify live refresh and unavailable-state presentation. |
| Search, filter, sort and pagination | Implemented | `source/catalog_view.*`, catalogue UI and tests | Preserve controller/touch parity. |
| Play statistics | Partial | PDM query path and explicit availability/error logging | Continue to show unavailable rather than inventing counters; test actual PDM service on hardware. |

## Artwork, fonts and rendering

| Area | Current state | Evidence / origin | Decision |
|---|---|---|---|
| SVG icon registry and cache | Implemented | `include/desknx/IconRegistry.hpp`, `source/SvgIconCache.hpp`, `assets/icons`, SVG validator | Protected baseline. No fallback to coloured initials or duplicate NanoSVG renderer. |
| Visual Engine 2.0 | Implemented foundation | `source/VisualEngine.hpp`, `source/UiComponents.hpp`, `source/TileRenderer.hpp`, `include/desknx/VisualTokens.hpp` | Continue incremental component migration. |
| Surface ownership / modal focus | Implemented | `SurfaceManager`, `ModalLayer`, `refreshSurfaceManager()`; input uses top surface | Keep as the only input/z-order owner. Do not add old `ModalStack` beside it. |
| Common geometry / hit targets | Partial | `VisualContracts.hpp` contains workspace dock/footer, semantic text, motion and delete-confirmation geometry | Extend to the remaining dialogs, side panels, context menus and repeated rows without changing established hit regions. |
| Shared control states | Partial | `UiComponents.hpp`, `UiComponentRules.hpp`; current UI uses some shared buttons/fields/cards | Compare PR #56 `ControlComponents.hpp` at component level; port missing contracts selectively, not as a duplicate renderer. |
| Window geometry library | Partial / candidate | Current branch has no `WindowComponents.hpp`; PR #56 adds reusable dialog, side-panel, context-menu and row geometry | High-value candidate for next VE stage. Reconcile with current `SurfaceManager` and component adapter first. |
| Async font raster queue | Implemented, bounded | `source/FontRenderer.hpp`: max 192 pending jobs, 2 new texture builds per frame, 2.5 ms soft raster pump budget, stale-request pruning | Preserve renderer-thread SDL texture ownership and thread-safety boundary. Do not assume TTF rasterization itself is off-thread. |
| UTF-8 / Cyrillic fallback | Implemented | `BitmapFont.hpp`, `VisualContracts.hpp`, FontRenderer; host tests | Hardware-check legibility at all scales and missing-font paths. |
| Motion settings | Implemented, first slice | `MotionController` in `VisualContracts.hpp`, Full / Reduced / Low Performance settings and persistence | Extend to modal/panel/workspace transitions only after compositing validation. |
| Artwork priority scheduling | Implemented in IconCache | `ArtworkRequestPolicy.hpp`, `IconCache.hpp`; up to 48 workspace prefetch candidates; cancel/promote behavior | Preserve visible/focused priority and bounded queues. |
| GPU texture upload budget | Implemented | IconCache: 2 uploads/frame, 6 MiB/frame, 32 MiB texture-cache limit | Keep measured limits until real-device profiling supports changes. |
| Workspace backgrounds | Separate bounded worker/cache | `WorkspaceBackgroundCache.hpp` | Not yet one scheduler with icons and SteamGridDB covers. Candidate for Unified Streaming Engine. |
| SteamGridDB | Implemented path, hardware/network gate | `SteamGridDbClient`, `SteamGridDbService`, cache and UI integration | Keep failures explicit; test credentials, offline mode, cache invalidation and rate limiting. |
| White-screen / modal compositing | Automated renderer recovery exists; hardware visual acceptance open | `RendererStatePolicy.hpp`, `RendererStateRecovery.hpp`, frame-boundary checks and SDL software-renderer regression | Do not claim fixed solely from CI. Reproduce and capture all modal/window paths on Switch. |

## Workspaces, widgets and persistence

| Area | Current state | Evidence / origin | Decision |
|---|---|---|---|
| Multi-workspace desktop model | Implemented | Native production UI, workspace operations, dock and active workspace persistence | Preserve Android-style desktop model and empty cells. |
| Drag, resize, remove, reorder | Implemented in native path | Native input code and HomeNX grid/edit tests | Keep gesture state machine; hardware-test touch/controller handoff and large tiles. |
| HomeNX workspace adapters / FileStore | Present and host-tested, not the production storage owner | `HomeNXWorkspaceProvider`, `HomeNXWorkspaceFileStore`, adapters and tests; current production `source/main.cpp` still has its own model/load/save path | P1 architecture issue: choose one authoritative production model/store and migrate with explicit format compatibility. Do not delete either path before call-site and migration audit. |
| Production layout/settings/history persistence | Implemented directly in `source/main.cpp`; tests incomplete at production boundary | Atomic temp/backup/commit helpers and recovery logic; `docs/AGENT_CHANGELOG.md` notes `PersistenceStore.*` is not called by the production UI | P0/P1: add fault-injection tests for the actual production paths, including every SD commit/rename failure and restart recovery. |
| JSON PersistenceStore | Utility module / not current production owner | `source/PersistenceStore.*` and `tests/persistence_store_test.cpp` round-trip JSON | Do not call this "production persistence" until wired to the actual UI paths. |
| Widget configuration/editor | Partially integrated | `WidgetConfig.hpp`, `WidgetLayout.hpp`, widget editor states and tests | Audit every widget kind for real data source, loading/stale/offline/error states and edit persistence. |
| Premium widget domain modules | Partial | Host tests and helper modules exist; not all are complete user-facing services | Keep incomplete features clearly labelled. |
| Music / achievements / social / theme shop | Prototype/domain only | `MusicPlayer.hpp`, `AchievementPacks.hpp`, `SocialFeatures.hpp`, `ThemeShop.hpp`; `docs/FEATURES.md` disclaims production completion | Do not advertise as implemented. Separate module projects after core stability. |
| Nextendo presence / catalog | Integrated; service-dependent | `NextendoPlayers.*`, `NextendoGameCatalog.*`, UI and diagnostics | Preserve honest online/error states. Friends list still requires consented OAuth and is not implemented. |

## System and overlay integration

| Area | Current state | Evidence / origin | Decision |
|---|---|---|---|
| System telemetry | Implemented, broader than PR #56 SensorService | `SystemTelemetry.hpp` handles battery/Joy-Con, temperature, clocks, network, quick settings and diagnostics | Do not add parallel `SensorService`; test session recovery after sleep on hardware. |
| SD storage statistics | Separate background worker | `storageStatsThreadMain()` runs `statvfs` outside rendering | Preserve; the old SensorService also owns this concern and would duplicate it. |
| IPC validation / heartbeat | Implemented and more developed than old audit branch | `ProtocolRules.hpp`, `ProtocolParser.cpp`, `MenuLiveness.hpp`, backend state-machine tests | Keep strict size/version/command checks and bounded Busy semantics. |
| ECS worker recovery | Implemented | `ExternalContent.cpp` uses exited-state publication, join/destroy/recreate, registration rollback and separate log | Keep ownership semantics; still needs real Atmosphère tests. |
| NPDM capability audit | Missing as a tracked repeatable tool | Old `audit/npdm-capability-inventory` adds `scripts/audit_npdm_capabilities.py` and review doc | Candidate P1 release/security audit tool; must remain read-only and not silently widen permissions. |
| Nextendo Tesla overlay | Implemented as separate NRO/OVL target | `overlays/NextendoOnline` and its workflow | Keep independent from launcher; overlay CI is separate. |
| Install / uninstall / recovery scripts | Implemented with host recovery tests | `scripts/install-system.sh`, `uninstall-system.sh`, `tests/test-recovery.sh` | Add test matrix for every interruption point; physical recovery test remains required. |
| Build manifest / firmware declaration | Partial | Toolchain refs exist in system workflow; default devkit container still `latest`; no signed build manifest for standalone package | Add pinned toolchain/container and manifest with commit, tool versions, target refs and artifact SHA-256. |

## Explicitly not in the release feature set

- Donor-title / full-RAM Homebrew Application Mode.
- Nextendo friends list / OAuth.
- MP4 or video wallpaper.
- Achievements, MTP, Discord, Spotify/music service and theme shop.
- Production updater and lock screen.
- Replacement of stock HOME proven safe on a physical console.

See `docs/FEATURES.md` and `docs/HOME_INTEGRATION.md` before adding public feature claims.
