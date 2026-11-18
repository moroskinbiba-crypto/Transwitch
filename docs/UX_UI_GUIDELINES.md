# DeskNX — UI/UX implementation notes

## Navigation
The persistent header contains Gallery, Settings and Pads only. Quick tools remain accessible through L3/controller navigation and a compact touch overflow button beside Pads; Minus retains the power menu. Remaining header space is reserved for console profile, Nextendo presence, clock and battery.

## Home workspace
The 6 × 3 workspace, up to 12 independent desktops, empty cells and current drag/resize mechanics are preserved. Game/NRO tiles retain full-bleed artwork, title overlay, 10 px corners and per-desktop Cover/Contain/Stretch modes. Widgets retain information-first layouts.

## Application library
The library is an artwork-first 3 × 3 browser. Each card shows cover/icon, title and type/update badge. The fixed detail panel preserves game statistics and Homebrew file metadata. D-pad navigation and touch taps/swipes are supported; vertical swipes move by one row. A host-tested viewport projection keeps selection visible.

Search, filter, sort and rescan actions remain available. Scanning stays asynchronous and does not delay the first desktop frame.

## Settings
Options are visually grouped as Appearance (wallpaper/font/icon pack/theme/icon style/reset), Workspace (widgets/workspace management/edit shortcut), Library (rescan) and System (language). A view-to-action map preserves existing handlers and saved settings.

## Constraints
No persistence schema changed. The existing asynchronous artwork cache and image-fit pipeline are reused. MP4/video wallpaper, animated covers, weather and temperature widgets remain unexposed until a decoder/platform API/network design is validated.
