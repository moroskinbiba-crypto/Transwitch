# Native-only build cutover

## Why the build changed

The repository contained two different interfaces. The actual SDL2 launcher lived in `source/main.cpp`, but the default standalone NRO target compiled only `src-switch/main.cpp`. That file opened a local HTML page in Horizon WebKit. The NRO therefore was a browser shell, not the native launcher.

This was an incorrect release target for DeskNX. The native HOME library applet already compiled the SDL2 source, and the same source also had a standalone compile path. The standalone NRO now links that exact native UI.

## Removed from the product

- React/TypeScript app and its mock catalogue under `src/`;
- WebKit entry point `src-switch/main.cpp`;
- Vite/npm build files and JS lockfiles;
- generated HTML/JS/CSS RomFS bundles;
- scripts that prepared website RomFS or compared native/web tile contracts.

## New release build

`make -j2` builds `DeskNX.nro` from `source/*.cpp` using devkitA64, libnx, SDL2, SDL2_image, SDL2_ttf and libcurl. The NRO embeds NACP metadata but no RomFS. `scripts/check-nro-assets.py` rejects an embedded RomFS or known WebKit launcher symbols.

The same production UI source is compiled into `projects/DeskNXMenu` with `DESKNX_SYSTEM_APPLET` for the optional qlaunch integration. The system backend is still a separate, experimental target and is not required by the standalone NRO.

## Runtime process model

Standalone DeskNX is one native application process. Its catalogue, icon decode, Nextendo feed and selected-title PDM query are worker threads owned by this process. No child NRO, browser applet, web server or system daemon is started by the standalone launcher. The optional qlaunch backend is only present when a user deliberately builds and installs the experimental system package.

## Validation required

- CI must link the actual native standalone NRO, not merely compile the standalone source object.
- NRO validation must confirm NACP is present, RomFS is absent and the WebKit wrapper symbols are absent.
- On hardware: cold start, installed-game catalogue, controller/touch, icon loading, Nextendo offline/online behavior, game launch and return.
- Atmosphère crash reports and startup diagnostics must be checked before claiming runtime stability.
