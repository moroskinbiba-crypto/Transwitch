# Native DeskNX frontend and workspace plan

Status: Phase 0 native standalone transition is being landed. The standalone NRO and
HOME library applet must share one native UI implementation; HTML/WebKit is not a
runtime target for this project.

## 1. Goal

Deliver a responsive Android-like desktop experience with multiple independent
workspaces. Each workspace owns a 6 × 3 grid, layout, background and widgets.
Users can create, rename, delete and reorder desktops; move icons between desktops;
keep empty cells; resize tiles; and persist the active desktop across restarts.

## 2. Target architecture

```
Standalone Homebrew NRO                 Experimental HOME applet
(no DESKNX_SYSTEM_APPLET)                (DESKNX_SYSTEM_APPLET)
          |                                        |
          +-------------------+--------------------+
                              |
                     source/main.cpp
                    Native SDL2 UI/runtime
                              |
              source/*.cpp + shared menu modules
                              |
           NS/PDM/catalogue, persistence, artwork,
              input, rendering, diagnostics
                              |
        (HOME applet only) versioned backend IPC
                              |
                   projects/sSystem
```

The standalone NRO must not call Nintendo's offline Web Applet APIs. It has no HTML,
React, JavaScript bundle or RomFS. The system applet may use the shared native UI with
system-specific launch/power/liveness behavior selected at compile time.

## 3. Delivery phases

### Phase 0 — remove the split frontend/runtime

- Compile the production `source/main.cpp` and its supporting modules into the NRO.
- Keep the simplified `projects/DeskNXMenu/source/main.cpp` prototype excluded from release targets.
- Remove the old WebKit host, React/Vite frontend, HTML assets, RomFS packaging and web dependencies.
- Make NRO asset validation require NRO0/ASET/NACP and reject a packaged RomFS.
- Keep a static guard and CI build so the NRO entry point cannot silently fall back to WebKit.
- Update install, build and diagnostics documentation.

Acceptance: the NRO is a native SDL2 program and the binary contains no RomFS section.
Physical Switch launch validation remains a separate required test.

### Phase 1 — core interaction

- Verify D-pad focus, app selection/launch, desktop switching and return behavior.
- Verify drag/drop, remove, resize, move across desktops and intentional empty cells.
- Keep controller actions predictable: A confirm, B back/cancel, X edit/remove, Y catalogue, shoulder desktop navigation.
- Prevent lost selection, invalid occupied cells and accidental destructive actions.

Acceptance: repeatable controller operation on a physical Switch.

### Phase 2 — touch and gesture quality

- Tap selects/launches.
- A 250 ms long press enters drag mode; small finger jitter does not start a drag.
- Drag/drop uses a visible preview and rejects invalid targets.
- Horizontal swipe changes desktops; catalogue scroll remains vertical.
- Cancel/drop restores the original tile when a gesture is interrupted.

Acceptance: touch behavior is tested on hardware, including controller/touch handoff.

### Phase 3 — workspaces and persistence

- Create, delete, rename and reorder workspaces.
- Persist names, tile positions/sizes, empty cells, per-desktop backgrounds, widget state,
  grid settings and the active workspace.
- Recover safely from missing, truncated or invalid state files without overwriting the
  last known-good backup.

Acceptance: settings and active desktop survive process exit and reboot.

### Phase 4 — application catalogue

- Discover installed games through Horizon services and NROs on SD.
- Retain title ID, name, icon, version/type and source path.
- Scan asynchronously; use incremental cache updates and lazy icon decoding.
- Support search, sort, filter, recent, favorites and hidden entries without blocking render.

Acceptance: accurate catalogue with responsive scrolling on Switch hardware.

### Phase 5 — widgets and personalization

- Widgets: clock, battery, storage, recent applications, calendar and system information.
- Make widgets movable/resizable/removable with low-frequency updates.
- Support static PNG/JPG/BMP wallpapers, per-workspace backgrounds, tile labels,
  transparency, fonts and themes.
- Keep game artwork fully visible according to the selected fit mode.

Acceptance: persisted visual customization without excessive memory or frame-time cost.

### Phase 6 — system functions and integration

- Native settings, controllers, album, power, sleep and return-to-HOME operations.
- Validate system applet transitions, launch/return, sleep/wake, heartbeat and recovery.
- Do not enable qlaunch replacement for daily use until recovery has been rehearsed.

Acceptance: repeated hardware tests on a recorded HOS/Atmosphère/libnx matrix.

### Phase 7 — optimization and release

- Profile frame time, scan time, texture cache and memory use.
- Bound caches, release unused textures, avoid filesystem and service work on render thread.
- Test corrupted config recovery, SD removal and interrupted writes.
- Publish NRO, SD package, install/uninstall guide, limitations and hardware test matrix.

Acceptance: tests and release checks pass, plus physical Switch smoke tests. Build success
alone is never marked as hardware success.

## 4. Current source map

- `source/main.cpp`: production native UI entry point shared by the two UI targets.
- `source/catalog.cpp`, `source/catalog_view.cpp`, `source/slauncher_applications.cpp`:
  catalogue model/discovery and view projection.
- `source/Layout.cpp`, `include/desknx/WorkspaceOps.hpp`: workspace operations.
- `source/PersistenceStore.cpp` and production persistence routines in `source/main.cpp`:
  persistence code; do not merge formats without migration tests.
- `projects/DeskNXMenu/source/`: native UI support modules; its prototype `main.cpp`
  is intentionally excluded from both release entrypoint lists.
