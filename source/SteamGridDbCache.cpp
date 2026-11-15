#include "SteamGridDbCache.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace desknx::steamgriddb {

std::string SteamGridDbCache::titleDirectory(std::uint64_t titleId) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(titleId));
    return std::string(kCacheRoot) + "/" + buffer;
}

std::string SteamGridDbCache::heroPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/hero.img";
}

std::string SteamGridDbCache::logoPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/logo.img";
}

std::string SteamGridDbCache::iconPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/icon.img";
}

std::string SteamGridDbCache::metadataPath(std::uint64_t titleId) {
    return titleDirectory(titleId) + "/metadata.json";
}

bool SteamGridDbCache::ensureDirectories() {
    std::error_code ec;

    // Create root cache directory
    std::filesystem::create_directories(kCacheRoot, ec);
    if (ec) {
        fprintf(stderr, "[steamgriddb-cache] failed to create root: %s\n", ec.message().c_str());
        return false;
    }

    // Also create the main DeskNX folder structure
    std::filesystem::create_directories("sdmc:/switch/DeskNX", ec);
    if (ec && ec.value() != 0) {
        // Ignore "already exists" errors
        fprintf(stderr, "[steamgriddb-cache] warning creating /switch/DeskNX: %s\n",
                ec.message().c_str());
    }

    fprintf(stderr, "[steamgriddb-cache] directories ensured\n");
    return true;
}

bool SteamGridDbCache::hasHero(std::uint64_t titleId) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(heroPath(titleId), ec);
}

bool SteamGridDbCache::hasLogo(std::uint64_t titleId) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(logoPath(titleId), ec);
}

bool SteamGridDbCache::hasIcon(std::uint64_t titleId) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(iconPath(titleId), ec);
}

bool SteamGridDbCache::clearArtwork(std::uint64_t titleId, ArtworkKind kind) {
    if (titleId == 0)
        return false;

    std::error_code ec;
    bool removed = false;

    auto removeFile = [&](const std::string& path) {
        if (std::filesystem::remove(path, ec)) {
            removed = true;
        }
        ec.clear();
    };

    // Clear requested artwork types
    if (kind == ArtworkKind::Hero || kind == ArtworkKind::Hero) {
        removeFile(heroPath(titleId));
    }
    if (kind == ArtworkKind::Logo || kind == ArtworkKind::Logo) {
        removeFile(logoPath(titleId));
    }
    if (kind == ArtworkKind::Icon || kind == ArtworkKind::Icon) {
        removeFile(iconPath(titleId));
    }

    // Update metadata
    nlohmann::json metadata = nlohmann::json::object();
    std::ifstream metaInput(metadataPath(titleId));
    if (metaInput.is_open()) {
        try {
            metaInput >> metadata;
        } catch (...) {
            metadata = nlohmann::json::object();
        }
    }

    if (kind == ArtworkKind::Hero) {
        metadata["hero"] = false;
        metadata.erase("selectedHeroId");
    }
    if (kind == ArtworkKind::Logo) {
        metadata["logo"] = false;
        metadata.erase("selectedLogoId");
    }
    if (kind == ArtworkKind::Icon) {
        metadata["icon"] = false;
        metadata.erase("selectedIconId");
    }

    // If no artwork remains, remove the directory
    const bool anyLeft = metadata.value("hero", false) || metadata.value("logo", false) ||
                         metadata.value("icon", false);
    if (!anyLeft) {
        removeFile(metadataPath(titleId));
        std::filesystem::remove(titleDirectory(titleId), ec);
    } else {
        std::ofstream metaOutput(metadataPath(titleId), std::ios::trunc);
        if (metaOutput.is_open()) {
            metaOutput << metadata.dump(2);
        }
    }

    return removed;
}

bool SteamGridDbCache::saveArtworkFromFile(std::uint64_t titleId, ArtworkKind kind,
                                           const std::string& sourceFile,
                                           const ProgressCallback& onProgress) {
    if (titleId == 0 || sourceFile.empty())
        return false;

    std::error_code ec;
    std::filesystem::create_directories(titleDirectory(titleId), ec);
    if (ec) {
        fprintf(stderr, "[steamgriddb-cache] failed to create title dir: %s\n",
                ec.message().c_str());
        return false;
    }

    const std::string destPath = kind == ArtworkKind::Hero ? heroPath(titleId)
                                : kind == ArtworkKind::Logo ? logoPath(titleId)
                                                            : iconPath(titleId);

    // Copy file with temp → rename pattern
    const std::string tempPath = destPath + ".tmp";
    try {
        std::ifstream src(sourceFile, std::ios::binary);
        if (!src.is_open()) {
            fprintf(stderr, "[steamgriddb-cache] failed to open source: %s\n", sourceFile.c_str());
            return false;
        }

        std::ofstream dst(tempPath, std::ios::binary | std::ios::trunc);
        if (!dst.is_open()) {
            fprintf(stderr, "[steamgriddb-cache] failed to open dest: %s\n", tempPath.c_str());
            return false;
        }

        // Copy in chunks
        std::vector<char> buffer(65536);
        std::streamsize bytesRead;
        while ((bytesRead = src.readsome(buffer.data(), buffer.size())) > 0) {
            dst.write(buffer.data(), bytesRead);
        }

        src.close();
        dst.close();

        if (onProgress)
            onProgress("Finalizing artwork...", 0.95f);

        // Atomic rename
        std::filesystem::remove(destPath, ec);
        ec.clear();
        std::filesystem::rename(tempPath, destPath, ec);
        if (ec) {
            fprintf(stderr, "[steamgriddb-cache] rename failed: %s\n", ec.message().c_str());
            return false;
        }

        // Update metadata
        nlohmann::json metadata = nlohmann::json::object();
        std::ifstream metaInput(metadataPath(titleId));
        if (metaInput.is_open()) {
            try {
                metaInput >> metadata;
            } catch (...) {
            }
        }

        metadata["titleId"] = titleId;
        const char* flag = kind == ArtworkKind::Hero ? "hero" : kind == ArtworkKind::Logo ? "logo"
                                                                                           : "icon";
        metadata[flag] = true;

        std::ofstream metaOutput(metadataPath(titleId), std::ios::trunc);
        if (metaOutput.is_open()) {
            metaOutput << metadata.dump(2);
        }

        if (onProgress)
            onProgress("Artwork saved", 1.0f);

        fprintf(stderr, "[steamgriddb-cache] saved %s for %016llX\n", flag,
                static_cast<unsigned long long>(titleId));
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "[steamgriddb-cache] exception: %s\n", e.what());
        return false;
    }
}

}  // namespace desknx::steamgriddb
