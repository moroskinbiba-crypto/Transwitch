# DeskNX native feature inventory

This document describes the production native SDL2/C++ UI in `source/main.cpp` and its supporting modules. The old React/WebKit prototype is not part of the release build.

## 1. Runtime modes

| Build | Purpose | Game launch behavior |
|---|---|---|
| Standalone `DeskNX.nro` | Homebrew launched from Homebrew Menu | Launches through Horizon's public application-launch API and uses the profile preselected by the system. It cannot override that UID from the launcher's profile button. |
| HOME library applet + `projects/sSystem` backend | Experimental qlaunch replacement | The UI sends the explicitly selected `AccountUid` to the system backend. The backend pushes the PreselectedUser launch parameter before starting the title. This is the mode that supports choosing a player inside DeskNX. |

The HOME integration is separate from the standalone NRO. Successful compilation does not prove runtime compatibility with a particular HOS/Atmosphère version; follow the recovery and hardware-test guidance in [HOME_INTEGRATION.md](HOME_INTEGRATION.md).

## 2. Implemented native features

| Feature | Current implementation |
|---|---|
| Multiple workspaces (up to 12) | Independent workspace model and persisted layout |
| 6 × 3 desktop with empty cells | Explicit grid positions; empty cells remain available |
| Tile editing | Controller/touch placement, move, resize, remove, cross-workspace move |
| Active desktop restore | Separate active-workspace sidecar, restored at startup |
| Installed games library | NS installed-title enumeration plus NACP/control metadata; no eShop/ApplicationView update query |
| Homebrew library | Bounded SD-card NRO scanner and path validation |
| Library tools | Search, filtering, sorting, recent-launch view, favorites |
| Game artwork | NACP icons, SD user icons/covers, icon packs, SteamGridDB search/cache/apply |
| Appearance | Themes, accent, wallpaper, per-desktop background fit, cover fit, title-overlay opacity and custom fonts |
| Input | Joy-Con/Pro Controller navigation and touch gestures |
| Persistence | Atomic layout/settings writes with backup/rollback paths; recent launches, favorites and calendar notes stored separately |
| Widgets | Clock, battery (console and Joy-Con), system monitor, storage (used/free), recent launches, calendar/day notes, favorites, game library, quick settings, Nextendo |
| Widget interaction | Per-widget configuration, detail/catalogue actions, responsive tile-size layouts; optional random game launch in Game Library widget |
| Play statistics | On-demand PDM playtime/launch statistics for the selected installed game |
| Nextendo player presence | Online counts by title, freshness/expiry handling; unavailable data is not fabricated as zero |
| Nextendo game compatibility | Signed, expiring game-version catalogue; local installed version is compared with accepted/recommended policy |
| Modal surfaces | Central surface stack for input ownership, focus and backdrop composition |

## 3. Explicitly limited or unfinished

- The Nextendo friends list is not implemented yet. It requires a user-consented OAuth flow with the `friends` scope; the expanded widget currently explains this requirement rather than showing fabricated entries.
- Donor-title Homebrew Application Mode is intentionally disabled. Homebrew NRO launch uses the supported library-applet route in HOME mode or the standalone chain-load path where available.
- Standalone mode uses the profile already selected by Horizon. The in-launcher player selector is available only in HOME system-applet mode, where the backend can pass a selected UID.
- MP4/video wallpaper, achievements, MTP, Discord, Spotify/music and a theme shop are not complete features and are not included in the production feature claims.

## 4. Validation boundaries

Host tests cover pure logic such as layout/persistence parsing, grid operations, touch gestures, widget configuration, IPC validation/state rules, signed Nextendo catalog parsing, artwork geometry, icon registry and surface-stack behavior.

CI builds the standalone NRO and HOME library applet. The experimental system backend has its own toolchain-dependent build. None of these jobs emulates Horizon services or substitutes for testing actual game launch, HOME/suspend/resume, touch/controller operation, network behavior, SD removal and frame-time/memory behavior on the target console.
