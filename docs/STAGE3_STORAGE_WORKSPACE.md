# Stage 3A — Storage & Workspace Architecture

## Scope

This increment establishes one shared persistence contract for the DeskNX workspace snapshot. It does not claim that every settings, catalogue, cache, or history file has already moved into a single storage service.

The canonical workspace model remains `desknx::model::Workspace`, and the deployed text formats remain unchanged:
- `DESKNX1`–`DESKNX4` for the full layout;
- `DESKNX-ACTIVE1` for the active-workspace sidecar.

No existing SD-card workspace file is deleted or rewritten as part of this code change. Normal writes continue to keep `.bak` files.

## What changed

### Shared storage contract

`include/desknx/WorkspaceStorage.hpp` now owns:
- the canonical layout, temp, backup, active-marker and marker-backup paths;
- bounded primary/backup reads and validation through the production layout parser;
- generation-aware active-marker validation;
- a single-file marker write path for active-desktop changes;
- a two-file full-snapshot publication path with injectable write/rename/SD-commit operations.

The active marker includes the layout shape hash and active workspace name. It is only accepted when it matches the layout generation selected at startup. This prevents a marker published for a newer layout from selecting an invalid workspace in an older recovered backup.

### Full snapshot transaction

The shared writer performs the following sequence:

1. Serialize and write the full layout and active marker to their temporary files.
2. Commit the temporary files to SD.
3. Publish the marker while preserving its previous generation in the marker backup.
4. Publish the full layout while preserving its previous generation in the layout backup.
5. Commit the published pair.

If layout publication fails after the marker has been published, the layout replacement rolls back. On the next load, the new marker fails the old layout's shape check and the matching marker backup is selected. The layout's embedded active index remains the final fallback when no matching marker exists.

The active-marker-only path remains lightweight: callers that already built a validated marker can write it directly without copying or serializing the complete workspace vector.

### Production integration

The standalone launcher in `source/main.cpp` now routes:
- synchronous full-layout saves;
- asynchronous full-layout saves;
- compact asynchronous active-workspace saves;
- startup layout reads;
- active-workspace marker reads/writes

through the shared repository.

`HomeNXWorkspaceFileStore` is now an adapter over the same storage API. The old `LayoutStore` is reduced to a compatibility facade; it no longer maintains its own file replacement, backup deletion, and rollback algorithm.

The experimental HOME prototype no longer treats the incomplete `workspaces.json` desktop-name list as a second source of workspace truth. It loads and saves complete tile/layout snapshots through the shared `layout.tsv` repository. Its `PersistenceStore` implementation and old JSON files are not deleted by this change.

## Tests and CI

`tests/workspace_storage_test.cpp` covers:
- complete snapshot save/load and commit boundaries;
- failure while writing the second temporary file;
- failure at the pre-publication SD commit;
- partial publication where the active marker is published but layout publication fails;
- rollback and matching marker-backup recovery;
- restore from a corrupted primary layout;
- creation of a missing workspace directory;
- active-marker-only writes without rewriting the layout;
- the pre-serialized marker path used by the background worker.

The new test is included in both native build and experimental HOME-backend workflows.

## Explicit limitations

- No physical Nintendo Switch / Atmosphère runtime test is claimed here.
- The experimental HOME prototype is not the production HOME applet entry point; the project Makefile continues to build the root `source/main.cpp` production UI.
- The legacy JSON `PersistenceStore` schema is not yet migrated to the canonical model. This change removes its use as an alternate workspace source in the prototype, but does not delete or automatically transform any JSON files.
- Settings, recent history, favorites, calendar notes, catalogue caches, artwork caches and media assets still have their own persistence paths and lifecycle rules.

## Next Stage 3 increments

1. Storage control plane: shared path registry, SD mount/removal state, serialized commit ownership, diagnostics and explicit retry/backoff policy for all persistent domains.
2. Migration manager: inventory legacy formats, schema versions, dry-run validation, safe promotion with retained originals, and migration tests for each data family.
3. Workspace identity: stable workspace IDs rather than relying solely on vector positions/names; preserve active selection across reorder/rename while remaining backward-compatible with the current marker.
4. Persistence lifecycle: retention and recovery policy for backups/temp files, crash-recovery matrix and filesystem fault-injection coverage across settings/history/favorites.
5. Hardware acceptance: boot, repeated desk switch, edit/resize/move, abrupt power loss, SD removal, recovery from backup, and restart validation on the target Switch.

Stage 3A is an implementation increment, not a claim that the entire Stage 3 roadmap is complete.
