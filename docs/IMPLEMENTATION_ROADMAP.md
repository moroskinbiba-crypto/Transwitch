# Transwitch implementation roadmap

Baseline audited: `main` at `6498f9b26760479e0935f745caf649f44a3fe3bc` (2026-11-18).

This file is the acceptance ledger for the implementation sequence. A checkbox is
only marked complete when the corresponding code and automated checks exist.
Successful compilation alone does not close a runtime acceptance item.

## Stage 1 — Runtime Stability & Performance

### Implemented in this branch
- [x] Move the existing PSM battery query out of `drawScreen()` into a dedicated low-priority Sensor Service Worker.
- [x] Move SD `statvfs()` sampling out of `drawScreen()` into the same worker.
- [x] Publish a bounded, copyable snapshot protected by a short SDL mutex.
- [x] Keep last-known readings when a sample fails and expose freshness/error state.
- [x] Retry failed PSM initialization with backoff; recreate PSM after repeated query failures.
- [x] Join the sensor worker before SDL teardown and let the worker own `psmExit()`.
- [x] Add rolling avg/P95/max samples for frame time, render work, SDL present wait,
      input-to-present latency, and workspace-switch presentation latency.
- [x] Add a host unit test for bounded rolling-window statistics.

### Still open
- [ ] Verify PSM behavior on console across sleep/resume and application-mode/HOME-applet use.
- [ ] Add temperature, CPU/GPU frequency, and Joy-Con readings only after confirming
      exact supported libnx/Horizon APIs and ownership requirements. The audited
      `main` branch did not contain the proposed `refreshSystemMonitor()` or
      `refreshQuickSettingsSnapshot()` functions.
- [ ] A worker isolates synchronous Horizon calls from rendering but cannot safely
      cancel a blocked libnx IPC call. Confirm service-call behavior on device before
      claiming hard timeouts.
- [ ] Add cache hit/miss, texture upload duration, decoded/queued/completed counts,
      and worker-queue high-water metrics.
- [ ] Separate input polling, update, rendering, present/VSync, and sleep measurements
      more finely; current input latency is measured through the first completed present.
- [ ] Verify there are no remaining filesystem/image decode or SDL texture creation
      operations on the render path. Workspace background loading is still synchronous.

## Stage 2 — Desktop Streaming

- [ ] Replace FIFO icon requests with priority-aware requests (focused tile, visible
      tiles, near-visible tiles, background prefetch).
- [ ] Preload the target workspace immediately after switching, without delaying the
      logical workspace change.
- [ ] Retain a small LRU of recently used workspaces and cancel obsolete low-priority
      work on rapid switching.
- [ ] Add bounded upload-time/byte budgets per frame rather than a fixed one-texture
      upload.
- [ ] Stream workspace backgrounds through CPU decode and render-thread texture upload.
- [ ] Add memory pressure telemetry and explicit cache-budget tests.

## Stage 3 — Visual Engine 2.0

- [ ] Complete tile, window, modal, button, motion, typography, and shared-component
      systems; retain the existing 10 px tile corner target.
- [ ] Fix and regression-test white-screen/window compositing before adding polish.

## Stage 4 — Widget System 2.0

- [ ] Standardize widget data snapshots and Loading/Ready/Stale/Offline/Unavailable/Error.
- [ ] Keep all network, SD and Horizon access outside widget rendering.
- [ ] Complete clock, battery, system, storage, recent and favorites widget variants.

## Stage 5 — Workspace UX

- [ ] Independent cursor/focus state per workspace.
- [ ] Reliable create/rename/delete/reorder, drag/drop, resize, free cells, widgets,
      image/video backgrounds, preview, and crash-safe persistence.

## Stage 6 — Library & System Integration

- [ ] Search/filter/sort and metadata refresh without blocking UI.
- [ ] Verify selected-profile handoff, game launch/return, suspend/resume, Nextendo
      offline/stale/cache states, and restoration of workspace/focus.

## Final acceptance gate

- [ ] 12 workspaces and 600 layout elements.
- [ ] Rapid switching without stalls, dropped input, stale work winning, or memory growth.
- [ ] All modal/window types opened/closed repeatedly without white screens.
- [ ] Slow SD, offline network, unavailable system services, and resume recovery.
- [ ] Game launch/return and selected-profile correctness.
- [ ] On-device test report with avg/P95/max, memory high-water, and crash logs.
