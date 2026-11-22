#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace desknx::nextendo {

enum class CompatibilityState : unsigned char {
    Unknown,
    NotListed,
    Disabled,
    Accepted,
    UpdateRequired
};

struct GameVersions {
    bool enabled{false};
    std::string policy;
    std::vector<std::string> accepted;
    std::string recommended;
};

struct VersionVerdict {
    CompatibilityState state{CompatibilityState::Unknown};
    std::string recommended;
};

struct GameCatalog {
    std::uint64_t revision{0};
    std::int64_t expiresAt{0};
    std::unordered_map<std::uint64_t, GameVersions> games;

    bool fresh(std::int64_t nowUnix) const {
        return revision > 0 && expiresAt > nowUnix;
    }
    VersionVerdict check(std::uint64_t titleId,
                         const std::string& installedVersion) const;
};

constexpr const char* GameCatalogKeyId = "catalog-key-1";
constexpr const char* GameCatalogPublicKey =
    "xI+atR6VesrCH89ZDZD4t3WQLcYOFEhk5F0HLMzgWk4=";

// Verifies the Ed25519 envelope before parsing or trusting any catalogue field.
// The response is bounded and must be an unexpired "game_catalog" document.
bool parseSignedGameCatalog(const std::string& envelope,
                            std::int64_t nowUnix,
                            GameCatalog& output,
                            std::string& error);

// Dotted numeric versions compare by component value; trailing zero components
// are equivalent (e.g. 4.0.1 and 4.0.1.0). Other formats require exact equality.
bool sameGameVersion(const std::string& left,const std::string& right);

// Fetches the public signed catalogue using the registered app's public Client ID.
// The caller owns the shared Nextendo network session and cancellation lifecycle.
using CatalogCancelCheck = bool (*)(void*);
bool fetchGameCatalog(const std::string& clientId,std::string& envelope,
                      std::string& error,
                      CatalogCancelCheck shouldCancel = nullptr,
                      void* cancelContext = nullptr);

} // namespace desknx::nextendo
