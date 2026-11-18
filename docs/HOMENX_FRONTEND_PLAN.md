# Native DeskNX frontend and HomeNX port plan

Status: the production UI is native C++/SDL2. The former React/WebKit prototype has been removed from the build and repository; there is no browser runtime in the standalone NRO.

## 1. Correct build architecture

There is one native UI entry point: `source/main.cpp`.

| Target | Compile definition | Role |
|---|---|---|
| Standalone `DeskNX.nro` | no `DESKNX_SYSTEM_APPLET` | Full native SDL2 launcher, started manually from Homebrew Menu / title override. |
| HOME library applet | `DESKNX_SYSTEM_APPLET` | Same native UI, but HOME actions and app launching are delegated to the optional system backend over IPC. |
| Experimental HOME backend | separate `projects/sSystem` target | Only required when replacing qlaunch; never launched by standalone DeskNX.nro. |

The standalone build compiles the production source directly. It has no WebKit applet, HTML bundle, React runtime, Vite build, RomFS website or second frontend process.

## 2. Native UI already implemented

- Up to 12 independent Android-style workspaces, each with its own tile collection.
- 6 × 3 grid, empty cells as intentional state, tile drag/move/resize and workspace switching.
- Native app drawer/library backed by NS enumeration, NACP names and SD NRO scanning.
- Native SDL2 rendering, controller and touch input.
- Native widgets, settings, themes, wallpaper/font/icon-pack selection.
- PDM play statistics and Nextendo player counts.
- Atomic persistence and rollback-friendly layout/settings files.

The app is not a web UI that calls a hardware bridge. It directly uses libnx services from its own process.

## 3. Internal work scheduling (not extra processes)

The SDL main/render loop owns input and drawing. Operations that can block or allocate large image buffers run on bounded worker threads:

- catalogue scan: one background scan task;
- icon decoder: one low-priority worker, bounded queue (48 requests), at most two decoded CPU images waiting for render-thread upload, bounded texture LRU;
- Nextendo: one delayed polling thread; network calls have explicit timeouts and do not block the renderer;
- PDM statistics: created on demand for the selected title.

No worker starts another NRO or system process. The standalone app launches a game only after a user action through Horizon's application-launch API. In the HOME-applet build only, IPC heartbeat is added for the external system backend.

## 4. HomeNX port status and next work

1. **IconStreamer:** SDL2 adaptation is implemented in `source/IconCache.hpp`; cross-build now checks both standalone NRO and HOME library applet. Physical Switch memory/performance validation remains mandatory.
2. **Artwork manager:** unify icon and SteamGridDB cover request scheduling, prioritize visible/selected tiles, cancel obsolete jobs and add cache/texture diagnostics.
3. **Workspaces:** wire the tested Provider/FileStore abstraction into production persistence without changing existing layouts or auto-placing newly discovered apps.
4. **Navigation:** compare mixed-size tile focus against HomeNX grid behavior and keep the current Android-style desktop model.
5. **Lifecycle:** test launch, resume, suspend, sleep/wake and repeated transitions; don't replace the existing Transwitch lifecycle wholesale.
6. **Homebrew Application Mode:** keep donor-title mode disabled until host-title ownership, ECS cleanup and rollback are proven on hardware.
7. **Optional modules:** lock screen, MTP, updater and social/music features must remain separate from the render thread.

## 5. Safety rules

- Do not add a second SDL event/render loop.
- NS enumeration, NACP reads, filesystem scans, network operations and image decoding must not run on the render thread.
- Keep all worker queues, image dimensions, compressed file sizes and caches bounded.
- Do not display fabricated online counts; unknown/offline is not zero.
- Keep the experimental qlaunch installation path separate from the standalone NRO.
