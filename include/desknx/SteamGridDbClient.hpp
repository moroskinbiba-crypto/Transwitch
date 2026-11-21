#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace desknx::steamgriddb {
enum class ArtworkKind : unsigned char { Hero, Grid, Logo, Icon };
struct GameResult {
    std::uint64_t id{0}; // SteamGridDB ID, never a Nintendo Title ID.
    std::string name;
    float matchScore{0.0f};
};
struct ArtworkAsset {
    std::uint64_t id{0};
    std::string url,thumb,mime;
    int width{0},height{0},score{0};
};
using CancelCheck = bool (*)(void*);
const char* artworkKindName(ArtworkKind kind);
float titleMatchScore(const std::string& query,const std::string& candidate);
bool searchGames(const std::string& apiKey,const std::string& title,
                 std::vector<GameResult>& results,std::string& error,
                 CancelCheck shouldCancel = nullptr,void* cancelContext = nullptr);
bool fetchAssets(const std::string& apiKey,std::uint64_t gameId,
                 ArtworkKind kind,std::vector<ArtworkAsset>& assets,
                 std::string& error,CancelCheck shouldCancel = nullptr,
                 void* cancelContext = nullptr);
bool downloadImage(const std::string& url,const std::string& destination,
                   std::string& error,CancelCheck shouldCancel = nullptr,
                   void* cancelContext = nullptr);
} // namespace desknx::steamgriddb
