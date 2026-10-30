#include "catalog.hpp"
#include "../include/desknx/Diagnostics.hpp"
#include "slaunch_applications.hpp"
#include "../include/desknx/StrictParse.hpp"
#include "../include/desknx/CatalogPagination.hpp"
#include <desknx/SafeReplace.hpp>

#include <switch.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <unordered_map>
#include <vector>

namespace desknx::catalog {
namespace {

using desknx::model::Candidate;
using desknx::model::Kind;
constexpr size_t MaxItems = static_cast<size_t>(desknx::model::MaxItems);
constexpr size_t GameReserve = 32;
constexpr size_t WidgetReserve = 4;
constexpr const char* CacheDir = "sdmc:/switch/DeskNX/cache";
constexpr const char* IconsDir = "sdmc:/switch/DeskNX/cache/icons";
constexpr const char* CacheFile = "sdmc:/switch/DeskNX/cache/catalogue.tsv";
constexpr const char* CacheTemp = "sdmc:/switch/DeskNX/cache/catalogue.tmp";
constexpr size_t MaxNroAssetBytes = 2 * 1024 * 1024;

struct GameRecord { u64 id; u64 last_updated; u32 viewFlags; };
struct NroFile { std::string path; u64 size; u64 mtime; };
struct Signatures {
    size_t gameCount{0};
    uint64_t gameHash{14695981039346656037ULL};
    uint64_t sdHash{14695981039346656037ULL};
};

// libnx service sessions are process-wide. Keep them alive for the menu
// lifetime, matching SLauncher's __appInit/__appExit lifecycle.
bool gAccountServiceReady = false;
bool gNsServiceReady = false;
std::uint32_t gAccountServiceError = 0;
std::uint32_t gNsServiceError = 0;

uint64_t hashBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
    return hash;
}
uint64_t hashText(uint64_t hash, const std::string& value) {
    hash = hashBytes(hash, value.data(), value.size());
    const unsigned char separator = 0xFF;
    return hashBytes(hash, &separator, sizeof(separator));
}
void EnsureCacheDirectories() {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/DeskNX", 0777);
    mkdir(CacheDir, 0777);
    mkdir(IconsDir, 0777);
}

void LogDiagnostic(const char* format, ...) {
    EnsureCacheDirectories();
    char message[1024]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    desknx::diag::Write("catalogue", "trace", "%s", message);

    // Keep the original focused log for backwards compatibility.
    constexpr const char* LogFile = "sdmc:/switch/DeskNX/catalogue.log";
    struct stat st{};
    if (stat(LogFile, &st) == 0 && st.st_size > 128 * 1024) {
        std::remove("sdmc:/switch/DeskNX/catalogue.log.old");
        std::rename(LogFile, "sdmc:/switch/DeskNX/catalogue.log.old");
    }
    FILE* file = std::fopen(LogFile, "a");
    if (!file) return;
    std::fprintf(file, "[%lld] %s\n", static_cast<long long>(std::time(nullptr)), message);
    std::fclose(file);
}
std::string fileTitle(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name.resize(dot);
    for (char& c : name) if (c == '_' || c == '-') c = ' ';
    return name;
}
bool IsRegularFile(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Match SLauncher's packaged-libnx title resolution: prefer English, then
// the first populated language. This avoids opening the set service for every
// title via nacpGetLanguageEntry and still gives a useful name when the
// console-language entry is absent.
std::string GetApplicationName(const NacpStruct& nacp) {
    // Keep all libnx NACP layout differences in the SLaunch compatibility
    // adapter (legacy NacpStruct::lang vs newer lang_data.lang).
    return desknx::slaunch::ResolveTitleName(nacp);
}

void CollectNros(const std::string& root, int depth, std::vector<NroFile>& out) {
    if (depth > 3 || out.size() >= MaxItems) return;
    DIR* dir = opendir(root.c_str());
    if (!dir) return;
    dirent* entry = nullptr;
    while ((entry = readdir(dir)) && out.size() < MaxItems) {
        std::string name = entry->d_name;
        if (name == "." || name == ".." || (!name.empty() && name[0] == '.')) continue;
        const std::string path = root + "/" + name;
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            // This directory belongs to DeskNX itself. It contains the persistent
            // catalogue, hundreds of cached icons and diagnostics, not user NROs.
            // Walking it on every launch adds avoidable SD-card I/O.
            if (depth == 0 && name == "DeskNX") continue;
            CollectNros(path, depth + 1, out);
        } else if (S_ISREG(st.st_mode) && name.size() > 4) {
            std::string ext = name.substr(name.size() - 4);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".nro" && name != "DeskNX.nro")
                out.push_back({path, static_cast<u64>(st.st_size), static_cast<u64>(st.st_mtime)});
        }
    }
    closedir(dir);
}
std::vector<GameRecord> ReadGameRecords(bool nsReady, std::uint32_t& errorCode) {
    std::vector<GameRecord> records;
    if (!nsReady) {
        LogDiagnostic("games_skip reason=ns_unavailable");
        return records;
    }

    // SLaunch sequence: enumerate every installed record, then query the
    // matching live views. View flags are presentation metadata, never filters.
    std::vector<NsApplicationRecord> installed;
    LogDiagnostic("ns_list begin");
    const Result listRc = desknx::slaunch::EnumerateInstalled(installed);
    LogDiagnostic("ns_list end rc=0x%08X records=%zu",
                  static_cast<unsigned int>(listRc), installed.size());
    if (R_FAILED(listRc)) {
        errorCode = static_cast<std::uint32_t>(listRc);
        return records;
    }

    const size_t listedCount = installed.size();
    installed.erase(std::remove_if(installed.begin(), installed.end(),
        [](const NsApplicationRecord& record) {
            return record.application_id == 0;
        }), installed.end());
    if (installed.size() != listedCount) {
        LogDiagnostic("ns_list ignored_invalid_ids count=%zu",
                      listedCount - installed.size());
    }

    std::vector<u64> titleIds;
    titleIds.reserve(installed.size());
    for (const auto& record : installed) titleIds.push_back(record.application_id);

    std::vector<NsApplicationView> views;
    const Result viewsRc = desknx::slaunch::QueryApplicationViews(titleIds, views);
    LogDiagnostic("ns_views rc=0x%08X requested=%zu returned=%zu",
                  static_cast<unsigned int>(viewsRc), titleIds.size(), views.size());
    if (R_FAILED(viewsRc)) views.clear();

    records.reserve(installed.size());
    for (size_t i = 0; i < installed.size(); ++i) {
        u32 flags = 0;
        if (i < views.size()) {
            flags = views[i].flags;
        }
        records.push_back({installed[i].application_id,
                           installed[i].last_updated, flags});
    }

    // Play statistics are loaded lazily for the selected library entry.
    return records;
}
Signatures MakeSignatures(const std::vector<GameRecord>& games, const std::vector<NroFile>& nros) {
    Signatures sig{};
    sig.gameCount = games.size();
    for (const auto& game : games) {
        sig.gameHash = hashBytes(sig.gameHash, &game.id, sizeof(game.id));
        sig.gameHash = hashBytes(sig.gameHash, &game.last_updated, sizeof(game.last_updated));
        // View flags are cached metadata too (update/game-card badges), and
        // can change independently of the installed record timestamp.
        sig.gameHash = hashBytes(sig.gameHash, &game.viewFlags, sizeof(game.viewFlags));
    }
    for (const auto& nro : nros) {
        sig.sdHash = hashText(sig.sdHash, nro.path);
        sig.sdHash = hashBytes(sig.sdHash, &nro.size, sizeof(nro.size));
        sig.sdHash = hashBytes(sig.sdHash, &nro.mtime, sizeof(nro.mtime));
    }
    return sig;
}
bool isHexTitleId(const std::string& value) {
    if (value.size() != 16) return false;
    for (unsigned char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
              (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

bool isCachedNroPath(const std::string& path) {
    constexpr char prefix[] = "sdmc:/switch/";
    if (path.compare(0, sizeof(prefix) - 1, prefix) != 0 ||
        path.find("..") != std::string::npos || path.size() <= sizeof(prefix) + 3)
        return false;
    std::string extension = path.substr(path.size() - 4);
    for (char& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension == ".nro" && IsRegularFile(path);
}

bool IsUsableCachedJpeg(const std::string& path);

bool IsMissingOrUsableCachedJpeg(const std::string& path) {
    // Some legitimate NACP/NRO files contain no icon. A missing icon is not a
    // reason to discard the entire application catalogue; an existing broken
    // image is, because the next full scan can rebuild it.
    if (!IsRegularFile(path)) return true;
    return IsUsableCachedJpeg(path);
}

bool ReadCache(const Signatures& sig, ScanResult& result, bool allowStale = false) {
    // The cache is stored on removable media and can be truncated or edited.
    // Bound the file before getline() so a corrupt single line cannot exhaust
    // the small heap available to the Switch applet.
    struct stat cacheStat{};
    constexpr size_t MaxCacheBytes = 1024 * 1024;
    constexpr size_t MaxCacheLineBytes = 2048;
    if (stat(CacheFile, &cacheStat) != 0 || !S_ISREG(cacheStat.st_mode) ||
        cacheStat.st_size <= 0 ||
        static_cast<uint64_t>(cacheStat.st_size) > MaxCacheBytes)
        return false;

    std::ifstream input(CacheFile, std::ios::binary);
    if (!input) return false;
    std::string line;
    if (!std::getline(input, line) || line.size() > 256) return false;
    const auto header = desknx::model::splitTabs(line);
    if (header.size() != 4 || header[0] != "DESKNXCAT2") return false;
    if (!allowStale) {
        char gameHash[32]{}, sdHash[32]{};
        std::snprintf(gameHash, sizeof(gameHash), "%016llX", static_cast<unsigned long long>(sig.gameHash));
        std::snprintf(sdHash, sizeof(sdHash), "%016llX", static_cast<unsigned long long>(sig.sdHash));
        if (header[1] != std::to_string(sig.gameCount) || header[2] != gameHash || header[3] != sdHash)
            return false;
    }

    std::vector<Candidate> items;
    while (std::getline(input, line)) {
        if (line.size() > MaxCacheLineBytes || items.size() >= MaxItems) return false;
        const auto fields = desknx::model::splitTabs(line);
        if (fields.size() != 8 || fields[0] != "C" ||
            fields[1].size() > 16 || fields[2].size() > 512 || fields[3].size() > 512)
            return false;

        Candidate item{};
        item.kind = desknx::model::parseKind(fields[1]);
        if (item.kind == Kind::Unsupported) return false;
        item.title = fields[2];
        item.data = fields[3];
        char* flagsEnd = nullptr;
        errno = 0;
        const unsigned long parsedFlags = std::strtoul(fields[7].c_str(), &flagsEnd, 10);
        if (errno != 0 || flagsEnd == fields[7].c_str() || !flagsEnd ||
            *flagsEnd != '\0' || parsedFlags > 0xFFFFFFFFul)
            return false;
        item.viewFlags = static_cast<uint32_t>(parsedFlags);
        if (!desknx::strict::parseInt32(fields[4], item.w) ||
            !desknx::strict::parseInt32(fields[5], item.h) ||
            !desknx::strict::parseInt32(fields[6], item.color))
            return false;
        if (item.w < 1 || item.w > 3 || item.h < 1 || item.h > 2 ||
            item.color < 0 || item.color > 7 || item.title.empty())
            return false;

        if (item.kind == Kind::Game) {
            if (!isHexTitleId(item.data)) return false;
            if (!allowStale) {
                const std::string iconPath=desknx::model::iconCachePath(item.kind,item.data);
                if (!IsMissingOrUsableCachedJpeg(iconPath)) return false;
            }
        } else if (item.kind == Kind::Nro) {
            if (!isCachedNroPath(item.data)) {
                if (allowStale) continue; // stale homebrew entry was removed from SD
                return false;
            }
            if (!allowStale) {
                const std::string iconPath=desknx::model::iconCachePath(item.kind,item.data);
                if (!IsMissingOrUsableCachedJpeg(iconPath)) return false;
            }
        } else if (!item.data.empty()) {
            return false;
        }
        items.push_back(std::move(item));
    }
    if (input.bad() || items.size() < WidgetReserve) return false;
    result.items = std::move(items);
    result.fromCache = true;
    return true;
}
size_t jpegSize(const u8* data, size_t length) {
    if (!data || length < 4 || data[0] != 0xFF || data[1] != 0xD8) return 0;
    for (size_t i = 2; i + 1 < length; ++i)
        if (data[i] == 0xFF && data[i + 1] == 0xD9) return i + 2;
    return 0;
}

bool IsUsableCachedJpeg(const std::string& path) {
    struct stat st{};
    constexpr size_t MaxIconBytes = 2 * 1024 * 1024;
    constexpr size_t HeaderBytes = 8 * 1024;
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 4 || static_cast<uint64_t>(st.st_size) > MaxIconBytes)
        return false;

    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<u8> bytes(std::min<size_t>(
        static_cast<size_t>(st.st_size), HeaderBytes));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    const size_t length = static_cast<size_t>(input.gcount());
    if (length < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) return false;

    // Validate the JPEG frame header before trusting a cached icon. This is
    // intentionally bounded: the scanner does not decode every icon on SD.
    size_t pos = 2;
    for (int segments = 0; segments < 256 && pos + 1 < length; ++segments) {
        while (pos < length && bytes[pos] != 0xFF) ++pos;
        while (pos < length && bytes[pos] == 0xFF) ++pos;
        if (pos >= length) return false;
        const u8 marker = bytes[pos++];
        if (marker == 0xD9 || marker == 0xDA) return false;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (pos + 2 > length) return false;
        const size_t segmentLength =
            (static_cast<size_t>(bytes[pos]) << 8) | bytes[pos + 1];
        if (segmentLength < 2 || segmentLength > length - pos) return false;
        const bool isSof = marker == 0xC0 || marker == 0xC1 || marker == 0xC2 ||
            marker == 0xC3 || marker == 0xC5 || marker == 0xC6 || marker == 0xC7 ||
            marker == 0xC9 || marker == 0xCA || marker == 0xCB || marker == 0xCD ||
            marker == 0xCE || marker == 0xCF;
        if (isSof) {
            if (segmentLength < 7) return false;
            const size_t height =
                (static_cast<size_t>(bytes[pos + 3]) << 8) | bytes[pos + 4];
            const size_t width =
                (static_cast<size_t>(bytes[pos + 5]) << 8) | bytes[pos + 6];
            return width > 0 && height > 0 && width <= 1024 && height <= 1024 &&
                   width * height <= 1024u * 1024u;
        }
        pos += segmentLength;
    }
    return false;
}
bool SaveBytes(const std::string& path, const unsigned char* data, size_t length) {
    if (!data || length == 0) return false;
    EnsureCacheDirectories();
    const std::string temp = path + ".tmp";
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
    output.flush();
    const bool good = output.good();
    output.close();
    if (!good) { std::remove(temp.c_str()); return false; }
    const std::string backup = path + ".bak";
    const auto replaceStatus = desknx::safeio::replaceFilePreservingOnFailure(
        temp.c_str(), path.c_str(), backup.c_str());
    if (replaceStatus != desknx::safeio::ReplaceStatus::Success) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}
bool CacheJpegIcon(const std::string& path, const u8* data, size_t length, bool replace = false) {
    if (!replace && IsRegularFile(path)) return true;
    const size_t actual = jpegSize(data, length);
    return actual != 0 && SaveBytes(path, data, actual);
}

void CacheNroIconAndMetadata(const NroFile& nro, Candidate& candidate,
                               bool forceRefresh = false) {
    const std::string iconPath = desknx::model::iconCachePath(Kind::Nro, nro.path);
    const bool iconValid = IsUsableCachedJpeg(iconPath);
    if (!iconValid && IsRegularFile(iconPath)) std::remove(iconPath.c_str());
    const bool needIcon = forceRefresh || !iconValid;
    std::ifstream file(nro.path, std::ios::binary);
    if (!file) return;
    NroStart start{};
    NroHeader header{};
    file.read(reinterpret_cast<char*>(&start), sizeof(start));
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != NROHEADER_MAGIC || header.size < sizeof(NroStart) + sizeof(NroHeader))
        return;
    const u64 assetPos = header.size;
    if (assetPos + sizeof(NroAssetHeader) > nro.size) return;
    file.seekg(static_cast<std::streamoff>(assetPos), std::ios::beg);
    NroAssetHeader asset{};
    file.read(reinterpret_cast<char*>(&asset), sizeof(asset));
    if (!file || asset.magic != NROASSETHEADER_MAGIC || asset.version != NROASSETHEADER_VERSION)
        return;
    auto sectionValid = [&](const NroAssetSection& section) {
        return section.size > 0 && section.size <= MaxNroAssetBytes &&
               section.offset <= nro.size - assetPos &&
               section.size <= nro.size - assetPos - section.offset;
    };
    if (needIcon && sectionValid(asset.icon)) {
        std::vector<u8> image(static_cast<size_t>(asset.icon.size));
        file.clear();
        file.seekg(static_cast<std::streamoff>(assetPos + asset.icon.offset), std::ios::beg);
        file.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
        if (file) CacheJpegIcon(iconPath, image.data(), image.size(), forceRefresh);
    }
    if (sectionValid(asset.nacp) && asset.nacp.size >= sizeof(NacpStruct)) {
        NacpStruct nacp{};
        file.clear();
        file.seekg(static_cast<std::streamoff>(assetPos + asset.nacp.offset), std::ios::beg);
        file.read(reinterpret_cast<char*>(&nacp), sizeof(nacp));
        if (file) {
            const std::string name = GetApplicationName(nacp);
            if (!name.empty()) candidate.title = name;
        }
    }
}
bool HasCachedGameIcon(u64 id) {
    char hex[32]{};
    std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
    const std::string path = desknx::model::iconCachePath(Kind::Game, hex);
    if (path.empty()) return false;
    if (IsUsableCachedJpeg(path)) return true;
    if (IsRegularFile(path)) {
        std::remove(path.c_str());
        LogDiagnostic("icon_cache invalid_jpeg title_id=%s removed=1", hex);
    }
    return false;
}

void CacheGameIcon(u64 id, const NsApplicationControlData& data, u64 controlSize) {
    // nsGetApplicationControlData reports NACP + the actual JPEG byte count.
    // Do not write the full fixed-size icon buffer: it contains trailing zeros
    // and can be much larger than the image returned by NS.
    if (controlSize <= sizeof(NacpStruct)) return;
    const u64 iconBytes = controlSize - sizeof(NacpStruct);
    if (iconBytes > sizeof(data.icon) || iconBytes > MaxNroAssetBytes) return;
    char hex[32]{};
    std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
    const std::string path = desknx::model::iconCachePath(Kind::Game, hex);
    if (!path.empty())
        CacheJpegIcon(path, data.icon, static_cast<size_t>(iconBytes), true);
}

using GameNameCache = std::unordered_map<u64, std::string>;
constexpr const char* GameNameCacheFile = "sdmc:/switch/DeskNX/cache/game-names.tsv";
constexpr const char* GameNameCacheTemp = "sdmc:/switch/DeskNX/cache/game-names.tmp";

GameNameCache ReadGameNameCache() {
    GameNameCache names;
    struct stat st{};
    constexpr size_t MaxNameCacheBytes = 512 * 1024;
    if (stat(GameNameCacheFile, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || static_cast<uint64_t>(st.st_size) > MaxNameCacheBytes)
        return names;
    std::ifstream input(GameNameCacheFile, std::ios::binary);
    if (!input) return names;
    std::string line;
    size_t lines = 0;
    while (std::getline(input, line)) {
        if (line.size() > 640 || ++lines > 4096) return {};
        const auto fields = desknx::model::splitTabs(line);
        if (fields.size() != 2 || !isHexTitleId(fields[0]) ||
            fields[1].empty() || fields[1].size() > 512)
            continue;
        char* end = nullptr;
        const unsigned long long id = std::strtoull(fields[0].c_str(), &end, 16);
        if (!end || *end != '\0' || id == 0) continue;
        names[static_cast<u64>(id)] = fields[1];
    }
    if (input.bad()) return {};
    return names;
}

bool WriteGameNameCache(const GameNameCache& names) {
    EnsureCacheDirectories();
    std::ofstream output(GameNameCacheTemp, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    for (const auto& entry : names) {
        char id[32]{};
        std::snprintf(id, sizeof(id), "%016llX",
                      static_cast<unsigned long long>(entry.first));
        output << id << '\t' << desknx::model::safeField(entry.second) << '\n';
    }
    output.flush();
    const bool good = output.good();
    output.close();
    if (!good) {
        std::remove(GameNameCacheTemp);
        return false;
    }
    const auto status = desknx::safeio::replaceFilePreservingOnFailure(
        GameNameCacheTemp, GameNameCacheFile,
        "sdmc:/switch/DeskNX/cache/game-names.bak");
    if (status != desknx::safeio::ReplaceStatus::Success) {
        std::remove(GameNameCacheTemp);
        return false;
    }
    return true;
}
bool WriteCache(const Signatures& sig, const ScanResult& result) {
    EnsureCacheDirectories();
    std::ofstream output(CacheTemp, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    char gameHash[32]{}, sdHash[32]{};
    std::snprintf(gameHash, sizeof(gameHash), "%016llX", static_cast<unsigned long long>(sig.gameHash));
    std::snprintf(sdHash, sizeof(sdHash), "%016llX", static_cast<unsigned long long>(sig.sdHash));
    output << "DESKNXCAT2\t" << sig.gameCount << '\t' << gameHash << '\t' << sdHash << '\n';
    for (const auto& item : result.items) {
        output << "C\t" << desknx::model::typeCode(item.kind) << '\t'
               << desknx::model::safeField(item.title) << '\t'
               << desknx::model::safeField(item.data) << '\t'
               << item.w << '\t' << item.h << '\t' << item.color << '\t'
               << item.viewFlags << '\n';
    }
    output.flush();
    const bool good = output.good();
    output.close();
    if (!good) { std::remove(CacheTemp); return false; }
    const auto replaceStatus = desknx::safeio::replaceFilePreservingOnFailure(
        CacheTemp, CacheFile, "sdmc:/switch/DeskNX/cache/catalogue.bak");
    if (replaceStatus != desknx::safeio::ReplaceStatus::Success) {
        std::remove(CacheTemp);
        return false;
    }
    return true;
}
} // namespace

bool InitializeServices(std::uint32_t& nsErrorCode) {
    if (!gAccountServiceReady) {
        const Result rc = accountInitialize(AccountServiceType_System);
        gAccountServiceReady = R_SUCCEEDED(rc);
        gAccountServiceError = static_cast<std::uint32_t>(rc);
    }
    if (!gNsServiceReady) {
        const Result rc = nsInitialize();
        gNsServiceReady = R_SUCCEEDED(rc);
        gNsServiceError = static_cast<std::uint32_t>(rc);
    }
    LogDiagnostic("service_init account_ready=%d account_rc=0x%08X ns_ready=%d ns_rc=0x%08X",
                  gAccountServiceReady ? 1 : 0,
                  static_cast<unsigned int>(gAccountServiceError),
                  gNsServiceReady ? 1 : 0,
                  static_cast<unsigned int>(gNsServiceError));
    nsErrorCode = gNsServiceReady ? 0 : gNsServiceError;
    return gNsServiceReady;
}

void ShutdownServices() {
    LogDiagnostic("service_shutdown ns_ready=%d account_ready=%d",
                  gNsServiceReady ? 1 : 0, gAccountServiceReady ? 1 : 0);
    if (gNsServiceReady) { nsExit(); gNsServiceReady = false; }
    if (gAccountServiceReady) { accountExit(); gAccountServiceReady = false; }
}

ScanResult Scan(ProgressCallback progress, void* context, bool forceRefresh) {
    ScanResult result;
    auto report = [&](bool complete) {
        result.complete = complete;
        if (progress) progress(result, context);
    };
    EnsureCacheDirectories();
    const std::time_t scanStarted = std::time(nullptr);
    // NS/account are initialized once on the main thread after setting the
    // actual firmware version; this worker only uses the persistent sessions.
    const bool nsReady = gNsServiceReady;
    LogDiagnostic("scan_begin ns_ready=%d ns_error=0x%08X",
                  nsReady ? 1 : 0, static_cast<unsigned int>(gNsServiceError));
    result.nsErrorCode = nsReady ? 0 : gNsServiceError;
    result.nsAvailable = nsReady;

    // Warm-start from the last known-good catalogue before touching NS or
    // recursively enumerating the SD card. This makes saved Title IDs and NRO
    // paths immediately available while the live refresh continues in the
    // background. Stale NRO paths are filtered by ReadCache's allowStale mode.
    ScanResult warmCache;
    const bool warmCacheAvailable = ReadCache(Signatures{}, warmCache, true);
    if (warmCacheAvailable) {
        result.items = std::move(warmCache.items);
        result.fromCache = true;
        result.gameCount = static_cast<size_t>(std::count_if(
            result.items.begin(), result.items.end(),
            [](const Candidate& item) { return item.kind == Kind::Game; }));
        result.nroCount = static_cast<size_t>(std::count_if(
            result.items.begin(), result.items.end(),
            [](const Candidate& item) { return item.kind == Kind::Nro; }));
        LogDiagnostic("scan_warm_cache games=%zu nros=%zu items=%zu force_refresh=%d",
                      result.gameCount, result.nroCount, result.items.size(),
                      forceRefresh ? 1 : 0);
        report(false);
    }

    const std::vector<GameRecord> games = ReadGameRecords(nsReady, result.nsErrorCode);
    if (result.nsErrorCode != 0) result.nsAvailable = false;
    LogDiagnostic("games_collected count=%zu ns_available=%d ns_error=0x%08X",
                  games.size(), result.nsAvailable ? 1 : 0,
                  static_cast<unsigned int>(result.nsErrorCode));
    result.gameCount = games.size();

    // If NS is unavailable, the already-published last-known-good catalogue is
    // sufficient for this session. Do not make the user wait for another SD
    // traversal just to rediscover the same cached NRO paths.
    if ((!nsReady || result.nsErrorCode != 0) && warmCacheAvailable) {
        result.gameCount = static_cast<size_t>(std::count_if(
            result.items.begin(), result.items.end(),
            [](const Candidate& item) { return item.kind == Kind::Game; }));
        result.nroCount = static_cast<size_t>(std::count_if(
            result.items.begin(), result.items.end(),
            [](const Candidate& item) { return item.kind == Kind::Nro; }));
        result.complete = true;
        LogDiagnostic("scan_offline_warm_cache games=%zu nros=%zu ns_error=0x%08X",
                      result.gameCount, result.nroCount,
                      static_cast<unsigned int>(result.nsErrorCode));
        report(true);
        return result;
    }

    std::vector<NroFile> nros;
    CollectNros("sdmc:/switch", 0, nros);
    result.nroCount = nros.size();
    LogDiagnostic("nro_scan root=switch depth_limit=3 count=%zu", nros.size());
    std::sort(nros.begin(), nros.end(), [](const NroFile& a, const NroFile& b) {
        return a.path < b.path;
    });
    const Signatures sig = MakeSignatures(games, nros);
    // If NS is unavailable, keep the last known-good game list visible rather
    // than replacing it with an empty catalogue. The UI reports the NS error,
    // and stale entries remain launch-attemptable until the service recovers.
    if ((!nsReady || result.nsErrorCode != 0) && ReadCache(sig, result, true)) {
        result.gameCount = static_cast<size_t>(std::count_if(result.items.begin(), result.items.end(),
            [](const Candidate& item) { return item.kind == Kind::Game; }));
        result.nroCount = static_cast<size_t>(std::count_if(result.items.begin(), result.items.end(),
            [](const Candidate& item) { return item.kind == Kind::Nro; }));
        result.nsAvailable = false;
        result.complete = true;
        LogDiagnostic("scan_stale_cache games=%zu nros=%zu items=%zu ns_error=0x%08X",
                      result.gameCount, result.nroCount, result.items.size(),
                      static_cast<unsigned int>(result.nsErrorCode));
        report(true);
        return result;
    }

    // Never reuse a cache with mismatched live signatures during a normal scan.
    if (!forceRefresh && nsReady && result.nsErrorCode == 0 && ReadCache(sig, result)) {
        // Volatile play statistics are fetched only for the selected game.
        result.complete = true;
        LogDiagnostic("scan_cache_hit games=%zu nros=%zu items=%zu elapsed=%llds",
                      result.gameCount, result.nroCount, result.items.size(),
                      static_cast<long long>(std::time(nullptr) - scanStarted));
        report(true);
        return result;
    }

    result.items.clear();
    result.fromCache = false;
    result.truncated = false;
    result.items.reserve(MaxItems);
    const size_t gameLimit = MaxItems - GameReserve;
    static NsApplicationControlData control{};
    GameNameCache nameCache = ReadGameNameCache();
    GameNameCache freshNameCache;
    freshNameCache.reserve(games.size());
    bool fetchedControlData = false;
    size_t controlDataOk = 0, controlDataFailed = 0;
    std::uint32_t firstControlDataError = 0;
    size_t colorIndex = 0;
    for (size_t i = 0; i < games.size(); ++i) {
        if (result.items.size() >= gameLimit) { result.truncated = true; break; }
        const u64 id = games[i].id;
        char hex[32]{};
        std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));

        const auto cachedName = nameCache.find(id);
        const bool iconCached = HasCachedGameIcon(id);
        std::string title;
        if (!forceRefresh && cachedName != nameCache.end()) {
            // The title cache is independent of the icon cache. An app may
            // legitimately have no icon; do not turn that into a repeated
            // synchronous NS request for already-resolved metadata.
            title = cachedName->second;
        } else {
            u64 actual = 0;
            Result controlRc = 0;
            if (nsReady)
                controlRc = desknx::slaunch::ReadControlData(id, control, actual);
            if (nsReady && R_SUCCEEDED(controlRc)) {
                ++controlDataOk;
                title = desknx::slaunch::ResolveTitleName(control.nacp);
                if (title.empty() && cachedName != nameCache.end())
                    title = cachedName->second;
                if (forceRefresh || !iconCached) CacheGameIcon(id, control, actual);
                fetchedControlData = true;
            } else {
                ++controlDataFailed;
                if (firstControlDataError == 0)
                    firstControlDataError = static_cast<std::uint32_t>(controlRc);
                if (cachedName != nameCache.end()) {
                    title = cachedName->second;
                }
            }
        }

        if (title.empty()) {
            std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
            title = std::string("Game ") + hex;
        }
        freshNameCache.emplace(id, title);
        Candidate game{Kind::Game, title, hex, 1, 1,
                       static_cast<int>(colorIndex++ % 8), games[i].viewFlags};
        result.items.push_back(std::move(game));
        if ((i + 1) % 12 == 0) report(false);
    }
    // Keep the per-title cache in sync with the installed set. If the only
    // change was an install/uninstall, existing names/icons are reused.
    LogDiagnostic("control_data successful=%zu failed=%zu first_error=0x%08X cached_names=%zu",
                  controlDataOk, controlDataFailed,
                  static_cast<unsigned int>(firstControlDataError), nameCache.size());
    if (nsReady && result.nsErrorCode == 0 &&
        (fetchedControlData || freshNameCache.size() != nameCache.size())) {
        const bool namesSaved = WriteGameNameCache(freshNameCache);
        LogDiagnostic("name_cache_write ok=%d entries=%zu", namesSaved ? 1 : 0,
                      freshNameCache.size());
    }
    report(false); // expose the installed-game part before scanning NRO assets

    for (const auto& nro : nros) {
        if (result.items.size() >= MaxItems - WidgetReserve) { result.truncated = true; break; }
        Candidate item{Kind::Nro, fileTitle(nro.path), nro.path, 1, 1,
                       static_cast<int>(result.items.size() % 8)};
        CacheNroIconAndMetadata(nro, item, forceRefresh);
        result.items.push_back(std::move(item));
        if (result.items.size() % 12 == 0) report(false);
    }
    if (games.size() > gameLimit || nros.size() >= MaxItems) result.truncated = true;
    result.items.push_back({Kind::Clock, "Clock widget", "", 2, 1, 1});
    result.items.push_back({Kind::Battery, "Battery widget", "", 2, 1, 2});
    result.items.push_back({Kind::System, "System info widget", "", 2, 1, 5});
    result.items.push_back({Kind::Recent, "Recent launch widget", "", 3, 1, 6});
    result.items.push_back({Kind::Storage, "SD storage widget", "", 3, 1, 3});
    result.items.push_back({Kind::Calendar, "Calendar widget", "", 3, 1, 4});
    // Preserve the last known-good catalogue if NS enumeration failed.
    if (nsReady && result.nsErrorCode == 0) WriteCache(sig, result);
    LogDiagnostic("scan_done games=%zu nros=%zu items=%zu truncated=%d ns_error=0x%08X elapsed=%llds",
                  result.gameCount, result.nroCount, result.items.size(),
                  result.truncated ? 1 : 0,
                  static_cast<unsigned int>(result.nsErrorCode),
                  static_cast<long long>(std::time(nullptr) - scanStarted));
    report(true);
    return result;
}

} // namespace desknx::catalog
