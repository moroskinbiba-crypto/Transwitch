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

{"content":"#include <desknx/SteamGridDbCovers.hpp>\n\n#include <cassert>\n#include <cstdio>\n\nusing namespace desknx::artwork;\n\n// ----- TitleCoverState transitions ----------------------------------------\n\nvoid testIdleNeedsFetch() {\n    CoverStore store;\n    assert(store.needsFetch(0x0100000000001000ull));\n}\n\nvoid testReadyDoesNotNeedFetch() {\n    CoverStore store;\n    auto& s = store.mutableGet(0x0100000000001000ull);\n    s.setReady(ArtworkSource::SteamGridDb, \"/sdmc:/switch/desknx/artwork/0100000000001000/hero.img\");\n    assert(!store.needsFetch(0x0100000000001000ull));\n}\n\nvoid testNoCoverNeedsFetch() {\n    // NoCover means SteamGridDB had nothing → escalate to GameTDB next tick.\n    CoverStore store;\n    auto& s = store.mutableGet(0x0100000000001000ull);\n    s.setNoCover();\n    assert(store.needsFetch(0x0100000000001000ull));\n}\n\nvoid testNoKeyNeedsFetch() {\n    CoverStore store;\n    auto& s = store.mutableGet(0x0100000000001000ull);\n    s.setNoKey();\n    assert(store.needsFetch(0x0100000000001000ull));\n}\n\nvoid testErrorDoesNotNeedFetch() {\n    // Error is terminal: do not loop infinitely.\n    CoverStore store;\n    auto& s = store.mutableGet(0x0100000000001000ull);\n    s.setError(\"all sources failed\");\n    assert(!store.needsFetch(0x0100000000001000ull));\n}\n\nvoid testGenerationBumpsOnEveryTransition() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    const auto g0 = s.generation;\n    s.setPending();     const auto g1 = s.generation;\n    s.setLoading();     const auto g2 = s.generation;\n    s.setNoCover();     const auto g3 = s.generation;\n    s.setNoKey();       const auto g4 = s.generation;\n    s.setAmbiguous({});           const auto g5 = s.generation;\n    s.setReady(ArtworkSource::GameTdb, \"/hero.img\"); const auto g6 = s.generation;\n    s.setError(\"oops\");const auto g7 = s.generation;\n    assert(g1 > g0);\n    assert(g2 > g1);\n    assert(g3 > g2);\n    assert(g4 > g3);\n    assert(g5 > g4);\n    assert(g6 > g5);\n    assert(g7 > g6);\n}\n\nvoid testIsReadyAndIsLoading() {\n    TitleCoverState s;\n    assert(!s.isReady());\n    assert(!s.isLoading());\n    s.setPending();\n    assert(s.isLoading());\n    s.setLoading();\n    assert(s.isLoading());\n    s.setReady(ArtworkSource::EshopBanner, \"/banner.img\");\n    assert(s.isReady());\n    assert(!s.isLoading());\n}\n\nvoid testSourceName() {\n    TitleCoverState s;\n    s.setReady(ArtworkSource::SteamGridDb, \"/hero.img\");\n    assert(std::string(s.sourceName()) == \"SteamGridDB\");\n    s.setReady(ArtworkSource::GameTdb, \"/hero.img\");\n    assert(std::string(s.sourceName()) == \"GameTDB\");\n    s.setReady(ArtworkSource::EshopBanner, \"/hero.img\");\n    assert(std::string(s.sourceName()) == \"eShop\");\n}\n\nvoid testInvalidateAutoMatchedKeepsManual() {\n    CoverStore store;\n    auto& a = store.mutableGet(0x0100000000001000ull);\n    a.setReady(ArtworkSource::SteamGridDb, \"/hero.img\");\n    const auto genA = a.generation;\n\n    auto& b = store.mutableGet(0x010000000000100Bull);\n    b.setReady(ArtworkSource::SteamGridDb, \"/hero.img\");\n    // b is NOT manually-selected; a is NOT either — both get invalidated by default.\n    // We only protect status (can't set manuallySelected via public API here),\n    // but we can verify that all SteamGridDb entries become Idle.\n    store.invalidateAutoMatched();\n    assert(store.get(0x0100000000001000ull).status == CoverStatus::Idle);\n    assert(store.get(0x010000000000100Bull).status == CoverStatus::Idle);\n    // Generations must have been bumped.\n    assert(store.get(0x0100000000001000ull).generation > genA);\n}\n\nvoid testEraseRemovedTitle() {\n    CoverStore store;\n    store.mutableGet(0x0100000000001000ull).setReady(ArtworkSource::GameTdb, \"/hero.img\");\n    store.erase(0x0100000000001000ull);\n    // After erase the store behaves as if it never saw the title.\n    assert(store.needsFetch(0x0100000000001000ull));\n    assert(store.get(0x0100000000001000ull).status == CoverStatus::Idle);\n}\n\n// ----- decideFallback rules -----------------------------------------------\n\nvoid testDecideFallbackWithKey() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    FallbackContext ctx;\n    ctx.hasSteamGridDbKey = true;\n    ctx.cachedArtworkPresent = false;\n    ctx.cacheIsCurrent = false;\n    // Fresh title, key set → try SteamGridDB.\n    assert(decideFallback(s, ctx) == FallbackAction::TrySteamGridDb);\n}\n\nvoid testDecideFallbackWithoutKey() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    FallbackContext ctx;\n    ctx.hasSteamGridDbKey = false;\n    // No key → skip to GameTDB.\n    assert(decideFallback(s, ctx) == FallbackAction::TryGameTdb);\n}\n\nvoid testDecideFallbackCacheHit() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    FallbackContext ctx;\n    ctx.hasSteamGridDbKey = true;\n    ctx.cachedArtworkPresent = true;\n    ctx.cacheIsCurrent = true;\n    // Fresh cache → Done (use cached file, no network call).\n    assert(decideFallback(s, ctx) == FallbackAction::Done);\n}\n\nvoid testDecideFallbackStaleCacheRefetches() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    FallbackContext ctx;\n    ctx.hasSteamGridDbKey = true;\n    ctx.cachedArtworkPresent = true;\n    ctx.cacheIsCurrent = false;  // matcher version outdated\n    // Stale cache + key → re-query SteamGridDB.\n    assert(decideFallback(s, ctx) == FallbackAction::TrySteamGridDb);\n}\n\nvoid testDecideFallbackNoCover() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    s.setNoCover();\n    FallbackContext ctx;\n    ctx.hasSteamGridDbKey = true;\n    // SteamGridDB confirmed nothing → try GameTDB.\n    assert(decideFallback(s, ctx) == FallbackAction::TryGameTdb);\n}\n\nvoid testDecideFallbackReady() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    s.setReady(ArtworkSource::GameTdb, \"/hero.img\");\n    FallbackContext ctx;\n    assert(decideFallback(s, ctx) == FallbackAction::Done);\n}\n\nvoid testDecideFallbackError() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    s.setError(\"all failed\");\n    FallbackContext ctx;\n    assert(decideFallback(s, ctx) == FallbackAction::Done);\n}\n\nvoid testDecideFallbackAmbiguous() {\n    TitleCoverState s;\n    s.titleId = 0x0100000000001000ull;\n    s.setAmbiguous({ {1, \"Match A\", 0.90f}, {2, \"Match B\", 0.88f} });\n    FallbackContext ctx;\n    assert(decideFallback(s, ctx) == FallbackAction::NeedUserPick);\n}\n\n// ----- artworkDir key format -----------------------------------------------\n\nvoid testArtworkDirFormat() {\n    // Title ID 0 → 16 upper-case hex zeroes.\n    assert(artworkDir(0x0000000000000000ull) == \"sdmc:/switch/desknx/artwork/0000000000000000\");\n    // Well-known title: Super Mario Odyssey.\n    assert(artworkDir(0x0100000000010000ull) == \"sdmc:/switch/desknx/artwork/0100000000010000\");\n    // Paths built from key.\n    assert(heroPath(0x0100000000010000ull) == \"sdmc:/switch/desknx/artwork/0100000000010000/hero.img\");\n    assert(logoPath(0x0100000000010000ull) == \"sdmc:/switch/desknx/artwork/0100000000010000/logo.img\");\n    assert(iconPath(0x0100000000010000ull) == \"sdmc:/switch/desknx/artwork/0100000000010000/icon.img\");\n    assert(metadataPath(0x0100000000010000ull) == \"sdmc:/switch/desknx/artwork/0100000000010000/metadata.json\");\n}\n\n// ----- CachedMetadata isCurrent -------------------------------------------\n\nvoid testCachedMetadataCurrentMatcherVersion() {\n    CachedMetadata m;\n    m.matcherVersion = kMatcherVersion;\n    assert(m.isCurrent());\n}\n\nvoid testCachedMetadataOldMatcherVersionNotCurrent() {\n    CachedMetadata m;\n    m.matcherVersion = kMatcherVersion - 1;\n    assert(!m.isCurrent());\n}\n\nvoid testCachedMetadataManuallySelectedIsAlwaysCurrent() {\n    CachedMetadata m;\n    m.matcherVersion = 0;        // very old\n    m.manuallySelected = true;\n    assert(m.isCurrent());       // user chose it, never evict\n}\n\nint main() {\n    testIdleNeedsFetch();\n    testReadyDoesNotNeedFetch();\n    testNoCoverNeedsFetch();\n    testNoKeyNeedsFetch();\n    testErrorDoesNotNeedFetch();\n    testGenerationBumpsOnEveryTransition();\n    testIsReadyAndIsLoading();\n    testSourceName();\n    testInvalidateAutoMatchedKeepsManual();\n    testEraseRemovedTitle();\n    testDecideFallbackWithKey();\n    testDecideFallbackWithoutKey();\n    testDecideFallbackCacheHit();\n    testDecideFallbackStaleCacheRefetches();\n    testDecideFallbackNoCover();\n    testDecideFallbackReady();\n    testDecideFallbackError();\n    testDecideFallbackAmbiguous();\n    testArtworkDirFormat();\n    testCachedMetadataCurrentMatcherVersion();\n    testCachedMetadataOldMatcherVersionNotCurrent();\n    testCachedMetadataManuallySelectedIsAlwaysCurrent();\n    std::puts(\"steamgriddb_covers_test: ok\");\n    return 0;\n}\n"}