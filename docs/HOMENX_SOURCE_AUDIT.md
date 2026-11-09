# HomeNX / Transwitch source audit

Audit baseline:

- Transwitch branch: `feature/homenx-android-desktops`
- HomeNX submodule pin: `8756d38ac037e7bcbfb9c42a3de8dde892afacb2`
- Scope: source-level architecture review before making a combined HOME executable.

## 1. Current HomeNX menu data flow

### Application catalogue

The system-mode `AppListLoader` first asks the HomeNX daemon for its catalog through `smi_cmd::getAppList`. In system mode it deliberately does not fall back to a second menu-side scan when the daemon catalog is absent. The standalone HomeNX NRO can scan NS records itself.

The HomeNX daemon owns the expensive system-side work:

- reads NS application records in chunks;
- queries application views and live flags;
- queues missing control metadata for background cache work;
- writes a versioned/structured binary catalogue under `sdmc:/config/SwitchU/`;
- coalesces refreshes around HOME launch/return and menu availability.

The menu converts pending catalogue records to `GridModel` entries and gives icon bytes/title IDs to `IconStreamer`. `IconStreamer` decodes off the UI thread, uploads on the render thread, limits in-flight decode work, and reuses a bounded texture pool.

### Display model

`GridModel` is a flat vector of entries. It can contain applications, folders, widgets, empty slots and widget continuation entries, but it does not represent independent workspaces itself.

`WiiUMenuApp` keeps:

- `m_allApps`: the complete application list used to compose the root view;
- `m_layoutSlots`: one ordered list of title IDs, with zero representing an empty slot;
- `m_gameSizes`: per-title dimensions;
- `FolderStore`: globally stored folder membership and folder properties;
- `WidgetStore`: global widget definitions and recent-activity data.

The current layout is stored in `sdmc:/config/SwitchU/layout.json`. It stores one `slots` array plus `gameSizes`; it does not store a workspace collection or active workspace.

When the catalogue changes, `reflowHomeGrid` reconciles the saved slot list against installed apps: removes missing/duplicate IDs, places newly discovered apps into free slots, appends when necessary, pads the list to a whole page, and rebuilds the display model. HomeNX then uses its configurable grid dimensions and page navigation to paginate that one root layout.

This behaviour is useful for a single HOME grid, but it is not the required Android-style workspace model.

### Folders and widgets

HomeNX already has mature separate stores:

- `FolderStore` persists folder IDs, names, color, size, page count and ordered title IDs in `folders.json`.
- `WidgetStore` persists widget definitions and recent activity in `widgets/widgets.json`.
- Root display composition creates synthetic IDs for folders and widgets, then mixes them with application entries in the root slot list.
- Folder contents are ordered separately from root layout slots.

These stores should initially be reused rather than replaced. Workspace membership/position must remain separate from global folder contents and widget definitions.

### Launch and lifecycle

HomeNX has one coordinated platform path:

- daemon owns application session/lifecycle and external-content/ECS registration;
- daemon manages the menu process and system actions;
- menu communicates through HomeNX SMI;
- `AppletLauncher` and the daemon cooperate for installed titles, system applets and Homebrew;
- `IconStreamer` and `AppListLoader` are menu-side services, not independent system lifecycle owners.

Transwitch already has `projects/sSystem`, `projects/DeskNXMenu` and `projects/hbloader`, including a separate qlaunch backend, IPC protocol, menu watchdog, application transitions and NRO handoff. Running this backend alongside the HomeNX daemon would create two owners for qlaunch/menu lifecycle, application launch and external-content registration. That is not a safe integration strategy.

**Decision:** use one platform lifecycle owner in the combined system build. HomeNX daemon/loader/menu integration is the candidate platform path; Transwitch's state-machine, watchdog, IPC validation, recovery and installer logic must be compared and selectively ported into that path before the old `projects/sSystem` package is retired. Do not ship two competing daemons.

## 2. Reuse decision matrix

