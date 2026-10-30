#include "catalog.hpp"
#include "slaunch_applications.hpp"
#include "../include/desknx/StrictParse.hpp"
#include "../include/desknx/CatalogPagination.hpp"
#include <desknx/SafeReplace.hpp>

#include <switch.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
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

struct GameRecord { u64 id; u64 last_updated; u32 viewFlags; u64 playtimeSeconds; u64 lastPlayed; u32 launchCount; };
struct NroFile { std::string path; u64 size; u64 mtime; };
struct Signatures {
    size_t gameCount{0};
    uint64_t gameHash{14695981039346656037ULL};
    uint64_t sdHash{14695981039346656037ULL};
};

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
    const NacpLanguageEntry* entry = nullptr;
    if (nacp.lang[0].name[0] != '\0') entry = &nacp.lang[0];
    for (int i = 1; !entry && i < 16; ++i) {
        if (nacp.lang[i].name[0] != '\0') entry = &nacp.lang[i];
    }
    if (!entry) return {};
    char name[129]{};
    std::strncpy(name, entry->name, sizeof(name) - 1);
    return std::string(name);
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
    if (!nsReady) return records;

    // SLaunch sequence: enumerate every installed record, then query the
    // matching live views. View flags are presentation metadata, never filters.
    std::vector<NsApplicationRecord> installed;
    const Result listRc = desknx::slaunch::EnumerateInstalled(installed);
    if (R_FAILED(listRc)) {
        errorCode = static_cast<std::uint32_t>(listRc);
        return records;
    }

    std::vector<u64> titleIds;
    titleIds.reserve(installed.size());
    for (const auto& record : installed) titleIds.push_back(record.application_id);

    std::vector<NsApplicationView> views;
    const Result viewsRc = desknx::slaunch::QueryApplicationViews(titleIds, views);
    if (R_FAILED(viewsRc)) views.clear();

    records.reserve(installed.size());
    for (size_t i = 0; i < installed.size(); ++i) {
        u32 flags = 0;
        if (i < views.size()) {
            const auto* bytes = reinterpret_cast<const u8*>(&views[i]);
            std::memcpy(&flags, bytes + 0x0C, sizeof(flags));
        }
        records.push_back({installed[i].application_id,
                           installed[i].last_updated,
                           flags, 0, 0, 0});
    }

    // SLaunch parity: query play statistics once per catalogue refresh, not
    // from the render loop. A missing PDM record is a normal never-played game.
    if (R_SUCCEEDED(pdmqryInitialize())) {
        for (auto& record : records) {
            PdmPlayStatistics stats{};
            if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationId(
                    record.id, false, &stats))) {
                record.playtimeSeconds = stats.playtime / 1000000000ULL;
                record.lastPlayed = stats.last_timestamp_user;
                record.launchCount = stats.total_launches;
            }
        }
        pdmqryExit();
    }
    return records;
}
Signatures MakeSignatures(const std::vector<GameRecord>& games, const std::vector<NroFile>& nros) {
    Signatures sig{};
    sig.gameCount = games.size();
    for (const auto& game : games) {
        sig.gameHash = hashBytes(sig.gameHash, &game.id, sizeof(game.id));
        sig.gameHash = hashBytes(sig.gameHash, &game.last_updated, sizeof(game.last_updated));
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

bool ReadCache(const Signatures& sig, ScanResult& result) {
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
    char gameHash[32]{}, sdHash[32]{};
    std::snprintf(gameHash, sizeof(gameHash), "%016llX", static_cast<unsigned long long>(sig.gameHash));
    std::snprintf(sdHash, sizeof(sdHash), "%016llX", static_cast<unsigned long long>(sig.sdHash));
    if (header[1] != std::to_string(sig.gameCount) || header[2] != gameHash || header[3] != sdHash)
        return false;

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
            if (!isHexTitleId(item.data) ||
                !IsRegularFile(desknx::model::iconCachePath(item.kind, item.data)))
                return false;
        } else if (item.kind == Kind::Nro) {
            if (!isCachedNroPath(item.data)) return false;
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

void CacheNroIconAndMetadata(const NroFile& nro, Candidate& candidate) {
    const std::string iconPath = desknx::model::iconCachePath(Kind::Nro, nro.path);
    const bool needIcon = !IsRegularFile(iconPath);
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
        if (file) CacheJpegIcon(iconPath, image.data(), image.size());
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
    struct stat st{};
    const std::string path = desknx::model::iconCachePath(Kind::Game, hex);
    return !path.empty() && stat(path.c_str(), &st) == 0 &&
           S_ISREG(st.st_mode) && st.st_size > 4;
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

ScanResult Scan(ProgressCallback progress, void* context) {
    ScanResult result;
    auto report = [&](bool complete) {
        result.complete = complete;
        if (progress) progress(result, context);
    };
    EnsureCacheDirectories();
    // Match SLaunch's library-applet path: libnx initializes NS as part of
    // applet startup. Do not open/close a second NS session inside the scanner;
    // use the real enumeration result as the availability check.
    const bool nsReady = true;
    result.nsAvailable = true;
    result.nsErrorCode = 0;
    const std::vector<GameRecord> games = ReadGameRecords(nsReady, result.nsErrorCode);
    if (result.nsErrorCode != 0) result.nsAvailable = false;
    result.gameCount = games.size();
    std::vector<NroFile> nros;
    CollectNros("sdmc:/switch", 0, nros);
    result.nroCount = nros.size();
    std::sort(nros.begin(), nros.end(), [](const NroFile& a, const NroFile& b) {
        return a.path < b.path;
    });
    const Signatures sig = MakeSignatures(games, nros);
    // Never reuse a cache after NS failed or returned a partial application list.
    if (nsReady && result.nsErrorCode == 0 && ReadCache(sig, result)) {
        // The catalogue cache intentionally excludes volatile play statistics.
        // Reattach them from the single PDM query above on every refresh.
        for (auto& item : result.items) {
            if (item.kind != Kind::Game) continue;
            char* end = nullptr;
            const unsigned long long id = std::strtoull(item.data.c_str(), &end, 16);
            if (!end || *end != '\\0') continue;
            const auto found = std::find_if(games.begin(), games.end(),
                [id](const GameRecord& game) { return game.id == static_cast<u64>(id); });
            if (found == games.end()) continue;
            item.playtimeSeconds = found->playtimeSeconds;
            item.lastPlayed = found->lastPlayed;
            item.launchCount = found->launchCount;
        }
        result.complete = true;
        report(true);
        return result;
    }

    result.items.reserve(MaxItems);
    const size_t gameLimit = MaxItems - GameReserve;
    static NsApplicationControlData control{};
    GameNameCache nameCache = ReadGameNameCache();
    GameNameCache freshNameCache;
    freshNameCache.reserve(games.size());
    bool fetchedControlData = false;
    size_t colorIndex = 0;
    for (size_t i = 0; i < games.size(); ++i) {
        if (result.items.size() >= gameLimit) { result.truncated = true; break; }
        const u64 id = games[i].id;
        char hex[32]{};
        std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));

        const auto cachedName = nameCache.find(id);
        const bool iconCached = HasCachedGameIcon(id);
        std::string title;
        if (cachedName != nameCache.end() && iconCached) {
            // SLauncher fast path: a resolved title with its icon needs no
            // synchronous NS control-data request on this scan.
            title = cachedName->second;
        } else {
            u64 actual = 0;
            if (nsReady && R_SUCCEEDED(desknx::slaunch::ReadControlData(
                    id, control, actual))) {
                title = desknx::slaunch::ResolveTitleName(control.nacp);
                if (title.empty() && cachedName != nameCache.end())
                    title = cachedName->second;
                if (!iconCached) CacheGameIcon(id, control, actual);
                fetchedControlData = true;
            } else if (cachedName != nameCache.end()) {
                title = cachedName->second;
            }
        }

        if (title.empty()) {
            std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
            title = std::string("Game ") + hex;
        }
        freshNameCache.emplace(id, title);
        Candidate game{Kind::Game, title, hex, 1, 1,
                       static_cast<int>(colorIndex++ % 8), games[i].viewFlags};
        game.playtimeSeconds = games[i].playtimeSeconds;
        game.lastPlayed = games[i].lastPlayed;
        game.launchCount = games[i].launchCount;
        result.items.push_back(std::move(game));
        if ((i + 1) % 12 == 0) report(false);
    }
    // Keep the per-title cache in sync with the installed set. If the only
    // change was an install/uninstall, existing names/icons are reused.
    if (nsReady && result.nsErrorCode == 0 &&
        (fetchedControlData || freshNameCache.size() != nameCache.size()))
        WriteGameNameCache(freshNameCache);
    report(false); // expose the installed-game part before scanning NRO assets

    for (const auto& nro : nros) {
        if (result.items.size() >= MaxItems - WidgetReserve) { result.truncated = true; break; }
        Candidate item{Kind::Nro, fileTitle(nro.path), nro.path, 1, 1,
                       static_cast<int>(result.items.size() % 8)};
        CacheNroIconAndMetadata(nro, item);
        result.items.push_back(std::move(item));
        if (result.items.size() % 12 == 0) report(false);
    }
    if (games.size() > gameLimit || nros.size() >= MaxItems) result.truncated = true;
    result.items.push_back({Kind::Clock, "Clock widget", "", 2, 1, 1});
    result.items.push_back({Kind::Battery, "Battery widget", "", 2, 1, 2});
    result.items.push_back({Kind::System, "System info widget", "", 2, 1, 5});
    result.items.push_back({Kind::Recent, "Recent launch widget", "", 3, 1, 6});
    // Preserve the last known-good catalogue if NS enumeration failed.
    if (nsReady && result.nsErrorCode == 0) WriteCache(sig, result);
    report(true);
    return result;
}

} // namespace desknx::catalog
