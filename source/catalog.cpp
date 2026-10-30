#include "catalog.hpp"
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
bool gNsReady = false;
bool gSetSysReady = false;
std::uint32_t gNsInitError = 0;

struct GameRecord { u64 id; u64 last_updated; };
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
    std::array<NsApplicationRecord, ApplicationPageCapacity> page{};
    s32 offset = 0;
    while (records.size() < 4096) {
        const ApplicationPageRequest request = MakeApplicationPageRequest(offset);
        // The count pointer is OUTPUT-only; capacity is the separate second
        // argument. SLauncher uses this exact zero-initialized pattern.
        s32 count = 0;
        const Result rc = nsListApplicationRecord(page.data(), request.capacity,
                                                   request.offset, &count);
        if (R_FAILED(rc)) {
            errorCode = static_cast<std::uint32_t>(rc);
            break;
        }
        if (!IsValidApplicationPageCount(request.capacity, count)) {
            errorCode = 0xD35C0001u; // invalid count returned by NS/libnx
            break;
        }
        if (count == 0) break;
        for (s32 i = 0; i < count && records.size() < 4096; ++i)
            records.push_back({page[static_cast<size_t>(i)].application_id,
                               page[static_cast<size_t>(i)].last_updated});
        offset = NextApplicationPageOffset(request.offset, count);
        if (count < request.capacity) break;
    }
    std::sort(records.begin(), records.end(), [](const GameRecord& a, const GameRecord& b) {
        return a.id < b.id;
    });
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
    if (header.size() != 4 || header[0] != "DESKNXCAT1") return false;
    char gameHash[32]{}, sdHash[32]{};
    std::snprintf(gameHash, sizeof(gameHash), "%016llX", static_cast<unsigned long long>(sig.gameHash));
    std::snprintf(sdHash, sizeof(sdHash), "%016llX", static_cast<unsigned long long>(sig.sdHash));
    if (header[1] != std::to_string(sig.gameCount) || header[2] != gameHash || header[3] != sdHash)
        return false;

    std::vector<Candidate> items;
    while (std::getline(input, line)) {
        if (line.size() > MaxCacheLineBytes || items.size() >= MaxItems) return false;
        const auto fields = desknx::model::splitTabs(line);
        if (fields.size() != 7 || fields[0] != "C" ||
            fields[1].size() > 16 || fields[2].size() > 512 || fields[3].size() > 512)
            return false;

        Candidate item{};
        item.kind = desknx::model::parseKind(fields[1]);
        if (item.kind == Kind::Unsupported) return false;
        item.title = fields[2];
        item.data = fields[3];
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
            NacpLanguageEntry* entry = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&nacp, &entry)) && entry && entry->name[0])
                candidate.title = entry->name;
        }
    }
}
void CacheGameIcon(u64 id, const NsApplicationControlData& data) {
    char hex[32]{};
    std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
    CacheJpegIcon(desknx::model::iconCachePath(Kind::Game, hex), data.icon, sizeof(data.icon), true);
}
bool WriteCache(const Signatures& sig, const ScanResult& result) {
    EnsureCacheDirectories();
    std::ofstream output(CacheTemp, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    char gameHash[32]{}, sdHash[32]{};
    std::snprintf(gameHash, sizeof(gameHash), "%016llX", static_cast<unsigned long long>(sig.gameHash));
    std::snprintf(sdHash, sizeof(sdHash), "%016llX", static_cast<unsigned long long>(sig.sdHash));
    output << "DESKNXCAT1\t" << sig.gameCount << '\t' << gameHash << '\t' << sdHash << '\n';
    for (const auto& item : result.items) {
        output << "C\t" << desknx::model::typeCode(item.kind) << '\t'
               << desknx::model::safeField(item.title) << '\t'
               << desknx::model::safeField(item.data) << '\t'
               << item.w << '\t' << item.h << '\t' << item.color << '\n';
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

bool InitializeServices() {
    if (gNsReady) return true;
    const Result setRc = setsysInitialize();
    if (R_FAILED(setRc)) {
        gNsInitError = static_cast<std::uint32_t>(setRc);
        return false;
    }
    gSetSysReady = true;

    // SLauncher sets the actual HOS version before opening ns. Without this,
    // libnx's version-gated NS command selection can run against 0.0.0.
    SetSysFirmwareVersion firmware{};
    const Result fwRc = setsysGetFirmwareVersion(&firmware);
    if (R_FAILED(fwRc)) {
        gNsInitError = static_cast<std::uint32_t>(fwRc);
        return false;
    }
    hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro) | BIT(31));

    const Result nsRc = nsInitialize();
    gNsReady = R_SUCCEEDED(nsRc);
    gNsInitError = gNsReady ? 0u : static_cast<std::uint32_t>(nsRc);
    return gNsReady;
}

void ShutdownServices() {
    if (gNsReady) { nsExit(); gNsReady = false; }
    if (gSetSysReady) { setsysExit(); gSetSysReady = false; }
}

ScanResult Scan(ProgressCallback progress, void* context) {
    ScanResult result;
    auto report = [&](bool complete) {
        result.complete = complete;
        if (progress) progress(result, context);
    };
    EnsureCacheDirectories();
    const bool nsReady = gNsReady;
    result.nsAvailable = nsReady;
    result.nsErrorCode = nsReady ? 0u : gNsInitError;
    const std::vector<GameRecord> games = ReadGameRecords(nsReady, result.nsErrorCode);
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
        result.complete = true;
        report(true);
        return result;
    }

    result.items.reserve(MaxItems);
    const size_t gameLimit = MaxItems - GameReserve;
    static NsApplicationControlData control{};
    size_t colorIndex = 0;
    for (size_t i = 0; i < games.size(); ++i) {
        if (result.items.size() >= gameLimit) { result.truncated = true; break; }
        const u64 id = games[i].id;
        u64 actual = 0;
        std::string title;
        if (nsReady && R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                                id, &control, sizeof(control), &actual))) {
            NacpLanguageEntry* entry = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&control.nacp, &entry)) && entry && entry->name[0])
                title = entry->name;
            CacheGameIcon(id, control);
        }
        if (title.empty()) {
            char fallback[32]{};
            std::snprintf(fallback, sizeof(fallback), "Game %08X", static_cast<unsigned int>(id & 0xFFFFFFFFu));
            title = fallback;
        }
        char hex[32]{};
        std::snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(id));
        result.items.push_back({Kind::Game, title, hex, 1, 1, static_cast<int>(colorIndex++ % 8)});
        if ((i + 1) % 12 == 0) report(false);
    }
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
