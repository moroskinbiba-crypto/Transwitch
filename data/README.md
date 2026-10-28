# Translation catalog format

The app loads `romfs/catalog.tsv` on first launch, then can refresh the live catalog from:

`https://raw.githubusercontent.com/moroskinbiba-crypto/Transwitch/main/romfs/catalog.tsv`

Each non-comment line has exactly 11 pipe-separated fields:

1. `id`: stable lowercase slug; use only ASCII letters, digits and hyphens.
2. `title`: game title.
3. `language`: target language.
4. `kind`: e.g. fan translation, partial translation, machine translation, voice patch.
5. `status`: e.g. Release or Work in progress.
6. `title_id`: 16 hexadecimal characters for the Switch title.
7. `layout`: `atmosphere_root` for archives containing `atmosphere/contents/<TITLE_ID>/romfs/...`, or `romfs_root` for archives whose files are relative to the game's `romfs/` folder.
8. `package_url`: direct HTTPS URL to a ZIP/7z/RAR patch archive. Leave empty for a discovery-only listing.
9. `source_url`: author's release page or primary project page.
10. `version`: patch version.
11. `notes`: short compatibility notes; do not include the pipe character.

The installer only writes inside the selected game's `atmosphere/contents/<TITLE_ID>/romfs/` directory. It does not install NSP/XCI files, system firmware, executables, or arbitrary archive paths. Before replacing an existing file it attempts to save a backup under `/switch/Transwitch/backups/<id>/`. The per-translation file list is stored under `/switch/Transwitch/installed/` and is used for uninstalling the patch.

Before publishing a catalog row, verify the direct package URL, that the archive contains only the patch (not copyrighted base game files), the target Title ID, supported game update versions, the author's source page, and any redistribution conditions. Prefer redirecting users to the author's own HTTPS release.
