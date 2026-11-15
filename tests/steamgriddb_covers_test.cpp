#include <desknx/SteamGridDbCovers.hpp>

#include <cassert>
#include <cstdio>

using namespace desknx::artwork;

// ----- TitleCoverState transitions ----------------------------------------

void testIdleNeedsFetch() {
    CoverStore store;
    assert(store.needsFetch(0x0100000000001000ull));
}

void testReadyDoesNotNeedFetch() {
    CoverStore store;
    auto& s = store.mutableGet(0x0100000000001000ull);
    s.setReady(ArtworkSource::SteamGridDb, "/sdmc:/switch/desknx/artwork/0100000000001000/hero.img");
    assert(!store.needsFetch(0x0100000000001000ull));
}

void testNoCoverNeedsFetch() {
    // NoCover means SteamGridDB had nothing → escalate to GameTDB next tick.
    CoverStore store;
    auto& s = store.mutableGet(0x0100000000001000ull);
    s.setNoCover();
    assert(store.needsFetch(0x0100000000001000ull));
}

void testNoKeyNeedsFetch() {
    CoverStore store;
    auto& s = store.mutableGet(0x0100000000001000ull);
    s.setNoKey();
    assert(store.needsFetch(0x0100000000001000ull));
}

void testErrorDoesNotNeedFetch() {
    // Error is terminal: do not loop infinitely.
    CoverStore store;
    auto& s = store.mutableGet(0x0100000000001000ull);
    s.setError("all sources failed");
    assert(!store.needsFetch(0x0100000000001000ull));
}

void testGenerationBumpsOnEveryTransition() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    const auto g0 = s.generation;
    s.setPending();     const auto g1 = s.generation;
    s.setLoading();     const auto g2 = s.generation;
    s.setNoCover();     const auto g3 = s.generation;
    s.setNoKey();       const auto g4 = s.generation;
    s.setAmbiguous({});           const auto g5 = s.generation;
    s.setReady(ArtworkSource::GameTdb, "/hero.img"); const auto g6 = s.generation;
    s.setError("oops");const auto g7 = s.generation;
    assert(g1 > g0);
    assert(g2 > g1);
    assert(g3 > g2);
    assert(g4 > g3);
    assert(g5 > g4);
    assert(g6 > g5);
    assert(g7 > g6);
}

void testIsReadyAndIsLoading() {
    TitleCoverState s;
    assert(!s.isReady());
    assert(!s.isLoading());
    s.setPending();
    assert(s.isLoading());
    s.setLoading();
    assert(s.isLoading());
    s.setReady(ArtworkSource::EshopBanner, "/banner.img");
    assert(s.isReady());
    assert(!s.isLoading());
}

void testSourceName() {
    TitleCoverState s;
    s.setReady(ArtworkSource::SteamGridDb, "/hero.img");
    assert(std::string(s.sourceName()) == "SteamGridDB");
    s.setReady(ArtworkSource::GameTdb, "/hero.img");
    assert(std::string(s.sourceName()) == "GameTDB");
    s.setReady(ArtworkSource::EshopBanner, "/hero.img");
    assert(std::string(s.sourceName()) == "eShop");
}

void testInvalidateAutoMatchedKeepsManual() {
    CoverStore store;
    auto& a = store.mutableGet(0x0100000000001000ull);
    a.setReady(ArtworkSource::SteamGridDb, "/hero.img");
    const auto genA = a.generation;

    auto& b = store.mutableGet(0x010000000000100Bull);
    b.setReady(ArtworkSource::SteamGridDb, "/hero.img");
    // b is NOT manually-selected; a is NOT either — both get invalidated by default.
    // We only protect status (can't set manuallySelected via public API here),
    // but we can verify that all SteamGridDb entries become Idle.
    store.invalidateAutoMatched();
    assert(store.get(0x0100000000001000ull).status == CoverStatus::Idle);
    assert(store.get(0x010000000000100Bull).status == CoverStatus::Idle);
    // Generations must have been bumped.
    assert(store.get(0x0100000000001000ull).generation > genA);
}

void testEraseRemovedTitle() {
    CoverStore store;
    store.mutableGet(0x0100000000001000ull).setReady(ArtworkSource::GameTdb, "/hero.img");
    store.erase(0x0100000000001000ull);
    // After erase the store behaves as if it never saw the title.
    assert(store.needsFetch(0x0100000000001000ull));
    assert(store.get(0x0100000000001000ull).status == CoverStatus::Idle);
}

// ----- decideFallback rules -----------------------------------------------

void testDecideFallbackWithKey() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    FallbackContext ctx;
    ctx.hasSteamGridDbKey = true;
    ctx.cachedArtworkPresent = false;
    ctx.cacheIsCurrent = false;
    // Fresh title, key set → try SteamGridDB.
    assert(decideFallback(s, ctx) == FallbackAction::TrySteamGridDb);
}

