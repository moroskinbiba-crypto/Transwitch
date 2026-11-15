# HomeNX — architecture audit and selected Transwitch ports

Audit date: 16 November 2026

Upstream repository: https://github.com/ad3333m/homenx
Audited ref: c744796cf686ab3886ffc7c432af7bdf4411bacd
License: GPL-3.0.

## Summary

HomeNX is not a single NRO that also happens to replace HOME. Its production build
separates a privileged system daemon from a native frontend library applet. The daemon
owns the HOME/application lifecycle, title launch, sleep/wake, ECS registration and
system event processing. The menu is a renderer/input process that communicates with
the daemon using a typed SMI protocol and applet storage. A separate Homebrew build
reuses the frontend code but runs as an Application NRO without the daemon.

Transwitch already has a similar split in projects/sSystem + projects/DeskNXMenu,
and a common native UI in source/main.cpp. The right approach is to make these
paths consistent and port proven lifecycle rules, not to replace the native UI with
HomeNX wholesale.

## Findings and selected changes in this pass

### 1. Explicit process applet type

HomeNX's projects/menu/src/main.cpp explicitly declares AppletType_Application for
its standalone Homebrew build and AppletType_LibraryApplet for its system HOME frontend.
Transwitch's shared source/main.cpp did not define __nx_applet_type; standalone startup
therefore depended on the launch environment's default. This pass makes the mode explicit:
- standalone NRO: AppletType_Application;
- system HOME frontend: AppletType_LibraryApplet.

The startup diagnostic now logs the selected applet type. This is an important launch
contract fix, but only physical-console testing can confirm whether it resolves the
reported startup exit.

HomeNX fixes its standalone heap to 208 MiB (0xD000000). Transwitch intentionally
keeps libnx's dynamic heap allocation for now: current launch paths may supply a heap
override, and forcing a large fixed heap can abort when the current process limit is
smaller. Revisit this only after capturing the actual launch mode and available heap
from a device log.

### 2. Result type compatibility in experimental backend

The qlaunch build used an Atmosphère ams::Result and attempted to cast it directly
to unsigned int. The current libnx/Stratosphere build rejects that conversion.
The log now formats sdMountRc.GetValue(), which is the explicit Result-code accessor
already used elsewhere in the same backend. This repairs the backend compile failure
without changing the runtime decision.

### 3. Build-time regression guards

The standalone guard now fails if the NRO process type is not explicitly
AppletType_Application. The HOME entrypoint guard checks for
AppletType_LibraryApplet. These prevent the two modes from silently drifting.

## Architecture/features worth porting next

| HomeNX component | Why it matters | Transwitch destination |
|---|---|---|
| Daemon-owned application session state machine | Bounded graceful exit, forced-termination fallback, accessor cleanup, explicit states | projects/sSystem/source/Application.cpp |
| Event-driven LibraryApplet runner | Waits on applet state/message/general-channel events rather than a tight poll | projects/sSystem/source/LibraryApplet.cpp |
| Menu holder and liveness handling | Validates lazy command-event handle before waiting; bounded stop/relaunch; avoids invalid-handle busy loops | projects/sSystem/source/main.cpp, MenuLiveness.* |
| Daemon-owned application catalogue | One service owner scans NS records, batches ApplicationView queries and publishes one catalog to the frontend | projects/sSystem, source/catalog.cpp |
| Metadata/icon cache | NACP/name/icon cached independently; frontend fetches icons lazily and prefetches a bounded initial set | source/slaunch_applications.*, IconCache.* |
| Homebrew as a borrowed application | ECS serves hbloader as an installed host title so homebrew can use full application memory | projects/sSystem, projects/hbloader |
| Touch editing | Ghost/preview state, edge page switching, folder-hover and hold-to-drag | source/main.cpp, TouchGesture.hpp |
| Persistent UI model | Dedicated grid/folder/widget models with transactional settings/layout storage | include/desknx/Model.hpp, source/Layout.cpp, persistence modules |

## Important differences and constraints

- HomeNX uses a newer nxui renderer with Deko3D by default and optional SDL2 backend.
  Its UI is much larger than Transwitch's current SDL2 UI; wholesale import would also
  import many dependencies and GPL-3.0 license obligations.
- HomeNX's standalone NRO and system HOME build are distinct compile modes. The
  standalone NRO uses envSetNextLoad for chaining; its system build delegates title
  and Homebrew application launch to the daemon.
- The HomeNX full-application Homebrew route depends on an installed host title,
  Atmosphère ECS and a compatible hbloader, and is not equivalent to a simple
  envSetNextLoad call. It requires separate hardware tests and reliable cleanup.
- HomeNX's most recent source disables its achievements/game-memory watcher; that
  subsystem is deliberately not imported.
- HomeNX root license is GPL-3.0. No HomeNX source files or assets were copied in this
  audit pass; only a small process-configuration pattern and the Result accessor fix
  were applied to Transwitch. If larger upstream source modules are imported later,
  preserve their copyright/license notices and update the distribution/license
  policy before releasing combined binaries.

## Verification boundary

- The previous Transwitch native NRO build and its NRO0/ASET/NACP/no-RomFS validation
  passed at commit 801234b992ea63476a6f29670722448344944d23:
  https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43768488600.
- The previous host tests and HOME library applet build passed:
  https://github.com/moroskinbiba-crypto/Transwitch/actions/runs/43768488594.
- The experimental backend failed compiling projects/sSystem/source/main.cpp
  because ams::Result was cast directly to an unsigned integer. Fixed in this pass.
- Physical Switch: NOT RUN. The latest NRO must be installed and launched on the user's
  console; the old 2808-1037 / startup-exit issue is not marked resolved until that test.
