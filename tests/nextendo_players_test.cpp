#include <desknx/NextendoPlayers.hpp>

#include <cassert>
#include <cstdio>

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
    std::puts("nextendo_players_test: ok");
    return 0;
}