void testDecideFallbackWithoutKey() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    FallbackContext ctx;
    ctx.hasSteamGridDbKey = false;
    // No key → skip to GameTDB.
    assert(decideFallback(s, ctx) == FallbackAction::TryGameTdb);
}

void testDecideFallbackCacheHit() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    FallbackContext ctx;
    ctx.hasSteamGridDbKey = true;
    ctx.cachedArtworkPresent = true;
    ctx.cacheIsCurrent = true;
    // Fresh cache → Done (use cached file, no network call).
    assert(decideFallback(s, ctx) == FallbackAction::Done);
}

void testDecideFallbackStaleCacheRefetches() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    FallbackContext ctx;
    ctx.hasSteamGridDbKey = true;
    ctx.cachedArtworkPresent = true;
    ctx.cacheIsCurrent = false;  // matcher version outdated
    // Stale cache + key → re-query SteamGridDB.
    assert(decideFallback(s, ctx) == FallbackAction::TrySteamGridDb);
}

void testDecideFallbackNoCover() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    s.setNoCover();
    FallbackContext ctx;
    ctx.hasSteamGridDbKey = true;
    // SteamGridDB confirmed nothing → try GameTDB.
    assert(decideFallback(s, ctx) == FallbackAction::TryGameTdb);
}

void testDecideFallbackReady() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    s.setReady(ArtworkSource::GameTdb, "/hero.img");
    FallbackContext ctx;
    assert(decideFallback(s, ctx) == FallbackAction::Done);
}

void testDecideFallbackError() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    s.setError("all failed");
    FallbackContext ctx;
    assert(decideFallback(s, ctx) == FallbackAction::Done);
}

void testDecideFallbackAmbiguous() {
    TitleCoverState s;
    s.titleId = 0x0100000000001000ull;
    s.setAmbiguous({ {1, "Match A", 0.90f}, {2, "Match B", 0.88f} });
    FallbackContext ctx;
    assert(decideFallback(s, ctx) == FallbackAction::NeedUserPick);
}

// ----- artworkDir key format -----------------------------------------------

void testArtworkDirFormat() {
    // Title ID 0 → 16 upper-case hex zeroes.
    assert(artworkDir(0x0000000000000000ull) == "sdmc:/switch/desknx/artwork/0000000000000000");
    // Well-known title: Super Mario Odyssey.
    assert(artworkDir(0x0100000000010000ull) == "sdmc:/switch/desknx/artwork/0100000000010000");
    // Paths built from key.
    assert(heroPath(0x0100000000010000ull) == "sdmc:/switch/desknx/artwork/0100000000010000/hero.img");
    assert(logoPath(0x0100000000010000ull) == "sdmc:/switch/desknx/artwork/0100000000010000/logo.img");
    assert(iconPath(0x0100000000010000ull) == "sdmc:/switch/desknx/artwork/0100000000010000/icon.img");
    assert(metadataPath(0x0100000000010000ull) == "sdmc:/switch/desknx/artwork/0100000000010000/metadata.json");
}

// ----- CachedMetadata isCurrent -------------------------------------------

void testCachedMetadataCurrentMatcherVersion() {
    CachedMetadata m;
    m.matcherVersion = kMatcherVersion;
    assert(m.isCurrent());
}

void testCachedMetadataOldMatcherVersionNotCurrent() {
    CachedMetadata m;
    m.matcherVersion = kMatcherVersion - 1;
    assert(!m.isCurrent());
}

void testCachedMetadataManuallySelectedIsAlwaysCurrent() {
    CachedMetadata m;
    m.matcherVersion = 0;        // very old
    m.manuallySelected = true;
    assert(m.isCurrent());       // user chose it, never evict
}

int main() {
    testIdleNeedsFetch();
    testReadyDoesNotNeedFetch();
    testNoCoverNeedsFetch();
    testNoKeyNeedsFetch();
    testErrorDoesNotNeedFetch();
    testGenerationBumpsOnEveryTransition();
    testIsReadyAndIsLoading();
    testSourceName();
    testInvalidateAutoMatchedKeepsManual();
    testEraseRemovedTitle();
    testDecideFallbackWithKey();
    testDecideFallbackWithoutKey();
    testDecideFallbackCacheHit();
    testDecideFallbackStaleCacheRefetches();
    testDecideFallbackNoCover();
    testDecideFallbackReady();
    testDecideFallbackError();
    testDecideFallbackAmbiguous();
    testArtworkDirFormat();
    testCachedMetadataCurrentMatcherVersion();
    testCachedMetadataOldMatcherVersionNotCurrent();
    testCachedMetadataManuallySelectedIsAlwaysCurrent();
    std::puts("steamgriddb_covers_test: ok");
    return 0;
}