| Area | HomeNX source | Transwitch source | Integration decision |
|---|---|---|---|
| NS catalogue and control metadata | daemon app manager, `AppListLoader` | `projects/sSystem` catalogue + IPC | One catalogue owner only. Prefer HomeNX daemon data path in the HomeNX system build; port any missing Transwitch safeguards and metadata fields. |
| Icon decode/cache | `IconStreamer` | Transwitch artwork/icon cache | Reuse `IconStreamer` for NS icons; keep Transwitch cover/artwork mapping as a separate presentation/cache layer. |
| Installed-title launch/session | daemon `ApplicationSession`, `AppletLauncher` | Transwitch backend state machine + `Application.cpp` | HomeNX owns final lifecycle. Audit and port watchdog, timeout, failure and recovery rules from Transwitch before cutover. |
| Homebrew NRO | HomeNX Loader, `AppletLauncher`, ECS | Transwitch `hbloader`, `ExternalContent.cpp` | Choose one NRO handoff/registration implementation. Do not register competing Album overrides. |
| Renderer and input | HomeNX `nxui`, `IconGrid`, focus/navigation | Transwitch native SDL2 UI | Use HomeNX platform/runtime and `nxui` rendering/input for the system menu; port Transwitch visual language and workspace interactions, not a second SDL event loop. Keep standalone NRO independent during migration. |
| App model | flat `GridModel` | `desknx::model::Workspace`, tile coordinates/sizes | Add a separate workspace provider/store. Do not overload the catalogue model with desktop identity. |
| Folders | `FolderStore` | Transwitch tile model | Reuse folder data semantics initially; store folder shortcut placement per workspace. |
| Widgets | `WidgetStore`, widget renderers | Transwitch widget tile kinds | Reuse compatible widget implementations; adapt placement and per-workspace layout. |
| Per-title size | `m_gameSizes` / widget dimensions | tile width/height in `Workspace` | Map into one normalized layout representation; validate against 6×3 bounds and supported Transwitch sizes. |
| Persistence | layout.json, folders.json, widgets.json | `DESKNX1..4`, active-workspace signature | Add explicit, versioned migration. Never overwrite legacy layouts before a validated new-format save. |

## 3. Transwitch semantics that must survive

The existing Transwitch model is more expressive than HomeNX's one-root-grid layout:

- up to 12 independent workspaces;
- a 6×3 coordinate grid;
- empty cells are intentional state, not just padding;
- tile coordinates and sizes are stored per workspace;
- moving a tile between workspaces preserves its identity and dimensions;
- per-workspace background, fit, artwork mode, label opacity and accent color;
- separately stored active-workspace identity/state;
- safe validation of dimensions, paths and layout records.

HomeNX's page system must not be relabelled as workspaces. HomeNX pages are slices of one ordered layout; Transwitch workspaces are independent collections of placed items.

## 4. Target architecture

1. **Single system owner:** HomeNX daemon and its loader/ECS/menu-start path are the only owners of qlaunch lifecycle and application handoff in the final HomeNX-based system build.
2. **Menu shell:** HomeNX `WiiUMenuApp` remains the system-facing activity and uses `nxui`/its focus and rendering infrastructure.
3. **Workspace provider:** a new Transwitch-owned `WorkspaceStore` becomes the authority for workspace collection, active workspace, stable slot identity, tile dimensions and workspace appearance.
4. **Catalogue is not a desktop:** catalogue refresh updates the available application records but does not implicitly place every newly installed title on every workspace.
5. **Presentation adapter:** compose only the active workspace into a `GridModel` for display; map application, folder and widget stable IDs without treating synthetic IDs as installed titles.
6. **Separate global stores:** folder membership and widget definitions remain global initially; their shortcuts/placements are workspace-local.
7. **Compatibility layer:** read the old Transwitch layout and HomeNX `layout.json` formats, validate them, then save a new versioned workspace file transactionally. Preserve originals/backups until successful migration is confirmed.
8. **Standalone safety:** keep the existing Transwitch NRO workflow intact while the integrated system frontend is developed separately.

## 5. First source-level implementation boundary

The first code change should not rewrite `WiiUMenuApp` or `GridModel` wholesale.

Implement and host-test the workspace persistence/provider first:

- fixed, explicit workspace ID and active workspace;
- 12-workspace upper bound;
- slot records based on stable title/folder/widget identities;
- 6×3 cell geometry, including multi-cell spans and intentional empty cells;
- per-workspace appearance metadata;
- bounded parsing, duplicate-ID checks and overlap checks;
- migration input adapters for existing Transwitch and HomeNX layouts;
- transactional save/backup/restore.

Only after this passes host tests should the HomeNX menu adapter be introduced. That keeps the first integration patch small and allows failures to be attributed to the storage/model layer instead of the render loop.

## 6. Risks and constraints

