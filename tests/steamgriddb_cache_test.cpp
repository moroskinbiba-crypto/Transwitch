#include "SteamGridDbCache.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main() {
    using Cache=desknx::steamgriddb::SteamGridDbCache;
    namespace fs=std::filesystem;
    fs::remove_all(Cache::kCacheRoot);

    const std::uint64_t titleA=0x0100000000001000ull;
    const std::uint64_t titleB=0x0100000000002000ull;
    const std::uint64_t sgdbGameId=98765;
    const std::string gridSource="/tmp/desknx-sgdb-grid.bin";
    const std::string heroSource="/tmp/desknx-sgdb-hero.bin";
    {
        std::ofstream out(gridSource,std::ios::binary);
        out<<"grid-artwork-test";
    }
    {
        std::ofstream out(heroSource,std::ios::binary);
        out<<"hero-artwork-test";
    }

    Cache cache;
    assert(cache.ensureDirectories());
    assert(cache.saveGameMapping(titleA,sgdbGameId,"Test Game"));
    assert(cache.mappedGameId(titleA)==sgdbGameId);
    assert(cache.saveArtworkFromFile(titleA,Cache::ArtworkKind::Grid,
        gridSource,sgdbGameId,111,"Test Game",true));
    assert(cache.hasGrid(titleA));
    assert(cache.selectedKind(titleA)==Cache::ArtworkKind::Grid);
    assert(cache.saveArtworkFromFile(titleA,Cache::ArtworkKind::Hero,
        heroSource,sgdbGameId,222,"Test Game",false));
    assert(cache.hasHero(titleA));
    assert(cache.selectedKind(titleA)==Cache::ArtworkKind::Grid);
    assert(cache.activateCachedArtwork(titleA,Cache::ArtworkKind::Hero));
    assert(cache.selectedKind(titleA)==Cache::ArtworkKind::Hero);
    assert(cache.hasArtwork(titleA,Cache::ArtworkKind::Hero));
    assert(!cache.hasArtwork(titleB,Cache::ArtworkKind::Grid));

    assert(cache.saveGameMapping(titleB,sgdbGameId+1,"Unused Game"));
    assert(cache.saveArtworkFromFile(titleB,Cache::ArtworkKind::Grid,
        gridSource,sgdbGameId+1,333,"Unused Game",true));
    const auto afterCleanup=cache.prune(Cache::kDefaultMaximumBytes,{titleA});
    assert(afterCleanup<=Cache::kDefaultMaximumBytes);
    assert(!fs::exists(Cache::titleDirectory(titleB)));
    assert(fs::exists(Cache::titleDirectory(titleA)));

    assert(cache.restoreSystemArtwork(titleA));
    assert(cache.selectedPath(titleA).empty());
    assert(cache.mappedGameId(titleA)==sgdbGameId);
    assert(cache.hasHero(titleA));
    assert(cache.hasGrid(titleA));
    // A hard size limit evicts the least-recently-used title directory while
    // keeping the cache strictly below the configured bound.
    assert(cache.prune(1,{titleA})<=1);
    assert(!fs::exists(Cache::titleDirectory(titleA)));

    std::remove(gridSource.c_str());
    std::remove(heroSource.c_str());
    fs::remove_all("sdmc:");
    std::puts("steamgriddb_cache_test: ok");
    return 0;
}
