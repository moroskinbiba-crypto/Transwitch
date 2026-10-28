#include <switch.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr const char* kCatalogUrl =
    "https://raw.githubusercontent.com/moroskinbiba-crypto/Transwitch/main/romfs/catalog.tsv";
constexpr const char* kSdRoot = "sdmc:/";
constexpr const char* kAppDir = "sdmc:/switch/Transwitch";
constexpr const char* kCatalogCache = "sdmc:/switch/Transwitch/catalog.tsv";
constexpr const char* kArchivePath = "sdmc:/switch/Transwitch/cache/translation.part";
constexpr curl_off_t kMaxDownloadBytes = 1024LL * 1024LL * 1024LL;
constexpr la_int64_t kMaxExpandedBytes = 2LL * 1024LL * 1024LL * 1024LL;
constexpr la_int64_t kMaxFileBytes = 512LL * 1024LL * 1024LL;

struct Translation {
    std::string id;
    std::string title;
    std::string language;
    std::string kind;
    std::string status;
    std::string titleId;
    std::string layout;
    std::string packageUrl;
    std::string sourceUrl;
    std::string version;
    std::string notes;
};

struct TransferProgress {
    std::string label;
    int lastPercent = -1;
};

PadState gPad;
bool gNetworkReady = false;

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isSafeId(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;
    for (unsigned char ch : value) {
        if (!(std::islower(ch) || std::isdigit(ch) || ch == '-')) return false;
    }
    return true;
}

bool isValidTitleId(const std::string& value) {
    if (value.size() != 16) return false;
    for (unsigned char ch : value) {
        if (!std::isxdigit(ch)) return false;
    }
    return true;
}

bool isHttpsUrl(const std::string& value) {
    return startsWith(value, "https://") && value.size() < 2048;
}

