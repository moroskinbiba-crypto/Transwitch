#include "catalog.hpp"
#include "../include/desknx/Diagnostics.hpp"
#include "slaunch_applications.hpp"
#include "../include/desknx/StrictParse.hpp"
#include "../include/desknx/CatalogPagination.hpp"
#include "../include/desknx/ApplicationSize.hpp"
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
constexpr size_t WidgetReserve = 6;
constexpr const char* CacheDir = "sdmc:/switch/DeskNX/cache";
constexpr const char* IconsDir = "sdmc:/switch/DeskNX/cache/icons";
constexpr const char* CacheFile = "sdmc:/switch/DeskNX/cache/catalogue.tsv";
constexpr const char* CacheTemp = "sdmc:/switch/DeskNX/cache/catalogue.tmp";
constexpr size_t MaxNroAssetBytes = 2 * 1024 * 1024;

struct GameRecord { u64 id; u64 last_updated; u32 viewFlags; bool updateStatusAvailable; };
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

std::string GetApplicationDisplayVersion(const NacpStruct& nacp) {
    const char* begin = nacp.display_version;
    const char* end = std::find(begin, begin + sizeof(nacp.display_version),
                                static_cast<char>(0));
    std::string version(begin, end);
    if (version.size() > 15) return {};
    for (char& ch : version)
        if (ch == '\\t' || ch == '\\n' || ch == '\\r') ch = ' ';
    return version;
}

// NRO directory inventory is checked from persisted directory/file signatures.
// Unchanged subtrees are reused without opendir/readdir; known NRO files are
// stat'ed so in-place updates are still detected. A changed directory is
// enumerated recursively, while unchanged children are reused independently.

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

bool parseUnsigned(const std::string& value, int base, u64& output) {
    if (value.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end, base);
    if (errno == ERANGE || end == value.c_str() || !end || *end != '\0')
        return false;
    output = static_cast<u64>(parsed);
    return true;
}

std::string encodeHex(const std::string& value) {
    static const char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char byte : value) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

