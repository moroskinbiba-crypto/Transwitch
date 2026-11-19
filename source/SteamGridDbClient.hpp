#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace desknx::steamgriddb_api {

struct GameMatch {
    std::uint64_t id{0};
    std::string name;
};

struct IconAsset {
    std::uint64_t id{0};
    std::string url;
    std::string thumb;
    unsigned width{0};
    unsigned height{0};
};

bool searchGames(const std::string& apiKey,const std::string& query,
                 std::vector<GameMatch>& results,std::string& error);
bool listIcons(const std::string& apiKey,std::uint64_t gameId,
               std::vector<IconAsset>& results,std::string& error);
bool downloadImage(const std::string& url,std::vector<unsigned char>& bytes,
                   std::string& error);
bool isSupportedImage(const std::vector<unsigned char>& bytes);

} // namespace desknx::steamgriddb_api
