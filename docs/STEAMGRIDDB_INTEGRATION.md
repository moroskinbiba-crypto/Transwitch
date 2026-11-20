# SteamGridDB artwork integration

## Player workflow

1. Open **Launcher Settings → SteamGridDB API Key** and paste an API key. The key is stored in the launcher's SD-card settings file and is not written to diagnostics.
2. Open **App Library**, select an installed game, then choose **ZL COVER** in the details panel (or press **ZL**).
3. Transwitch searches using the installed game's title. Press **Y** to edit the query or search again.
4. Choose the matching SteamGridDB game. The artwork list then loads small thumbnails in the preview panel as you move through the choices.
5. Navigate with Up/Down or tap an artwork row to preview it. Press **A APPLY COVER** or tap the Apply button only after reviewing the preview. The full-size selected image is downloaded and published to the SD cache.
6. The selected cover is available offline and is loaded by the normal asynchronous icon pipeline.

The thumbnail is only a preview; applying still downloads the full artwork URL. If a thumbnail is unavailable, the UI indicates this and still allows the full image to be applied.

## Implementation

- `source/SteamGridDbClient.*` performs HTTPS requests to the SteamGridDB API. The client supports autocomplete search, artwork listing for `heroes`, `grids`, and `icons`, and bounded PNG/JPEG downloads.
- Search, artwork listing, thumbnail downloads, and full-image downloads use the low-priority SDL worker. The renderer thread only decodes the downloaded preview and creates/destroys its SDL texture.
- Preview thumbnails use SteamGridDB's `thumb` URL where present, falling back to the full URL only when no thumbnail URL is provided.
- Preview downloads are limited to the client's 8 MiB response cap; decoded images are limited to 2048 pixels per dimension and 4 MP. Stale preview results are discarded when the selected artwork changes.
- `source/SteamGridDbCache.*` stores selected artwork under:
  `sdmc:/switch/DeskNX/steamgriddb/<TITLE_ID>/hero.img`
  Metadata is stored next to the image.
- `source/IconCache.hpp` checks the SteamGridDB cache before the built-in game icon, decodes the image on its existing worker, and uploads the texture on the render thread. The normal icon cache is invalidated after a new cover is published.
- Cached art is loaded locally on subsequent launches; an internet connection is not needed to display a previously downloaded cover.
- Downloads are HTTPS-only, have connection/total timeouts, and are size-bounded.

## Persistence and privacy

The API key is stored in `sdmc:/switch/DeskNX/settings.ini` as `steamgriddb_key`. It is plain text on the SD card, as are the other launcher preferences. Do not share that file publicly while it contains a key. The key is not emitted into logs.

## Change log

- Added persistent API-key configuration to Launcher Settings.
- Added a game-library cover action for installed games.
- Added title search and manual SteamGridDB game-match selection.
- Added selection from hero, grid, and icon artwork returned by SteamGridDB.
- Added asynchronous artwork thumbnail preview before the user confirms applying a cover.
- Kept full-resolution image download separate from the thumbnail preview.
- Added background download and SD-card cache publishing.
- Connected cached SteamGridDB artwork to the existing asynchronous tile icon pipeline.
- Added bounded handling for larger landscape artwork while keeping the decoded allocation capped at 4 MP.
