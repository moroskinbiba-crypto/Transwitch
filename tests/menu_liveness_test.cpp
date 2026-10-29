#include <cassert>
#include <cstdint>
#include <limits>
#include <desknx/MenuLiveness.hpp>

using desknx::watchdog::BusyResult;
using desknx::watchdog::BusyTickResult;
using desknx::watchdog::MenuLiveness;

int main() {
    // A ready handshake establishes a deadline; ordinary heartbeats renew it.
    {
        MenuLiveness liveness;
        liveness.MarkReady(1000, 100);
        assert(liveness.LastHeartbeatTick() == 1000);
        assert(liveness.HeartbeatDeadlineTick() == 2500);
        liveness.Heartbeat(1100, 100);
        assert(liveness.LastHeartbeatTick() == 1100);
        assert(liveness.HeartbeatDeadlineTick() == 2600);
        assert(!liveness.HeartbeatTimedOut(2599, true));
        assert(liveness.HeartbeatTimedOut(2600, true));
        assert(!liveness.HeartbeatTimedOut(3000, false));
    }

    // Busy is bounded, rejects nested pauses, and a heartbeat during a pause
    // restores a fresh watchdog interval when that pause ends.
    {
        MenuLiveness liveness;
        liveness.MarkReady(1000, 100);
        liveness.Heartbeat(1100, 100);
        assert(liveness.HandleBusy(3, 1200, 100) == BusyResult::Started);
        assert(liveness.BusyPauseActive(1499));
        assert(!liveness.HeartbeatTimedOut(1400, true));
        assert(liveness.HandleBusy(2, 1300, 100) == BusyResult::Rejected);
        liveness.Heartbeat(1400, 100); // observed, but does not extend active pause
        assert(liveness.LastHeartbeatTick() == 1100);
        assert(liveness.HeartbeatDeadlineTick() == 2600);
        assert(liveness.UpdateBusyPause(1499, 100, true) == BusyTickResult::None);
        assert(liveness.UpdateBusyPause(1500, 100, true) == BusyTickResult::EndedWithHeartbeat);
        assert(liveness.LastHeartbeatTick() == 1500);
        assert(liveness.HeartbeatDeadlineTick() == 3000);
        assert(!liveness.BusyPauseActive(1500));
        assert(!liveness.HeartbeatTimedOut(2999, true));
        assert(liveness.HeartbeatTimedOut(3000, true));
    }

    // If no heartbeat arrives during Busy, only the unused pre-pause time budget
    // is restored; Busy cannot indefinitely refresh the watchdog.
    {
        MenuLiveness liveness;
        liveness.MarkReady(1000, 100);
        assert(liveness.HandleBusy(3, 1100, 100) == BusyResult::Started);
        assert(liveness.HeartbeatDeadlineTick() == 2500);
        assert(liveness.UpdateBusyPause(1400, 100, true) == BusyTickResult::EndedWithoutHeartbeat);
        assert(liveness.HeartbeatDeadlineTick() == 2800);
        assert(!liveness.HeartbeatTimedOut(2799, true));
        assert(liveness.HeartbeatTimedOut(2800, true));
    }

    // Early BusyEnd restores the remaining budget, rejects duplicate ends, and
    // requires both cooldown and a heartbeat newer than the end before re-pause.
    {
        MenuLiveness liveness;
        liveness.MarkReady(1000, 100);
        liveness.Heartbeat(1100, 100);
        assert(liveness.HandleBusy(10, 1200, 100) == BusyResult::Started);
        assert(liveness.HandleBusy(0, 1500, 100) == BusyResult::Ended);
        assert(liveness.HeartbeatDeadlineTick() == 2900);
        assert(liveness.HandleBusy(0, 1501, 100) == BusyResult::Rejected);
        assert(liveness.HandleBusy(2, 1600, 100) == BusyResult::Rejected); // cooldown
        liveness.Heartbeat(1600, 100);
        assert(liveness.HandleBusy(2, 1700, 100) == BusyResult::Started);
    }

    // A heartbeat parsed just after Busy expiry must not be overwritten by
    // a later main-loop tick that uses an older tick snapshot.
    {
        MenuLiveness liveness;
        liveness.MarkReady(1000, 100);
        liveness.Heartbeat(1100, 100);
        assert(liveness.HandleBusy(3, 1200, 100) == BusyResult::Started);
        assert(liveness.Heartbeat(1510, 100) == BusyTickResult::EndedWithoutHeartbeat);
        assert(liveness.LastHeartbeatTick() == 1510);
        assert(liveness.HeartbeatDeadlineTick() == 3010);
        assert(liveness.UpdateBusyPause(1520, 100, true) == BusyTickResult::None);
        assert(liveness.HeartbeatDeadlineTick() == 3010);
    }

    // Reject bad durations, an expired watchdog, zero frequency and regressing
    // ticks. Saturating deadline arithmetic must not wrap at uint64_t max.
    {
        MenuLiveness liveness;
        liveness.MarkReady(1000, 100);
        assert(liveness.HandleBusy(0, 1100, 100) == BusyResult::Rejected);
        assert(liveness.HandleBusy(121, 1100, 100) == BusyResult::Rejected);
        assert(liveness.HandleBusy(1, 1100, 0) == BusyResult::Rejected);
        assert(liveness.HandleBusy(1, 2500, 100) == BusyResult::Rejected);

        liveness.MarkReady(std::numeric_limits<std::uint64_t>::max() - 10, 100);
        assert(liveness.HeartbeatDeadlineTick() == std::numeric_limits<std::uint64_t>::max());
        assert(liveness.HeartbeatTimedOut(std::numeric_limits<std::uint64_t>::max(), true));
    }

    return 0;
}