std::vector<std::string> splitPipe(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (;;) {
        const size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

bool fileExists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool directoryExists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool makeDirectories(const std::string& path) {
    if (path.empty()) return false;
    std::string current;
    size_t pos = 0;
    if (startsWith(path, "sdmc:/")) {
        current = "sdmc:/";
        pos = 6;
    } else if (startsWith(path, "/")) {
        current = "/";
        pos = 1;
    } else {
        current = "";
    }

    while (pos < path.size()) {
        size_t end = path.find('/', pos);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(pos, end - pos);
        if (!part.empty()) {
            if (!current.empty() && !endsWith(current, "/")) current += "/";
            current += part;
            if (mkdir(current.c_str(), 0777) != 0 && !directoryExists(current)) {
                return false;
            }
        }
        pos = end + 1;
    }
    return directoryExists(path);
}

std::string parentDirectory(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return "";
    return path.substr(0, slash);
}

bool ensureParentDirectory(const std::string& path) {
    const std::string parent = parentDirectory(path);
    return !parent.empty() && makeDirectories(parent);
}

bool copyFile(const std::string& source, const std::string& destination) {
    FILE* in = std::fopen(source.c_str(), "rb");
    if (!in) return false;
    if (!ensureParentDirectory(destination)) {
        std::fclose(in);
        return false;
    }
    FILE* out = std::fopen(destination.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }

    char buffer[64 * 1024];
    bool okay = true;
    for (;;) {
        const size_t count = std::fread(buffer, 1, sizeof(buffer), in);
        if (count > 0 && std::fwrite(buffer, 1, count, out) != count) {
            okay = false;
            break;
        }
        if (count < sizeof(buffer)) {
            if (std::ferror(in)) okay = false;
            break;
        }
    }
    if (std::fclose(in) != 0) okay = false;
    if (std::fclose(out) != 0) okay = false;
    if (!okay) std::remove(destination.c_str());
    return okay;
}

std::string readTextFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input) return "";
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

bool writeTextFile(const std::string& path, const std::string& contents) {
    if (!ensureParentDirectory(path)) return false;
    std::ofstream output(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    return output.good();
}

std::vector<Translation> parseCatalog(const std::string& contents) {
    std::vector<Translation> result;
    size_t start = 0;
    while (start < contents.size()) {
        size_t end = contents.find('\n', start);
        if (end == std::string::npos) end = contents.size();
        std::string line = contents.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        start = end + 1;
        if (line.empty() || line[0] == '#') continue;

        const std::vector<std::string> f = splitPipe(line);
        if (f.size() != 11) continue;
        Translation t { f[0], f[1], f[2], f[3], f[4], f[5],
                        f[6], f[7], f[8], f[9], f[10] };
        if (!isSafeId(t.id) || t.title.empty() || t.language.empty() ||
            !isValidTitleId(t.titleId) || !isHttpsUrl(t.sourceUrl)) {
            continue;
        }
        if (t.layout != "atmosphere_root" && t.layout != "title_root" && t.layout != "romfs_root") continue;
        if (!t.packageUrl.empty() && !isHttpsUrl(t.packageUrl)) continue;
        result.push_back(t);
    }

    std::sort(result.begin(), result.end(), [](const Translation& a, const Translation& b) {
        if (a.title == b.title) return a.language < b.language;
        return a.title < b.title;
    });
    return result;
}

struct MemorySink {
    std::string data;
    size_t limit = 2 * 1024 * 1024;
};

size_t memoryWrite(void* data, size_t size, size_t count, void* userData) {
    MemorySink* sink = static_cast<MemorySink*>(userData);
    const size_t bytes = size * count;
    if (bytes > sink->limit || sink->data.size() > sink->limit - bytes) return 0;
    sink->data.append(static_cast<const char*>(data), bytes);
    return bytes;
}

size_t fileWrite(void* data, size_t size, size_t count, void* userData) {
    FILE* file = static_cast<FILE*>(userData);
    return std::fwrite(data, size, count, file);
}

int progressCallback(void* userData, curl_off_t downloadTotal, curl_off_t downloadNow,
                     curl_off_t, curl_off_t) {
    TransferProgress* progress = static_cast<TransferProgress*>(userData);
    if (downloadTotal > kMaxDownloadBytes || downloadNow > kMaxDownloadBytes) return 1;
    int percent = -1;
    if (downloadTotal > 0) {
        percent = static_cast<int>((downloadNow * 100) / downloadTotal);
    }
    if (percent >= 0 && percent != progress->lastPercent) {
        progress->lastPercent = percent;
        consoleClear();
        std::printf("TRANSWITCH / NETWORK\n\n%s\n\n", progress->label.c_str());
        if (downloadTotal > 0) {
            std::printf("Downloaded: %d%%\n", percent);
        } else {
            std::printf("Downloaded: %lld bytes\n", static_cast<long long>(downloadNow));
        }
        std::printf("\nDo not close the application.\n");
        consoleUpdate(NULL);
    }
    return 0;
}

bool curlBase(CURL* curl, char* errorBuffer, size_t errorSize) {
    if (!curl) return false;
    std::memset(errorBuffer, 0, errorSize);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Transwitch/0.1 (+https://github.com/moroskinbiba-crypto/Transwitch)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    return true;
}

bool downloadText(const std::string& url, std::string& output, std::string& error) {
    if (!gNetworkReady) {
        error = "Network socket initialization failed.";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "Could not initialize HTTPS client.";
        return false;
    }

    char errorBuffer[CURL_ERROR_SIZE];
    MemorySink sink;
    bool okay = false;
    curlBase(curl, errorBuffer, sizeof(errorBuffer));
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, memoryWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);

    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        output = sink.data;
        okay = true;
    } else {
        error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(rc);
    }
    curl_easy_cleanup(curl);
    return okay;
}

bool downloadFile(const Translation& translation, const std::string& destination,
                  std::string& error) {
    if (!gNetworkReady) {
        error = "Network socket initialization failed.";
        return false;
    }
    if (!ensureParentDirectory(destination)) {
        error = "Could not create the Transwitch cache directory.";
        return false;
    }

    FILE* file = std::fopen(destination.c_str(), "wb");
    if (!file) {
        error = "Could not open a file on the SD card.";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(file);
        std::remove(destination.c_str());
        error = "Could not initialize HTTPS client.";
        return false;
    }

    char errorBuffer[CURL_ERROR_SIZE];
    TransferProgress progress { "Downloading: " + translation.title, -1 };
    curlBase(curl, errorBuffer, sizeof(errorBuffer));
    curl_easy_setopt(curl, CURLOPT_URL, translation.packageUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

    const CURLcode rc = curl_easy_perform(curl);
    const bool fileOkay = std::fflush(file) == 0;
    std::fclose(file);
    bool okay = rc == CURLE_OK && fileOkay;

    if (!okay) {
        error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(rc);
        std::remove(destination.c_str());
    }
    curl_easy_cleanup(curl);
    return okay;
}

bool normalizeArchivePath(const char* raw, std::string& normalized) {
    if (!raw || !*raw) return false;
    std::string input(raw);
    std::replace(input.begin(), input.end(), '\\', '/');
    if (startsWith(input, "/") || (input.size() >= 2 && input[1] == ':')) return false;

    std::vector<std::string> components;
    size_t start = 0;
    while (start <= input.size()) {
        size_t end = input.find('/', start);
        if (end == std::string::npos) end = input.size();
        const std::string part = input.substr(start, end - start);
        if (part == "..") return false;
        if (!part.empty() && part != ".") components.push_back(part);
        if (end == input.size()) break;
        start = end + 1;
    }

    normalized.clear();
    for (size_t i = 0; i < components.size(); ++i) {
        if (i) normalized += "/";
        normalized += components[i];
    }
    return !normalized.empty();
}

bool findMarker(const std::string& path, const std::string& marker, size_t& pos) {
    size_t found = path.find(marker);
    while (found != std::string::npos) {
        if (found == 0 || path[found - 1] == '/') {
            pos = found;
            return true;
        }
        found = path.find(marker, found + 1);
    }
    return false;
}

bool getDestinationRelativePath(const Translation& translation, const char* archiveName,
                                std::string& destinationRelative) {
    std::string path;
    if (!normalizeArchivePath(archiveName, path)) return false;

    const std::string titlePrefix =
        "atmosphere/contents/" + translation.titleId + "/";
    size_t markerPos = 0;

    // Standard Atmosphere archive: atmosphere/contents/<TITLE_ID>/romfs|exefs/...
    if (findMarker(path, titlePrefix, markerPos)) {
        destinationRelative = path.substr(markerPos);
        return true;
    }

    if (translation.layout == "title_root") {
        const std::string idFolder = translation.titleId + "/";
        if (findMarker(path, idFolder, markerPos)) {
            const std::string remainder = path.substr(markerPos + idFolder.size());
            destinationRelative = titlePrefix + remainder;
            return true;
        }
        if (startsWith(path, "romfs/") || startsWith(path, "exefs/")) {
            destinationRelative = titlePrefix + path;
            return true;
        }
        return false;
    }

    if (translation.layout == "romfs_root") {
        if (startsWith(path, "romfs/")) {
            destinationRelative = titlePrefix + path;
        } else {
            destinationRelative = titlePrefix + "romfs/" + path;
        }
        return true;
    }
    return false;
}

bool isAllowedTarget(const Translation& translation, const std::string& relative) {
    const std::string prefix =
        "atmosphere/contents/" + translation.titleId + "/";
    if (!startsWith(relative, prefix) || relative.size() <= prefix.size()) return false;

    const std::string gameRelative = relative.substr(prefix.size());
    if (!(startsWith(gameRelative, "romfs/") || startsWith(gameRelative, "exefs/"))) {
        return false;
    }
    if (gameRelative.find("/../") != std::string::npos ||
        startsWith(gameRelative, "../") || endsWith(gameRelative, "/..") ||
        gameRelative.find("..") != std::string::npos) return false;
    return true;
}

std::string backupPath(const Translation& translation, const std::string& relative) {
    return std::string(kSdRoot) + "switch/Transwitch/backups/" +
           translation.id + "/" + relative;
}

std::string manifestPath(const Translation& translation) {
    return std::string(kSdRoot) + "switch/Transwitch/installed/" + translation.id + ".lst";
}

bool saveBackupIfNeeded(const Translation& translation, const std::string& relative,
                        const std::string& target) {
    if (!fileExists(target)) return true;
    const std::string backup = backupPath(translation, relative);
    if (fileExists(backup)) return true; // Keep the original backup across patch updates.
    return copyFile(target, backup);
}

bool addManifestEntry(const Translation& translation, const std::string& relative) {
    const std::string manifest = manifestPath(translation);
    const std::string existing = readTextFile(manifest);
    size_t start = 0;
    while (start < existing.size()) {
        size_t end = existing.find('\n', start);
        if (end == std::string::npos) end = existing.size();
        if (existing.substr(start, end - start) == relative) return true;
        start = end + 1;
    }
    if (!ensureParentDirectory(manifest)) return false;
    FILE* file = std::fopen(manifest.c_str(), "ab");
    if (!file) return false;
    const bool okay = std::fwrite(relative.data(), 1, relative.size(), file) == relative.size() &&
                      std::fwrite("\n", 1, 1, file) == 1 &&
                      std::fflush(file) == 0;
    std::fclose(file);
    return okay;
}

bool extractArchive(const Translation& translation, const std::string& archivePath,
                    std::string& error) {
    struct archive* archive = archive_read_new();
    if (!archive) {
        error = "Could not initialize archive reader.";
        return false;
    }
    archive_read_support_filter_all(archive);
    archive_read_support_format_zip(archive);
    archive_read_support_format_7zip(archive);
    archive_read_support_format_rar(archive);
    archive_read_support_format_rar5(archive);

    if (archive_read_open_filename(archive, archivePath.c_str(), 64 * 1024) != ARCHIVE_OK) {
        error = archive_error_string(archive) ? archive_error_string(archive) : "Invalid archive.";
        archive_read_free(archive);
        return false;
    }

    const std::string expectedPrefix =
        "atmosphere/contents/" + translation.titleId + "/";
    la_int64_t totalExpanded = 0;
    size_t installedFiles = 0;
    struct archive_entry* entry = nullptr;
    bool okay = true;

    while (true) {
        const int status = archive_read_next_header(archive, &entry);
        if (status == ARCHIVE_EOF) break;
        if (status < ARCHIVE_WARN) {
            error = archive_error_string(archive) ? archive_error_string(archive) : "Archive read error.";
            okay = false;
            break;
        }

        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry)) {
            error = "Archive contains a link entry; refusing to install it.";
            okay = false;
            break;
        }

        if (archive_entry_filetype(entry) == AE_IFDIR) {
            archive_read_data_skip(archive);
            continue;
        }
        if (archive_entry_filetype(entry) != AE_IFREG) {
            archive_read_data_skip(archive);
            continue;
        }

        const la_int64_t declaredSize = archive_entry_size(entry);
        if (declaredSize < 0 || declaredSize > kMaxFileBytes ||
            totalExpanded > kMaxExpandedBytes - declaredSize) {
            error = "Archive exceeds Transwitch safety limits.";
            okay = false;
            break;
        }

        std::string relative;
        if (!getDestinationRelativePath(translation, archive_entry_pathname(entry), relative)) {
            // Packages may contain readme files and other top-level metadata.
            archive_read_data_skip(archive);
            continue;
        }
        if (!isAllowedTarget(translation, relative)) {
            error = "Archive attempted to write outside the selected game's romfs folder.";
            okay = false;
            break;
        }

        const std::string target = std::string(kSdRoot) + relative;
        if (!saveBackupIfNeeded(translation, relative, target)) {
            error = "Could not back up an existing file. Check free space on the SD card.";
            okay = false;
            break;
        }
        if (!addManifestEntry(translation, relative)) {
            error = "Could not write the installation manifest.";
            okay = false;
            break;
        }
        if (!ensureParentDirectory(target)) {
            error = "Could not create a folder on the SD card.";
            okay = false;
            break;
        }

        FILE* out = std::fopen(target.c_str(), "wb");
        if (!out) {
            error = "Could not create a translation file on the SD card.";
            okay = false;
            break;
        }

        char buffer[64 * 1024];
        la_ssize_t bytesRead = 0;
        la_int64_t fileWritten = 0;
        bool writeOkay = true;
        while ((bytesRead = archive_read_data(archive, buffer, sizeof(buffer))) > 0) {
            if (std::fwrite(buffer, 1, static_cast<size_t>(bytesRead), out) !=
                static_cast<size_t>(bytesRead)) {
                writeOkay = false;
                break;
            }
            fileWritten += bytesRead;
            if (fileWritten > kMaxFileBytes) {
                writeOkay = false;
                break;
            }
        }
        if (bytesRead < 0) writeOkay = false;
        if (std::fclose(out) != 0) writeOkay = false;
        if (!writeOkay) {
            error = "A file could not be extracted completely.";
            okay = false;
            break;
        }

        totalExpanded += fileWritten;
        ++installedFiles;
        if (installedFiles % 8 == 0) {
            consoleClear();
            std::printf("TRANSWITCH / INSTALL\n\n%s\n\n", translation.title.c_str());
            std::printf("Files installed: %lu\n", static_cast<unsigned long>(installedFiles));
            std::printf("Please keep the app open.\n");
            consoleUpdate(NULL);
        }
    }

    archive_read_close(archive);
    archive_read_free(archive);

    if (okay && installedFiles == 0) {
        error = "No files for this game's romfs were found in the archive.";
        okay = false;
    }
    if (okay) {
        consoleClear();
        std::printf("Installation complete.\n\n");
        std::printf("Files installed: %lu\n", static_cast<unsigned long>(installedFiles));
        consoleUpdate(NULL);
    }
    (void)expectedPrefix;
    return okay;
}

