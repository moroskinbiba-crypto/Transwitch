# DeskNX — UI/UX implementation notes

## Navigation
The persistent header contains Gallery, Settings and Pads only. Quick tools remain accessible through L3/controller navigation and a compact touch overflow button beside Pads; Minus retains the power menu. Remaining header space is reserved for console profile, Nextendo presence, clock and battery.

## Home workspace
The 6 × 3 workspace, up to 12 independent desktops, empty cells and current drag/resize mechanics are preserved. Game/NRO tiles retain full-bleed artwork, title overlay, 10 px corners and per-desktop Cover/Contain/Stretch modes. Widgets retain information-first layouts.


## Reusable component layer

Use `source/UiComponents.hpp` for repeated surfaces and controls rather than reimplementing their states at each call site. Shared components include layered surfaces, buttons (Glass, Rail, Minimal, Chip and Solid) and dividers. Active, focused and disabled states are resolved independently from input routing.

The active theme supplies accent/focus tokens. The common corner radius remains capped at 10 px to preserve existing launcher geometry. The shared soft-card helper now uses the component surface renderer, so workspace, widget and modal panels share the same shadow, fill and inset-edge treatment. Header controls, desktop tabs, footer action chips and library filter chips use the shared button renderer.

Component adoption must not change layout bounds, touch hit regions, controller mappings, keyboard ownership or game/homebrew launch routing. Keep icon textures and artwork on their existing caches; UI components must not perform file I/O or create textures from worker threads.

## Widgets

Widgets are UI components, never title-launch targets. A tile click is routed to a dedicated widget handler; it must not reach the game/NRO chain-load path. Keep widget actions safe when a feed or system statistic is unavailable.

Rendering must adapt to the actual tile footprint instead of drawing one fixed layout and clipping it. Supported footprints are 1×1, 2×1, 3×1, 2×2, 3×2 and 2×3. Compact tiles show a single useful datum/row, banners show a short summary, and larger/portrait tiles expose additional rows or detail. Keep text and progress bars inside the measured content bounds.

The Nextendo widget is a separate live-ranking component. It shows popular games sorted by the latest per-title player counts, merges regional variants into one game row, and opens a larger ranked list on activation. Do not invent player counts, label unknown title IDs with guessed game names, or keep expired online data visible. Ranking is prepared when a network snapshot arrives and the renderer reads a small fixed-size cache.


### System and storage data

The storage widget keeps SD free/total space and, at larger sizes, adds the number of installed titles, discovered Homebrew NROs and current-process memory usage/limit. These are local measurements; counts are refreshed with the catalogue/storage cadence.

The battery widget shows the Switch battery separately from the left and right Joy-Con. Joy-Con charge is read through libnx HID power information, whose battery level is a coarse 0–4 step value; show the step count and charging state rather than presenting it as a precise percentage.

The system widget shows both Horizon OS firmware and the running Atmosphère version. Query the latter through SPL/Exosphere config item 65000; show an unavailable state when the service/config cannot be read.

### Installed application versions

Show the installed control.nacp DisplayVersion in the library card and detail panel. Do not replace it with an online version guess. Use the Nintendo NS application-view NeedsUpdate flag only as update-status metadata: green means NS returned a view with no update hint, yellow means NS marks an update available, and muted means the update status is unknown. Never treat missing NS data as proof that an application is up to date.

## Application library
The library is an artwork-first 3 × 3 browser. Each card shows cover/icon, title and installed-version/update badge. The selected game's detail panel shows installed size from NS occupied-size metadata, the installed NACP DisplayVersion, fresh Nextendo per-title online players (or UNKNOWN when the title is not in the fresh feed), and PDM play/launch history. The installed version string is local; the NS flag only indicates update availability and is not a remote version comparison.

Game detail actions support persistent Rename and Choose icon. Rename is stored separately from NACP and reapplied after catalogue refresh. A one-off icon is stored under media/game-icons/<TITLEID>.<ext> and takes precedence over the selected icon pack. ZL/ZR are the controller shortcuts. The normal add-to-workspace library opens with Games selected; the dedicated widget picker retains its Widgets filter.

Search, filter, sort and rescan actions remain available. Scanning stays asynchronous and does not delay the first desktop frame. Game occupied-size data is collected in the background and stored in the catalogue cache; live player counts use the existing Nextendo feed and show UNKNOWN rather than inventing zero when the feed is stale or the title is not listed.

## Settings
Options are visually grouped as Appearance (wallpaper/font/icon pack/theme/accent/SteamGridDB artwork key), Workspace (widgets/workspace management/edit shortcut), Library (rescan) and System (language/icon style/reset). Theme and accent rows are left/right carousels: controller or touch arrows change the selected value without closing Settings. The API key is stored locally in settings.ini as plaintext and is not sent anywhere yet; remote SteamGridDB artwork lookup is a later integration step.

## Constraints
The catalogue cache schema is now DESKNXCAT5 to store per-game occupied size. Custom game display names are stored separately in cache/game-overrides.tsv and individual game art in media/game-icons/. The existing asynchronous artwork cache and image-fit pipeline are reused. MP4/video wallpaper, animated covers, weather and temperature widgets remain unexposed until a decoder/platform API/network design is validated.
