#include "catalog.hpp"

#include <switch.h>
#include <algorithm>
#include <array>
#include <cctype>
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
        if (depth == 0 && name == "DeskNX") continue;
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
std::vector<GameRecord> ReadGameRecords(bool nsReady) {
    std::vector<GameRecord> records;
    if (!nsReady) return records;
    constexpr s32 PageSize = 128;
    std::array<NsApplicationRecord, PageSize> page{};
    s32 offset = 0;
    while (records.size() < 4096) {
        s32 count = 0;
        const Result rc = nsListApplicationRecord(page.data(), PageSize, offset, &count);
        if (R_FAILED(rc) || count <= 0) break;
        const s32 bounded = std::max<s32>(0, std::min<s32>(count, PageSize));
        for (s32 i = 0; i < bounded && records.size() < 4096; ++i)
            records.push_back({page[static_cast<size_t>(i)].application_id,
                               page[static_cast<size_t>(i)].last_updated});
        offset += bounded;
        if (bounded < PageSize) break;
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
bool ReadCache(const Signatures& sig, ScanResult& result) {
    std::ifstream input(CacheFile, std::ios::binary);
    if (!input) return false;
    std::string line;
    if (!std::getline(input, line)) return false;
    const auto header = desknx::model::splitTabs(line);
    if (header.size() != 4 || header[0] != "DESKNXCAT1") return false;
    char gameHash[32]{}, sdHash[32]{};
    std::snprintf(gameHash, sizeof(gameHash), "%016llX", static_cast<unsigned long long>(sig.gameHash));
    std::snprintf(sdHash, sizeof(sdHash), "%016llX", static_cast<unsigned long long>(sig.sdHash));
    if (header[1] != std::to_string(sig.gameCount) || header[2] != gameHash || header[3] != sdHash)
        return false;
    std::vector<Candidate> items;
    while (std::getline(input, line)) {
        const auto fields = desknx::model::splitTabs(line);
        if (fields.size() != 7 || fields[0] != "C") return false;
        Candidate item{};
        item.kind = desknx::model::parseKind(fields[1]);
        item.title = fields[2];
        item.data = fields[3];
        item.w = std::atoi(fields[4].c_str());
        item.h = std::atoi(fields[5].c_str());
        item.color = std::atoi(fields[6].c_str());
        if (item.w < 1 || item.w > 3 || item.h < 1 || item.h > 2) return false;
        items.push_back(std::move(item));
        if (items.size() > MaxItems) return false;
    }
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
    std::remove(path.c_str());
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
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
    std::remove(CacheFile);
    if (std::rename(CacheTemp, CacheFile) != 0) {
        std::remove(CacheTemp);
        return false;
    }
    return true;
}
} // namespace

ScanResult Scan() {
    ScanResult result;
    EnsureCacheDirectories();
    const bool nsReady = R_SUCCEEDED(nsInitialize());
    const std::vector<GameRecord> games = ReadGameRecords(nsReady);
    std::vector<NroFile> nros;
    CollectNros("sdmc:/switch", 0, nros);
    std::sort(nros.begin(), nros.end(), [](const NroFile& a, const NroFile& b) {
        return a.path < b.path;
    });
    const Signatures sig = MakeSignatures(games, nros);
    if (ReadCache(sig, result)) {
        if (nsReady) nsExit();
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
    }
    if (nsReady) nsExit();

    for (const auto& nro : nros) {
        if (result.items.size() >= MaxItems - WidgetReserve) { result.truncated = true; break; }
        Candidate item{Kind::Nro, fileTitle(nro.path), nro.path, 1, 1,
                       static_cast<int>(result.items.size() % 8)};
        CacheNroIconAndMetadata(nro, item);
        result.items.push_back(std::move(item));
    }
    if (games.size() > gameLimit || nros.size() >= MaxItems) result.truncated = true;
    result.items.push_back({Kind::Clock, "Clock widget", "", 2, 1, 1});
    result.items.push_back({Kind::Battery, "Battery widget", "", 2, 1, 2});
    result.items.push_back({Kind::System, "System info widget", "", 2, 1, 5});
    result.items.push_back({Kind::Recent, "Recent launch widget", "", 3, 1, 6});
    WriteCache(sig, result);
    return result;
}

} // namespace desknx::catalog
