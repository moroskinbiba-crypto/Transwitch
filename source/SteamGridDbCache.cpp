#include "SteamGridDbCache.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace desknx::steamgriddb {
namespace {

struct ArtworkMetadata {
    std::uint64_t titleId{0};
    bool hero{false};
    bool logo{false};
    bool icon{false};
};

void skipWhitespace(const std::string& text, std::size_t& position) {
    while (position < text.size()) {
        const char ch = text[position];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            break;
        ++position;
    }
}

bool findValuePosition(const std::string& text, const char* key, std::size_t& position) {
    if (key == nullptr)
        return false;
    const std::string quotedKey = std::string("\"") + key + "\"";
    const std::size_t keyPosition = text.find(quotedKey);
    if (keyPosition == std::string::npos)
        return false;
    position = text.find(':', keyPosition + quotedKey.size());
    if (position == std::string::npos)
        return false;
    ++position;
    skipWhitespace(text, position);
    return position < text.size();
}

bool readBoolField(const std::string& text, const char* key, bool fallback) {
    std::size_t position = 0;
    if (!findValuePosition(text, key, position))
        return fallback;
    if (text.compare(position, 4, "true") == 0)
        return true;
    if (text.compare(position, 5, "false") == 0)
        return false;
    return fallback;
}

std::uint64_t readUnsignedField(const std::string& text, const char* key,
                                std::uint64_t fallback) {
    std::size_t position = 0;
    if (!findValuePosition(text, key, position))
        return fallback;
    if (text[position] < '0' || text[position] > '9')
        return fallback;

    const char* start = text.c_str() + position;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(start, &end, 10);
    if (end == start || (*end != ',' && *end != '}' &&
                         *end != ' ' && *end != '\t' &&
                         *end != '\r' && *end != '\n'))
        return fallback;
    return static_cast<std::uint64_t>(value);
}

bool readMetadata(const std::string& path, ArtworkMetadata& metadata) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    if (file.bad())
        return false;

    metadata.titleId = readUnsignedField(text, "titleId", metadata.titleId);
    metadata.hero = readBoolField(text, "hero", metadata.hero);
    metadata.logo = readBoolField(text, "logo", metadata.logo);
    metadata.icon = readBoolField(text, "icon", metadata.icon);
    return true;
}

bool writeMetadata(const std::string& path, const ArtworkMetadata& metadata) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file << "{\n"
             << "  \"titleId\": " << metadata.titleId << ",\n"
             << "  \"hero\": " << (metadata.hero ? "true" : "false") << ",\n"
             << "  \"logo\": " << (metadata.logo ? "true" : "false") << ",\n"
             << "  \"icon\": " << (metadata.icon ? "true" : "false") << "\n"
             << "}\n";
        file.flush();
        if (!file.good()) {
            file.close();
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return false;
    }
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return false;
    }
    return true;
}

bool copyFileToTemporary(const std::string& sourcePath, const std::string& temporaryPath) {
    std::ifstream source(sourcePath, std::ios::binary);
    if (!source.is_open()) {
        fprintf(stderr, "[steamgriddb-cache] failed to open source: %s\n", sourcePath.c_str());
        return false;
    }

    std::ofstream destination(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!destination.is_open()) {
        fprintf(stderr, "[steamgriddb-cache] failed to open dest: %s\n", temporaryPath.c_str());
        return false;
    }

    char buffer[64 * 1024];
    while (true) {
        source.read(buffer, static_cast<std::streamsize>(sizeof(buffer)));
        const std::streamsize bytesRead = source.gcount();
        if (bytesRead > 0)
            destination.write(buffer, bytesRead);
        if (!destination.good())
            break;
        if (bytesRead < static_cast<std::streamsize>(sizeof(buffer)))
            break;
    }

    const bool copyOk = !source.bad() && destination.good();
    destination.close();
    if (!copyOk || !destination.good()) {
        std::error_code ec;
        std::filesystem::remove(temporaryPath, ec);
        fprintf(stderr, "[steamgriddb-cache] artwork copy failed: %s\n", sourcePath.c_str());
        return false;
    }
    return true;
}

} // namespace

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
    std::filesystem::create_directories(kCacheRoot, ec);
    if (ec) {
        fprintf(stderr, "[steamgriddb-cache] failed to create root: %s\n", ec.message().c_str());
        return false;
    }

    ec.clear();
    std::filesystem::create_directories("sdmc:/switch/DeskNX", ec);
    if (ec) {
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
        ec.clear();
        if (std::filesystem::remove(path, ec))
            removed = true;
        if (ec)
            fprintf(stderr, "[steamgriddb-cache] failed to remove %s: %s\n",
                    path.c_str(), ec.message().c_str());
    };

    ArtworkMetadata metadata;
    readMetadata(metadataPath(titleId), metadata);

    if (kind == ArtworkKind::Hero) {
        removeFile(heroPath(titleId));
        metadata.hero = false;
    } else if (kind == ArtworkKind::Logo) {
        removeFile(logoPath(titleId));
        metadata.logo = false;
    } else {
        removeFile(iconPath(titleId));
        metadata.icon = false;
    }

    const bool anyLeft = metadata.hero || metadata.logo || metadata.icon;
    if (!anyLeft) {
        removeFile(metadataPath(titleId));
        ec.clear();
        std::filesystem::remove(titleDirectory(titleId), ec);
    } else if (!writeMetadata(metadataPath(titleId), metadata)) {
        fprintf(stderr, "[steamgriddb-cache] warning: failed to update metadata after clear\n");
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
    const std::string temporaryPath = destPath + ".tmp";

    if (!copyFileToTemporary(sourceFile, temporaryPath))
        return false;

    if (onProgress)
        onProgress("Finalizing artwork...", 0.95f);

    ec.clear();
    std::filesystem::remove(destPath, ec);
    if (ec) {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryPath, cleanupError);
        fprintf(stderr, "[steamgriddb-cache] failed to replace artwork: %s\n",
                ec.message().c_str());
        return false;
    }
    ec.clear();
    std::filesystem::rename(temporaryPath, destPath, ec);
    if (ec) {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryPath, cleanupError);
        fprintf(stderr, "[steamgriddb-cache] rename failed: %s\n", ec.message().c_str());
        return false;
    }

    ArtworkMetadata metadata;
    readMetadata(metadataPath(titleId), metadata);
    metadata.titleId = titleId;
    if (kind == ArtworkKind::Hero)
        metadata.hero = true;
    else if (kind == ArtworkKind::Logo)
        metadata.logo = true;
    else
        metadata.icon = true;

    if (!writeMetadata(metadataPath(titleId), metadata))
        fprintf(stderr, "[steamgriddb-cache] warning: artwork saved without updated metadata\n");

    if (onProgress)
        onProgress("Artwork saved", 1.0f);

    const char* flag = kind == ArtworkKind::Hero ? "hero"
                     : kind == ArtworkKind::Logo ? "logo" : "icon";
    fprintf(stderr, "[steamgriddb-cache] saved %s for %016llX\n", flag,
            static_cast<unsigned long long>(titleId));
    return true;
}

} // namespace desknx::steamgriddb
