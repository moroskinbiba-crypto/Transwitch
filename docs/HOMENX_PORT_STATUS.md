# HomeNX → Transwitch port status

Checked against HomeNX `master` at `63205f0d2ad855822ed9f3c5c3201d4bd6245505` and Transwitch `main` at `16594497ca8586115898b5a274826798ddb1e81c` (18 November 2026).

This is an implementation ledger, not a claim that every HomeNX feature should be copied. Preserve Transwitch's Android-style workspaces and UI; port the subsystem mechanisms that solve concrete gaps.

## Status

| Subsystem | HomeNX reference | Transwitch state | Decision |
|---|---|---|---|
| Installed title catalogue | `projects/menu/src/launcher/AppListLoader.cpp`, `projects/common/include/switchu/control_cache.hpp`, daemon catalogue | `source/catalog.cpp` already enumerates NS records, queries views, reads NACP/icons, warm-starts cache and has stale-cache fallback | Keep current catalogue; improve event-driven refresh and background metadata fills. Do not replace with HomeNX format wholesale. |
| Icon streaming | `projects/menu/src/launcher/IconStreamer.cpp` | Async worker port in branch `port/homenx-async-icon-streamer`; render thread only creates SDL textures | First port milestone. Run CI, then hardware perf/memory tests. |
| Artwork / covers | `projects/cards/src/Library.cpp`, `CardsActivity.cpp`, `docs/cards-artwork-performance.md` | `SteamGridDbCache.cpp` and `IconCache.hpp`; no unified background decode/cache admission across all paths | Next: unify icon and cover request queues, add cancellation, pin selected tile and instrument frame/upload/memory timings. |
| Nextendo presence | `projects/cards/src/Library.cpp`, `CardsActivity.cpp` | `source/NextendoPlayers.cpp`, `include/desknx/NextendoPlayers.hpp`, integrated into native cards | Already present; preserve it and add an explicit Settings opt-out later. |
| Game launch lifecycle | `projects/daemon/src/app_manager.cpp` | `projects/sSystem/source/Application.cpp` and backend state machine | Existing implementation is substantial. Compare transition edge cases and hardware-test; do not wholesale replace. |
| Homebrew Application Mode | `projects/loader`, daemon ECS / host-title flow | `projects/hbloader` and ECS exist, but README says donor-title Application Mode is deliberately disabled | High-priority missing capability. Adapt the proven host-title flow after validating exact NPDM/ECS/loader requirements and recovery. |
| Workspaces / persistence | HomeNX grid, `GridModel`, `WidgetStore` | Native production path has real desktops and transactional files; separate `HomeNXWorkspaceProvider/FileStore` abstractions and tests also exist | Unify the duplicate model/store layers without changing user layout or auto-placing new apps. |
| Grid navigation | `projects/menu/src/widgets/GridNavigation.cpp` | Native workspace input/model has its own navigation logic; adapter tests exist | Compare geometry/focus behavior and port the nearest-direction selection algorithm where it improves multi-size tile focus. |
| Widgets | `WidgetStore.cpp`, `WidgetModel.cpp` | Native widgets are tile kinds in the desktop model; separate adapter work exists | Keep Transwitch grid semantics; unify widget persistence and per-workspace constraints. |
| Lock screen / wake | `lock_screen_policy.hpp`, `LockScreen.cpp`, daemon wake path | No equivalent complete production feature identified | Port only after backend sleep/wake state is stable. |
| MTP / storage manager | `projects/cards/src/Mtp.cpp`, StorageTab | No equivalent production service identified | Optional later module; must stay outside render loop. |
| Updater | NXToggle / ReleaseUpdater | No equivalent transactionally safe on-device updater identified | Later; preserve checksum, staged install, backup and rollback rules. |
| Discord / Spotify / Social | HomeNX service modules | Some independent feature scaffolding, but not equivalent complete services | Separate modules only; no networking in UI/render thread. |

## Delivery order

1. **Artwork worker:** finish compile/CI, then test on a physical Switch. Verify no filesystem/decode work occurs in `IconCache::get()`, no stale result is uploaded after icon-pack change, memory stays bounded, and the selected/visible images remain correct.
2. **Artwork manager:** consolidate icon and SteamGridDB cover loading into one bounded scheduler; prioritise visible/selected tiles, prefetch immediate neighbours, cancel obsolete tasks, and add cache/accounting diagnostics.
3. **Workspace unification:** make the tested Provider/FileStore layer the actual persistence/mutation API of the production native UI. Add migration/backup tests before removing old code.
4. **Launch/backend parity:** audit all lifecycle transitions against HomeNX, add host tests for state changes/timeouts, then hardware-test HOME/game/sleep/wake/repeated launch.
5. **Homebrew Application Mode:** implement donor-title lifecycle only when all failure/rollback paths are testable; keep current safe applet-mode fallback.
6. **Optional features:** lock screen, MTP, updater, media/social services as separately testable components.

## Non-negotiable constraints

- Do not replace Transwitch's workspaces with HomeNX's paginated grid or CardsNX.
- Do not add a second system lifecycle owner or a second SDL event/render loop.
- NS enumeration, NACP reads, filesystem traversal, HTTP and image decoding must not run on the render thread.
- Keep all cache/queue/memory limits explicit and instrumented; no unbounded background jobs.
- Never show a fabricated online count or treat failed network responses as zero.
- Preserve GPL-3.0 notices for any source copied or adapted from HomeNX. Current IconCache worker is an SDL2 adaptation of the documented IconStreamer pipeline; the implementation was written for Transwitch rather than copying the HomeNX nxui class.
- CI compilation does not prove Switch runtime stability. Keep experimental qlaunch installation disabled until hardware recovery tests pass.


## CI update — 18 November 2026

The asynchronous IconCache port now passes the repository's cross-build checks on branch `port/homenx-async-icon-streamer`, commit `b303741d257d9ed8f62ce529f6acb453807c09eb`:
- `Build DeskNX` run `44252212096`: success, including the production HOME library applet and native standalone-path compile-check.
- `Build Nintendo Switch NRO` run `44252212151`: success, but this is the separate WebKit-wrapper build and does not exercise the SDL icon worker.
- Physical Switch testing remains pending. In particular, do not infer worker stability or Atmosphère compatibility from these builds.
