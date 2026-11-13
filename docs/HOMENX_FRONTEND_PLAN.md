# HomeNX-based frontend plan (Android-like desktops)

Status: active. Owner: Transwitch/DeskNX.
Baseline commit: `00ee5b5` (`main`). HomeNX pin: `8756d38ac037e7bcbfb9c42a3de8dde892afacb2`.

## 1. Goal

Replace HomeNX's auto-paginated single root grid with a Transwitch-owned,
Android-style home screen, on top of HomeNX's system backend:

- independent workspaces (up to 12), each a real collection, not a page slice;
- 6 × 3 cell grid per workspace, with per-tile position and size;
- drag to move, drag to reorder, resize handles, empty cells are intentional state;
- per-workspace wallpaper, fit mode, artwork mode, label opacity, accent colour;
- app drawer (all titles) separate from desktops; shared dock; workspace overview;
- widgets as first-class grid citizens (clock, battery, storage, recent, calendar).

Non-goals for this plan: replacing HomeNX's title discovery, launch/lifecycle, ECS
registration or loader. Those stay HomeNX-owned.

## 2. What actually runs on a console today (verified, not assumed)

| Artifact | Built from | Reality on hardware |
|---|---|---|
| `DeskNX.nro` | `Makefile.switch` → **only** `src-switch/main.cpp` | Hosts the React UI in the Horizon OS WebKit applet via `webConfigShow`. No IPC to `ns:`/`pdm:`/`psm:`; applet mode (Album) is refused with a diagnostic screen. Requires Title Override. |
| HOME library applet | `projects/DeskNXMenu/Makefile` → `SOURCES := ../../source` + `-DDESKNX_SYSTEM_APPLET` | The real native SDL2 launcher. Not install-ready; served by the optional backend. |
| System package | `projects/sSystem` (qlaunch role) + `projects/hbloader` + `projects/common` | Experimental. Bricking risk; never hardware-validated. |

Consequence: the web UI's hardware service (`src/services/switchHardwareService.ts`)
is **simulation only** — no bridge daemon exists, so every call falls back to mock
data. Launching from the web UI does nothing on hardware. This is the single most
important constraint for the frontend plan: the Android-desktop UI must ultimately be
rendered by the **native** path (`nxui`), not by the WebKit applet.

## 3. Target architecture

```
HomeNX daemon (catalog, launch, ECS, SMI)   ← unchanged, single lifecycle owner
        │  SMI IPC
        ▼
Transwitch frontend (native, nxui)
  ├─ WorkspaceStore      durable collection, active workspace, per-desk appearance
  ├─ WorkspaceProvider   mutation + navigation API (create/rename/delete/move/resize)
  ├─ GridAdapter         workspace → nxui GridModel projection (active desk only)
  └─ MenuActivity        replaces WiiUMenuApp composition; keeps nxui focus/render
```

Hard rules:

1. One lifecycle owner in the combined build. HomeNX daemon/loader/menu win; the old
   `projects/sSystem` package is retired only after selective porting of its
   watchdog, IPC validation, recovery and installer logic.
2. Catalog refresh never implicitly places new titles on any desktop.
3. Persistence is transactional: temp file → backup → publish, with validation
   before overwrite. Never destroy the last readable layout.
4. No second SDL/event loop inside the system menu.

## 4. Work items, in execution order

### Phase 0 — correctness and honesty (this change)

| ID | Item | Acceptance |
|---|---|---|
| F3/F4 | Tile-shape contract unified with the native model: `1×1, 2×1, 3×1, 2×2, 3×2` (`2×3` removed, it never existed natively) | `scripts/check-tile-contract.mjs` passes in CI; no shape is rewritten on load |
| F4 | `parseLayout` snaps to the nearest supported shape instead of collapsing to `1×1`; serializer clamps position/size/colour and never emits `NaN` | round-trip of a `3×1` desk is byte-identical |
| F4 | `parseInt(x) ?? -1` NaN leak in accent colour | NaN maps to `-1` |
| F6 | `DEFAULT_USERS` / new profiles shared the module-level `INITIAL_WORKSPACES` array | `cloneWorkspaces()` on every consumer |
| F7 | `alert()` (13 sites) and `confirm()` in reset flow | non-blocking `notify()` + `<Toaster />` |
| B6 | `metadata.json` carried `MAJOR_CAPABILITY_SERVER_SIDE_GEMINI_API` | capabilities empty |
| F1 | `switchHardwareService.ts` claimed a real IPC bridge | header documents simulation-only status |

Deferred deliberately: destructive `confirm()` in archive/delete/profile-delete stays
until a two-step in-UI confirmation exists — silently deleting saves is worse than a
blocking dialog.

### Phase 1 — workspace layer on `main` (no behaviour change)

1. Cherry-pick the host-tested platform-neutral layer from
   `feature/homenx-android-desktops`: `include/desknx/HomeNXWorkspace{Adapter,Provider,FileStore}.hpp`,
   `include/desknx/TouchGesture.hpp`, `tests/homenx_workspace_*_test.cpp`.
2. Fix that branch's red CI (`Build pinned HomeNX base`, run `42653207028`, failed on
   `test(integration): verify active workspace sidecar-only saves`).
3. Wire the three test binaries into `build.yml` so the layer stays covered on `main`.
4. Acceptance: `make -j2` + `make -C projects/DeskNXMenu -j2` green, all host tests green,
   provider tests cover load/recovery, sidecar fallback, navigation, create/rename/delete,
   first-free placement, cross-workspace move with full destination, 12-desk limit.

