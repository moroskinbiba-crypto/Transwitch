# DeskNX system HOME integration

## Current status

DeskNX currently has two build paths:

1. **Standalone NRO** — compiles in the standard devkitPro Switch container and can be launched from Homebrew Menu.
2. **Experimental HOME backend + library applet** — source scaffolding is present under projects/sSystem/ and projects/DeskNXMenu/. The applet uses ECS to be served into a selected library-applet slot; the backend is designed to own HOME lifecycle and title launching.

The standard CI builds the standalone NRO and the library applet. A separate workflow, Build experimental DeskNX HOME backend, attempts a full compile with selected libnx and Atmosphère refs (default: Atmosphère 1.12.1 and a pinned libnx commit). Even if that job passes, it only verifies compilation against that toolchain; it does not validate the firmware on a real console or make the build production-ready. Do not interpret the source tree or a generated package as proof that qlaunch replacement is operational.

## Architecture (based on public sLaunch patterns)

- **DeskNX menu applet** — draws the workspace and reads controller/touch input.
- **qlaunch backend** — system applet process that owns HOME/sleep lifecycle and launches applications.
- **Target-aware hbloader** — serves NRO launch requests through the album library-applet slot, with a one-shot target file and a return-to-menu path when the target exits.
- **ECS** — Atmosphère External Content Storage serves the applet's ExeFS from SD to a selected library applet program ID.
- **IPC protocol** — `projects/common/include/desknx/Protocol.hpp`; each message is versioned and includes a payload size. Unknown commands and malformed sizes are rejected. Version 2 adds a bounded `Busy` pause for blocking keyboard dialogs: the UI requests at most 30 seconds and sends an explicit zero-second end signal when the dialog returns. The backend preserves heartbeat time remaining, accepts at most one active pause, and resumes heartbeat monitoring when the pause expires. This is watchdog mitigation, not a substitute for a responsive UI.
- **Workspace storage** — the UI persists its layout to sdmc:/switch/DeskNX/layout.tsv.
- **Logs** — backend log: sdmc:/switch/DeskNX/logs/daemon.log; ECS log: sdmc:/switch/DeskNX/logs/ecs.log.
- **Installer/recovery** — guarded scripts require explicit risk acknowledgement, create a manifest, check ownership markers and compare current qlaunch files with stored SHA-256 checksums before uninstalling. Transaction stage/rollback paths include the process id to avoid collisions with stale files or parallel operations.
- **Custom takeover slot** — an optional `switch/DeskNX/config/takeover.txt` may contain one known Shop or Offline Web applet program ID (for example, `0x0100000000001042`). DeskNX tries it first and then falls back to the built-in candidates. Invalid, unknown, and unsupported applet IDs are ignored.

The upstream sLaunch repository contains significantly more functionality and years of firmware-specific hardening. DeskNX is not a drop-in replacement for sLaunch and is not yet at its reliability level.

## Build

### Standalone NRO and HOME applet

The standard GitHub Actions job uses the devkitPro devkitA64 container and builds:
- DeskNX.nro
- The HOME library applet ExeFS SdOut/switch/DeskNX/bin/Menu/main and main.npdm

The experimental workflow additionally builds `projects/hbloader/` and stages `SdOut/switch/DeskNX/bin/hbloader/{main,main.npdm}`. In system mode, the UI sends a validated NRO path through IPC; the backend verifies the SD-card file, writes a one-shot request, registers hbloader with ECS, then starts it. On completion, HOME returns to DeskNX. The current fork intentionally supports Homebrew NROs as an applet only; donor-slot application mode is disabled.

These artifacts prove compilation only for the tested build paths.

### Stratosphere backend

The CI defaults to Atmosphère 1.12.1 and libnx commit b156b4774fb2df7b93402169509266da7547fc4c. Use the workflow's `atmosphere_ref` and `libnx_ref` inputs to select refs matching the target console; successful compilation against one ref does not make the output compatible with other firmware versions.

To build the system component, provide a version-matched Atmosphère tree and compatible libstratosphere / AtmosphereLibs:

    make system-backend ATMOSPHERE_DIR=/path/to/Atmosphere
    make system-package ATMOSPHERE_DIR=/path/to/Atmosphere
    make package-system ATMOSPHERE_DIR=/path/to/Atmosphere

