#pragma once

#include <cstdint>
#include <limits>
#include <desknx/ProtocolRules.hpp>

namespace desknx::watchdog {

enum class BusyResult : std::uint8_t {
    Rejected,
    Started,
    Ended
};

enum class BusyTickResult : std::uint8_t {
    None,
    EndedWithHeartbeat,
    EndedWithoutHeartbeat
};

// Pure heartbeat/Busy controller shared by qlaunch production code and host
// tests. The UI still decides when a menu is ready/alive; this object owns the
// tick deadlines and the rules that prevent Busy from masking a dead menu.
class MenuLiveness final {
    static constexpr std::uint64_t HeartbeatTimeoutSeconds = 15;
    static constexpr std::uint64_t BusyMinimumIntervalSeconds = 5;

    std::uint64_t m_lastHeartbeatTick = 0;
    std::uint64_t m_busyUntilTick = 0;
    std::uint64_t m_lastBusyCommandTick = 0;
    std::uint64_t m_lastBusyEndTick = 0;
    std::uint64_t m_heartbeatDeadlineTick = 0;
    std::uint64_t m_busyRemainingTicks = 0;
    bool m_hasLastBusyCommand = false;
    bool m_busyHeartbeatSeen = false;
    bool m_busyPauseProcessed = false;

    static constexpr std::uint64_t AddSaturated(std::uint64_t a, std::uint64_t b) {
        return b > std::numeric_limits<std::uint64_t>::max() - a
            ? std::numeric_limits<std::uint64_t>::max() : a + b;
    }

    static constexpr std::uint64_t DurationTicks(std::uint64_t frequency,
                                                  std::uint64_t seconds) {
        return seconds != 0 && frequency > std::numeric_limits<std::uint64_t>::max() / seconds
            ? std::numeric_limits<std::uint64_t>::max() : frequency * seconds;
    }

public:
    void Reset() {
        m_lastHeartbeatTick = 0;
        m_busyUntilTick = 0;
        m_lastBusyCommandTick = 0;
        m_lastBusyEndTick = 0;
        m_heartbeatDeadlineTick = 0;
        m_busyRemainingTicks = 0;
        m_hasLastBusyCommand = false;
        m_busyHeartbeatSeen = false;
        m_busyPauseProcessed = false;
    }

    void MarkReady(std::uint64_t now, std::uint64_t frequency) {
        Reset();
        m_lastHeartbeatTick = now;
        m_heartbeatDeadlineTick =
            AddSaturated(now, DurationTicks(frequency, HeartbeatTimeoutSeconds));
    }

    void Heartbeat(std::uint64_t now, std::uint64_t frequency) {
        if (BusyPauseActive(now)) {
            m_busyHeartbeatSeen = true;
            return;
        }
        m_lastHeartbeatTick = now;
        m_heartbeatDeadlineTick =
            AddSaturated(now, DurationTicks(frequency, HeartbeatTimeoutSeconds));
    }

    BusyResult HandleBusy(std::uint32_t seconds, std::uint64_t now,
                          std::uint64_t frequency) {
        if (frequency == 0)
            return BusyResult::Rejected;

        if (ipc::rules::IsBusyEndSignal(seconds)) {
            if (m_busyUntilTick == 0 || now >= m_busyUntilTick || m_busyPauseProcessed)
                return BusyResult::Rejected;

            if (m_busyHeartbeatSeen) {
                m_lastHeartbeatTick = now;
                m_heartbeatDeadlineTick =
                    AddSaturated(now, DurationTicks(frequency, HeartbeatTimeoutSeconds));
            } else {
                m_heartbeatDeadlineTick = AddSaturated(now, m_busyRemainingTicks);
            }
            m_lastBusyEndTick = now;
            m_busyUntilTick = now;
            m_busyPauseProcessed = true;
            return BusyResult::Ended;
        }

        if (!ipc::rules::IsValidBusySeconds(seconds) ||
            m_heartbeatDeadlineTick == 0 || now >= m_heartbeatDeadlineTick)
            return BusyResult::Rejected;

        // Permit only one pause per heartbeat interval. A fresh heartbeat after
        // the previous pause is mandatory before any later Busy pause.
        if (m_busyUntilTick != 0 &&
            (now < m_busyUntilTick || !m_busyPauseProcessed ||
             m_lastHeartbeatTick <= m_lastBusyEndTick))
            return BusyResult::Rejected;

        if (m_hasLastBusyCommand &&
            (now < m_lastBusyCommandTick ||
             now - m_lastBusyCommandTick <
                DurationTicks(frequency, BusyMinimumIntervalSeconds)))
            return BusyResult::Rejected;

        m_busyRemainingTicks = m_heartbeatDeadlineTick - now;
        m_busyUntilTick = AddSaturated(now, DurationTicks(frequency, seconds));
        m_busyHeartbeatSeen = false;
        m_busyPauseProcessed = false;
        m_lastBusyCommandTick = now;
        m_hasLastBusyCommand = true;
        return BusyResult::Started;
    }

    BusyTickResult UpdateBusyPause(std::uint64_t now, std::uint64_t frequency,
                                   bool menuReady) {
        if (!menuReady || m_busyUntilTick == 0 || m_busyPauseProcessed ||
            now < m_busyUntilTick)
            return BusyTickResult::None;

        if (m_busyHeartbeatSeen) {
            m_lastHeartbeatTick = now;
            m_heartbeatDeadlineTick =
                AddSaturated(now, DurationTicks(frequency, HeartbeatTimeoutSeconds));
        } else {
            m_heartbeatDeadlineTick = AddSaturated(now, m_busyRemainingTicks);
        }
        m_lastBusyEndTick = now;
        m_busyPauseProcessed = true;
        return m_busyHeartbeatSeen ? BusyTickResult::EndedWithHeartbeat
                                   : BusyTickResult::EndedWithoutHeartbeat;
    }

    bool BusyPauseActive(std::uint64_t now) const {
        return m_busyUntilTick != 0 && now < m_busyUntilTick;
    }

    bool HeartbeatTimedOut(std::uint64_t now, bool menuReady) const {
        return menuReady && m_heartbeatDeadlineTick != 0 &&
               !BusyPauseActive(now) && now >= m_heartbeatDeadlineTick;
    }

    std::uint64_t HeartbeatDeadlineTick() const {
        return m_heartbeatDeadlineTick;
    }

    std::uint64_t LastHeartbeatTick() const {
        return m_lastHeartbeatTick;
    }
};

} // namespace desknx::watchdog
