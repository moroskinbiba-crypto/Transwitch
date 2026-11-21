#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace desknx::steamgriddb {
class SteamGridDbCache {
public:
 enum class ArtworkKind { Hero,Grid,Logo,Icon };
 static constexpr const char* kCacheRoot="sdmc:/switch/DeskNX/steamgriddb";
 static constexpr std::uint64_t kDefaultMaximumBytes=256u*1024u*1024u;
 bool ensureDirectories();
 static std::string titleDirectory(std::uint64_t id);
 static std::string heroPath(std::uint64_t id);
 static std::string gridPath(std::uint64_t id);
 static std::string logoPath(std::uint64_t id);
 static std::string iconPath(std::uint64_t id);
 static std::string metadataPath(std::uint64_t id);
 static std::string pathFor(std::uint64_t id,ArtworkKind kind);
 bool hasArtwork(std::uint64_t id,ArtworkKind kind)const;
 bool hasHero(std::uint64_t id)const;
 bool hasGrid(std::uint64_t id)const;
 bool hasLogo(std::uint64_t id)const;
 bool hasIcon(std::uint64_t id)const;
 std::string selectedPath(std::uint64_t id)const;
 ArtworkKind selectedKind(std::uint64_t id)const;
 std::uint64_t mappedGameId(std::uint64_t id)const;
 bool saveGameMapping(std::uint64_t titleId,std::uint64_t gameId,const std::string& gameName);
 bool saveArtworkFromFile(std::uint64_t titleId,ArtworkKind kind,const std::string& sourceFile,
                          std::uint64_t gameId,std::uint64_t assetId,const std::string& gameName,
                          bool makeActive=true);
 bool clearArtwork(std::uint64_t titleId,ArtworkKind kind);
 bool restoreSystemArtwork(std::uint64_t titleId);
 std::uint64_t cacheBytes()const;
 std::uint64_t prune(std::uint64_t maximumBytes,const std::vector<std::uint64_t>& installedTitleIds);
private:
 static const char* kindKey(ArtworkKind kind);
};
} // namespace desknx::steamgriddb
