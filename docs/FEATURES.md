# DeskNX native feature inventory

This document describes the native implementation in `source/main.cpp` and supporting modules. The old React/WebKit prototype is not part of the project build and is no longer shipped.

## 1. Standalone application

`DeskNX.nro` is compiled from the production SDL2/C++ UI. The NRO owns its window, renderer, input loop, catalogue and launch requests. It does not open a WebKit applet and does not depend on HTML, JavaScript, React, mock catalogue data or RomFS assets.

The standalone target is one Homebrew application. Its catalogue, image decoder, Nextendo feed and on-demand PDM query use internal worker threads so file/network/service work does not stall the render loop.

## 2. Native UI and desktop model

| Feature | Native implementation |
|---|---|
| Multiple independent workspaces (up to 12) | `source/main.cpp`, `source/Layout.cpp`, `include/desknx/Model.hpp` |
| 6 × 3 desktop with explicit empty cells | Native workspace/tile model |
| Move, drag, resize and remove tiles | Native controller/touch input handlers |
| Switch desktop and remember last active desktop | Layout plus active-workspace sidecar |
| Library of installed games | NS enumeration, ApplicationView and NACP in `source/catalog.cpp` |
| Homebrew NRO discovery | SD filesystem scan in `source/catalog.cpp` |
| Native game names and identifiers | NACP/control data, cache and fallback metadata |
| Themes, wallpaper, fonts, icon packs | Native settings and SDL resource loaders |
| Clock, battery, system, recent, storage and calendar widgets | Native tile renderers |
| Playtime / launch statistics | PDM query, loaded on demand for the selected game |
| Nextendo online player counts | `source/NextendoPlayers.cpp`, card badges and total indicator |
| Background artwork decode | `source/IconCache.hpp`, bounded SDL worker and render-thread texture uploads |

## 3. HomeNX-derived mechanisms

Already present or being ported:
- Nextendo online presence endpoint selection and per-title merge strategy.
- Delayed polling and expiry semantics; failed requests do not become fabricated zero counts.
- Asynchronous icon file lookup/decode with bounded queues and LRU texture cache.
- HomeNX workspace/provider and grid adapter abstractions with host tests.
- Separate lifecycle/backend components for the optional qlaunch integration.

Remaining candidates:
- consolidate SteamGridDB covers and system icons behind one artwork scheduler;
- prioritize selected/visible tiles and prefetch adjacent tiles;
- make the tested workspace Provider/FileStore the single production persistence API;
- compare grid focus behavior for differently sized tiles;
- lock screen, MTP, safe updater, music and social modules as optional independent features.

## 4. Separate experimental HOME integration

The standalone NRO does not require `projects/sSystem`, `projects/hbloader` or the HOME library applet. These are only for a separate experimental qlaunch-replacement flow. Do not install the system override based solely on successful compilation; it needs hardware recovery and lifecycle tests for the exact HOS/Atmosphère combination.

## 5. Not implemented / not claimed

- Donor-title Homebrew Application Mode is intentionally disabled.
- A successful CI build is not a hardware runtime validation.
- Online presence is unavailable while offline or when the endpoint cannot be reached; no fake value is shown.
- MP4/video wallpaper, achievements, MTP, Discord, Spotify/music and theme shop are not considered complete features.
