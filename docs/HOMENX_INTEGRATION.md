# HomeNX integration boundary

## Pinned upstream

Transwitch tracks HomeNX as a Git submodule:

- Repository: https://github.com/ad3333m/homenx
- Pinned commit: `8756d38ac037e7bcbfb9c42a3de8dde892afacb2`
- Path: `vendor/HomeNX`

The pin is intentional. Do not silently advance it to upstream `master`: review upstream changes, licenses and build output first. CI checks out submodules recursively because HomeNX has its own Atmosphere-libs, eSpeak NG and libnxtc submodules.

## Architecture decision

HomeNX is the system/platform base; Transwitch owns the Android-style home-screen model.

Reuse from HomeNX, after explicit adapter work:

- system HOME lifecycle, qlaunch/daemon and loader integration;
- title/app discovery, launch and return-to-HOME behavior;
- native renderer, input/focus infrastructure and texture/icon streaming;
- existing folder and widget implementations where their storage and UI contracts fit.

Keep Transwitch semantics:

- multiple independent workspaces, not HomeNX's automatically paginated single root grid;
- per-workspace tile positions and sizes, empty slots, wallpaper and active-workspace persistence;
- a separate app drawer;
- a shared Dock and workspace overview.

The next source-level port must adapt HomeNX's `GridModel` and `WiiUMenuApp` to a workspace provider. It must not just map each HomeNX auto-generated page to a Transwitch desktop: page membership and tile positions need to persist independently.

## Build boundary

`.github/workflows/build-homenx-base.yml` builds the pinned upstream HomeNX standalone NRO as a separate artifact. This is a provenance/build check, not yet a combined Transwitch HOME replacement. It deliberately does not install or package Atmosphère's qlaunch override.

The native Transwitch NRO is still built by `.github/workflows/build-nro.yml`. Do not replace it with the unmodified HomeNX artifact and call that integration complete.

## Licensing

Transwitch's pre-existing files remain under the root MIT license while they are independent. HomeNX is GPL-3.0 and remains a separately pinned upstream component with its own license and third-party notices under `vendor/HomeNX`.

Before copying or linking HomeNX implementation code into Transwitch's combined executable/distribution, review the GPL-3.0 obligations and update the combined project's licensing and notices accordingly. Do not remove upstream copyright headers or third-party notices. The submodule pin alone does not relicense the existing Transwitch code.
