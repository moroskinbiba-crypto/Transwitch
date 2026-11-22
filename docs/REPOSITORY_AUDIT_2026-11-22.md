# Complete Repository Audit — Transwitch

**Date:** 22 November 2026  
**Repository:** `moroskinbiba-crypto/Transwitch`  
**Audit baseline:** `recovered/unified-best-20261122` at `888898f70aec3cd7973e35912bebb0fc3ae0494b`  
**Comparison base:** `main` at `6498f9b26760479e0935f745caf649f44a3fe3bc`  
**Pull request:** [#58 — Unified DeskNX](https://github.com/moroskinbiba-crypto/Transwitch/pull/58)

## 1. Executive result

The recovered branch is a coherent native DeskNX build, not a complete consolidation of every feature ever proposed in the repository. The repository currently has **72 branches and 58 pull requests**. The recovered branch is **489 commits ahead of main and zero commits behind**, with 138 changed files (18,208 additions / 2,251 deletions).

The code-level review focused on all active PR metadata and the feature-bearing branches that contain distinct catalog, workspace, persistence, visual, system, Nextendo, SteamGridDB and HomeNX work. The remaining 72 branch names were inventoried. This is a repository-wide lineage and production-path audit; it is not a claim that every historical commit in all 72 refs was individually line-reviewed. Candidate branches with unique code were compared separately, and no automatic bulk merge was performed.

**Overall assessment:**
- The native standalone NRO, SVG visual system, catalog, artwork queues, runtime diagnostics, widget framework, workspaces and overlay build form a strong current base.
- Several headline changes from older feature branches are already present or replaced by more advanced implementations.
- The biggest remaining engineering gap is not "missing commits": it is the gap between standalone utility modules/tests and the code path actually used by production `source/main.cpp`.
- The highest-risk areas are production persistence recovery, duplicate workspace/persistence abstractions, incomplete reusable window geometry, independent artwork queues, and unverified Switch runtime behavior.
- The qlaunch/ECS replacement must remain experimental even though its current CI build passes. CI does not prove compatibility or recoverability on a target console.

## 2. Repository inventory and method

### 2.1 Counts at audit time

| Item | Observed |
|---|---:|
| Branches | 72 |
| Pull requests, all states | 58 |
| Open pull requests | 11 |
| Files in recovered tree | 233 blobs |
| Production/source files | 34 under `source/` |
| Public/internal headers | 50 under `include/` |
| Host test files | 46 under `tests/` |
| GitHub Actions workflows | 4 |
| Main production UI translation unit | 9,199 lines |

The branch comparison shows current `main` is an ancestor of the recovered branch: compare status `ahead`, ahead=489, behind=0. Do not confuse this with a semantic claim that every feature in every other branch has been integrated.

### 2.2 Tools and comparison method

- GitHub branch, PR, tree, commit and Actions APIs for the full ref/PR inventory.
- Three-way compare metadata (ahead/behind and changed file lists) for feature-bearing refs.
- Direct source inspection for the production entry point, catalogue, rendering, telemetry, storage, IPC and system backend.
- Test/workflow inspection to distinguish host-tested helpers from code used by the Switch build.
- CI status was checked on the exact baseline commit, not on a sibling commit.

Ahead/behind numbers across divergent historical branches are not a feature score. An older branch can show hundreds of commits behind while still carrying one useful change; each candidate was evaluated by file/function and by whether the current production path already has the behavior.

## 3. Production architecture found

### 3.1 Standalone native build

The standalone target is native C++/SDL2, built from `source/*.cpp` using the root `Makefile` and `Makefile.switch`. It is not the old React/WebKit prototype. The package checks embed NACP metadata and reject RomFS/WebKit markers. Runtime assets are packaged on SD, including the SVG icon set and fonts.

### 3.2 Optional HOME system path

The separate path consists of:
- production menu applet source selected from `source/main.cpp`;
- qlaunch/backend code under `projects/sSystem`;
- target-aware loader under `projects/hbloader`;
- common IPC/state logic under `projects/common`.

The simplified `projects/DeskNXMenu/source/main.cpp` is a prototype and must not become the selected production entry point. The current Makefile guard checks source ordering and required IPC/liveness/launch symbols.

### 3.3 Main UI and subsystem concentration

The production UI and much interaction/state orchestration still reside in `source/main.cpp` (9,199 lines). Support modules are extracted, but screen construction, much input dispatch, settings, workspace mutations and production persistence still converge in that file. This is now the principal architecture and reviewability bottleneck.

Recommended extraction order is behavior-preserving: first pure geometry/state contracts, then workspace controller/persistence adapters, then screen/input controllers, then artwork coordinator. Do not perform a one-shot rewrite of the main loop.

## 4. Branch and PR disposition

### 4.1 Active open PRs

| PR | Topic | Audit disposition |
|---|---|---|
| #58 | Recovered unified SVG / runtime build | Current integration line; keep as the base under audit. |
| #56 | Runtime stability / desktop streaming / Visual Engine 2.0 | Selectively integrated. Remaining candidates: reusable window geometry and controls. Do not add duplicate SensorService or ModalStack. |
| #55 | UI surface / artwork foundations | Its head is an ancestor of the recovered line; no unique tree delta against current baseline. |
| #54 | Combined stable release | Ancestor / superseded line; no unique code delta against current baseline. |
| #52 | SOCOMA SVG theme | Mostly superseded by current visual line; audit theme/asset delta before closing. |
| #51 | Performance and stability | Four unique catalogue commits are functionally present or superseded by the current stronger scanner guards. |
| #48 | Premium widget editor | Branch is behind the recovered line with no unique file delta. Verify UI call sites before closing. |
| #47 | SteamGridDB integration | Branch is behind the recovered line with no unique file delta. |
| #46 | Earlier SOCOMA theme | Superseded by newer SOCOMA/visual theme changes; do not merge wholesale. |
| #44 | UX polish | Older UX branch; many fixes are present in current native UI. Compare any individual missed behavior, not the full source file. |
| #43 | HomeNX/UX plan | Older combined branch; adapters are present, but production workspace ownership remains duplicated. |

PRs #47/#48 and #55 being behind the current line does not automatically prove every user-visible path is complete; they are not candidates for blind merges. Their branch heads need only be closed after their current production call sites have been checked.

### 4.2 Feature-bearing branches and decisions

| Branch family | Findings | Decision |
|---|---|---|
| `feat/runtime-stability-sensors` | 64 unique commits / 34 changed files relative to recovered; includes SensorService, MotionController, ControlComponents, WindowComponents, TextLayout/Typography and Visual Engine audit docs. | MotionController, text/layout contracts and delete-dialog geometry were selectively integrated. SensorService and ModalStack duplicate richer current subsystems. WindowComponents/ControlComponents remain selective port candidates. |
| `feat/theme-socoma` | 55 unique commits; includes generic PersistentFiles, SettingsFile, LaunchHistory and a production-persistence test suite. | Do not take its old main.cpp wholesale. Adapt its tested file transaction/recovery ideas to current production save/load functions. |
| `perf-stability-iteration` | Four unique commits around incremental scan manifest truncation, cache miss counters and inventory signatures. | Current `source/catalog.cpp` includes stronger completeness, manifest-match and last-known-good guards. No wholesale port needed. |
| `feat/visual-themes-v01-modern` / `agent/transwitch-ux-polish` | Version/storage metadata, widget layout, localized UI, library navigation, cached font warmup and catalogue resilience. | Most behavior is present in current native UI; preserve selected semantics. Any missing screen behavior needs focused diff, not full source replacement. |
| `feat/desktop-streaming-priority` | Priority policy and speculative prefetch. | Present in current `ArtworkRequestPolicy` / `IconCache`; verify with instrumentation on hardware. |
| `feat/steamgriddb-full-integration` / `feat/premium-widgets-editor` | Branches are ancestors of current line with no unique file delta in compare. | Do not remerge. Verify production user flows individually. |
| `feat/nextendo-players`, `feat/quicksettings-profile`, `feat/music-player`, `feat/f7-f12-domains` | Domain modules and host tests. | Nextendo/Quick Settings are integrated. Music, achievements, social, Bluetooth sink and theme shop are not complete production services. |
| `feat/workspace-layer-v2`, HomeNX branches | Provider/FileStore/adapters and extensive tests; a parallel workspace architecture. | Current UI still uses its own production model and storage. Unify carefully; do not switch stores until migration/recovery tests exist. |
| `audit/ecs-thread-recovery`, `audit/menu-liveness-controller`, `audit/safe-persistence`, `audit/production-ipc-validation` | Earlier backend recovery, IPC bounds, SafeReplace and watchdog work. | Current system backend contains these behaviors plus newer retry/rollback logic; keep current implementation. |
| `audit/npdm-capability-inventory` | Read-only NPDM capability inventory script and review document. | Useful missing audit utility; add as a separate read-only tool after script review. Never widen capabilities automatically. |
| `feature/nextendo-online-overlay` | Standalone Tesla overlay sources and parser tests. | Present in current tree and has its own successful build workflow. |
| `feature/socoma-vector-theme`, `feature/visual-engine-1.0` | Older SVG rasterizer/renderer paths. | Superseded by current SVG cache and Visual Engine; do not add parallel rasterizer/renderer. |
| Cards Home branches | Alternative CardsNX-style home view and renderer. | Archived/alternative design, not merged into the Android-style desktop product. Keep out of the default build unless explicitly re-scoped. |
| `fix/honest-play-stats-v2`, `fix/tile-contract-and-nonblocking-ui` | Old React/Web frontend paths. | Historical after native-only cutover; do not restore frontend code to the native target. |

## 5. Confirmed strengths in the current baseline

1. **SVG identity is protected.** The current source includes the SVG registry/cache, expected icon set and a validation script; the CI verifies assets before packaging.
2. **The render loop has explicit diagnostics.** Renderer-state recovery runs at frame boundaries; UI metrics track frame, render, input, present wait and workspace switch.
3. **Artwork is bounded and prioritised.** Icon decode has a worker, priority queue, generation invalidation, stale-prefetch cancellation and bounded GPU upload/cache limits.
4. **Font work is bounded.** Raster requests are queued and pumped with limits; SDL texture operations stay on the renderer thread. This is more conservative than moving TTF calls into a worker without a separately owned font runtime.
5. **System telemetry is broad.** `SystemTelemetry` is a more complete owner than the narrower PR #56 `SensorService`; SD stats already run in a separate worker.
6. **Input focus ownership is centralised.** `SurfaceManager` is the active stack for rendering/input ownership and knows more surfaces than the older ModalStack.
7. **Catalogue completeness is guarded.** Truncated/incomplete NRO inventory does not replace a complete cache; metadata manifests are checked against the live inventory before accepting a cache hit.
8. **Native artifacts are independently validated.** The NRO workflow validates NACP/RomFS properties; overlay is separate.

## 6. Findings / risk register

### P0 — production persistence lacks tests at its actual boundary

The current production paths in `source/main.cpp` persist layout, active workspace, settings and launch history using direct parsing/writing/backup/SD-commit logic. `source/PersistenceStore.*` exists and has a JSON round-trip test, but the changelog explicitly states that production HOME UI does **not** call it. The separate `feat/theme-socoma` branch contains `PersistentFiles.hpp`, `SettingsFile.hpp`, `LaunchHistory.hpp` and `production_persistence_test.cpp`, but those are not present in the recovered tree.

**Risk:** a green `persistence_store_test` can pass while the live settings/layout/history paths still have untested recovery edges.

**Action:** write tests against the current production serializers/loaders/transaction callbacks (or extract those exact functions into testable modules). Inject failure at temp write, fsdevCommitDevice, rename/backup rotation, post-publish commit, corrupt primary, corrupt backup, missing file, oversized file, and abrupt shutdown. Include migration tests for existing formats. Do not copy the old DESKNXSET1 parser over the current DESKNXSET2 implementation.

### P0 — physical console acceptance is still absent

The COMPAT matrix is the intended source of truth for actual hardware verification and currently has no verified HOS/Atmosphère combination. CI builds do not test renderer behavior, NS/PSM sessions, sleep/wake, game handoff, SD filesystem timing or qlaunch recovery.

**Action:** establish a repeatable hardware smoke protocol with HOS/Atmosphère/libnx versions, commit SHA, logs and pass/fail per scenario. Do not mark HOME replacement production-ready until recovery has been exercised on a recoverable test console.

### P1 — workspace ownership is duplicated

The repo has a tested HomeNX Provider/FileStore/Adapter family, while the production UI still owns a parallel model/load/save path in `source/main.cpp`.

**Risk:** tests can exercise an adapter/file store that users never use; fixes can diverge between the two implementations.

**Action:** map every production operation (create, delete, rename, reorder, move tile, resize, empty cell, select active desk, per-workspace appearance) to a single authoritative controller/store. Define the existing-file migration contract and rollback tests before removing either implementation.

### P1 — artwork pipelines are not one scheduler

`IconCache`, `WorkspaceBackgroundCache` and SteamGridDB/cache service have separate request, decode, priority, invalidation and accounting concerns.

**Risk:** duplicated work and memory budgets; inconsistent cancellation and freshness across desktop, library, background and cover paths.

**Action:** implement an ArtworkManager façade and shared bounded request policy first; migrate one producer at a time while keeping worker ownership and render-thread texture creation unchanged.

### P1 — Visual Engine shared geometry is not complete

The current baseline has SurfaceManager, UiComponents, VisualContracts and shared geometry for dock/footer/delete confirmation. PR #56 additionally has ControlComponents and WindowComponents with tested dialog/side-panel/context-menu/repeated-row layouts, which are not present in the recovered tree.

**Action:** selectively port missing pure geometry/state contracts and migrate one window at a time. Do not reintroduce ModalStack, and do not change visuals or hitboxes in a bulk edit. Require draw/hit-test parity tests per window.

### P1 — `source/main.cpp` remains a 9,199-line production unit

**Risk:** high coupling, difficult local reasoning and large regression surface. Several subsystems can be tested independently but their production orchestration stays inline.

**Action:** extract in small steps behind stable interfaces. Start from pure workspace geometry/state and production persistence, not the main loop. Every extraction must keep NRO and HOME targets building.

### P1 — CI is not fully reproducible

The native workflows use `devkitpro/devkita64:latest` and current package installs. A new container/package release can change the output without a repository code change. The backend has pinned Atmosphère/libnx input defaults, but the base devkit container remains floating.

**Action:** pin tested container digest and package versions; emit a build manifest (commit, compiler, devkitPro, libnx, Atmosphère refs and artifact hashes). Add a scheduled dependency-refresh workflow rather than floating on every build.

### P1 — reusable domains must not be mistaken for finished features

MusicPlayer, Achievements, SocialFeatures, BluetoothSink and ThemeShop helper modules/tests exist, but `docs/FEATURES.md` explicitly says music, achievements, MTP, Discord, Spotify and theme shop are not complete features. Nextendo friends list is also not implemented.

**Action:** maintain separate "production", "prototype/domain" and "planned" statuses; do not expose dead/placeholder controls as complete features.

### P2 — NPDM capability review tooling is missing from current baseline

Old `audit/npdm-capability-inventory` contains a read-only capability inventory script and review. This is valuable for release/security review, provided it reports only and never rewrites NPDM permissions.

### P2 — branch and PR hygiene

There are 72 branches and 11 open PRs. Several open PRs are ancestors/duplicates of the current recovered line; others overlap. Avoid merge-by-title. After the relevant feature paths are verified, close superseded PRs with a concise explanation and archive their branch names in this audit. Do not delete branches as part of Stage 0.

## 7. CI evidence on the audited commit

At the time of this audit, the following Actions runs on `888898f70aec3cd7973e35912bebb0fc3ae0494b` completed successfully:

| Workflow | Run | Result |
|---|---:|---|
| Build DeskNX | 45052223734 | Success |
| Build native Nintendo Switch NRO | 45052223784 | Success |
| Build Nextendo Online Overlay | 45052223827 | Success |
| Build experimental DeskNX HOME backend | 45052223789 | Success |

The same SHA also had an earlier duplicate backend run (45052220193) that completed successfully. The successful HOME backend build proves compilation against the workflow-selected Atmosphère/libnx refs; it does **not** establish compatibility with every console firmware or successful qlaunch operation on hardware.

Host tests and package checks are green on this commit. Hardware tests remain **NOT RUN / NOT VERIFIED**.

## 8. Recommended next sequence after Stage 0

1. **Stage 1A — production persistence test seam (P0):** make the existing production paths fault-injectable; test backup/commit recovery without changing the on-disk format.
2. **Stage 1B — release/build manifest (P1):** pin toolchain/container and publish a provenance manifest with SHA-256 for NRO and SD package.
3. **Stage 2A — workspace single-owner design (P1):** map actual call sites and migrate the production UI to one tested workspace controller/store with format migration.
4. **Stage 2B — missing VE window contracts (P1):** port selected WindowComponents/ControlComponents contracts; cover dialog/side panel/context menu and repeated-row hitboxes.
5. **Stage 3 — ArtworkManager façade (P1):** consolidate scheduling and diagnostics without moving SDL texture operations off the renderer thread.
6. **Stage 4 — production module extraction (P1):** split `source/main.cpp` gradually, one stable seam at a time.
7. **Hardware gate:** standalone NRO smoke, then experimental HOME lifecycle and recovery on a test console.

Stage 0 does not authorise a bulk merge of all 72 branches. The next changes should address the confirmed gaps above, not replay history.
