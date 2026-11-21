#pragma once

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace desknx::nro_scan {

struct File {
    std::string path;
    std::uint64_t size{0};
    std::uint64_t mtime{0};
};

struct Report {
    bool complete{true};
    bool rootOpened{false};
    bool limitReached{false};
    std::size_t directoriesVisited{0};
    std::size_t entriesVisited{0};
    std::size_t filesFound{0};
    std::size_t inaccessibleDirectories{0};
    std::size_t changedEntries{0};
    std::size_t directoriesBeyondDepth{0};
    int firstError{0};
};

// NROs commonly live in nested folders below sdmc:/switch. Eight directory
// levels cover normal pack layouts without allowing an accidental deep tree
// to turn every startup into an unbounded SD traversal.
constexpr int MaxDepth = 8;
constexpr std::size_t MaxFiles = 512;
constexpr std::size_t MaxEntries = 8192;
constexpr std::size_t MaxDirectories = 2048;

inline bool hasNroExtension(const std::string& name) {
    if (name.size() <= 4) return false;
    const std::size_t offset = name.size() - 4;
    return name[offset] == '.' &&
        std::tolower(static_cast<unsigned char>(name[offset + 1])) == 'n' &&
        std::tolower(static_cast<unsigned char>(name[offset + 2])) == 'r' &&
        std::tolower(static_cast<unsigned char>(name[offset + 3])) == 'o';
}

namespace detail {

enum class EntryType { Unknown, Directory, Regular, Other };

inline EntryType typeOf(const dirent& entry) {
#if defined(DT_DIR) && defined(DT_REG)
    if (entry.d_type == DT_DIR) return EntryType::Directory;
    if (entry.d_type == DT_REG) return EntryType::Regular;
#if defined(DT_UNKNOWN)
    if (entry.d_type != DT_UNKNOWN) return EntryType::Other;
#endif
#endif
    return EntryType::Unknown;
}

inline void rememberError(Report& report, int error) {
    report.complete = false;
    if (report.firstError == 0) report.firstError = error;
}

inline void scanDirectory(const std::string& root, int depth, bool isRoot,
                          std::vector<File>& output, Report& report) {
    if (output.size() >= MaxFiles ||
        report.directoriesVisited >= MaxDirectories) {
        report.complete = false;
        report.limitReached = true;
        return;
    }

    DIR* directory = opendir(root.c_str());
    if (!directory) {
        ++report.inaccessibleDirectories;
        rememberError(report, errno);
        return;
    }
    if (isRoot) report.rootOpened = true;
    ++report.directoriesVisited;

    for (;;) {
        errno = 0;
        dirent* entry = readdir(directory);
        if (!entry) {
            if (errno != 0) rememberError(report, errno);
            break;
        }
        if (report.entriesVisited >= MaxEntries) {
            report.complete = false;
            report.limitReached = true;
            break;
        }
        ++report.entriesVisited;

        const std::string name = entry->d_name;
        if (name.empty() || name == "." || name == ".." || name[0] == '.')
            continue;

        // DeskNX owns this directory for its settings, icon cache and logs.
        // Do not recurse into it, but do not hide any other user folder.
        if (isRoot && name == "DeskNX") continue;

        const std::string path = root + "/" + name;
        EntryType type = typeOf(*entry);
        struct stat st{};

        // libnx/newlib supplies directory-entry types on supported builds.
        // Use those values to avoid a separate SD stat IPC for every ordinary
        // file. Fall back to stat whenever the filesystem reports DT_UNKNOWN.
        if (type == EntryType::Unknown) {
            if (stat(path.c_str(), &st) != 0) {
                const int error = errno;
                if (error == ENOENT) ++report.changedEntries;
                else rememberError(report, error);
                continue;
            }
            if (S_ISDIR(st.st_mode)) type = EntryType::Directory;
            else if (S_ISREG(st.st_mode)) type = EntryType::Regular;
            else type = EntryType::Other;
        }

        if (type == EntryType::Directory) {
            if (depth >= MaxDepth) {
                ++report.directoriesBeyondDepth;
                report.complete = false;
                continue;
            }
            scanDirectory(path, depth + 1, false, output, report);
            if (output.size() >= MaxFiles) {
                report.complete = false;
                report.limitReached = true;
                break;
            }
            continue;
        }

        // Non-NRO regular files need no metadata query. NROs still receive
        // exactly one stat call to obtain size/mtime for safe caching.
        if (type != EntryType::Regular || !hasNroExtension(name))
            continue;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            const int error = errno;
            if (error == ENOENT) ++report.changedEntries;
            else rememberError(report, error);
            continue;
        }

        output.push_back({path, static_cast<std::uint64_t>(st.st_size),
                          static_cast<std::uint64_t>(st.st_mtime)});
        ++report.filesFound;
        if (output.size() >= MaxFiles) {
            report.complete = false;
            report.limitReached = true;
            break;
        }
    }

    if (closedir(directory) != 0)
        rememberError(report, errno);
}

} // namespace detail

inline Report Collect(const std::string& root, std::vector<File>& output) {
    output.clear();
    Report report{};
    detail::scanDirectory(root, 0, true, output, report);
    if (!report.rootOpened) report.complete = false;
    return report;
}

} // namespace desknx::nro_scan
