# Stage 2 — Runtime Stability 2.0

## Stage 2A — lifecycle and IPC ownership hardening

Implementation branch: `stage2/runtime-stability-20261122`

Base: `recovered/unified-best-20261122` at `19d53a8`.

This increment is deliberately limited to runtime ownership and recovery paths. It does not claim that qlaunch replacement is production-ready or that any behavior has been verified on a physical Switch.

### Implemented

- **Exclusive deferred-operation ownership.** The system backend accepts only one pending menu-close operation at a time: title launch, Homebrew launch, return-to-game, close-app, or power action. Conflicting operations are rejected without overwriting the current request. The operation is released at the menu-holder close barrier and cancelled on HOME dismissal, SD removal, sleep, or menu watchdog teardown.
- **Safe launch request parsing.** LaunchTitle flags are parsed into local variables and copied into pending state only after the request has passed validation and acquired the operation gate.
- **Natural game-exit fallback.** When optional `pmshell` is unavailable, the backend now polls the application's explicit AM `IsCompleted` command at a bounded cadence. It does not use the generic state-change event, because focus and suspend transitions can signal that event. When `pmshell` is available, both process disappearance and AM completion are still required before closing the application holder.
- **Sleep re-entry and failure recovery.** Duplicate sleep notifications are ignored while a sleep sequence is active. If AM refuses `appletStartSleepSequence()`, the backend returns to a valid menu/game/recovery state instead of waiting indefinitely for a wake event that may never arrive.
- **Wake recovery.** A live menu applet is reused after wake; a finished holder is reaped before another menu is launched. Stale wake notifications outside the Sleeping state are ignored.
- **Sleep-loop quiescence.** The backend continues pumping lifecycle messages while sleeping, but skips menu IPC, watchdog checks, process polling and periodic service retries until the wake transition is received.
- **Regression tests in both host CI workflows.** Added tests for the pending-operation gate and application-holder release policy.

### Host tests

The new tests are standalone C++17 targets:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Iprojects/common/include \
  tests/pending_operation_gate_test.cpp -o /tmp/pending-operation-test
/tmp/pending-operation-test

c++ -std=c++17 -Wall -Wextra -Werror -Iprojects/common/include \
  tests/application_lifecycle_policy_test.cpp -o /tmp/application-lifecycle-test
/tmp/application-lifecycle-test
```

Both commands are now part of `.github/workflows/build.yml` and `.github/workflows/build-system-backend.yml`.

### Remaining Stage 2 work

1. **Unify the runtime state machine.** `projects/common/BackendMachine.*` is currently host-testable but is not the production transition owner. The next increment should introduce an adapter around real AM/applet events and route production actions through one tested transition model. Avoid a risky all-at-once replacement of the sysmodule loop.
2. **Launch/return acceptance.** Exercise launch of an installed title, title launch failure, same-title resume, switching from a suspended title to a different title, natural game exit while HOME is visible, and return-to-game after repeated HOME presses.
3. **Homebrew lifecycle.** Exercise target validation, hbloader start failure, NRO exit, HOME dismissal, Album ECS unregister failure/retry, and SD removal during a Homebrew session.
4. **Sleep/wake acceptance.** Test sleep from HOME, from a game, and during Homebrew; duplicate sleep notifications; wake with an open menu; wake after the menu holder has already completed; and API failure injection where available.
5. **Input and applet ownership.** Verify controller/touch ownership, focus handoff, applet holder completion/join/close ordering, HOME during launch stabilization, and no stale input after resume.
6. **System-service fault injection.** Exercise unavailable or transient `pmshell`, AM completion-query failure, ECS register/unregister errors, and SD unavailable/read-only behavior.
7. **Firmware matrix.** Repeat acceptance against each supported Atmosphère/HOS pair and retain the daemon/ECS logs plus exact build artifact for every run.

### Physical Switch validation status

**Not verified on hardware.** Host tests validate deterministic policy only; they do not emulate Horizon AM, PM, ECS, qlaunch applet slots, real sleep/wake, or title focus. Do not install a qlaunch override on the only recoverable copy of a console. Preserve the stock qlaunch directory and a full SD backup before any system-mode experiment.

Stage 2A is complete only after the CI host tests and target backend build pass. Stage 2 as a whole remains open until the production-state-machine integration and the hardware acceptance matrix above are completed.
