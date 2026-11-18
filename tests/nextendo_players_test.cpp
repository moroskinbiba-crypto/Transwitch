#include <desknx/NextendoPlayers.hpp>

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace desknx::nextendo;

namespace {

constexpr std::uint64_t T0 = 1'000'000;

PlayerSnapshot makeSnapshot(unsigned titleA, unsigned titleB, unsigned total) {
    PlayerSnapshot s;
    s.perTitle[0x0100000000001000ull] = titleA;
    s.perTitle[0x010000000000100Bull] = titleB;
    s.total = total;
    return s;
}

void testUnknownBeforeFirstSnapshot() {
    PresenceFeed feed;
    feed.setEnabled(true);
    assert(!feed.hasTotal(T0));
    assert(feed.total(T0) == 0);
    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Unknown);
    assert(feed.refreshDue(T0));
}

void testOnlineAndOffline() {
    PresenceFeed feed;
    feed.setEnabled(true);
    assert(feed.apply(makeSnapshot(5, 0, 12), T0));
    assert(feed.hasTotal(T0));
    assert(feed.total(T0) == 12);
    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Online);
    assert(feed.presenceOf(0x010000000000100Bull, T0) == Presence::Offline);
    // unknown title: offline because the snapshot covers all titles
    assert(feed.presenceOf(0xFFFFFFFFFFFFFF01ull, T0) == Presence::Offline);
}

void testSnapshotExpires() {
    PresenceFeed feed;
    feed.setEnabled(true);
    feed.apply(makeSnapshot(1, 0, 1), T0);
    // just inside TTL
    assert(feed.hasTotal(T0 + SnapshotTtlMs - 1));
    // exactly at TTL boundary: expired
    assert(!feed.hasTotal(T0 + SnapshotTtlMs));
    assert(feed.presenceOf(0x0100000000001000ull, T0 + SnapshotTtlMs) == Presence::Unknown);
}

void testRefreshCadence() {
    PresenceFeed feed;
    feed.setEnabled(true);
    feed.apply(makeSnapshot(1, 0, 1), T0);
    assert(!feed.refreshDue(T0 + RefreshIntervalMs - 1));
    assert(feed.refreshDue(T0 + RefreshIntervalMs));
}

void testDisabledHidesEverything() {
    PresenceFeed feed;
    feed.setEnabled(true);
    feed.apply(makeSnapshot(5, 0, 5), T0);
    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Online);

    feed.setEnabled(false);
    // the snapshot is wiped on disable
    assert(!feed.hasTotal(T0));
    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Unknown);
    // cannot apply while disabled
    assert(!feed.apply(makeSnapshot(1, 0, 1), T0));
    assert(!feed.refreshDue(T0));

    // re-enable starts fresh: no stale dots
    feed.setEnabled(true);
    assert(!feed.hasTotal(T0));
    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Unknown);
    assert(feed.refreshDue(T0));
}

void testInvalidSnapshotIsRejected() {
    PresenceFeed feed;
    feed.setEnabled(true);

    // total < per-title sum
    PlayerSnapshot bad1;
    bad1.perTitle[0x0100000000001000ull] = 5;
    bad1.total = 2;  // should be >= 5
    assert(!PresenceFeed::plausible(bad1));
    assert(!feed.apply(bad1, T0));

    // per-title count too large
    PlayerSnapshot bad2;
    bad2.perTitle[0x0100000000001000ull] = MaxPlayersPerTitle + 1;
    bad2.total = MaxPlayersPerTitle + 1;
    assert(!PresenceFeed::plausible(bad2));
    assert(!feed.apply(bad2, T0));

    // valid snapshot: endpoint total may exceed our per-title sum (titles we don't own)
    PlayerSnapshot good;
    good.perTitle[0x0100000000001000ull] = 3;
    good.total = 300;  // higher is fine
    assert(PresenceFeed::plausible(good));
    assert(feed.apply(good, T0));
    assert(feed.total(T0) == 300);
}

void testInvalidateResetsToUnknown() {
    PresenceFeed feed;
    feed.setEnabled(true);
    feed.apply(makeSnapshot(5, 0, 5), T0);
    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Online);
    feed.invalidate();
    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Unknown);
    assert(!feed.hasTotal(T0));
    // must fetch again
    assert(feed.refreshDue(T0));
}

