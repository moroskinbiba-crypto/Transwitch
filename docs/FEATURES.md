# Native launcher feature inventory

Both supported UI targets now compile the same production renderer and application
logic from `source/main.cpp` plus the shared source modules. The difference is the
build mode: the standalone NRO is built without `DESKNX_SYSTEM_APPLET`; the HOME
library applet defines it and sends lifecycle/launch/power requests over IPC.

## Standalone NRO

| Area | Implementation | Verification boundary |
|---|---|---|
| Native interface | SDL2 window, renderer, controller and touch event loop in `source/main.cpp` | Compile in CI; render/input still require Switch testing |
| Installed games | NS/ApplicationView/NACP discovery through `source/catalog.cpp` and related modules | Host tests cannot emulate Horizon services |
| Homebrew | NRO discovery from SD-card paths, path validation and next-load request | Test with known NROs on hardware |
| Workspaces | Native grid model, empty cells, tile movement/resizing, active desktop persistence | Unit tests cover model/operations; hardware tests cover interaction |
| Appearance | Settings, themes, wallpapers, fonts, icon packs and per-workspace backgrounds | Decode and performance must be checked on device |
| Diagnostics | Early libnx hook plus SDL/runtime/catalogue logs | Log absence can indicate failure before SD mount/hook |

The standalone build does not call `webOfflineCreate` or `webConfigShow`, does not
launch the Horizon offline Web applet, and does not package HTML/React or a RomFS.

## HOME library applet and system backend

| Component | Responsibility | State |
|---|---|---|
| `projects/DeskNXMenu` | Library applet using the same native UI with system-applet IPC enabled | Buildable target; hardware lifecycle not certified |
| `projects/common` | Versioned protocol, state machine, timeout and safe-replace rules | Host tests cover parsers/rules/state transitions |
| `projects/hbloader` | Target-aware NRO loader | Must be hardware-tested with title/app return paths |
| `projects/sSystem` | Experimental qlaunch backend and ECS/lifecycle operations | Experimental; not a daily-driver HOME replacement |

## Known limits

- A successful NRO or applet build is not evidence of service permissions or runtime stability on a physical Switch.
- Standalone launch of an installed title uses libnx applet launch APIs; system-mode launch uses the backend IPC path.
- Standalone Homebrew chaining requires `envHasNextLoad()`; if unavailable, the UI reports that the user should use a supported hbmenu/title-override flow.
- System HOME takeover, sleep/wake, repeated app transitions and recovery require physical-hardware validation.
- Video wallpapers (MP4) are not supported; static JPG/PNG images are used.
