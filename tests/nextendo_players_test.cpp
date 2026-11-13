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

{"content":"#include <desknx/NextendoPlayers.hpp>\n\n#include <cassert>\n#include <cstdio>\n\nusing namespace desknx::nextendo;\n\nnamespace {\n\nconstexpr std::uint64_t T0 = 1'000'000;\n\nPlayerSnapshot makeSnapshot(unsigned titleA, unsigned titleB, unsigned total) {\n    PlayerSnapshot s;\n    s.perTitle[0x0100000000001000ull] = titleA;\n    s.perTitle[0x010000000000100Bull] = titleB;\n    s.total = total;\n    return s;\n}\n\nvoid testUnknownBeforeFirstSnapshot() {\n    PresenceFeed feed;\n    feed.setEnabled(true);\n    assert(!feed.hasTotal(T0));\n    assert(feed.total(T0) == 0);\n    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Unknown);\n    assert(feed.refreshDue(T0));\n}\n\nvoid testOnlineAndOffline() {\n    PresenceFeed feed;\n    feed.setEnabled(true);\n    assert(feed.apply(makeSnapshot(5, 0, 12), T0));\n    assert(feed.hasTotal(T0));\n    assert(feed.total(T0) == 12);\n    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Online);\n    assert(feed.presenceOf(0x010000000000100Bull, T0) == Presence::Offline);\n    // unknown title: offline because the snapshot covers all titles\n    assert(feed.presenceOf(0xFFFFFFFFFFFFFF01ull, T0) == Presence::Offline);\n}\n\nvoid testSnapshotExpires() {\n    PresenceFeed feed;\n    feed.setEnabled(true);\n    feed.apply(makeSnapshot(1, 0, 1), T0);\n    // just inside TTL\n    assert(feed.hasTotal(T0 + SnapshotTtlMs - 1));\n    // exactly at TTL boundary: expired\n    assert(!feed.hasTotal(T0 + SnapshotTtlMs));\n    assert(feed.presenceOf(0x0100000000001000ull, T0 + SnapshotTtlMs) == Presence::Unknown);\n}\n\nvoid testRefreshCadence() {\n    PresenceFeed feed;\n    feed.setEnabled(true);\n    feed.apply(makeSnapshot(1, 0, 1), T0);\n    assert(!feed.refreshDue(T0 + RefreshIntervalMs - 1));\n    assert(feed.refreshDue(T0 + RefreshIntervalMs));\n}\n\nvoid testDisabledHidesEverything() {\n    PresenceFeed feed;\n    feed.setEnabled(true);\n    feed.apply(makeSnapshot(5, 0, 5), T0);\n    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Online);\n\n    feed.setEnabled(false);\n    // the snapshot is wiped on disable\n    assert(!feed.hasTotal(T0));\n    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Unknown);\n    // cannot apply while disabled\n    assert(!feed.apply(makeSnapshot(1, 0, 1), T0));\n    assert(!feed.refreshDue(T0));\n\n    // re-enable starts fresh: no stale dots\n    feed.setEnabled(true);\n    assert(!feed.hasTotal(T0));\n    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Unknown);\n    assert(feed.refreshDue(T0));\n}\n\nvoid testInvalidSnapshotIsRejected() {\n    PresenceFeed feed;\n    feed.setEnabled(true);\n\n    // total < per-title sum\n    PlayerSnapshot bad1;\n    bad1.perTitle[0x0100000000001000ull] = 5;\n    bad1.total = 2;  // should be >= 5\n    assert(!PresenceFeed::plausible(bad1));\n    assert(!feed.apply(bad1, T0));\n\n    // per-title count too large\n    PlayerSnapshot bad2;\n    bad2.perTitle[0x0100000000001000ull] = MaxPlayersPerTitle + 1;\n    bad2.total = MaxPlayersPerTitle + 1;\n    assert(!PresenceFeed::plausible(bad2));\n    assert(!feed.apply(bad2, T0));\n\n    // valid snapshot: endpoint total may exceed our per-title sum (titles we don't own)\n    PlayerSnapshot good;\n    good.perTitle[0x0100000000001000ull] = 3;\n    good.total = 300;  // higher is fine\n    assert(PresenceFeed::plausible(good));\n    assert(feed.apply(good, T0));\n    assert(feed.total(T0) == 300);\n}\n\nvoid testInvalidateResetsToUnknown() {\n    PresenceFeed feed;\n    feed.setEnabled(true);\n    feed.apply(makeSnapshot(5, 0, 5), T0);\n    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Online);\n    feed.invalidate();\n    assert(feed.presenceOf(0x0100000000001000ull, T0) == Presence::Unknown);\n    assert(!feed.hasTotal(T0));\n    // must fetch again\n    assert(feed.refreshDue(T0));\n}\n\nvoid testClockWrapDoesNotCrash() {\n    PresenceFeed feed;\n    feed.setEnabled(true);\n    feed.apply(makeSnapshot(1, 0, 1), T0);\n    // nowMs < appliedAtMs: treat as expired, not as \"valid forever\"\n    assert(!feed.hasTotal(T0 - 1));\n    assert(feed.presenceOf(0x0100000000001000ull, T0 - 1) == Presence::Unknown);\n    // refreshDue also returns true when the clock appears to have gone backwards\n    assert(feed.refreshDue(T0 - 1));\n}\n\n} // namespace\n\nint main() {\n    testUnknownBeforeFirstSnapshot();\n    testOnlineAndOffline();\n    testSnapshotExpires();\n    testRefreshCadence();\n    testDisabledHidesEverything();\n    testInvalidSnapshotIsRejected();\n    testInvalidateResetsToUnknown();\n    testClockWrapDoesNotCrash();\n    std::puts(\"nextendo_players_test: ok\");\n    return 0;\n}\n"}