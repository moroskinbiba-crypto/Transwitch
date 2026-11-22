# Runtime performance and telemetry

## VE-2.1 scope

The runtime path is split into four independently observable areas:

- controller/touch input handling;
- CPU-side draw work;
- renderer presentation wait (including VSync);
- active-workspace switch latency through the first presented frame.

The Diagnostics panel shows the current five-second averages and maxima. The runtime log records the same values. Workspace changes also emit a `performance / workspace_switch` event with the selected workspace index and tile count.

## Horizon telemetry worker

`SystemTelemetry` owns a low-priority SDL worker. It samples PSM/HID battery state, TS temperature, CLKRST CPU/GPU rates, NIFM connectivity, AUDCTL mute state, LBL brightness and process memory. The input/render thread only publishes demand, enqueues quick-setting commands and copies the latest immutable value snapshot.

The worker uses separate sample intervals:
- battery and controller state: 3 seconds;
- temperatures and CPU/GPU clock rates: 2.5 seconds;
- quick-settings status: 3.5 seconds;
- process memory: 5 seconds.

Service initialization failures are retried with a bounded delay; unavailable readings remain explicitly unavailable rather than being presented as zero. The worker is stopped before Horizon service teardown.

## Artwork scheduling

When a workspace becomes active, the selected tile is queued at high decode priority before nearby visible artwork. Library navigation similarly promotes the currently selected entry. Image decoding stays on the worker; SDL texture upload and destruction remain on the render thread and retain the existing per-frame upload budget.

## Validation

The production DeskNX and native NRO CI builds compile the Switch-only telemetry integration. Host tests continue to cover workspace persistence, selection/navigation, cache rules and UI components.

Physical Switch validation is still required for:
- real controller-to-first-frame latency on each renderer path;
- worker/service behavior across sleep and wake;
- CPU/GPU/temperature availability on supported Horizon versions;
- SD-card pressure while artwork decoding and saves are active;
- sustained quick-settings changes and failed/unavailable services.

Do not use host-only frame measurements as a substitute for console measurements.
