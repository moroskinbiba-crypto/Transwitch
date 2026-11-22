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

### Implemented in this branch
- [x] Replace FIFO icon requests with priority-aware scheduling (focused tile, rendered
      tiles, nearby workspace prefetch and lower-priority catalogue thumbnails).
- [x] Preload up to 48 game/homebrew covers after the first ready frame and immediately
      after each workspace change; the current focus cell is scheduled first.
- [x] Cancel queued/completed speculative requests on rapid workspace changes; a
      currently decoding request is discarded unless the new visible frame promotes it.
- [x] Replace the one-texture limit with a two-upload / 6 MiB per-frame budget.
- [x] Keep the existing bounded 64-entry / 32 MiB LRU texture cache and renderer-thread
      texture ownership unchanged.
- [x] Add a host unit test for focus-distance priority ranking.

### Still open
- [ ] Keep an explicit small LRU of recently used workspaces, not only the global icon
      texture LRU.
- [ ] Move workspace-background stat/decode/scale/texture creation out of draw.
- [ ] Add cache hit/miss, queue depth/high-water and texture-upload timing telemetry.
- [ ] Add memory-pressure tests and real-console profiling for large, rapidly switched
      desktops.

## Stage 3 — Visual Engine 2.0

### Audit completed
- [x] Record the current renderer structure and the main window/composition risks in
      `docs/VISUAL_ENGINE_2_AUDIT.md`.

### Implemented in this branch
- [x] Introduce one authoritative modal z-order stack shared by rendering, keyboard and
      touch dispatch. Parent surfaces may remain visible beneath child surfaces, but only
      the top-most layer owns input.
- [x] Add host tests for empty/single/nested modal stacks and previously mismatched
      precedence pairs (Power/Media, Recent/Delete, System/Workspace Settings).
- [x] Add shared ModalBackdrop and ModalSurface primitives and migrate the outer shell
      of every current modal without changing its geometry, palette or hit regions.
- [x] Reset SDL clip and draw-blend state at frame start and immediately before Present.

### Still open
- [x] Introduce a reusable ConfirmationDialog layout/renderer and migrate workspace
      deletion; the visible buttons and touch hit regions use the same geometry.
- [x] Add tested shared Dialog, SidePanel and ContextMenu geometry contracts; migrate
      Workspace Picker, Workspace Options and the App Library details surface to them.
- [ ] Continue reusable content/renderer primitives for remaining modal and panel
      variants, migrating one window at a time while preserving hit regions.
- [x] Add scoped clip and texture-modulation guards to wallpaper, workspace-background,
      tile-artwork and rounded-mask paths.
- [x] Restore pre-existing SDL clip rectangles in scoped guards and diagnose clip/draw-
      blend and off-screen render-target invariant violations at frame boundaries.
- [x] Restore the default screen render target before composition/Present when a stray
      off-screen target is detected; verify the policy with host tests.
- [x] Add an SDL software-renderer regression test that repeatedly injects clip,
      blend-mode and off-screen-target leaks, restores the frame baseline, and verifies
      pixels outside the leaked clip are painted on the next full frame.
- [ ] Add on-device visual regression captures for every modal/window path and reproduce
      the known white-screen/window compositing issue before declaring it fixed.
- [x] Add shared Button variants/states and selectable-row visual contracts; migrate
      workspace picker actions, workspace menu rows and confirmation actions.
- [x] Add badge, toggle, value-field, card and status-indicator primitives; migrate
      library badges, Settings values/edit-mode toggle, library service status, and
      System Tools/library-details cards.
- [x] Migrate the App Library toolbar to shared Toolbar buttons and one source-of-truth
      geometry for rendering and touch hit testing.
- [x] Migrate System Tools cards and Back action to one shared layout for drawing and
      touch dispatch; preserve the six-card keyboard navigation order.
- [x] Migrate Media Picker header controls and file rows to shared hit-test geometry;
      touch no longer activates invisible row gutters or pixels beyond drawn controls.
- [x] Migrate User Picker and Power dialog rows/buttons to shared layouts; remove
      touch activation from row gaps and out-of-bounds button edges.
- [x] Migrate Recent header actions and history rows to shared geometry; row gaps and
      pixels beyond the painted Back/Launch buttons remain inert.
- [x] Consolidate Launcher Settings and Workspace Settings surfaces, close buttons and
      selectable rows into shared window layouts with exact touch boundaries.
- [x] Align App Library visible list rows and add action with shared control geometry;
      preserve the double-tap launch path and make list gutters non-interactive.
- [x] Move the main header toolbar and workspace dock into shared chrome layouts;
      rendering, pointer hit targets and drag-to-workspace use one geometry source.
- [ ] Migrate remaining status/footer hint controls and the rest of the workspace chrome
      to shared control styles.
- [x] Add semantic typography roles (Display, Heading, Subheading, Body, Caption,
      Metadata), a role-aware drawing adapter and initial migrations in confirmation,
      library and workspace-picker surfaces.
- [x] Apply role line-height and UTF-8-safe multiline wrapping to confirmation text;
      wrapped layouts are cached by text, scale, width and line limit.
- [x] Add a deterministic 12-new-texture-attempts-per-frame budget, defer overflow
      to subsequent frames, and report cache misses/deferred misses/texture build time.
- [ ] Extend multiline role rendering to remaining content blocks and profile the
      deferred-label fallback quality on a physical Switch.
- [x] Add a UTF-8-aware 5x7 Cyrillic bitmap fallback with upper/lowercase mapping,
      including Ё/ё, and host tests for all 33 Russian letters plus malformed UTF-8.
- [ ] Verify fallback legibility at all UI scales and on-device when both custom and
      shared TTF fonts are unavailable.
- [x] Add a deterministic motion controller with Full, Reduced and LowPerformance
      timing modes; use its 160 ms eased transition for desktop tile focus feedback.
- [x] Expose Full / Reduced / Low Power motion modes in Launcher Settings and persist
      them as a backward-compatible optional `motion=` key in `settings.ini`.
- [ ] Extend the controller to modal, panel and workspace transitions after
      on-device compositing checks.
- [ ] Preserve the 10 px tile corner target and cover/title overlay behaviour.

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