bool isInstalled(const Translation& translation) {
    return fileExists(manifestPath(translation));
}

bool uninstallTranslation(const Translation& translation, std::string& error) {
    const std::string manifest = manifestPath(translation);
    const std::string contents = readTextFile(manifest);
    if (contents.empty()) {
        error = "No installation manifest found for this translation.";
        return false;
    }

    const std::string allowedPrefix =
        "atmosphere/contents/" + translation.titleId + "/";
    size_t restored = 0;
    size_t start = 0;
    while (start < contents.size()) {
        size_t end = contents.find('\n', start);
        if (end == std::string::npos) end = contents.size();
        std::string relative = contents.substr(start, end - start);
        start = end + 1;
        if (relative.empty()) continue;
        if (!startsWith(relative, allowedPrefix) ||
            !isAllowedTarget(translation, relative) || relative.find("..") != std::string::npos) {
            error = "The installation manifest contains an unsafe path.";
            return false;
        }

        const std::string target = std::string(kSdRoot) + relative;
        const std::string backup = backupPath(translation, relative);
        if (fileExists(backup)) {
            if (!copyFile(backup, target)) {
                error = "Could not restore a backup. The manifest was kept.";
                return false;
            }
            std::remove(backup.c_str());
        } else {
            std::remove(target.c_str());
        }
        ++restored;
    }

    std::remove(manifest.c_str());
    consoleClear();
    std::printf("Translation removed.\n\nFiles processed: %lu\n", static_cast<unsigned long>(restored));
    std::printf("Original files were restored when backups existed.\n");
    consoleUpdate(NULL);
    return true;
}

