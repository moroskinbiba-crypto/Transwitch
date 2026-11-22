# Stage 3B — Storage Control Plane & Migration Manager

## Scope

This increment builds on Stage 3A's shared workspace repository. It centralizes the SD durability boundary and introduces a versioned, testable migration pipeline for the production file formats.

It does not yet move every file family to one global path registry or replace every domain-specific debounce scheduler.

## Storage control plane

`include/desknx/StorageControlPlane.hpp` provides a platform-independent control plane for SD mount generations and commits.

- Every mounted/unmounted state transition advances a generation.
- A commit must acquire an exclusive lease for the current generation.
- Parallel commit requests are rejected instead of racing `fsdevCommitDevice`.
- A lease from a previous SD generation is rejected as stale.
- Failed commits enter wrap-safe exponential backoff: 250 ms, 500 ms, 1 s … capped at 30 s.
- A successful commit clears the failure count and retry deadline.

The production `commitSdCard()` function now routes all callers through this control plane while retaining the existing SDL mutex around the libnx commit operation. If that mutex cannot be created, commit fails closed rather than allowing concurrent workers to race. This includes workspace, settings, history and auxiliary snapshot writers that already use the shared commit callback.

The mount state is initialized from the verified `statvfs("sdmc:/")` probe at startup. The state object supports removal/remount generation changes; periodic hot-plug detection and automatic stale-device remount are not enabled in this increment.

## Migration manager

`include/desknx/StorageMigration.hpp` provides:
- bounded schema-header inspection;
- explicit minimum-readable and current versions;
- rejection of malformed, unsupported legacy and future-version inputs;
- contiguous or direct migration-step plans;
- dry-run plan validation;
- in-memory transforms with per-step and final validation;
- output isolation: the caller's destination buffer is changed only after the full migration validates.

The migration engine does not write files itself. Callers must publish validated bytes with `ProductionPersistence::writeAtomically`, which retains the old primary as `.bak`.

### Registered production formats

| Domain | Known versions | Current |
|---|---|---|
| Workspace layout | DESKNX1–DESKNX4 | DESKNX4 |
| Launcher settings | DESKNXSET1–DESKNXSET2 | DESKNXSET2 |
| Launch history | DESKNXH1 | DESKNXH1 |
| Active workspace | DESKNX-ACTIVE1 | DESKNX-ACTIVE1 |

Format-specific steps live in `include/desknx/StorageMigrations.hpp`.

### Applied migrations

- **Workspace:** versions 1–3 are parsed by the production layout parser and converted through the canonical DESKNX4 serializer. Startup validates the conversion in memory and compares it with the canonical serialization of the loaded model. A mismatch blocks rewrite; the existing checked layout-save path performs publication only after the outputs agree, retaining its backup.
- **Settings:** DESKNXSET1 is converted to DESKNXSET2. Legacy theme values are normalized using the exact old runtime mapping (0–7 → 0, 8 → 1); duplicate keys and unknown key/value rows are preserved. A migration is automatically published only when the source was the validated primary file. If startup recovered settings from `.bak`, automatic migration is deferred so the recovered backup is not overwritten by a potentially corrupt primary.

History and active-workspace formats are registered and version-checked, but have no conversion steps because their current schema is already the deployed format.

## Tests

New host test targets:
- `storage_control_plane_test.cpp`: mount generations, exclusive commit leases, stale lease rejection, backoff growth/cap, successful retry reset and tick wraparound.
- `storage_migration_test.cpp`: version gates, dry-run immutability, chained plans, future-schema rejection, malformed plans, failed transform/output validation and unchanged destination output.
- `storage_migration_adapters_test.cpp`: real DESKNX3→DESKNX4 and DESKNXSET1→DESKNXSET2 conversions, preservation of unknown settings rows, legacy theme mapping, and atomic migration publication with the old file retained in `.bak`.

All three are included in both build workflows.

## Explicit limitations / follow-up

1. The SD mount generation is wired at verified startup. Runtime removal/reinsert detection and safe remount while writer threads may be active still need a coordinated lifecycle gate and physical testing.
2. Per-domain dirty/retry scheduling remains partly distributed across workspace, auxiliary and launcher-settings schedulers. The central backoff currently protects the shared durability commit boundary.
3. Favorites, calendar notes, game-name overrides, artwork/cache manifests and catalogue snapshots need explicit schema descriptors and migration validators before any conversion.
4. The old JSON `PersistenceStore` files are not deleted or auto-migrated. They remain legacy data until a dedicated import/export plan is reviewed.
5. Stable workspace IDs, backup retention/cleanup, migration audit reports, and Nintendo Switch hardware acceptance remain open.

No changes are made to `main`; this branch is based on the Stage 3A head and remains a separate draft increment.
