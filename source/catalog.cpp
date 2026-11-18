#include "catalog.hpp"
#include "../include/desknx/Diagnostics.hpp"
#include "slaunch_applications.hpp"
#include "../include/desknx/StrictParse.hpp"
#include "../include/desknx/CatalogPagination.hpp"
#include "../include/desknx/NroScanner.hpp"
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
constexpr size_t WidgetReserve = 7;
constexpr const char* CacheDir = "sdmc:/switch/DeskNX/cache";
constexpr const char* IconsDir = "sdmc:/switch/DeskNX/cache/icons";
constexpr const char* CacheFile = "sdmc:/switch/DeskNX/cache/catalogue.tsv";
constexpr const char* CacheTemp = "sdmc:/switch/DeskNX/cache/catalogue.tmp";
constexpr size_t MaxNroAssetBytes = 2 * 1024 * 1024;

struct GameRecord { u64 id; u64 last_updated; u32 viewFlags; bool updateStatusAvailable; };
using NroFile = desknx::nro_scan::File;
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

std::string GetApplicationDisplayVersion(const NacpStruct& nacp) {
    const char* begin=nacp.display_version;
    const char* end=std::find(begin,begin+sizeof(nacp.display_version),
                              static_cast<char>(0));
    std::string version(begin,end);
    if(version.size()>15)return {};
    for(char& ch:version)
        if(ch=='\t'||ch=='\n'||ch=='\r')ch=' ';
    return version;
}

// NRO traversal is shared with the host regression suite in NroScanner.hpp.
// It uses directory-entry types when available and stats only NROs or entries
// whose type is unknown.
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
                           installed[i].last_updated, flags, i < views.size()});
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
        sig.gameHash = hashBytes(sig.gameHash, &game.updateStatusAvailable,
                                 sizeof(game.updateStatusAvailable));
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

bool isCachedNroPath(const std::string& path, bool retainUnverifiable = false) {
    constexpr char prefix[] = "sdmc:/switch/";
    if (path.compare(0, sizeof(prefix) - 1, prefix) != 0 ||
        path.find("..") != std::string::npos || path.size() <= sizeof(prefix) + 3)
        return false;
    std::string extension = path.substr(path.size() - 4);
    for (char& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (extension != ".nro") return false;

    struct stat st{};
    errno = 0;
    if (stat(path.c_str(), &st) == 0) return S_ISREG(st.st_mode);
    // ENOENT/ENOTDIR means the shortcut is genuinely stale. For EACCES,
    // EIO, or a transient filesystem fault, keep the old shortcut while the
    // full scanner marks the inventory incomplete; do not confuse "unknown"
    // with "deleted" and erase the user's homebrew entry from the warm cache.
    return retainUnverifiable && errno != ENOENT && errno != ENOTDIR;
}

bool IsUsableCachedImage(const std::string& path);

bool IsMissingOrUsableCachedImage(const std::string& path) {
    // Some legitimate NACP/NRO files contain no icon. A missing icon is not a
    // reason to discard the entire application catalogue; an existing broken
    // image is, because the next full scan can rebuild it.
    if (!IsRegularFile(path)) return true;
    return IsUsableCachedImage(path);
}

bool EnsureBuiltinWidgets(std::vector<Candidate>& items) {
    struct WidgetSpec { Kind kind; const char* title; int width; int height; int color; };
    static constexpr WidgetSpec widgets[] = {
        {Kind::Clock, "Clock widget", 2, 1, 1},
        {Kind::Battery, "Battery widget", 2, 1, 2},
        {Kind::System, "System info widget", 2, 1, 5},
        {Kind::Recent, "Recent launch widget", 3, 1, 6},
        {Kind::Storage, "SD storage widget", 3, 1, 3},
        {Kind::Calendar, "Calendar widget", 3, 1, 4},
        {Kind::Nextendo, "Nextendo popular games", 3, 2, 9},
    };
    bool changed = false;
    for (const auto& spec : widgets) {
        const bool exists = std::any_of(items.begin(), items.end(),
            [&](const Candidate& item) { return item.kind == spec.kind; });
        if (!exists) {
            items.push_back({spec.kind, spec.title, "", spec.width,
                             spec.height, spec.color});
            changed = true;
        }
    }
    return changed;
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
    if (header.size() != 4 || header[0] != "DESKNXCAT4") return false;
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
        if (fields.size() != 10 || fields[0] != "C" ||
            fields[1].size() > 16 || fields[2].size() > 512 ||
            fields[3].size() > 512 || fields[8].size() > 15 ||
            (fields[9] != "0" && fields[9] != "1"))
            return false;

        Candidate item{};
        item.kind = desknx::model::parseKind(fields[1]);
        if (item.kind == Kind::Unsupported) return false;
        item.title = fields[2];
        item.data = fields[3];
        item.displayVersion = fields[8];
        item.updateStatusAvailable = fields[9] == "1";
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
                if (!IsMissingOrUsableCachedImage(iconPath)) return false;
            }
        } else if (item.kind == Kind::Nro) {
            if (!isCachedNroPath(item.data, allowStale)) {
                if (allowStale) continue; // confirmed missing/invalid shortcut
                return false;
            }
            if (!allowStale) {
                const std::string iconPath=desknx::model::iconCachePath(item.kind,item.data);
                if (!IsMissingOrUsableCachedImage(iconPath)) return false;
            }
        } else if (!item.data.empty()) {
            return false;
        }
        items.push_back(std::move(item));
    }
    if (input.bad()) return false;
    (void)EnsureBuiltinWidgets(items);
    if (items.size() < WidgetReserve) return false;
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