bool waitForA() {
    while (appletMainLoop()) {
        padUpdate(&gPad);
        const u64 keys = padGetButtonsDown(&gPad);
        if (keys & HidNpadButton_A) return true;
        if (keys & HidNpadButton_B) return false;
        svcSleepThread(16 * 1000 * 1000);
    }
    return false;
}

u64 waitForKey() {
    while (appletMainLoop()) {
        padUpdate(&gPad);
        const u64 keys = padGetButtonsDown(&gPad);
        if (keys) return keys;
        svcSleepThread(16 * 1000 * 1000);
    }
    return HidNpadButton_B;
}

std::vector<std::string> getLanguages(const std::vector<Translation>& entries) {
    std::vector<std::string> languages;
    languages.push_back("All");
    for (const auto& entry : entries) {
        if (std::find(languages.begin(), languages.end(), entry.language) == languages.end()) {
            languages.push_back(entry.language);
        }
    }
    std::sort(languages.begin() + 1, languages.end());
    return languages;
}

std::vector<size_t> filteredEntries(const std::vector<Translation>& entries,
                                   const std::string& language) {
    std::vector<size_t> visible;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (language == "All" || entries[i].language == language) visible.push_back(i);
    }
    return visible;
}

void drawList(const std::vector<Translation>& entries, const std::string& language,
              size_t selected) {
    const std::vector<size_t> visible = filteredEntries(entries, language);
    consoleClear();
    std::printf("TRANSWITCH  |  FAN TRANSLATIONS\n");
    std::printf("--------------------------------------------------\n");
    std::printf("Catalog: %lu entries  |  Language: %s\n\n",
                static_cast<unsigned long>(entries.size()), language.c_str());

    if (visible.empty()) {
        std::printf("No translations in this filter.\n");
    } else {
        const size_t pageStart = (selected / 9) * 9;
        const size_t pageEnd = std::min(pageStart + 9, visible.size());
        for (size_t row = pageStart; row < pageEnd; ++row) {
            const Translation& t = entries[visible[row]];
            const char* marker = isInstalled(t) ? "[INSTALLED] " :
                                 (t.packageUrl.empty() ? "[SOURCE]    " : "[AVAILABLE] ");
            std::printf("%s%s %s\n", row == selected ? " > " : "   ", marker, t.title.c_str());
            std::printf("               %s | %s | v%s\n",
                        t.language.c_str(), t.kind.c_str(), t.version.c_str());
        }
        std::printf("\nPage %lu/%lu\n",
                    static_cast<unsigned long>(selected / 9 + 1),
                    static_cast<unsigned long>((visible.size() + 8) / 9));
    }

    std::printf("\nUP/DOWN: select  A: details  X: language filter\n");
    std::printf("Y: refresh catalog  B: exit\n");
    consoleUpdate(NULL);
}