- HomeNX is GPL-3.0. Any combined executable/distribution that incorporates HomeNX source must satisfy its GPL-3.0 and third-party notice requirements. The submodule alone does not change the license of pre-existing Transwitch-only code.
- HomeNX system daemon, loader, SMI and ECS code are a coherent system. Mixing pieces of them with the existing Transwitch qlaunch daemon without a single ownership decision risks double registration, double lifecycle handling and unbounded menu recovery loops.
- The CI build of an upstream standalone `HomeNX.nro` proves compile reproducibility only; it does not prove the Transwitch workspace port or system HOME compatibility.
- Hardware testing remains required for qlaunch takeover, game launch/return, sleep/wake, controller input and recovery.

## 7. Audit status

This is a source-level design audit, not a completed integration. HomeNX standalone NRO has successfully built in CI from the pinned submodule at least once; the build workflow also cross-compiles the Transwitch adapter/provider/file-store headers for AArch64. Repeated builds are still being used to validate the latest changes. This does not prove that the combined system menu works.


## 8. Implemented platform-neutral workspace adapter

The first source-level integration layer now exists in Transwitch:

- `include/desknx/HomeNXWorkspaceAdapter.hpp` validates and projects one Transwitch workspace into a 6×3 HomeNX-facing cell array.
- Each tile becomes an explicit typed entry. Only an installed-game entry carries a native title ID. Homebrew paths are validated as SD-card `.nro` files; synthetic widget/system entries are never misrepresented as installed titles.
- Multi-cell tiles are projected as one primary cell plus continuation cells. Empty cells remain explicit.
- The projection preserves workspace appearance metadata and stable item keys that do not change when a tile moves to another cell.
- `tests/homenx_workspace_projection_test.cpp` exercises geometry, continuation cells, paths, title IDs, metadata and rejection of malformed layouts.

## 9. Implemented independent workspace provider

`include/desknx/HomeNXWorkspaceProvider.hpp` is the next platform-neutral layer:

- owns the independent workspace collection and active workspace index;
- loads existing Transwitch layout formats through the existing `Layout` parser;
- restores the active workspace from the independently signed sidecar, falling back to the primary layout's active index if the sidecar is stale or invalid;
- exports both persistence payloads without depending on the Switch filesystem;
- switches forward/backward or to a selected workspace;
- creates, renames and deletes workspaces with the existing 12-workspace bound;
- moves tiles across workspaces without losing a tile when the destination is full;
- places items in explicit or first-free cells;
- returns the active workspace through the validated HomeNX projection.

The provider deliberately does not write files itself. The eventual Switch adapter will own the SD-card paths, durable writes, backup and rollback. This keeps host tests deterministic and avoids adding filesystem assumptions to the model.

Host CI now compiles and runs both test suites with `-Wall -Wextra -Werror`:
- `tests/homenx_workspace_projection_test.cpp`
- `tests/homenx_workspace_provider_test.cpp`

The provider tests cover load/recovery, sidecar fallback, navigation, create/rename/delete, first-free placement, cross-workspace moves, full-destination safety and the 12-workspace limit.

**Integration boundary:** these two layers are now implemented and host-tested, but they are not yet wired into HomeNX's `WiiUMenuApp`. The next change must add the Switch persistence adapter and connect the active projection to HomeNX's menu composition, without replacing the HomeNX catalogue or launching a second system daemon.


## 10. Implemented transactional workspace file store

`include/desknx/HomeNXWorkspaceFileStore.hpp` now connects the platform-neutral provider to a configurable file layout:

- default Transwitch paths under `sdmc:/switch/DeskNX/` for the primary layout, active-workspace marker, temporary files and backups;
- bounded file reads and writes;
- primary-layout parsing before backup fallback, including recovery from corrupt or oversized primaries;
- active-workspace sidecar validation against the exact workspace shape;
- backup-marker fallback when the primary marker is missing or invalid;
- transactional replacement through the existing `desknx::safeio::replaceFilePreservingOnFailure` helper;
- optional durable-commit callback so the eventual HomeNX adapter can invoke its SD commit primitive without coupling the host-testable store to HomeNX headers.

The save order publishes the active marker before the full layout. The marker includes the workspace shape signature, so an interrupted layout replacement cannot apply a stale active index to a different workspace collection.

`tests/homenx_workspace_file_store_test.cpp` covers save/load, backup generation, rollback on failed commit, corrupt-primary recovery, oversized-primary recovery, and preservation of the in-memory provider on unrecoverable load. It runs in the system backend's host-test job with warnings treated as errors.

**Verified at source/CI level:** projection, provider and file-store host tests passed together in Actions run `42651789939`. The target AArch64 smoke compile of these headers and `Layout.cpp` passed in HomeNX base run `42651790108`. These checks do not mean the store has been wired to `WiiUMenuApp` yet.
