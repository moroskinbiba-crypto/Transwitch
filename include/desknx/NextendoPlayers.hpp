#pragma once

#include <cstdint>
#include <unordered_map>

namespace desknx::nextendo {

// Online presence for installed titles, as HomeNX shows it on its game cards:
// a green dot on games being played right now, and a "N players online" total.
//
// The rules live here, away from the network, because the important part is not
// the HTTP call but what the UI is allowed to claim:
//   * no answer yet, a failed refresh, a disabled feed or an expired snapshot
//     all mean UNKNOWN - never "0 players" and never "offline";
//   * only a fresh snapshot may mark a title as Online or Offline;
//   * a nonsensical payload is rejected instead of replacing good data;
//   * the feed is polled on a fixed cadence and self-expires, so a dead network
//     silently degrades to UNKNOWN rather than showing stale counts forever.
//
// The transport (HTTP + JSON) is deliberately outside this file: it maps the
// endpoint's shape onto PlayerSnapshot and calls apply().

using TitleId = std::uint64_t;

constexpr std::uint64_t RefreshIntervalMs = 180u * 1000u;
// A snapshot older than this is no longer trusted: the poll interval plus two
// missed rounds. Longer than that and a green dot could point at a session that
// ended minutes ago.
constexpr std::uint64_t SnapshotTtlMs = 3u * RefreshIntervalMs;
// Title IDs above this are not real eShop/application IDs.
constexpr std::uint64_t MaxTitleId = 0xFFFFFFFFFFFFFFFFull;
// No single title has this many players in a homebrew network; a payload
// claiming otherwise is corrupt or hostile.
constexpr unsigned MaxPlayersPerTitle = 100000u;

struct PlayerSnapshot {
    std::unordered_map<TitleId, unsigned> perTitle;
    unsigned total{0};
};

enum class Presence : unsigned char {
    Unknown,  // no trustworthy data: draw nothing
    Offline,  // fresh snapshot, not playing
    Online,   // fresh snapshot, playing now
};

// Network transport is kept separate from the feed model so the latter remains host-testable.
bool initializeNetwork();
void shutdownNetwork();
bool fetchSnapshot(PlayerSnapshot& snapshot);

class PresenceFeed final {
public:
    void setEnabled(bool enabled) {
        if (m_enabled == enabled) return;
        m_enabled = enabled;
        // Disabling must not leave dots on screen, and re-enabling must not show
        // data collected before the user opted out.
        invalidate();
    }

    bool enabled() const { return m_enabled; }

    // Installs a snapshot. Returns false - leaving the previous state alone when
    // it is still valid - if the feed is disabled or the payload is absurd.
    bool apply(const PlayerSnapshot& snapshot, std::uint64_t nowMs) {
        if (!m_enabled) return false;
        if (!plausible(snapshot)) return false;

        m_snapshot = snapshot;
        m_hasSnapshot = true;
        m_appliedAtMs = nowMs;
        return true;
    }

    // Drops the snapshot: after a failed refresh, an explicit opt-out or a
    // network loss the UI goes back to "unknown", not to "everyone offline".
    void invalidate() {
        m_snapshot = PlayerSnapshot{};
        m_hasSnapshot = false;
        m_appliedAtMs = 0;
    }

    bool hasFreshSnapshot(std::uint64_t nowMs) const {
        if (!m_enabled || !m_hasSnapshot) return false;
        if (nowMs < m_appliedAtMs) return false;  // clock went backwards
        return nowMs - m_appliedAtMs < SnapshotTtlMs;
    }

    // What the tile should draw. Unknown covers disabled, missing, failed and
    // expired states, so the caller never has to distinguish them.
    Presence presenceOf(TitleId titleId, std::uint64_t nowMs) const {
        if (!hasFreshSnapshot(nowMs)) return Presence::Unknown;
        const auto found = m_snapshot.perTitle.find(titleId);
        if (found != m_snapshot.perTitle.end() && found->second > 0u) return Presence::Online;
        return Presence::Offline;
    }

    // True only when a fresh total may be shown; the pill stays hidden otherwise
    // instead of advertising a stale or invented number.
    bool hasTotal(std::uint64_t nowMs) const { return hasFreshSnapshot(nowMs); }

    unsigned total(std::uint64_t nowMs) const {
        return hasFreshSnapshot(nowMs) ? m_snapshot.total : 0u;
    }

    unsigned activeTitleCount(std::uint64_t nowMs) const {
        if (!hasFreshSnapshot(nowMs)) return 0u;
        unsigned count = 0;
        for (const auto& entry : m_snapshot.perTitle)
            if (entry.second > 0u) ++count;
        return count;
    }

    unsigned playersFor(TitleId titleId, std::uint64_t nowMs) const {
        if (!hasFreshSnapshot(nowMs)) return 0u;
        const auto found = m_snapshot.perTitle.find(titleId);
        return found == m_snapshot.perTitle.end() ? 0u : found->second;
    }

    // The poll timer: true when the caller should fetch again.
    bool refreshDue(std::uint64_t nowMs) const {
        if (!m_enabled) return false;
        if (!m_hasSnapshot) return true;
        if (nowMs < m_appliedAtMs) return true;
        return nowMs - m_appliedAtMs >= RefreshIntervalMs;
    }

    static bool plausible(const PlayerSnapshot& snapshot) {
        std::uint64_t sum = 0;
        for (const auto& entry : snapshot.perTitle) {
            if (entry.first > MaxTitleId) return false;
            if (entry.second > MaxPlayersPerTitle) return false;
            sum += entry.second;
            if (sum > static_cast<std::uint64_t>(MaxPlayersPerTitle) * 1024u) return false;
        }
        if (snapshot.total > MaxPlayersPerTitle) return false;
        // The total reported by the endpoint may legitimately exceed the sum we
        // track (players in titles this console does not own), but it may never
        // be smaller than the per-title numbers we just summed.
        return snapshot.total >= sum;
    }

private:
    bool m_enabled{false};
    bool m_hasSnapshot{false};
    std::uint64_t m_appliedAtMs{0};
    PlayerSnapshot m_snapshot{};
};

} // namespace desknx::nextendo