void testClockWrapDoesNotCrash() {
    PresenceFeed feed;
    feed.setEnabled(true);
    feed.apply(makeSnapshot(1, 0, 1), T0);
    // nowMs < appliedAtMs: treat as expired, not as "valid forever"
    assert(!feed.hasTotal(T0 - 1));
    assert(feed.presenceOf(0x0100000000001000ull, T0 - 1) == Presence::Unknown);
    // refreshDue also returns true when the clock appears to have gone backwards
    assert(feed.refreshDue(T0 - 1));
}


void testPopularGamesAreSortedAndRegionalRowsAreMerged() {
    PlayerSnapshot snapshot;
    snapshot.perTitle[0x0100152000022000ull] = 26; // Mario Kart 8 Deluxe
    snapshot.perTitle[0x0100F8F0000A2000ull] = 5;  // Splatoon 2 region A
    snapshot.perTitle[0x01003BC0000A0000ull] = 7;  // Splatoon 2 region B
    snapshot.perTitle[0x0100C2500FC20000ull] = 28; // Splatoon 3
    snapshot.perTitle[0x01009B90006DC000ull] = 6;  // Mario Maker 2
    snapshot.perTitle[0x0100B04011742000ull] = 4;  // Monster Hunter Rise
    snapshot.perTitle[0x0100F43008C44000ull] = 3;  // Pokemon Legends: Z-A
    snapshot.perTitle[0x0100A250097F0000ull] = 1;  // Dragon Ball FighterZ
    snapshot.perTitle[0x01006A800016E000ull] = 32; // Smash Ultimate
    snapshot.perTitle[0x0100E65002BB8000ull] = 0;  // not currently online
    snapshot.perTitle[0xDEADBEEFDEADBEEFull] = 99; // unknown IDs are not guessed
    snapshot.total = 211;

    const PopularGames popular = rankPopularGames(snapshot);
    assert(popular.available);
    assert(popular.totalPlayers == 211);
    assert(popular.count == 8);
    assert(std::strcmp(popular.games[0].title, "Super Smash Bros. Ultimate") == 0);
    assert(popular.games[0].players == 32);
    assert(std::strcmp(popular.games[1].title, "Splatoon 3") == 0);
    assert(popular.games[1].players == 28);
    assert(std::strcmp(popular.games[2].title, "Mario Kart 8 Deluxe") == 0);
    assert(popular.games[2].players == 26);
    assert(std::strcmp(popular.games[3].title, "Splatoon 2") == 0);
    assert(popular.games[3].players == 12);
    assert(std::strcmp(popular.games[4].title, "Super Mario Maker 2") == 0);
    assert(popular.games[4].players == 6);
    assert(std::strcmp(popular.games[5].title, "Monster Hunter Rise") == 0);
    assert(popular.games[5].players == 4);
    assert(std::strcmp(popular.games[6].title, "Pokemon Legends: Z-A") == 0);
    assert(popular.games[6].players == 3);
    assert(std::strcmp(popular.games[7].title, "Dragon Ball FighterZ") == 0);
    assert(popular.games[7].players == 1);
}

void testPopularGamesRespectLimitAndEmptySnapshot() {
    PlayerSnapshot snapshot;
    snapshot.total = 0;
    const PopularGames empty = rankPopularGames(snapshot);
    assert(empty.available);
    assert(empty.count == 0);
    const PopularGames limited = rankPopularGames(snapshot, 3);
    assert(limited.available);
    assert(limited.count == 3);
    assert(limited.games[0].players == 32);
    assert(limited.games[1].players == 28);
    assert(limited.games[2].players == 26);
}

void testFreshSnapshotCopyIsSafe() {
    PresenceFeed feed;
    feed.setEnabled(true);
    PlayerSnapshot source = makeSnapshot(2, 0, 2);
    PlayerSnapshot copy;
    assert(!feed.copyFreshSnapshot(copy, T0));
    assert(feed.apply(source, T0));
    assert(feed.copyFreshSnapshot(copy, T0));
    assert(copy.total == 2);
    assert(copy.perTitle.size() == 2);
    assert(!feed.copyFreshSnapshot(copy, T0 + SnapshotTtlMs));
}

} // namespace

int main() {
    testUnknownBeforeFirstSnapshot();
    testOnlineAndOffline();
    testSnapshotExpires();
    testRefreshCadence();
    testDisabledHidesEverything();
    testInvalidSnapshotIsRejected();
    testInvalidateResetsToUnknown();
    testClockWrapDoesNotCrash();
    testPopularGamesAreSortedAndRegionalRowsAreMerged();
    testPopularGamesRespectLimitAndEmptySnapshot();
    testFreshSnapshotCopyIsSafe();
    std::puts("nextendo_players_test: ok");
    return 0;
}
