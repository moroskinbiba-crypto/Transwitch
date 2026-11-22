# Runtime performance and telemetry

## VE-2.1 scope

The runtime path is split into four independently observable areas:

- controller/touch input handling;
- CPU-side draw work;
- renderer presentation wait (including VSync);
- active-workspace switch latency through the first presented frame.

The Diagnostics panel shows the current five-second averages, P95 frame time and maxima. P95 is calculated from a bounded rolling sample of the most recent 128 frames in each metrics window; the runtime log records average/max input, rendering and presentation wait. Workspace changes also emit a `performance / workspace_switch` event with the selected workspace index and tile count.

The main loop emits five-second timings for layout/auxiliary saves, launcher settings, play-stat queries, font warmup, SteamGridDB picker/cache cleanup, telemetry snapshots and active-workspace marker scheduling. Each stage records calls, average/max duration and calls over 10 ms. SD commit logs separate mutex wait from `fsdevCommitDevice` duration when either phase is measurable.

## Horizon telemetry worker

`SystemTelemetry` owns a low-priority SDL worker. It samples PSM/HID battery state, TS temperature, CLKRST CPU/GPU rates, NIFM connectivity, AUDCTL mute state, LBL brightness and process memory. The input/render thread only publishes demand, enqueues quick-setting commands and copies the latest immutable value snapshot.

The worker uses separate sample intervals:
- battery and controller state: 3 seconds;
- temperatures and CPU/GPU clock rates: 2.5 seconds;
- quick-settings status: 3.5 seconds;
- process memory: 5 seconds.

Service initialization failures are retried with a bounded delay; unavailable readings remain explicitly unavailable rather than being presented as zero. Failed TS/CLKRST sessions are closed and reopened on the next sample, and a fully failed PSM read pair causes a bounded service reinitialization attempt. The worker is stopped before Horizon service teardown.

## Homebrew inventory

The cache-aware scanner uses `dirent::d_type` to avoid metadata requests for
ordinary files and directories. It falls back to `stat()` only for
`DT_UNKNOWN`, and stats NROs when size/mtime metadata is required. Logs
distinguish fallback, NRO and directory-signature checks and list slow paths.

The inventory remains bounded (12 directory levels, 4096 directories, 65536
entries and 512 NRO files per pass). Reaching a bound or an SD I/O error marks
the pass incomplete; incomplete results are never published as the new
complete cache. The last-known-good catalogue is retained when possible.

## Artwork scheduling

When a workspace becomes active, the selected tile is queued at high decode priority before nearby visible artwork. Library navigation similarly promotes the currently selected entry. Image decoding stays on the worker; SDL texture upload and destruction remain on the render thread and retain the existing per-frame upload budget.

## Validation

The production DeskNX and native NRO CI builds compile the Switch-only telemetry integration. Host tests continue to cover workspace persistence, selection/navigation, cache rules and UI components.

Physical Switch validation is still required for:
- real controller-to-first-frame latency on each renderer path;
- scans on representative SD cards, including directories above 8192 entries
  and filesystems that return `DT_UNKNOWN`;
- cache invalidation after NRO install, update, rename and removal;
- worker/service behavior across sleep and wake;
- CPU/GPU/temperature availability on supported Horizon versions;
- SD-card pressure while artwork decoding and saves are active, including
  mutex wait versus controller commit duration;
- sustained quick-settings changes and failed/unavailable services.

Do not use host-only frame measurements as a substitute for console measurements.
