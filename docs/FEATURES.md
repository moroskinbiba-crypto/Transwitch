# Launcher feature inventory

Where each launcher capability lives today, what it needs to reach a console, and what
disappears if nobody ports it. HomeNX paths refer to the pinned upstream
(`ad3333m/homenx@8756d38`), Transwitch paths to this repository.

## 1. What actually works on a console today

`DeskNX.nro` is a web-shell: `src-switch/main.cpp` opens the React bundle inside the Horizon
OS WebKit applet. So on hardware you get **the visuals and the input**, nothing more.

| Works | Does not work |
|---|---|
| Drawing the home screen, switching desktops, drag/resize/remove tiles, editing names and wallpapers | Reading installed games (no `ns:` access) |
| Touch and D-pad navigation | Launching a game or an NRO |
| Persisting the layout to the webview's `localStorage` | Reading `pdm:qry` playtime, `psm` battery, temperatures |
| | Album, controllers, profiles as *system* objects |

Everything below marked "simulated" is real code with a mock data source.

## 2. Implemented in the web UI

| Feature | Where | State on hardware |
|---|---|---|
| Up to 12 independent workspaces, 6×3 grid, five tile shapes, empty cells as state | `src/App.tsx`, `src/utils/desknxLayout.ts`, `src/components/DeskNXGrid.tsx` | works (visual) |
| Drag to move, resize handles, remove, move tile to another desktop | `DeskNXGrid.tsx`, `App.tsx` | works (visual) |
| Per-desktop wallpaper, fit, artwork mode, label opacity, accent colour, blur/brightness | `UnifiedDeskCustomizer.tsx`, `desknxSettings.ts` | works (visual) |
| Widgets: clock, battery, storage, recent activity, system tools | `src/components/Widgets.tsx` | renders; data is mock |
| Themes, fonts, font scaling | `SettingsModal.tsx`, `desknxSettings.ts` | works (visual) |
| Dock, app drawer, top/bottom bars, Joy-Con overlay | `BottomBar.tsx`, `UlauncherHub.tsx`, `JoyconOverlay.tsx` | works (visual) |
| User profiles with separate desktops | `UserProfileModal.tsx` | works (visual, `localStorage`) |
| Album viewer, controller test, diagnostics, settings import/export | `AlbumModal.tsx`, `ControllersModal.tsx`, `DiagnosticsModal.tsx` | simulated |
| Catalogue of installed games and homebrew | `src/data/mockCatalog.ts` | simulated — 24 demo entries |
| Launch flow with play statistics | `AppLaunchModal.tsx`, `utils/playStats.ts` | simulated (statistics no longer invented, see #12) |
| UI sound effects | `SoundEngine` in `src/data/mockCatalog.ts` — WebAudio oscillators | beeps from the synthesizer, no audio files |
| Hardware bridge | deleted in #14 (was `switchHardwareService.ts`) | never existed |

## 3. HomeNX subsystems available to port

| Feature | HomeNX source | What it needs | Cost |
|---|---|---|---|
| **Cover art: SteamGridDB** | `projects/menu/src/SteamGridDb{Manager,Backdrop,PickerScreen,Tab}.cpp`, `CardsActivity.cpp` (`steamGridDbKey`, `steamGridDbFind`) | API key from steamgriddb.com → Settings > Look; fallback GameTDB → eShop banner; cache keyed by title ID | medium — HTTP + cache + picker UI |
| **Online players (Nextendo/Prelude)** | `CardsActivity.cpp` (`m_players`, per-minute refresh, green dots, total pill, Settings toggle), `openNextendo(mode)` | `nextendo.nro` on SD, network to `https://nextendo.network`, Prelude switches network and restarts the console | medium — background fetch + opt-out |
| **Music player** | `projects/cards/src/Audio.{hpp,cpp}` (SDL_mixer): playlist from `sdmc:/switch/SwitchU/sounds/wiiu/music` (`.mp3`/`.ogg`), shuffle, repeat, skip, volume, fade before launching a game, pause on sleep/applet, position/duration, 48-band spectrum for a visualiser | SDL_mixer in the portlibs, one decode buffer in memory | high — audio device + thread + UI |
| **Quick Settings** | `QuickSettingsOverlay.cpp` | overlay rendering, volume/brightness/sleep actions | low — pure UI over existing APIs |
| **Notifications** | `NotificationBell/Panel/Log.cpp` | event sources (downloads, screenshots, friends) | medium |
| **Lock screen** | `LockScreen.cpp` | background + input gate before the grid | low |
| **Profile carousel** | `ProfileCarouselScreen.cpp` | account service | medium |
| **Friends / Discord / Miis** | `SocialScreen.cpp` + `friends/FriendStore.hpp`, `social/DiscordService.hpp`, `social/MiiStore.hpp` | friends service, Discord RPC, Mii data | high |
| **Theme shop** | `ThemeShopScreen.cpp`, `ThemeCatalogClient.cpp`, `ThemeHttp.cpp`, `ThemePackageInstaller.cpp` | HTTP, package install, theming pipeline | high |
| **Folders, zoom** | `FolderStore.cpp`, `FolderZoom.cpp`, `FolderOptionsScreen.cpp` | folder model + animation | medium |
| **Widgets (native)** | `WidgetStore.cpp`, `WidgetModel.cpp`, `BatteryWidget.cpp`, `DateTimeWidget.cpp`, `TitlePillWidget.cpp` | widget model already mirrored by Transwitch | low |
| **Achievements** | `projects/achievements/` (packs per title, e.g. `packs/0100000000010000.txt`) | memory-watch per title, Tesla overlay | high, entirely separate app |
| **Bluetooth audio sink (A2DP)** | `projects/a2dp-sink/` | BT stack, profile work | very high, experimental |
| **Game cards / manager / updater** | `projects/cards`, `projects/manager`, `projects/updater` | their own frontends | high |

Note: HomeNX's music, covers and player counts all live in its **cards** app, not necessarily in
the HOME menu — porting them means porting the data layer, then drawing it in our own UI.

## 4. What disappears unless deliberately ported

| At risk | Why |
|---|---|
| Web-only interaction polish — toasts, transitions, CSS effects | React/CSS do not exist in the `nxui` path; each becomes native drawing code or is lost |
| The demo catalogue (`mockCatalog.ts`) | Only the WebKit path uses it. Useful for design work, dead weight in the native build |
| Album / controller / profile screens as currently written | They are simulated views; the native versions must talk to real services |
| The 6×3 desktop model itself | Safe — it is already mirrored natively (`Model.hpp`, workspace layer from #13) and guarded by the tile-contract check |
| `sound` module (synthesised beeps) | Replaced by HomeNX's real sound set from the SD card, or lost if nobody wires audio |

## 5. Recommended order

1. **SteamGridDB covers** and **Nextendo players** — highest perceived value, both are pure
   network + cache work and do not touch the system lifecycle. They need Phase 2's native data
   path first (see `docs/HOMENX_FRONTEND_PLAN.md` §4a).
2. **Quick Settings, lock screen, native widgets** — cheap UI wins on top of what already exists.
3. **Music player** — big feature, but needs SDL_mixer and an audio device in the menu process;
   do it after the menu builds reliably as a HOME applet.
4. **Notifications, folders, theme shop** — medium, depend on event sources that do not exist yet.
5. **Achievements, A2DP, social** — separate products; do not let them block the launcher.

{"content":"# Launcher feature inventory\n\nWhere each launcher capability lives today, what it needs to reach a console, and what\ndisappears if nobody ports it. HomeNX paths refer to the pinned upstream\n(`ad3333m/homenx@8756d38`), Transwitch paths to this repository.\n\n## 1. What actually works on a console today\n\n`DeskNX.nro` is a web-shell: `src-switch/main.cpp` opens the React bundle inside the Horizon\nOS WebKit applet. So on hardware you get **the visuals and the input**, nothing more.\n\n| Works | Does not work |\n|---|---|\n| Drawing the home screen, switching desktops, drag/resize/remove tiles, editing names and wallpapers | Reading installed games (no `ns:` access) |\n| Touch and D-pad navigation | Launching a game or an NRO |\n| Persisting the layout to the webview's `localStorage` | Reading `pdm:qry` playtime, `psm` battery, temperatures |\n| | Album, controllers, profiles as *system* objects |\n\nEverything below marked \"simulated\" is real code with a mock data source.\n\n## 2. Implemented in the web UI\n\n| Feature | Where | State on hardware |\n|---|---|---|\n| Up to 12 independent workspaces, 6×3 grid, five tile shapes, empty cells as state | `src/App.tsx`, `src/utils/desknxLayout.ts`, `src/components/DeskNXGrid.tsx` | works (visual) |\n| Drag to move, resize handles, remove, move tile to another desktop | `DeskNXGrid.tsx`, `App.tsx` | works (visual) |\n| Per-desktop wallpaper, fit, artwork mode, label opacity, accent colour, blur/brightness | `UnifiedDeskCustomizer.tsx`, `desknxSettings.ts` | works (visual) |\n| Widgets: clock, battery, storage, recent activity, system tools | `src/components/Widgets.tsx` | renders; data is mock |\n| Themes, fonts, font scaling | `SettingsModal.tsx`, `desknxSettings.ts` | works (visual) |\n| Dock, app drawer, top/bottom bars, Joy-Con overlay | `BottomBar.tsx`, `UlauncherHub.tsx`, `JoyconOverlay.tsx` | works (visual) |\n| User profiles with separate desktops | `UserProfileModal.tsx` | works (visual, `localStorage`) |\n| Album viewer, controller test, diagnostics, settings import/export | `AlbumModal.tsx`, `ControllersModal.tsx`, `DiagnosticsModal.tsx` | simulated |\n| Catalogue of installed games and homebrew | `src/data/mockCatalog.ts` | simulated — 24 demo entries |\n| Launch flow with play statistics | `AppLaunchModal.tsx`, `utils/playStats.ts` | simulated (statistics no longer invented, see #12) |\n| UI sound effects | `SoundEngine` in `src/data/mockCatalog.ts` — WebAudio oscillators | beeps from the synthesizer, no audio files |\n| Hardware bridge | deleted in #14 (was `switchHardwareService.ts`) | never existed |\n\n## 3. HomeNX subsystems available to port\n\n| Feature | HomeNX source | What it needs | Cost |\n|---|---|---|---|\n| **Cover art: SteamGridDB** | `projects/menu/src/SteamGridDb{Manager,Backdrop,PickerScreen,Tab}.cpp`, `CardsActivity.cpp` (`steamGridDbKey`, `steamGridDbFind`) | API key from steamgriddb.com → Settings > Look; fallback GameTDB → eShop banner; cache keyed by title ID | medium — HTTP + cache + picker UI |\n| **Online players (Nextendo/Prelude)** | `CardsActivity.cpp` (`m_players`, per-minute refresh, green dots, total pill, Settings toggle), `openNextendo(mode)` | `nextendo.nro` on SD, network to `https://nextendo.network`, Prelude switches network and restarts the console | medium — background fetch + opt-out |\n| **Music player** | `projects/cards/src/Audio.{hpp,cpp}` (SDL_mixer): playlist from `sdmc:/switch/SwitchU/sounds/wiiu/music` (`.mp3`/`.ogg`), shuffle, repeat, skip, volume, fade before launching a game, pause on sleep/applet, position/duration, 48-band spectrum for a visualiser | SDL_mixer in the portlibs, one decode buffer in memory | high — audio device + thread + UI |\n| **Quick Settings** | `QuickSettingsOverlay.cpp` | overlay rendering, volume/brightness/sleep actions | low — pure UI over existing APIs |\n| **Notifications** | `NotificationBell/Panel/Log.cpp` | event sources (downloads, screenshots, friends) | medium |\n| **Lock screen** | `LockScreen.cpp` | background + input gate before the grid | low |\n| **Profile carousel** | `ProfileCarouselScreen.cpp` | account service | medium |\n| **Friends / Discord / Miis** | `SocialScreen.cpp` + `friends/FriendStore.hpp`, `social/DiscordService.hpp`, `social/MiiStore.hpp` | friends service, Discord RPC, Mii data | high |\n| **Theme shop** | `ThemeShopScreen.cpp`, `ThemeCatalogClient.cpp`, `ThemeHttp.cpp`, `ThemePackageInstaller.cpp` | HTTP, package install, theming pipeline | high |\n| **Folders, zoom** | `FolderStore.cpp`, `FolderZoom.cpp`, `FolderOptionsScreen.cpp` | folder model + animation | medium |\n| **Widgets (native)** | `WidgetStore.cpp`, `WidgetModel.cpp`, `BatteryWidget.cpp`, `DateTimeWidget.cpp`, `TitlePillWidget.cpp` | widget model already mirrored by Transwitch | low |\n| **Achievements** | `projects/achievements/` (packs per title, e.g. `packs/0100000000010000.txt`) | memory-watch per title, Tesla overlay | high, entirely separate app |\n| **Bluetooth audio sink (A2DP)** | `projects/a2dp-sink/` | BT stack, profile work | very high, experimental |\n| **Game cards / manager / updater** | `projects/cards`, `projects/manager`, `projects/updater` | their own frontends | high |\n\nNote: HomeNX's music, covers and player counts all live in its **cards** app, not necessarily in\nthe HOME menu — porting them means porting the data layer, then drawing it in our own UI.\n\n## 4. What disappears unless deliberately ported\n\n| At risk | Why |\n|---|---|\n| Web-only interaction polish — toasts, transitions, CSS effects | React/CSS do not exist in the `nxui` path; each becomes native drawing code or is lost |\n| The demo catalogue (`mockCatalog.ts`) | Only the WebKit path uses it. Useful for design work, dead weight in the native build |\n| Album / controller / profile screens as currently written | They are simulated views; the native versions must talk to real services |\n| The 6×3 desktop model itself | Safe — it is already mirrored natively (`Model.hpp`, workspace layer from #13) and guarded by the tile-contract check |\n| `sound` module (synthesised beeps) | Replaced by HomeNX's real sound set from the SD card, or lost if nobody wires audio |\n\n## 5. Recommended order\n\n1. **SteamGridDB covers** and **Nextendo players** — highest perceived value, both are pure\n   network + cache work and do not touch the system lifecycle. They need Phase 2's native data\n   path first (see `docs/HOMENX_FRONTEND_PLAN.md` §4a).\n2. **Quick Settings, lock screen, native widgets** — cheap UI wins on top of what already exists.\n3. **Music player** — big feature, but needs SDL_mixer and an audio device in the menu process;\n   do it after the menu builds reliably as a HOME applet.\n4. **Notifications, folders, theme shop** — medium, depend on event sources that do not exist yet.\n5. **Achievements, A2DP, social** — separate products; do not let them block the launcher.\n"}