bool decodeHex(const std::string& value, std::string& output) {
    if ((value.size() & 1u) != 0 || value.size() > 1024)
        return false;
    auto digit = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    output.clear();
    output.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2) {
        const int high = digit(static_cast<unsigned char>(value[i]));
        const int low = digit(static_cast<unsigned char>(value[i + 1]));
        if (high < 0 || low < 0) return false;
        output.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

// NRO inventory stores the last parsed title and file signature. Rebuilding
// the catalogue after one install/update therefore reads NACP/icon sections
// only for the changed NROs. Directory enumeration still runs so removals are
// detected; unchanged NRO payloads are not reopened.
struct NroMetadata {
    u64 size{0};
    u64 mtime{0};
    std::string title;
};
using NroMetadataCache = std::unordered_map<std::string, NroMetadata>;
constexpr const char* NroMetadataFile = "sdmc:/switch/DeskNX/cache/nro-metadata.tsv";
constexpr const char* NroMetadataTemp = "sdmc:/switch/DeskNX/cache/nro-metadata.tmp";

NroMetadataCache ReadNroMetadataCache(bool* validOut = nullptr) {
    if(validOut)*validOut=false;
    NroMetadataCache cache;
    struct stat st{};
    constexpr size_t MaxBytes = 768 * 1024;
    if (stat(NroMetadataFile, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || static_cast<u64>(st.st_size) > MaxBytes)
        return cache;
    std::ifstream input(NroMetadataFile, std::ios::binary);
    if (!input) return cache;
    std::string line;
    if (!std::getline(input, line) || line != "DESKNXNR1") return {};
    size_t count = 0;
    while (std::getline(input, line)) {
        if (line.size() > 2200 || ++count > MaxItems) return {};
        const auto fields = desknx::model::splitTabs(line);
        if (fields.size() != 4 || fields[0].empty() || fields[0].size() > 1024 ||
            fields[3].size() > 1024)
            return {};
        std::string path, title;
        u64 size = 0, mtime = 0;
        if (!decodeHex(fields[0], path) || path.empty() || path.size() > 512 ||
            !parseUnsigned(fields[1], 10, size) ||
            !parseUnsigned(fields[2], 10, mtime) ||
            !decodeHex(fields[3], title) || title.size() > 512)
            return {};
        cache[path] = {size, mtime, std::move(title)};
    }
    if (input.bad()) return {};
    if(validOut)*validOut=true;
    return cache;
}

bool WriteNroMetadataCache(const NroMetadataCache& cache) {
    EnsureCacheDirectories();
    std::ofstream output(NroMetadataTemp, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "DESKNXNR1\n";
    size_t written = 0;
    for (const auto& entry : cache) {
        if (written >= MaxItems || entry.first.empty() || entry.first.size() > 512 ||
            entry.second.title.size() > 512)
            continue;
        output << encodeHex(entry.first) << '\t' << entry.second.size << '\t'
               << entry.second.mtime << '\t' << encodeHex(entry.second.title) << '\n';
        ++written;
    }
    output.flush();
    const bool good = output.good();
    output.close();
    if (!good) {
        std::remove(NroMetadataTemp);
        return false;
    }
    const auto status = desknx::safeio::replaceFilePreservingOnFailure(
        NroMetadataTemp, NroMetadataFile,
        "sdmc:/switch/DeskNX/cache/nro-metadata.bak", true);
    if (status != desknx::safeio::ReplaceStatus::Success) {
        std::remove(NroMetadataTemp);
        return false;
    }
    return true;
}

using GameTimestampCache = std::unordered_map<u64, u64>;
constexpr const char* GameTimestampFile = "sdmc:/switch/DeskNX/cache/game-timestamps.tsv";
constexpr const char* GameTimestampTemp = "sdmc:/switch/DeskNX/cache/game-timestamps.tmp";

GameTimestampCache ReadGameTimestampCache() {
    GameTimestampCache cache;
    struct stat st{};
    constexpr size_t MaxBytes = 128 * 1024;
    if (stat(GameTimestampFile, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || static_cast<u64>(st.st_size) > MaxBytes)
        return cache;
    std::ifstream input(GameTimestampFile, std::ios::binary);
    if (!input) return cache;
    std::string line;
    if (!std::getline(input, line) || line != "DESKNXGT1") return {};
    size_t count = 0;
    while (std::getline(input, line)) {
        if (line.size() > 64 || ++count > 4096) return {};
        const auto fields = desknx::model::splitTabs(line);
        if (fields.size() != 2 || !isHexTitleId(fields[0])) continue;
        u64 id = 0, timestamp = 0;
        if (parseUnsigned(fields[0], 16, id) && id != 0 &&
            parseUnsigned(fields[1], 10, timestamp))
            cache[id] = timestamp;
    }
    if (input.bad()) return {};
    return cache;
}

bool WriteGameTimestampCache(const GameTimestampCache& cache) {
    EnsureCacheDirectories();
    std::ofstream output(GameTimestampTemp, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "DESKNXGT1\n";
    for (const auto& entry : cache) {
        char id[17]{};
        std::snprintf(id, sizeof(id), "%016llX",
                      static_cast<unsigned long long>(entry.first));
        output << id << '\t' << entry.second << '\n';
    }
    output.flush();
    const bool good = output.good();
    output.close();
    if (!good) {
        std::remove(GameTimestampTemp);
        return false;
    }
    const auto status = desknx::safeio::replaceFilePreservingOnFailure(
        GameTimestampTemp, GameTimestampFile,
        "sdmc:/switch/DeskNX/cache/game-timestamps.bak", true);
    if (status != desknx::safeio::ReplaceStatus::Success) {
        std::remove(GameTimestampTemp);
        return false;
    }
    return true;
}

struct DirectoryStamp {
    u64 mtime{0};
    u64 ctime{0};
    u64 size{0};
};
using DirectoryStampCache = std::unordered_map<std::string, DirectoryStamp>;
constexpr const char* NroDirectoryCacheFile =
    "sdmc:/switch/DeskNX/cache/nro-directories.tsv";
constexpr const char* NroDirectoryCacheTemp =
    "sdmc:/switch/DeskNX/cache/nro-directories.tmp";
constexpr size_t MaxCachedDirectories = 2048;

DirectoryStamp directoryStampFromStat(const struct stat& st) {
    return {static_cast<u64>(st.st_mtime),static_cast<u64>(st.st_ctime),
            st.st_size>=0?static_cast<u64>(st.st_size):0};
}
bool sameDirectoryStamp(const DirectoryStamp& a,const DirectoryStamp& b) {
    return a.mtime==b.mtime&&a.ctime==b.ctime&&a.size==b.size;
}
DirectoryStampCache ReadNroDirectoryStampCache(bool* validOut=nullptr) {
    if(validOut)*validOut=false;
    DirectoryStampCache cache;
    struct stat st{};
    constexpr size_t MaxBytes=3u*1024u*1024u;
    if(stat(NroDirectoryCacheFile,&st)!=0||!S_ISREG(st.st_mode)||
       st.st_size<=0||static_cast<u64>(st.st_size)>MaxBytes)
        return cache;
    std::ifstream input(NroDirectoryCacheFile,std::ios::binary);
    if(!input)return cache;
    std::string line;
    if(!std::getline(input,line)||line!="DESKNXDIR1")return {};
    size_t count=0;
    while(std::getline(input,line)) {
        if(line.size()>1200||++count>MaxCachedDirectories)return {};
        const auto fields=desknx::model::splitTabs(line);
        if(fields.size()!=4||fields[0].empty()||fields[0].size()>1024)return {};
        std::string path;
        DirectoryStamp stamp{};
        if(!decodeHex(fields[0],path)||path.empty()||path.size()>512||
           (path!="sdmc:/switch"&&path.compare(0,13,"sdmc:/switch/")!=0)||
           path.find("..")!=std::string::npos||
           !parseUnsigned(fields[1],10,stamp.mtime)||
           !parseUnsigned(fields[2],10,stamp.ctime)||
           !parseUnsigned(fields[3],10,stamp.size))
            return {};
        cache[path]=stamp;
    }
    if(input.bad())return {};
    if(validOut)*validOut=true;
    return cache;
}
bool WriteNroDirectoryStampCache(const DirectoryStampCache& cache) {
    if(cache.empty()||cache.size()>MaxCachedDirectories)return false;
    for(const auto& entry:cache)
        if(entry.first.empty()||entry.first.size()>512)return false;
    EnsureCacheDirectories();
    std::ofstream output(NroDirectoryCacheTemp,std::ios::binary|std::ios::trunc);
    if(!output)return false;
    output<<"DESKNXDIR1\n";
    for(const auto& entry:cache) {
        output<<encodeHex(entry.first)<<'\t'<<entry.second.mtime<<'\t'
              <<entry.second.ctime<<'\t'<<entry.second.size<<'\n';
    }
    output.flush();
    const bool good=output.good();
    output.close();
    if(!good){std::remove(NroDirectoryCacheTemp);return false;}
    const auto status=desknx::safeio::replaceFilePreservingOnFailure(
        NroDirectoryCacheTemp,NroDirectoryCacheFile,
        "sdmc:/switch/DeskNX/cache/nro-directories.bak",true);
    if(status!=desknx::safeio::ReplaceStatus::Success) {
        std::remove(NroDirectoryCacheTemp);
        return false;
    }
    return true;
}
bool SameNroDirectoryStampCache(const DirectoryStampCache& a,
                                const DirectoryStampCache& b) {
    if(a.size()!=b.size())return false;
    for(const auto& entry:a) {
        const auto found=b.find(entry.first);
        if(found==b.end()||!sameDirectoryStamp(entry.second,found->second))
            return false;
    }
    return true;
}
struct NroDirectoryScanStats {
    size_t reusedDirectories{0};
    size_t enumeratedDirectories{0};
    size_t directoriesVisited{0};
    size_t entriesVisited{0};
    size_t checkedNroFiles{0};
    bool complete{true};
    bool truncated{false};
};

void CollectNrosIncremental(const std::string& root,int depth,
    std::vector<NroFile>& out,const DirectoryStampCache& oldDirectories,
    const NroMetadataCache& oldNros,DirectoryStampCache& freshDirectories,
    bool allowCache,NroDirectoryScanStats& stats) {
    if(depth>desknx::nro_scan::MaxDepth) {
        stats.complete=false;stats.truncated=true;return;
    }
    if(out.size()>=MaxItems) {
        stats.complete=false;stats.truncated=true;return;
    }
    if(stats.directoriesVisited>=desknx::nro_scan::MaxDirectories) {
        stats.complete=false;stats.truncated=true;return;
    }
    ++stats.directoriesVisited;
    struct stat rootStat{};
    if(stat(root.c_str(),&rootStat)!=0||!S_ISDIR(rootStat.st_mode)) {
        stats.complete=false;
        return;
    }
    const DirectoryStamp currentStamp=directoryStampFromStat(rootStat);
    const auto oldRoot=oldDirectories.find(root);
    bool reusable=allowCache&&oldRoot!=oldDirectories.end()&&
        sameDirectoryStamp(oldRoot->second,currentStamp);
    const std::string prefix=root+"/";
    std::vector<std::pair<std::string,DirectoryStamp>> cachedSubdirectories;
    std::vector<NroFile> cachedFiles;
    if(reusable) {
        for(const auto& entry:oldDirectories) {
            if(entry.first.compare(0,prefix.size(),prefix)!=0)continue;
            struct stat childStat{};
            if(stat(entry.first.c_str(),&childStat)!=0||!S_ISDIR(childStat.st_mode)||
               !sameDirectoryStamp(entry.second,directoryStampFromStat(childStat))) {
                reusable=false;
                break;
            }
            cachedSubdirectories.push_back(
                {entry.first,directoryStampFromStat(childStat)});
        }
    }
    if(reusable) {
        for(const auto& entry:oldNros) {
            if(entry.first.compare(0,prefix.size(),prefix)!=0)continue;
            struct stat fileStat{};
            ++stats.checkedNroFiles;
            if(stat(entry.first.c_str(),&fileStat)!=0||!S_ISREG(fileStat.st_mode)) {
                reusable=false;
                break;
            }
            cachedFiles.push_back({entry.first,
                static_cast<u64>(fileStat.st_size),
                static_cast<u64>(fileStat.st_mtime)});
        }
    }
    if(reusable) {
        if(stats.directoriesVisited+cachedSubdirectories.size()>
           desknx::nro_scan::MaxDirectories) {
            stats.complete=false;stats.truncated=true;return;
        }
        stats.directoriesVisited+=cachedSubdirectories.size();
        freshDirectories[root]=currentStamp;
        for(const auto& entry:cachedSubdirectories)
            freshDirectories[entry.first]=entry.second;
        const size_t remaining=MaxItems-out.size();
        if(cachedFiles.size()>remaining) {
            stats.truncated=true;
            cachedFiles.resize(remaining);
        }
        out.insert(out.end(),cachedFiles.begin(),cachedFiles.end());
        stats.reusedDirectories+=cachedSubdirectories.size()+1;
        return;
    }

    DIR* dir=opendir(root.c_str());
    if(!dir) {
        stats.complete=false;
        return;
    }
    ++stats.enumeratedDirectories;
    freshDirectories[root]=currentStamp;
    dirent* entry=nullptr;
    while((entry=readdir(dir))) {
        if(stats.entriesVisited>=desknx::nro_scan::MaxEntries) {
            stats.complete=false;stats.truncated=true;break;
        }
        ++stats.entriesVisited;
        if(out.size()>=MaxItems) {
            stats.complete=false;stats.truncated=true;break;
        }
        const std::string name=entry->d_name;
        if(name=="."||name==".."||(!name.empty()&&name[0]=='.'))continue;
        if(depth==0&&name=="DeskNX")continue;
        const std::string path=root+"/"+name;
        struct stat childStat{};
        if(stat(path.c_str(),&childStat)!=0) {
            stats.complete=false;
            continue;
        }
        if(S_ISDIR(childStat.st_mode)) {
            if(depth<desknx::nro_scan::MaxDepth)
                CollectNrosIncremental(path,depth+1,out,oldDirectories,
                    oldNros,freshDirectories,allowCache,stats);
            else {
                stats.complete=false;
                stats.truncated=true;
            }
        } else if(S_ISREG(childStat.st_mode)&&name.size()>4) {
            std::string extension=name.substr(name.size()-4);
            for(char& c:extension)
                c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(extension==".nro"&&name!="DeskNX.nro")
                out.push_back({path,static_cast<u64>(childStat.st_size),
                               static_cast<u64>(childStat.st_mtime)});
        }
    }
    if(out.size()>=MaxItems) {
        stats.complete=false;
        stats.truncated=true;
    }
    if(closedir(dir)!=0)stats.complete=false;
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
    if (header.size() != 4 ||
        (header[0] != "DESKNXCAT2" && header[0] != "DESKNXCAT3")) return false;
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
        if ((fields.size() != 8 && fields.size() != 12) || fields[0] != "C" ||
            fields[1].size() > 16 || fields[2].size() > 512 || fields[3].size() > 512 ||
            (fields.size() == 12 && (fields[8].size() > 15 ||
                (fields[9] != "0" && fields[9] != "1") ||
                (fields[11] != "0" && fields[11] != "1"))))
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
        if (fields.size() == 12) {
            item.displayVersion = fields[8];
            item.updateStatusAvailable = fields[9] == "1";
            char* sizeEnd = nullptr;
            errno = 0;
            const unsigned long long occupiedBytes = std::strtoull(fields[10].c_str(), &sizeEnd, 10);
            if (errno != 0 || !sizeEnd || sizeEnd == fields[10].c_str() || *sizeEnd != '\0')
                return false;
            item.occupiedSizeBytes = static_cast<std::uint64_t>(occupiedBytes);
            item.occupiedSizeAvailable = fields[11] == "1" && occupiedBytes > 0;
        }
        if (!desknx::strict::parseInt32(fields[4], item.w) ||
            !desknx::strict::parseInt32(fields[5], item.h) ||
            !desknx::strict::parseInt32(fields[6], item.color))
            return false;
        if (item.w < 1 || item.w > 3 || item.h < 1 || item.h > 3 ||
            item.color < 0 || item.color > 7 || item.title.empty())
            return false;

        if (item.kind == Kind::Game) {
            if (!isHexTitleId(item.data)) return false;
            if (!allowStale) {
                const std::string iconPath=desknx::model::iconCachePath(item.kind,item.data);
                if (!IsMissingOrUsableCachedImage(iconPath)) return false;
            }
        } else if (item.kind == Kind::Nro) {
            if (!isCachedNroPath(item.data)) {
                if (allowStale) continue; // stale homebrew entry was removed from SD
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
                               bool forceRefresh = false,
                               bool readMetadata = true) {
    const std::string iconPath = desknx::model::iconCachePath(Kind::Nro, nro.path);
    const bool iconValid = IsUsableCachedImage(iconPath);
    if (!iconValid && IsRegularFile(iconPath)) std::remove(iconPath.c_str());
    const bool needIcon = forceRefresh || !iconValid;
    if (!needIcon && !readMetadata) return;
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
    if (readMetadata && sectionValid(asset.nacp) &&
        asset.nacp.size >= sizeof(NacpStruct)) {
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
    output << "DESKNXCAT3\t" << sig.gameCount << '\t' << gameHash << '\t' << sdHash << '\n';
    for (const auto& item : result.items) {
        output << "C\t" << desknx::model::typeCode(item.kind) << '\t'
               << desknx::model::safeField(item.title) << '\t'
               << desknx::model::safeField(item.data) << '\t'
               << item.w << '\t' << item.h << '\t' << item.color << '\t'
               << item.viewFlags << '\t'
               << desknx::model::safeField(item.displayVersion) << '\t'
               << (item.updateStatusAvailable ? 1 : 0) << '\t'
               << item.occupiedSizeBytes << '\t'
               << (item.occupiedSizeAvailable ? 1 : 0) << '\n';
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

    // Publish the installed IDs immediately. Metadata/icon extraction below can
    // perform up to 90 Horizon IPC calls; the library and widget picker should
    // not remain empty while those calls (or the SD NRO walk) are in progress.
    GameNameCache nameCache = ReadGameNameCache();
    const GameTimestampCache gameTimestampCache = ReadGameTimestampCache();
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
            seed.updateStatusAvailable = game.updateStatusAvailable;
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
                    item.kind == Kind::Storage || item.kind == Kind::Calendar;
            })),
        result.nroCount);
    report(false);

    bool nroMetadataCacheValid=false,directoryCacheValid=false;
    const NroMetadataCache nroMetadataCache=
        ReadNroMetadataCache(&nroMetadataCacheValid);
    const DirectoryStampCache directoryStampCache=
        ReadNroDirectoryStampCache(&directoryCacheValid);
    const bool inventoryCacheUsable=!forceRefresh&&warmCacheAvailable&&
        nroMetadataCacheValid&&directoryCacheValid&&
        nroMetadataCache.size()==result.nroCount;
    const std::time_t nroScanStarted = std::time(nullptr);
    std::vector<NroFile> nros;
    DirectoryStampCache freshDirectoryStamps;
    NroDirectoryScanStats directoryScanStats;
    CollectNrosIncremental("sdmc:/switch",0,nros,directoryStampCache,
        nroMetadataCache,freshDirectoryStamps,inventoryCacheUsable,directoryScanStats);
    LogDiagnostic("nro_scan_elapsed seconds=%lld",
        static_cast<long long>(std::time(nullptr) - nroScanStarted));
    if(!directoryScanStats.complete&&warmCacheAvailable) {
        // A transient SD I/O failure must not turn the library into an empty
        // catalogue. Keep the published last-good snapshot and retry next run.
        result.gameCount=static_cast<size_t>(std::count_if(result.items.begin(),result.items.end(),
            [](const Candidate& item){return item.kind==Kind::Game;}));
        result.nroCount=static_cast<size_t>(std::count_if(result.items.begin(),result.items.end(),
            [](const Candidate& item){return item.kind==Kind::Nro;}));
        result.complete=true;
        result.fromCache=true;
        LogDiagnostic("nro_scan_preserved_cache complete=0 games=%zu nros=%zu",
            result.gameCount,result.nroCount);
        report(true);
        return result;
    }
    LogDiagnostic("nro_inventory directories_reused=%zu directories_enumerated=%zu directories_visited=%zu entries_visited=%zu nro_stat_checks=%zu cache_usable=%d complete=%d truncated=%d",
        directoryScanStats.reusedDirectories,directoryScanStats.enumeratedDirectories,
        directoryScanStats.directoriesVisited,directoryScanStats.entriesVisited,
        directoryScanStats.checkedNroFiles,inventoryCacheUsable?1:0,
        directoryScanStats.complete?1:0,directoryScanStats.truncated?1:0);
    result.nroCount = nros.size();
    LogDiagnostic("nro_scan root=switch depth_limit=8 count=%zu", nros.size());
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

    // A catalogue hit is safe only when the NRO metadata manifest covers the
    // complete live inventory. Otherwise we must rebuild the manifests once;
    // persisting a partial NRO list here would make later incremental scans
    // silently miss files hidden behind a truncated catalogue.
    bool nroMetadataMatchesInventory = nroMetadataCacheValid &&
        nroMetadataCache.size() == nros.size();
    if (nroMetadataMatchesInventory) {
        for (const auto& nro : nros) {
            const auto cached = nroMetadataCache.find(nro.path);
            if (cached == nroMetadataCache.end() ||
                cached->second.size != nro.size ||
                cached->second.mtime != nro.mtime) {
                nroMetadataMatchesInventory = false;
                break;
            }
        }
    }
    // Never reuse a cache with mismatched live signatures or incomplete NRO
    // metadata during a normal scan.
    const bool catalogueCacheHit =
        !forceRefresh && nsReady && result.nsErrorCode == 0 &&
        ReadCache(sig, result);
    // Keep valid game metadata from a cache hit even when the NRO directory
    // manifest needs repair; the game signature is independent of that manifest.
    std::unordered_map<std::string, Candidate> cachedGameMetadata;
    if (catalogueCacheHit) {
        for (const auto& cached : result.items) {
            if (cached.kind == Kind::Game && isHexTitleId(cached.data))
                cachedGameMetadata.emplace(cached.data, cached);
        }
    }
    if (catalogueCacheHit && nroMetadataMatchesInventory) {
        if (directoryScanStats.complete && !directoryScanStats.truncated &&
            (!directoryCacheValid ||
             !SameNroDirectoryStampCache(directoryStampCache,freshDirectoryStamps))) {
            const bool saved=WriteNroDirectoryStampCache(freshDirectoryStamps);
            LogDiagnostic("nro_directory_cache_write ok=%d entries=%zu",
                saved?1:0,freshDirectoryStamps.size());
        }
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
    // ReadCache() may have populated cached counts even when its NRO manifest
    // was incomplete; restore the live enumeration totals before rebuilding.
    result.gameCount = games.size();
    result.nroCount = nros.size();
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
        result.items.push_back({Kind::Game, title, id, 1, 1,
            static_cast<int>(i % 12), games[i].viewFlags});
    }
    (void)EnsureBuiltinWidgets(result.items);
    report(false);
    static NsApplicationControlData control{};
    GameNameCache freshNameCache;
    freshNameCache.reserve(games.size());
    GameTimestampCache freshGameTimestampCache;
    freshGameTimestampCache.reserve(games.size());
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
        const auto cachedTimestamp = gameTimestampCache.find(id);
        const bool timestampUnchanged =
            cachedTimestamp != gameTimestampCache.end() &&
            cachedTimestamp->second == games[i].last_updated;
        if (cachedTimestamp != gameTimestampCache.end())
            freshGameTimestampCache[id] = cachedTimestamp->second;
        const bool iconCached = HasCachedGameIcon(id);
        const bool cachedNameUsable = cachedName != nameCache.end() &&
            !desknx::model::isTitleId(cachedName->second) &&
            !(cachedName->second.compare(0, 5, "Game ") == 0 &&
              desknx::model::isTitleId(cachedName->second.substr(5)));
        const bool appUnchanged = timestampUnchanged &&
            cachedNameUsable && iconCached;
        std::string title;
        std::string displayVersion;
        std::uint64_t occupiedSizeBytes = 0;
        bool occupiedSizeAvailable = false;
        const auto cachedMetadata = cachedGameMetadata.find(hex);
        if (cachedMetadata != cachedGameMetadata.end()) {
            displayVersion = cachedMetadata->second.displayVersion;
            occupiedSizeBytes = cachedMetadata->second.occupiedSizeBytes;
            occupiedSizeAvailable = cachedMetadata->second.occupiedSizeAvailable;
        }
        const bool skipMetadataRead = !forceRefresh && appUnchanged &&
            !displayVersion.empty() && occupiedSizeAvailable;
        if (!occupiedSizeAvailable) {
            NsApplicationOccupiedSize occupiedSize{};
            const Result occupiedRc = nsCalculateApplicationOccupiedSize(id, &occupiedSize);
            if (R_SUCCEEDED(occupiedRc)) {
                const auto decoded = desknx::application_size::decode(
                    reinterpret_cast<const std::uint8_t*>(&occupiedSize), sizeof(occupiedSize));
                occupiedSizeBytes = decoded.bytes;
                occupiedSizeAvailable = decoded.available;
            }
        }
        Result metadataRc=0;
        bool metadataRead=false;
        bool iconAvailable=iconCached;
        const char* nameSource=skipMetadataRead?"cache":"nacp";
        // Name metadata and artwork are independent cache entries. A valid
        // name must never suppress the one-time NACP read needed to repair a
        // missing icon, and a stale title-id placeholder must not be treated
        // as a resolved game name.
        if (skipMetadataRead) {
            title = cachedName->second;
        } else {
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
                displayVersion = GetApplicationDisplayVersion(control.nacp);
                if (forceRefresh || !iconCached || !timestampUnchanged) {
                    if (CacheGameIcon(id, control, actual)) {
                        ++cachedGameIcons;
                        iconAvailable=true;
                    } else {
                        ++failedGameIcons;
                        iconAvailable=false;
                    }
                }
                fetchedControlData = true;
                freshGameTimestampCache[id] = games[i].last_updated;
            } else {
                ++controlDataFailed;
                if (firstControlDataError == 0)
                    firstControlDataError = static_cast<std::uint32_t>(metadataRc);
                if (cachedNameUsable) {
                    title = cachedName->second;
                    nameSource="cache_fallback";
                }
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
            result.items[i].occupiedSizeBytes = occupiedSizeBytes;
            result.items[i].occupiedSizeAvailable = occupiedSizeAvailable;
            result.items[i].updateStatusAvailable = games[i].updateStatusAvailable;
            result.items[i].viewFlags = games[i].viewFlags;
            result.items[i].color = static_cast<int>(colorIndex++ % 12);
        } else {
            // Defensive fallback for a malformed staging snapshot.
            result.items.insert(result.items.begin() + static_cast<std::ptrdiff_t>(
                std::min(i, result.items.size())),
                Candidate{Kind::Game, title, hex, 1, 1,
                          static_cast<int>(colorIndex++ % 12), games[i].viewFlags});
            result.items.back().displayVersion = displayVersion;
            result.items.back().occupiedSizeBytes = occupiedSizeBytes;
            result.items.back().occupiedSizeAvailable = occupiedSizeAvailable;
            result.items.back().updateStatusAvailable = games[i].updateStatusAvailable;
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
    bool timestampCacheChanged =
        freshGameTimestampCache.size() != gameTimestampCache.size();
    if (!timestampCacheChanged) {
        for (const auto& entry : freshGameTimestampCache) {
            const auto old = gameTimestampCache.find(entry.first);
            if (old == gameTimestampCache.end() || old->second != entry.second) {
                timestampCacheChanged = true;
                break;
            }
        }
    }
    if (nsReady && result.nsErrorCode == 0 && timestampCacheChanged) {
        const bool timestampsSaved = WriteGameTimestampCache(freshGameTimestampCache);
        LogDiagnostic("game_timestamp_cache_write ok=%d entries=%zu",
                      timestampsSaved ? 1 : 0, freshGameTimestampCache.size());
    }
    report(false); // expose the installed-game part before scanning NRO assets

    NroMetadataCache refreshedNroMetadata;
    refreshedNroMetadata.reserve(nros.size());
    size_t nroMetadataReused = 0, nroMetadataRefreshed = 0;
    bool nroMetadataComplete =
        directoryScanStats.complete && !directoryScanStats.truncated;
    for (const auto& nro : nros) {
        if (result.items.size() >= MaxItems - WidgetReserve) {
            result.truncated = true;
            nroMetadataComplete = false;
            break;
        }
        Candidate item{Kind::Nro, fileTitle(nro.path), nro.path, 1, 1,
                       static_cast<int>(result.items.size() % 12)};
        const auto cached = nroMetadataCache.find(nro.path);
        const bool unchanged = !forceRefresh &&
            cached != nroMetadataCache.end() &&
            cached->second.size == nro.size &&
            cached->second.mtime == nro.mtime;
        if (unchanged) {
            item.title = cached->second.title;
            const std::string iconPath =
                desknx::model::iconCachePath(Kind::Nro, nro.path);
            if (!IsUsableCachedImage(iconPath))
                CacheNroIconAndMetadata(nro, item, false, false);
            ++nroMetadataReused;
        } else {
            CacheNroIconAndMetadata(nro, item, forceRefresh || !unchanged, true);
            ++nroMetadataRefreshed;
        }
        refreshedNroMetadata.emplace(nro.path,
            NroMetadata{nro.size, nro.mtime, item.title});
        result.items.push_back(std::move(item));
        if (result.items.size() % 12 == 0) report(false);
    }
    bool nroMetadataChanged =
        refreshedNroMetadata.size() != nroMetadataCache.size();
    if (!nroMetadataChanged) {
        for (const auto& entry : refreshedNroMetadata) {
            const auto old = nroMetadataCache.find(entry.first);
            if (old == nroMetadataCache.end() ||
                old->second.size != entry.second.size ||
                old->second.mtime != entry.second.mtime ||
                old->second.title != entry.second.title) {
                nroMetadataChanged = true;
                break;
            }
        }
    }
    const bool nroCacheSaved = nroMetadataComplete
        ? ((!nroMetadataCacheValid || nroMetadataChanged)
            ? WriteNroMetadataCache(refreshedNroMetadata) : true)
        : false;
    LogDiagnostic("nro_metadata_cache reused=%zu refreshed=%zu changed=%d complete=%d saved=%d entries=%zu",
        nroMetadataReused, nroMetadataRefreshed, nroMetadataChanged ? 1 : 0,
        nroMetadataComplete ? 1 : 0, nroCacheSaved ? 1 : 0,
        refreshedNroMetadata.size());
    // Publish directory signatures only after the matching metadata manifest
    // is known complete. A truncated catalogue must never pair a full
    // directory tree with a partial NRO manifest.
    if (nroMetadataComplete && nroCacheSaved &&
        (!directoryCacheValid ||
         !SameNroDirectoryStampCache(directoryStampCache,freshDirectoryStamps))) {
        const bool saved=WriteNroDirectoryStampCache(freshDirectoryStamps);
        LogDiagnostic("nro_directory_cache_write ok=%d entries=%zu",
            saved?1:0,freshDirectoryStamps.size());
    }
    if (games.size() > gameLimit || nros.size() >= MaxItems ||
        !directoryScanStats.complete || directoryScanStats.truncated)
        result.truncated = true;
    (void)EnsureBuiltinWidgets(result.items);
    const bool scanComplete=directoryScanStats.complete &&
        !directoryScanStats.truncated && !result.truncated;
    // Do not persist a partial SD inventory as the new last-known-good cache.
    if (nsReady && result.nsErrorCode == 0 && scanComplete)
        WriteCache(sig, result);
    LogDiagnostic("scan_done games=%zu nros=%zu items=%zu truncated=%d ns_error=0x%08X elapsed=%llds",
                  result.gameCount, result.nroCount, result.items.size(),
                  result.truncated ? 1 : 0,
                  static_cast<unsigned int>(result.nsErrorCode),
                  static_cast<long long>(std::time(nullptr) - scanStarted));
    report(scanComplete);
    return result;
}

} // namespace desknx::catalog
