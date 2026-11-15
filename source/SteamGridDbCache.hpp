#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace desknx::steamgriddb {

// Manages SteamGridDB artwork cache on SD card
// Root: sdmc:/switch/DeskNX/steamgriddb/
class SteamGridDbCache {
public:
    enum class ArtworkKind { Hero, Logo, Icon };

    SteamGridDbCache() = default;
    ~SteamGridDbCache() = default;

    // Ensure cache directories exist on SD card
    bool ensureDirectories();

    // Check if artwork exists for a title
    bool hasHero(std::uint64_t titleId) const;
    bool hasLogo(std::uint64_t titleId) const;
    bool hasIcon(std::uint64_t titleId) const;

    // Get full paths to artwork files
    static std::string heroPath(std::uint64_t titleId);
    static std::string logoPath(std::uint64_t titleId);
    static std::string iconPath(std::uint64_t titleId);
    static std::string metadataPath(std::uint64_t titleId);

    // Clear artwork for a title (optionally specific kind)
    bool clearArtwork(std::uint64_t titleId, ArtworkKind kind);

    // Get cache root directory
    static constexpr const char* kCacheRoot = "sdmc:/switch/DeskNX/steamgriddb";

    // Save artwork from URL (downloads via HTTP)
    // Returns true if successful
    using ProgressCallback = std::function<void(const std::string&, float)>;
    bool saveArtworkFromFile(std::uint64_t titleId, ArtworkKind kind,
                             const std::string& sourceFile,
                             const ProgressCallback& onProgress = {});

private:
    static std::string titleDirectory(std::uint64_t titleId);
};

}  // namespace desknx::steamgriddb
