# HOME integration and boot sequence

## Current boundary

DeskNX currently builds as a regular Homebrew NRO. It does not hook or replace qlaunch, install a sysmodule, modify Atmosphère configuration, or run at cold boot. This boundary is deliberate: the workspace UI should be exercised before system-level changes are attempted.

## Target architecture

A future HOME replacement requires independently reviewable components:

- **UI applet** — renders workspaces and handles gamepad/touch input.
- **System integration/daemon** — handles the HOME lifecycle and communicates with the UI.
- **Application catalogue** — enumerates installed titles and control data, plus Homebrew NROs.
- **Workspace storage** — stores desktops, blank cells, tile geometry, widget settings and themes.
- **Installer and recovery** — validates Atmosphère/firmware compatibility, backs up changed files and provides a rollback route.

The exact applet/daemon implementation must target the selected Atmosphère/libnx versions and be tested against an unmodified boot setup first. A plain NRO cannot provide cold-boot replacement by itself.

## Safety requirements before system integration

1. Verify a clean boot with DeskNX disabled.
2. Install only a versioned package with a manifest and checksums.
3. Keep the original HOME files/configuration and offer one documented rollback procedure.
4. Never enable replacement silently from the UI prototype.
5. Test failed launch, sleep/wake, user switching, returning from games, and missing/corrupt layout files.