### Phase 2 — native frontend skeleton (`nxui`)

1. Introduce `MenuActivity` in the HomeNX-derived menu target; compose `GridModel` from
   **only** the active workspace via `GridAdapter` (multi-cell tiles become one primary
   cell plus continuation cells with stable keys).
2. Input: D-pad focus movement across cells, shoulder = workspace switch, `+` = drawer,
   `-` = overview; touch drag with 250 ms long-press to enter edit mode, drag to move,
   corner handles to resize, drop outside grid = cancel.
3. Persistence wired to `HomeNXWorkspaceFileStore` with the native `SafeReplace`
   semantics; active-workspace sidecar published before the layout file.
4. Acceptance: capture photos/screens on console of create → move → resize → rename →
   delete → reboot → state restored; no tile lost; no overlap accepted.

### Phase 3 — cutover and hardware acceptance

1. Retire `projects/sSystem` from the combined package after porting its liveness/busy
   watchdog, IPC validation and recovery chord into the HomeNX path.
2. Recovery rehearsal **before** enabling autostart: prepared restore of stock qlaunch,
   recovery chord verified, SD removal, sleep/wake, game launch/return, NRO launch.
3. Only then revisit the "install-ready" status.

### Phase 4 — product polish

Drawer (search/sort/filter), shared dock, workspace overview thumbnails, per-workspace
widgets in the picker, two-step confirm for destructive actions, archive/delete flows
routed through the backend instead of alerts.

## 4a. Feature ports requested from HomeNX

Product features the user wants carried over. Both are backend-coupled, so they land after
Phase 2 gives the native frontend a real data path.

### Cover art via SteamGridDB

HomeNX reference: `CardsActivity.cpp` — `steamGridDbKey()`, `steamGridDbFind(query, key, 12)`,
`setSteamGridDbKey()`; the key is entered under Settings > Look (steamgriddb.com → Preferences →
API). The artwork picker (button Y) can search a different name and choose a cover or backdrop.
Resolution order per `cardsnx-release.md`: **SteamGridDB when a key is set → GameTDB → eShop
banner (nlib.cc)**. Backdrop caching is described in `cards-artwork-performance.md`.

Work items: a settings row for the API key (stored on SD, never logged), a resolver with that
three-step fallback, an artwork cache keyed by title ID using the same bounded/transactional
write rules as `layout.tsv`, and an artwork picker in the desktop customiser.

### Online players via Prelude (Nextendo)

HomeNX reference: `CardsActivity.cpp` — `m_players` refreshed roughly once a minute, per-title
"played online now" green dots, a total pill reading "{n} players online in Nextendo", and a
Settings toggle ("Nextendo players"). Network selection lives in `Library.hpp` (`nextendo`), the
app path in `Config.hpp` (`sdmc:/switch/prelude.nro`). Prelude is downloaded from
`https://github.com/NextendoNetwork/Prelude-Nextendo/releases/latest/download/nextendo.nro` and
started through `openNextendo(mode)` — `0` = NINTENDO, `1` = NEXTENDO; it switches the network
and restarts the console, and it only runs as an application, not as an applet. Endpoint base:
`https://nextendo.network`.

Work items: a background player-count fetch on a minute cadence with a hard timeout that never
blocks the UI thread, a per-title dot on the tile/card, a total in the status bar, an opt-out
toggle, and an explicit "Nextendo not installed / not in NEXTENDO mode" state instead of a
silent zero.

## 5. Open defect backlog (unchanged by Phase 0)

| ID | Severity | Item | Where |
|---|---|---|---|
| B1 | MAJOR | `Makefile.switch` builds only the web shell; the native UI is unreachable from the standalone NRO | `Makefile.switch` |
| B2 | MINOR | `CFLAGS += $(INCLUDE)` before `INCLUDE` is defined; works only via sub-make environment inheritance | both Makefiles |
| B3/F2 | BLOCKER (hardware) | No hardware data path in the web UI; launch is a no-op; stats are literals (`3600*10`, `launchCount: 15`) | `App.tsx`, `DeskNXGrid.tsx` |
| B4 | MINOR | `psm`/`pdmqry` initialised but unused in the web-shell NRO | `src-switch/main.cpp` |
| B5 | MAJOR hygiene | `romfs/**` duplicated 4×, `.vite/` and both lockfiles committed | repo root |
| B7 | MINOR | `permissions: contents: write` on every push | `build-nro.yml` |
| B9 | MAJOR process | PR #8 open with **zero** check-runs since 2026-10-30 | GitHub |

## 6. Risks

- **GPL-3.0**: combining HomeNX source into a distributed executable brings GPL-3.0 and
  third-party notice obligations onto the combined work. The pinned submodule does not
  relicense pre-existing MIT Transwitch code. Legal review gates any release.
- **qlaunch takeover**: replacing HOME can leave a console without a menu. Recovery must
  be rehearsed on a sacrificial unit, never on a daily driver first.
- **Contract drift**: two languages, one file format. The Phase 0 CI guard exists because
  this already broke 3-wide tiles once.

## 7. Definition of done

A user can, on a physical Switch with verified recovery: boot the HomeNX-based menu, create
and rename desktops, drag games and homebrew onto them, resize tiles to any supported shape,
add widgets, switch desktops with shoulders, open the drawer, launch a game and return to the
same desktop with the same layout — and the layout survives reboot, SD removal and a
power cut mid-save without data loss.