bool pngDimensions(const u8* data, size_t length, size_t& width, size_t& height) {
    static constexpr u8 signature[] = {137,80,78,71,13,10,26,10};
    if (!data || length < 24 ||
        std::memcmp(data, signature, sizeof(signature)) != 0 ||
        std::memcmp(data + 12, "IHDR", 4) != 0)
        return false;
    width = (static_cast<size_t>(data[16]) << 24) |
            (static_cast<size_t>(data[17]) << 16) |
            (static_cast<size_t>(data[18]) << 8) | data[19];
    height = (static_cast<size_t>(data[20]) << 24) |
             (static_cast<size_t>(data[21]) << 16) |
             (static_cast<size_t>(data[22]) << 8) | data[23];
    return width > 0 && height > 0 && width <= 1024 && height <= 1024 &&
           width * height <= 1024u * 1024u;
}

size_t imageSize(const u8* data, size_t length) {
    if (const size_t jpeg = jpegSize(data, length)) return jpeg;
    size_t width = 0, height = 0;
    return pngDimensions(data, length, width, height) ? length : 0;
}

bool IsUsableCachedImage(const std::string& path) {
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
    size_t pngWidth = 0, pngHeight = 0;
    if (pngDimensions(bytes.data(), length, pngWidth, pngHeight)) return true;
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
bool CacheIconImage(const std::string& path, const u8* data, size_t length, bool replace = false) {
    if (!replace && IsRegularFile(path)) return true;
    const size_t actual = imageSize(data, length);
    return actual != 0 && SaveBytes(path, data, actual);
}

void CacheNroIconAndMetadata(const NroFile& nro, Candidate& candidate,
                               bool forceRefresh = false) {
    const std::string iconPath = desknx::model::iconCachePath(Kind::Nro, nro.path);
    const bool iconValid = IsUsableCachedImage(iconPath);
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
        if (file) CacheIconImage(iconPath, image.data(), image.size(), forceRefresh);
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
    if (IsUsableCachedImage(path)) return true;
    if (IsRegularFile(path)) {
        std::remove(path.c_str());
        LogDiagnostic("icon_cache invalid_image title_id=%s removed=1", hex);
    }
    return false;
}

bool CacheGameIcon(u64 id, const NsApplicationControlData& data, u64 controlSize) {
    // NS returns NACP followed by a JPEG or PNG icon. Keep the actual payload
    // length; the fixed control-data struct has unused bytes after the image.
    if (controlSize <= sizeof(NacpStruct)) return false;
    const u64 iconBytes = controlSize - sizeof(NacpStruct);
    if (iconBytes > sizeof(data.icon) || iconBytes > MaxNroAssetBytes) return false;
    char hex[32]{};
    std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
    const std::string path = desknx::model::iconCachePath(Kind::Game, hex);
    return !path.empty() &&
        CacheIconImage(path, data.icon, static_cast<size_t>(iconBytes), true);
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
    output << "DESKNXCAT4\t" << sig.gameCount << '\t' << gameHash << '\t' << sdHash << '\n';
    for (const auto& item : result.items) {
        output << "C\t" << desknx::model::typeCode(item.kind) << '\t'
               << desknx::model::safeField(item.title) << '\t'
               << desknx::model::safeField(item.data) << '\t'
               << item.w << '\t' << item.h << '\t' << item.color << '\t'
               << item.viewFlags << '\t'
               << desknx::model::safeField(item.displayVersion) << '\t'
               << (item.updateStatusAvailable ? 1 : 0) << '\n';
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

bool RefreshNsService(std::uint32_t& nsErrorCode) {
    if (gNsServiceReady) {
        nsExit();
        gNsServiceReady = false;
    }
    const Result rc = nsInitialize();
    gNsServiceReady = R_SUCCEEDED(rc);
    gNsServiceError = static_cast<std::uint32_t>(rc);
    nsErrorCode = gNsServiceReady ? 0 : gNsServiceError;
    LogDiagnostic("ns_service_refresh ready=%d rc=0x%08X",
                  gNsServiceReady ? 1 : 0,
                  static_cast<unsigned int>(gNsServiceError));
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
    std::vector<Candidate> warmNroItems;
    if (warmCacheAvailable) {
        result.items = std::move(warmCache.items);
        for (const auto& item : result.items)
            if (item.kind == Kind::Nro) warmNroItems.push_back(item);
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

    // The NRO scanner runs after the installed-game seed is published so a
    // slow SD card walk never delays the first usable library snapshot.
    std::vector<NroFile> nros;
    desknx::nro_scan::Report nroScan{};

    // Keep the last-known-good game list when NS is offline, but merge newly
    // discovered NROs so homebrew discovery does not depend on NS availability.
    if ((!nsReady || result.nsErrorCode != 0) && warmCacheAvailable) {
        nroScan = desknx::nro_scan::Collect("sdmc:/switch", nros);
        std::sort(nros.begin(), nros.end(), [](const NroFile& a, const NroFile& b) {
            return a.path < b.path;
        });
        LogDiagnostic("nro_scan root=switch complete=%d count=%zu dirs=%zu entries=%zu inaccessible=%zu depth_skips=%zu limit=%d first_error=%d",
            nroScan.complete ? 1 : 0, nros.size(), nroScan.directoriesVisited,
            nroScan.entriesVisited, nroScan.inaccessibleDirectories,
            nroScan.directoriesBeyondDepth, nroScan.limitReached ? 1 : 0,
            nroScan.firstError);
        std::unordered_map<std::string, bool> knownNros;
        for (const auto& item : result.items)
            if (item.kind == Kind::Nro) knownNros.emplace(item.data, true);
        for (const auto& nro : nros) {
            if (knownNros.find(nro.path) != knownNros.end()) continue;
            Candidate item{Kind::Nro, fileTitle(nro.path), nro.path, 1, 1,
                           static_cast<int>(result.items.size() % 12)};
            CacheNroIconAndMetadata(nro, item, forceRefresh);
            result.items.push_back(std::move(item));
            knownNros.emplace(nro.path, true);
        }
        result.gameCount = static_cast<size_t>(std::count_if(
            result.items.begin(), result.items.end(),
            [](const Candidate& item) { return item.kind == Kind::Game; }));
        result.nroCount = static_cast<size_t>(std::count_if(
            result.items.begin(), result.items.end(),
            [](const Candidate& item) { return item.kind == Kind::Nro; }));
        result.truncated = nroScan.limitReached || !nroScan.complete;
        LogDiagnostic("scan_offline_warm_cache games=%zu nros=%zu scan_complete=%d ns_error=0x%08X",
                      result.gameCount, result.nroCount, nroScan.complete ? 1 : 0,
                      static_cast<unsigned int>(result.nsErrorCode));
        result.complete = true;
        report(true);
        return result;
    }

    // Publish the installed IDs immediately. Metadata/icon extraction below can
    // perform up to 90 Horizon IPC calls; the library and widget picker should
    // not remain empty while those calls (or the SD NRO walk) are in progress.
    GameNameCache nameCache = ReadGameNameCache();
    auto usableCachedName = [](const std::string& name) {
        return !name.empty() && !desknx::model::isTitleId(name) &&
            !(name.compare(0, 5, "Game ") == 0 &&
              desknx::model::isTitleId(name.substr(5)));
    };

    // Drop uninstalled game IDs from the warm snapshot, but preserve valid
    // cached NRO entries while the live SD scan runs.
    result.items.erase(std::remove_if(result.items.begin(), result.items.end(),
        [&](const Candidate& item) {
            if (item.kind != Kind::Game) return false;
            return std::none_of(games.begin(), games.end(),
                [&](const GameRecord& game) {
                    char id[17]{};
                    std::snprintf(id, sizeof(id), "%016llX",
                        static_cast<unsigned long long>(game.id));
                    return item.data == id;
                });
        }), result.items.end());

    const size_t gameLimit = MaxItems - GameReserve;
    size_t earlyGameCount = 0;
    for (const auto& game : games) {
        if (earlyGameCount >= gameLimit) {
            result.truncated = true;
            break;
        }
        char id[17]{};
        std::snprintf(id, sizeof(id), "%016llX",
                      static_cast<unsigned long long>(game.id));
        const auto cached = nameCache.find(game.id);
        const std::string title = cached != nameCache.end() &&
            usableCachedName(cached->second)
            ? cached->second : std::string("Game ") + id;
        auto existing = std::find_if(result.items.begin(), result.items.end(),
            [&](const Candidate& item) {
                return item.kind == Kind::Game && item.data == id;
            });
        if (existing == result.items.end()) {
            Candidate seed{Kind::Game, title, id, 1, 1,
                static_cast<int>(earlyGameCount % 12), game.viewFlags};
            seed.updateStatusAvailable=game.updateStatusAvailable;
            result.items.push_back(std::move(seed));
        } else {
            existing->viewFlags = game.viewFlags;
            existing->updateStatusAvailable = game.updateStatusAvailable;
            if (!usableCachedName(existing->title) && usableCachedName(title))
                existing->title = title;
        }
        ++earlyGameCount;
    }
    (void)EnsureBuiltinWidgets(result.items);
    result.gameCount = games.size();
    result.nroCount = static_cast<size_t>(std::count_if(
        result.items.begin(), result.items.end(),
        [](const Candidate& item) { return item.kind == Kind::Nro; }));
    LogDiagnostic("catalogue_seed published_games=%zu widgets=%zu cached_nros=%zu",
        earlyGameCount,
        static_cast<size_t>(std::count_if(result.items.begin(), result.items.end(),
            [](const Candidate& item) {
                return item.kind == Kind::Clock || item.kind == Kind::Battery ||
                    item.kind == Kind::System || item.kind == Kind::Recent ||
                    item.kind == Kind::Storage || item.kind == Kind::Calendar ||
                    item.kind == Kind::Nextendo;
            })),
        result.nroCount);
    report(false);

    // The initial IDs and widgets are now visible to the UI. Perform the
    // directory walk only after that seed has been published, then use the
    // complete inventory for cache validation and metadata resolution.
    nroScan = desknx::nro_scan::Collect("sdmc:/switch", nros);
    std::sort(nros.begin(), nros.end(), [](const NroFile& a, const NroFile& b) {
        return a.path < b.path;
    });
    LogDiagnostic("nro_scan root=switch complete=%d count=%zu dirs=%zu entries=%zu inaccessible=%zu depth_skips=%zu limit=%d first_error=%d",
        nroScan.complete ? 1 : 0, nros.size(), nroScan.directoriesVisited,
        nroScan.entriesVisited, nroScan.inaccessibleDirectories,
        nroScan.directoriesBeyondDepth, nroScan.limitReached ? 1 : 0,
        nroScan.firstError);

    result.nroCount = nros.size();
    const Signatures sig = MakeSignatures(games, nros);

    // A partial directory walk is not an authoritative inventory. In that
    // case skip both cache hits and cache publication, and merge valid NROs
    // from the previous snapshot below.
    if (nroScan.complete && !forceRefresh && nsReady &&
        result.nsErrorCode == 0 && ReadCache(sig, result)) {
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
    // Keep a stable game-index prefix so each metadata result updates its
    // already-published row rather than hiding all games until the scan ends.
    for (size_t i = 0; i < games.size() && i < gameLimit; ++i) {
        char id[17]{};
        std::snprintf(id, sizeof(id), "%016llX",
                      static_cast<unsigned long long>(games[i].id));
        const auto cached = nameCache.find(games[i].id);
        const std::string title = cached != nameCache.end() &&
            usableCachedName(cached->second)
            ? cached->second : std::string("Game ") + id;
        Candidate seed{Kind::Game, title, id, 1, 1,
            static_cast<int>(i % 12), games[i].viewFlags};
        seed.updateStatusAvailable=games[i].updateStatusAvailable;
        result.items.push_back(std::move(seed));
    }
    (void)EnsureBuiltinWidgets(result.items);
    report(false);
    static NsApplicationControlData control{};
    GameNameCache freshNameCache;
    freshNameCache.reserve(games.size());
    bool fetchedControlData = false;
    size_t controlDataOk = 0, controlDataFailed = 0;
    size_t resolvedGameNames = 0, unresolvedGameNames = 0;
    size_t cachedGameIcons = 0, failedGameIcons = 0;
    char firstUnresolvedTitleId[17]{};
    std::uint32_t firstControlDataError = 0;
    size_t colorIndex = 0;
    for (size_t i = 0; i < games.size() && i < gameLimit; ++i) {
        if (i >= gameLimit) { result.truncated = true; break; }
        const u64 id = games[i].id;
        char hex[32]{};
        std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));

        const auto cachedName = nameCache.find(id);
        const bool iconCached = HasCachedGameIcon(id);
        const bool cachedNameUsable = cachedName != nameCache.end() &&
            !desknx::model::isTitleId(cachedName->second) &&
            !(cachedName->second.compare(0, 5, "Game ") == 0 &&
              desknx::model::isTitleId(cachedName->second.substr(5)));
        std::string title;
        std::string displayVersion;
        Result metadataRc=0;
        bool metadataRead=false;
        bool iconAvailable=iconCached;
        const char* nameSource=(cachedNameUsable&&iconCached&&!forceRefresh)
            ?"cache":"nacp";
        // A stable catalogue returns from ReadCache above. On a cache miss,
        // refresh installed control.nacp so the visible version is current.
        u64 actual = 0;
        if (nsReady)
            metadataRc = desknx::slaunch::ReadControlData(id, control, actual);
        metadataRead=true;
        if (nsReady && R_SUCCEEDED(metadataRc)) {
            ++controlDataOk;
            const std::string resolved = desknx::slaunch::ResolveTitleName(control.nacp);
            if (!resolved.empty() && !desknx::model::isTitleId(resolved))
                title = resolved;
            else if (cachedNameUsable) {
                title = cachedName->second;
                nameSource="cache_fallback";
            }
            displayVersion=GetApplicationDisplayVersion(control.nacp);
            if (forceRefresh || !iconCached) {
                if (CacheGameIcon(id, control, actual)) {
                    ++cachedGameIcons;
                    iconAvailable=true;
                } else {
                    ++failedGameIcons;
                    iconAvailable=false;
                }
            }
            fetchedControlData = true;
        } else {
            ++controlDataFailed;
            if (firstControlDataError == 0)
                firstControlDataError = static_cast<std::uint32_t>(metadataRc);
            if (cachedNameUsable) {
                title = cachedName->second;
                nameSource="cache_fallback";
            }
        }

        if (title.empty()) {
            std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
            title = std::string("Game ") + hex;
        }
        const bool placeholderTitle = desknx::model::isTitleId(title) ||
            (title.compare(0, 5, "Game ") == 0 &&
             desknx::model::isTitleId(title.substr(5)));
        LogDiagnostic("game_metadata title_id=%s name_source=%s resolved=%d icon_available=%d cached_name=%d cached_icon=%d metadata_read=%d rc=0x%08X title=%s",
            hex,nameSource,placeholderTitle?0:1,iconAvailable?1:0,
            cachedNameUsable?1:0,iconCached?1:0,metadataRead?1:0,
            static_cast<unsigned int>(metadataRc),title.c_str());
        if (!placeholderTitle) {
            freshNameCache.emplace(id, title);
            ++resolvedGameNames;
        } else {
            ++unresolvedGameNames;
            if (firstUnresolvedTitleId[0] == '\0')
                std::snprintf(firstUnresolvedTitleId, sizeof(firstUnresolvedTitleId), "%016llX",
                    static_cast<unsigned long long>(id));
        }
        if (i < result.items.size() &&
            result.items[i].kind == Kind::Game && result.items[i].data == hex) {
            result.items[i].title = title;
            result.items[i].displayVersion = displayVersion;
            result.items[i].viewFlags = games[i].viewFlags;
            result.items[i].updateStatusAvailable=games[i].updateStatusAvailable;
            result.items[i].color = static_cast<int>(colorIndex++ % 12);
        } else {
            // Defensive fallback for a malformed staging snapshot.
            Candidate fallback{Kind::Game, title, hex, 1, 1,
                static_cast<int>(colorIndex++ % 12), games[i].viewFlags};
            fallback.displayVersion=displayVersion;
            fallback.updateStatusAvailable=games[i].updateStatusAvailable;
            result.items.insert(result.items.begin() + static_cast<std::ptrdiff_t>(
                std::min(i, result.items.size())), std::move(fallback));
        }
        if ((i + 1) % 3 == 0 || i + 1 == games.size()) report(false);
    }
    // Keep the per-title cache in sync with the installed set. If the only
    // change was an install/uninstall, existing names/icons are reused.
    LogDiagnostic("control_data successful=%zu failed=%zu first_error=0x%08X cached_names=%zu",
                  controlDataOk, controlDataFailed,
                  static_cast<unsigned int>(firstControlDataError), nameCache.size());
    LogDiagnostic("game_artwork resolved_names=%zu unresolved_names=%zu icons_written=%zu icon_failures=%zu first_unresolved=%s",
                  resolvedGameNames, unresolvedGameNames, cachedGameIcons,
                  failedGameIcons, firstUnresolvedTitleId[0] ? firstUnresolvedTitleId : "none");
    if (nsReady && result.nsErrorCode == 0 &&
        (fetchedControlData || freshNameCache.size() != nameCache.size())) {
        const bool namesSaved = WriteGameNameCache(freshNameCache);
        LogDiagnostic("name_cache_write ok=%d entries=%zu", namesSaved ? 1 : 0,
                      freshNameCache.size());
    }
    report(false); // expose the installed-game part before scanning NRO assets

    for (const auto& nro : nros) {
        if (result.items.size() >= MaxItems - WidgetReserve) {
            result.truncated = true;
            break;
        }
        Candidate item{Kind::Nro, fileTitle(nro.path), nro.path, 1, 1,
                       static_cast<int>(result.items.size() % 12)};
        CacheNroIconAndMetadata(nro, item, forceRefresh);
        result.items.push_back(std::move(item));
        if (result.items.size() % 12 == 0) report(false);
    }

    // Keep still-valid cached homebrew entries when one folder could not be
    // enumerated. ReadCache has already discarded paths that no longer exist.
    if (!nroScan.complete) {
        for (const auto& cached : warmNroItems) {
            const bool alreadyPresent = std::any_of(result.items.begin(), result.items.end(),
                [&](const Candidate& item) {
                    return item.kind == Kind::Nro && item.data == cached.data;
                });
            if (!alreadyPresent && result.items.size() < MaxItems - WidgetReserve)
                result.items.push_back(cached);
        }
    }

    result.nroCount = static_cast<size_t>(std::count_if(
        result.items.begin(), result.items.end(),
        [](const Candidate& item) { return item.kind == Kind::Nro; }));
    if (games.size() > gameLimit || nroScan.limitReached || !nroScan.complete)
        result.truncated = true;
    (void)EnsureBuiltinWidgets(result.items);

    // Never publish a partial SD walk as the new last-known-good catalogue.
    if (nsReady && result.nsErrorCode == 0 && nroScan.complete)
        WriteCache(sig, result);
    LogDiagnostic("scan_done games=%zu nros=%zu items=%zu truncated=%d ns_error=0x%08X elapsed=%llds",
                  result.gameCount, result.nroCount, result.items.size(),
                  result.truncated ? 1 : 0,
                  static_cast<unsigned int>(result.nsErrorCode),
                  static_cast<long long>(std::time(nullptr) - scanStarted));
    report(true);
    return result;
}

} // namespace desknx::catalog
