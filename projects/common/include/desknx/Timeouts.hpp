#pragma once

#include <cstdint>

// Backend timing constants (ТЗ §3.3). All values live in this header only and
// are logged by the backend at startup. Ticks are converted with
// armGetSystemTickFreq() inside the backend; here they are plain milliseconds.

namespace desknx::timeouts {

constexpr uint64_t MenuReadyMs = 25000;      // was 8 s; slow SD makes boot long
constexpr uint64_t HeartbeatMs = 15000;      // paused while the menu reports Busy
constexpr uint64_t QuickExitMs = 3000;       // menu died before MenuReady
constexpr uint64_t ExhaustedRetryMs = 5000;  // not more often after slots ran out
constexpr uint64_t AppSettleMs = 1500;       // HOME ignored during this window
constexpr uint64_t AppCloseSoftMs = 15000;   // graceful exit wait
constexpr uint64_t AppCloseHardMs = 3000;    // then Terminate
constexpr uint64_t RecoveryHoldMs = 3000;    // all-buttons-held chord
constexpr uint64_t LoopPeriodMs = 16;        // main loop period (BK-01)

} // namespace desknx::timeouts