void showMessage(const std::string& title, const std::string& message) {
    consoleClear();
    std::printf("TRANSWITCH\n\n%s\n\n%s\n\n", title.c_str(), message.c_str());
    std::printf("Press A or B to continue.\n");
    consoleUpdate(NULL);
    waitForKey();
}

bool installTranslation(const Translation& translation) {
    if (translation.packageUrl.empty()) {
        showMessage("Download unavailable",
                    "This record is a source listing only. Open the author's source page to find a release.");
        return false;
    }

    consoleClear();
    std::printf("INSTALL TRANSLATION\n\n");
    std::printf("%s\n", translation.title.c_str());
    std::printf("Language: %s\n", translation.language.c_str());
    std::printf("Game Title ID: %s\n\n", translation.titleId.c_str());
    std::printf("The patch will be written only to:\n");
    std::printf("atmosphere/contents/%s/romfs/\n\n", translation.titleId.c_str());
    std::printf("Existing files will be backed up where possible.\n");
    std::printf("Close the game before installing.\n\n");
    std::printf("Press A to confirm, B to cancel.\n");
    consoleUpdate(NULL);
    if (!waitForA()) return false;

    std::string error;
    std::remove(kArchivePath);
    if (!downloadFile(translation, kArchivePath, error)) {
        showMessage("Download failed", error);
        return false;
    }

    const bool installed = extractArchive(translation, kArchivePath, error);
    std::remove(kArchivePath);
    if (!installed) {
        showMessage("Install incomplete", error +
                    "\nA file list may exist under /switch/Transwitch/installed. "
                    "Use Remove/Restore if you need to roll back.");
        return false;
    }

    showMessage("Success", "The patch is installed. Restart the game to load the translation.");
    return true;
}

