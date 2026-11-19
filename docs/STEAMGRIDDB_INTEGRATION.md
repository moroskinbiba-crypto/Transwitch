# SteamGridDB artwork integration

## Player workflow

1. Open **Launcher Settings → SteamGridDB API Key** and paste an API key. The key is stored in the launcher's SD-card settings file and is not written to diagnostics.
2. Open **App Library**, select an installed game, then choose **ZL COVER** in the details panel (or press **ZL**).
3. The keyboard opens with the installed game's title. The query can be edited before searching.
4. Choose the matching SteamGridDB game, then choose one of its available **hero**, **grid**, or **icon** images.
5. The selected image is downloaded and published to the SD cache. The tile renderer reloads it without restarting the launcher.

The same installed title can be searched with a different query if the default title does not match SteamGridDB's catalogue.

## Implementation

- `source/SteamGridDbClient.*` performs HTTPS requests to the SteamGridDB API. The client supports autocomplete search, artwork listing for `heroes`, `grids`, and `icons`, and bounded PNG/JPEG downloads.
- Network work runs on a low-priority SDL worker. The frame loop only collects completed results; it does not wait for API calls or image downloads.
- `source/SteamGridDbCache.*` stores selected artwork under:
  `sdmc:/switch/DeskNX/steamgriddb/<TITLE_ID>/hero.img`
  Metadata is stored next to the image.
- `source/IconCache.hpp` checks the SteamGridDB cache before the built-in game icon, decodes the image on its existing worker, and uploads the texture on the render thread. The normal icon cache is invalidated after a new cover is published.
- Cached art is loaded locally on subsequent launches; an internet connection is not needed to display a previously downloaded cover.
- Downloads are HTTPS-only, have connection/total timeouts, and are size-bounded. The image loader has additional dimension and pixel limits.

## Persistence and privacy

The API key is stored in `sdmc:/switch/DeskNX/settings.ini` as `steamgriddb_key`. It is plain text on the SD card, as are the other launcher preferences. Do not share that file publicly while it contains a key. The key is not emitted into logs.

## Change log

- Added persistent API-key configuration to Launcher Settings.
- Added a game-library cover action for installed games.
- Added title search and manual SteamGridDB game-match selection.
- Added selection from hero, grid, and icon artwork returned by SteamGridDB.
- Added background download and SD-card cache publishing.
- Connected cached SteamGridDB artwork to the existing asynchronous tile icon pipeline.
- Added bounded handling for larger landscape artwork while keeping the decoded allocation capped at 4 MP.

## Current limitations

- The artwork chooser currently presents artwork type, dimensions, and asset ID as a list; it does not yet render thumbnail previews.
- The selected SteamGridDB image is stored in the per-title hero slot and is used as the tile artwork regardless of whether it came from the hero, grid, or icon endpoint.
- A dedicated **restore system icon / remove custom artwork** control has not yet been added to the picker.