The system makefile expects $(ATMOSPHERE_DIR)/libraries/libstratosphere/stratosphere.specs and compatible headers/libraries in $(DEVKITPRO)/AtmosphereLibs. A standard libnx-only container is not sufficient. The daemon may need libnx master built against the same firmware-compatible Atmosphère branch. See sLaunch's public build instructions at https://github.com/etonedemid/slaunch for upstream toolchain requirements; do not mix headers and libraries from mismatched releases.

## Installer and recovery

The scripts operate on a mounted SD-card root and **do not download, build, or verify binary compatibility**. The experimental Actions artifact is a PC-side bundle containing a SdOut directory and the scripts; it is deliberately not laid out as an immediately active SD-root override. Extract it on a PC, inspect it, then invoke the installer. Do not extract the bundle directly into the SD-card root.

Install:

    bash scripts/install-system.sh --sd-root /path/to/sd-card --acknowledge-risk

If a qlaunch override already exists, preserve it with:

    bash scripts/install-system.sh --sd-root /path/to/sd-card --acknowledge-risk --backup-existing

Restore:

    bash scripts/uninstall-system.sh --sd-root /path/to/sd-card --acknowledge-risk

The uninstall script checks the ownership marker in the installed qlaunch directory and verifies the SHA-256 checksums of the qlaunch executable and NPDM before replacing them. If those files changed after install, it refuses by default; --force-changed-files preserves the DeskNX build before restoring the backup. If the emergency chord has already moved DeskNX to `0100000000001000.disabled-DeskNX`, the uninstaller recognizes that marked path, archives it without re-enabling it, and restores any previous qlaunch override.

### Emergency recovery chord

If all known HOME applet slots fail to start, no game/menu is running, and the system backend remains alive, hold **L + R + ZL + ZR + Plus + Minus + D-pad Down** for three seconds. The backend will only accept this chord if the exact DeskNX install marker exists in the qlaunch override. It renames that marked override to `0100000000001000.disabled-DeskNX` and requests a reboot so the system can fall back to the stock HOME. If the reboot request fails, the backend attempts to restore the original override path. This is a last-resort guard and cannot help if the daemon itself cannot run, SD access is broken, or the marker was removed.

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
- The IPC currently covers readiness, heartbeat, bounded Busy pause/end, title launch, Homebrew launch and return-to-game; it is not a full replacement for all normal HOME operations or sLaunch command breadth. Host tests cover protocol validation boundaries, but they do not emulate the full qlaunch watchdog state machine.
- Homebrew launches currently use the library-applet/album slot, so available memory and permissions are limited. Full-RAM donor-slot launches are intentionally disabled.
- Controller setup, selected-user behavior, suspended-title lifecycle, service initialization, failure recovery and qlaunch NPDM need an expert code review before release.
- The installer is conservative, but it cannot guarantee recovery if the SD contents are corrupted or the console has unrelated boot problems.

### Process termination safety

The backend treats `pmshell` as optional at startup, but it no longer treats
a failed PID query as proof that a running title has exited. When the service
is available, the backend waits for the application process to disappear.
Only Atmosphere PM's explicit `ProcessNotFound` result counts as confirmation;
other PM query errors preserve the holder and abort a replacement launch. A
zero PID is rejected. The backend checks the result of forced termination.
When `pmshell` initialization fails, it gives the title a short graceful-exit
window and uses AM's direct application-termination command. If termination is
not confirmed, the application holder is kept open and a replacement title is
not launched over it.

Natural game exit detection also uses the explicit PM `ProcessNotFound`
result. The application state-change event is not sufficient because it can
also be signalled by focus/suspend transitions. If `pmshell` is unavailable,
the backend deliberately retains the holder instead of guessing that a
suspended game has exited; automatic return-to-menu then requires the PM
service to be available. This path still requires validation on a physical
console and matching firmware.

## Licensing

System-layer code adapted from sLaunch is covered by its upstream GPL-3.0-or-later terms; see projects/sSystem/COPYING.sLaunch and THIRD_PARTY.md. The ECS implementation in upstream sLaunch also credits uLaunch as an upstream source. Review these notices before redistributing a combined build.
