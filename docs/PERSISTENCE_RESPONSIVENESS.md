# Transwitch responsive persistence

## Problem

Frequent workspace actions previously called the persistence enqueue path synchronously. Even when the actual file write was on a worker, each movement/resize could serialize the entire workspace several times, allocate a new immutable job and replace a pending job. Favorites, history, calendar notes, game-name overrides, custom game-icon copies and SteamGridDB commit operations also had paths that called SD writes or `fsdevCommitDevice` from the UI/event or render loop.

## New flow

1. **Input changes memory first.** Workspace controls update the in-memory model and set a save deadline. No file serialization, SD commit or worker join occurs in the key/touch handler.
2. **Coalesced layout scheduler.** Full workspace changes use a 450 ms quiet period; active-workspace marker updates use 550 ms. A 2000 ms maximum deferral ensures sustained drag/resize activity is periodically persisted rather than postponing a write forever. A full-layout save dominates a marker-only request in the same burst.
3. **Immutable snapshot.** When the deadline expires, the UI thread copies the current workspace value model once. The low-priority worker performs all layout serialization, shape hashing, file metadata checks, atomic replace and SD commits.
4. **One in-flight layout writer.** While a save runs, newer edits are coalesced into the scheduler. The worker is reaped only after its completion flag is set; an immutable snapshot is never modified while the worker reads it.
5. **Auxiliary save queue.** Favorites, history, calendar notes and game-name overrides each have a replaceable pending snapshot. Game-icon copying and SteamGridDB durability commits use the same low-priority worker. Renderer cache invalidation happens on the UI thread after the background commit succeeds.
6. **Explicit durability boundaries.** Before handing control to a launched title/homebrew and during normal shutdown, pending snapshots are drained. Startup repair/migration and the final checked shutdown snapshot retain their synchronous fallback path.

## Failure handling

- Atomic temporary/backup replacement is retained for every file-based save.
- Failed auxiliary snapshots are kept and retried after a quiet interval unless a newer snapshot for the same data type supersedes them.
- If a worker cannot be created, the save is re-queued; the checked startup/launch/shutdown path may use its existing synchronous fallback.
- A failed commit is reported. The UI does not block waiting for a retry during ordinary navigation.

## Verification

Host test: `tests/coalesced_layout_save_scheduler_test.cpp` covers quiet-period debounce, maximum deferral, full-layout priority, explicit drain deadlines and SDL tick wraparound.

The repository CI additionally exercises existing workspace persistence, shutdown persistence, touch gestures, drag/resize, workspace projection and recovery tests.

Physical console verification remains necessary for SD latency, sustained touch-drag, rapid D-pad repeats, save coalescing during slow-card conditions, launch/exit durability and frame-time impact.