void detailsScreen(const Translation& translation) {
    for (;;) {
        consoleClear();
        std::printf("TRANSWITCH / TRANSLATION DETAILS\n");
        std::printf("--------------------------------------------------\n\n");
        std::printf("%s\n\n", translation.title.c_str());
        std::printf("Language: %s\n", translation.language.c_str());
        std::printf("Type: %s\n", translation.kind.c_str());
        std::printf("Release status: %s\n", translation.status.c_str());
        std::printf("Patch version: %s\n", translation.version.c_str());
        std::printf("Game Title ID: %s\n", translation.titleId.c_str());
        std::printf("Installer: %s\n\n",
                    translation.packageUrl.empty() ? "source link only" : "LayeredFS romfs/exefs");
        std::printf("Notes: %s\n\n", translation.notes.c_str());
        std::printf("Author/source:\n%s\n\n", translation.sourceUrl.c_str());
        std::printf("A: install/update   X: remove/restore   B: back\n");
        if (isInstalled(translation)) {
            std::printf("\nStatus: installation manifest found.\n");
        }
        consoleUpdate(NULL);

        const u64 keys = waitForKey();
        if (keys & HidNpadButton_B) return;
        if (keys & HidNpadButton_A) {
            installTranslation(translation);
        } else if (keys & HidNpadButton_X) {
            if (!isInstalled(translation)) {
                showMessage("Not installed", "Transwitch has no installation manifest for this patch.");
                continue;
            }
            consoleClear();
            std::printf("REMOVE TRANSLATION\n\n%s\n\n", translation.title.c_str());
            std::printf("This removes files recorded by Transwitch and restores saved backups.\n");
            std::printf("Press A to confirm, B to cancel.\n");
            consoleUpdate(NULL);
            if (waitForA()) {
                std::string error;
                if (!uninstallTranslation(translation, error)) {
                    showMessage("Remove failed", error);
                } else {
                    waitForKey();
                }
            }
        }
    }
}

