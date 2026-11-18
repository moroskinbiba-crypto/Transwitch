#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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

    bool copyFreshSnapshot(PlayerSnapshot& out, std::uint64_t nowMs) const {
        if (!hasFreshSnapshot(nowMs)) return false;
        out = m_snapshot;
        return true;
    }

    unsigned total(std::uint64_t nowMs) const {
        return hasFreshSnapshot(nowMs) ? m_snapshot.total : 0u;
    }

    bool hasTitleSnapshot(TitleId titleId,std::uint64_t nowMs) const {
        return hasFreshSnapshot(nowMs) && m_snapshot.perTitle.find(titleId)!=m_snapshot.perTitle.end();
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


// Compact, deterministic ranking data for the home-screen widget. The public
// feed currently reports title IDs rather than display names, so only IDs with
// an explicit, verified mapping are shown. Regional Splatoon 2 title IDs are
// combined into one row instead of appearing as duplicate games.
constexpr std::size_t MaxPopularGames = 10;
constexpr std::size_t MaxTrackedGames = 32;

struct PopularGame {
    TitleId titleId{0};
    const char* title{nullptr};
    unsigned players{0};
};

struct PopularGames {
    bool available{false}; // false means no fresh snapshot; never invent a zero
    unsigned totalPlayers{0};
    std::array<PopularGame, MaxPopularGames> games{};
    std::size_t count{0};
};

inline const char* displayNameForTitle(TitleId id) {
    switch (id) {
        case 0x0100152000022000ull: return "Mario Kart 8 Deluxe";
        case 0x0100F8F0000A2000ull:
        case 0x01003BC0000A0000ull:
        case 0x01003C700009C000ull: return "Splatoon 2";
        case 0x0100C2500FC20000ull: return "Splatoon 3";
        case 0x01009B90006DC000ull: return "Super Mario Maker 2";
        case 0x0100B04011742000ull: return "Monster Hunter Rise";
        case 0x0100F43008C44000ull: return "Pokemon Legends: Z-A";
        case 0x0100A250097F0000ull: return "Dragon Ball FighterZ";
        case 0x01009B500007C000ull: return "ARMS";
        case 0x010047700D540000ull: return "Clubhouse Games: 51 Worldwide Classics";
        case 0x0100BDE00862A000ull: return "Mario Tennis Aces";
        case 0x0100C9C00E25C000ull: return "Mario Golf: Super Rush";
        case 0x010019401051C000ull: return "Mario Strikers: Battle League";
        case 0x0100000000010000ull: return "Super Mario Odyssey";
        case 0x01006A800016E000ull: return "Super Smash Bros. Ultimate";
        case 0x01006F8002326000ull: return "Animal Crossing: New Horizons";
        case 0x01006FE013472000ull: return "Mario Party Superstars";
        case 0x010028600EBDA000ull: return "Super Mario 3D World + Bowser's Fury";
        case 0x0100965017338000ull: return "Super Mario Party Jamboree";
        case 0x0100E65002BB8000ull: return "Stardew Valley";
        case 0x0100A8E016236000ull: return "Kirby's Dream Buffet";
        case 0x0100EA6014BB8000ull: return "Just Dance 2022";
        case 0x0100B6E012EBE000ull: return "UNO";
        default: return nullptr;
    }
}

inline PopularGames rankPopularGames(const PlayerSnapshot& snapshot,
                                     std::size_t limit = MaxPopularGames) {
    struct Aggregate {
        TitleId id{0};
        const char* title{nullptr};
        std::uint64_t players{0};
    };
    std::array<Aggregate, MaxTrackedGames> grouped{};
    std::size_t groupedCount = 0;

    for (const auto& entry : snapshot.perTitle) {
        if (entry.second == 0) continue;
        const char* name = displayNameForTitle(entry.first);
        if (!name) continue;

        std::size_t found = groupedCount;
        for (std::size_t i = 0; i < groupedCount; ++i) {
            if (std::strcmp(grouped[i].title, name) == 0) {
                found = i;
                break;
            }
        }
        if (found == groupedCount) {
            if (groupedCount >= grouped.size()) continue;
            grouped[groupedCount++] = {entry.first, name, entry.second};
        } else {
            grouped[found].players = std::min<std::uint64_t>(
                std::numeric_limits<unsigned>::max(),
                grouped[found].players + entry.second);
        }
    }

    std::sort(grouped.begin(), grouped.begin() + groupedCount,
        [](const Aggregate& a, const Aggregate& b) {
            if (a.players != b.players) return a.players > b.players;
            return std::strcmp(a.title, b.title) < 0;
        });

    PopularGames result{};
    result.available = true;
    result.totalPlayers = snapshot.total;
    result.count = std::min({groupedCount, limit, result.games.size()});
    for (std::size_t i = 0; i < result.count; ++i) {
        result.games[i] = {
            grouped[i].id, grouped[i].title,
            static_cast<unsigned>(grouped[i].players)
        };
    }
    return result;
}

} // namespace desknx::nextendo
