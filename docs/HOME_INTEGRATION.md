# DeskNX system HOME integration

## Current status

DeskNX currently has two build paths:

1. **Standalone NRO** — compiles in the standard devkitPro Switch container and can be launched from Homebrew Menu.
2. **Experimental HOME backend + library applet** — source scaffolding is present under projects/sSystem/ and projects/DeskNXMenu/. The applet uses ECS to be served into a selected library-applet slot; the backend is designed to own HOME lifecycle and title launching.

The current CI builds the standalone NRO and the library applet. It does **not** yet build the Stratosphere backend with the exact libnx/Atmosphère pair or test it on hardware. Do not interpret the presence of projects/sSystem/ or a generated applet as proof that qlaunch replacement is operational.

## Architecture (based on public sLaunch patterns)

- **DeskNX menu applet** — draws the workspace and reads controller/touch input.
- **qlaunch backend** — system applet process that owns HOME/sleep lifecycle and launches applications.
- **ECS** — Atmosphère External Content Storage serves the applet's ExeFS from SD to a selected library applet program ID.
- **IPC protocol** — projects/common/include/desknx/Protocol.hpp; each message is versioned and includes a payload size. Unknown commands and malformed sizes are rejected.
- **Workspace storage** — the UI persists its layout to sdmc:/switch/DeskNX/layout.tsv.
- **Logs** — backend log: sdmc:/switch/DeskNX/logs/daemon.log; ECS log: sdmc:/switch/DeskNX/logs/ecs.log.
- **Installer/recovery** — guarded scripts require explicit risk acknowledgement, create a manifest, check ownership markers and compare current qlaunch files with stored SHA-256 checksums before uninstalling.

The upstream sLaunch repository contains significantly more functionality and years of firmware-specific hardening. DeskNX is not a drop-in replacement for sLaunch and is not yet at its reliability level.

## Build

### Standalone NRO and HOME applet

The standard GitHub Actions job uses the devkitPro devkitA64 container and builds:
- DeskNX.nro
- The HOME library applet ExeFS SdOut/switch/DeskNX/bin/Menu/main and main.npdm

These artifacts prove compilation only for the tested build paths.

### Stratosphere backend

To build the system component, provide a version-matched Atmosphère tree and compatible libstratosphere / AtmosphereLibs:

    make system-backend ATMOSPHERE_DIR=/path/to/Atmosphere
    make system-package ATMOSPHERE_DIR=/path/to/Atmosphere
    make package-system ATMOSPHERE_DIR=/path/to/Atmosphere

The system makefile expects $(ATMOSPHERE_DIR)/libraries/libstratosphere/stratosphere.specs and compatible headers/libraries in $(DEVKITPRO)/AtmosphereLibs. A standard libnx-only container is not sufficient. The daemon may need libnx master built against the same firmware-compatible Atmosphère branch. See sLaunch's public build instructions at https://github.com/etonedemid/slaunch for upstream toolchain requirements; do not mix headers and libraries from mismatched releases.

## Installer and recovery

The scripts operate on a mounted SD-card root and **do not download, build, or verify binary compatibility**.

Install:

    bash scripts/install-system.sh --sd-root /path/to/sd-card --acknowledge-risk

If a qlaunch override already exists, preserve it with:

    bash scripts/install-system.sh --sd-root /path/to/sd-card --acknowledge-risk --backup-existing

Restore:

    bash scripts/uninstall-system.sh --sd-root /path/to/sd-card --acknowledge-risk

The uninstall script checks the ownership marker in the installed qlaunch directory and verifies the SHA-256 checksums of the qlaunch executable and NPDM before replacing them. If those files changed after install, it refuses by default; --force-changed-files preserves the DeskNX build before restoring the backup.

Before trying any system install:
1. Keep a copy of the entire SD card and the exact original atmosphere/contents/0100000000001000 directory.
2. Confirm the console/SD card can be recovered by mounting it on a PC.
3. Compile the backend using the same Atmosphère/libnx version as the console.
4. Inspect the generated NPDM and verify program ID, services, syscalls, applet slot and heap assumptions.
5. Test boot, HOME press during game launch, repeated HOME, sleep/wake, title launch/exit, SD removal, bad layout file, and a failed/missing menu applet.
6. Keep recovery files untouched until a known-good boot has been confirmed.

## Known limitations / next engineering gates

- The backend has not been compiled against the target libstratosphere in the default CI.
- No physical-console runtime test has been reported.
- Firmware slot selection and ECS registration are firmware-sensitive.
- The current IPC covers readiness, title launch and return-to-game only; it is not yet a full replacement for all normal HOME operations or sLaunch command breadth.
- Controller setup, selected-user behavior, suspended-title lifecycle, service initialization, failure recovery and qlaunch NPDM need an expert code review before release.
- The installer is conservative, but it cannot guarantee recovery if the SD contents are corrupted or the console has unrelated boot problems.

## Licensing

System-layer code adapted from sLaunch is covered by its upstream GPL-3.0-or-later terms; see projects/sSystem/COPYING.sLaunch and THIRD_PARTY.md. The ECS implementation in upstream sLaunch also credits uLaunch as an upstream source. Review these notices before redistributing a combined build.