bool refreshCatalog(std::vector<Translation>& entries) {
    std::string contents;
    std::string error;
    if (!downloadText(kCatalogUrl, contents, error)) {
        showMessage("Catalog update failed", error);
        return false;
    }
    std::vector<Translation> fresh = parseCatalog(contents);
    if (fresh.empty()) {
        showMessage("Catalog update rejected",
                    "The online catalog was empty or did not pass validation. Existing entries were kept.");
        return false;
    }
    if (!writeTextFile(kCatalogCache, contents)) {
        showMessage("Catalog cache error",
                    "The catalog was downloaded but could not be saved to the SD card.");
        return false;
    }
    entries.swap(fresh);
    showMessage("Catalog updated",
                "The latest catalog has been saved to /switch/Transwitch/catalog.tsv.");
    return true;
}
} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&gPad);

    makeDirectories(kAppDir);
    makeDirectories("sdmc:/switch/Transwitch/cache");
    makeDirectories("sdmc:/switch/Transwitch/installed");
    makeDirectories("sdmc:/switch/Transwitch/backups");

    const Result socketResult = socketInitializeDefault();
    gNetworkReady = R_SUCCEEDED(socketResult);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::string catalogText = readTextFile(kCatalogCache);
    if (catalogText.empty()) catalogText = readTextFile("romfs:/catalog.tsv");
    std::vector<Translation> entries = parseCatalog(catalogText);

    size_t selected = 0;
    size_t languageIndex = 0;
    for (;;) {
        const std::vector<std::string> languages = getLanguages(entries);
        if (languageIndex >= languages.size()) languageIndex = 0;
        const std::string language = languages.empty() ? "All" : languages[languageIndex];
        const std::vector<size_t> visible = filteredEntries(entries, language);
        if (!visible.empty() && selected >= visible.size()) selected = visible.size() - 1;
        if (visible.empty()) selected = 0;

        drawList(entries, language, selected);
        const u64 keys = waitForKey();

        if (keys & HidNpadButton_B) break;
        if (keys & HidNpadButton_Up) {
            if (selected > 0) --selected;
        } else if (keys & HidNpadButton_Down) {
            if (selected + 1 < visible.size()) ++selected;
        } else if (keys & HidNpadButton_X) {
            languageIndex = languages.empty() ? 0 : (languageIndex + 1) % languages.size();
            selected = 0;
        } else if (keys & HidNpadButton_Y) {
            refreshCatalog(entries);
            selected = 0;
            languageIndex = 0;
        } else if ((keys & HidNpadButton_A) && !visible.empty()) {
            detailsScreen(entries[visible[selected]]);
        }
    }

    curl_global_cleanup();
    if (gNetworkReady) socketExit();
    consoleExit(NULL);
    return 0;
}
