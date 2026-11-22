#pragma once

// Transactional replacement for small SD-card files.
// Rename the existing file out of the way first, publish the completed temp
// file, and restore the old file if publishing fails. On rollback failure the
// previous copy is deliberately left at the backup path for manual recovery.
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace desknx::safeio {

enum class ReplaceStatus { Success, Failed, RollbackFailed };

struct ReplaceOperations {
    void* context{nullptr};
    int (*statPath)(void*, const char*, struct stat*){nullptr};
    int (*removePath)(void*, const char*){nullptr};
    int (*renamePath)(void*, const char*, const char*){nullptr};
};

inline int defaultStatPath(void*, const char* path, struct stat* result) {
    return ::stat(path, result);
}
inline int defaultRemovePath(void*, const char* path) {
    return std::remove(path);
}
inline int defaultRenamePath(void*, const char* from, const char* to) {
    return std::rename(from, to);
}
inline ReplaceOperations defaultReplaceOperations() {
    return {nullptr, defaultStatPath, defaultRemovePath, defaultRenamePath};
}

inline ReplaceStatus replaceFilePreservingOnFailureWithOps(
    const char* temporary, const char* destination, const char* backup,
    const ReplaceOperations& operations, bool keepBackupOnSuccess = false) {
    if (!temporary || !temporary[0] || !destination || !destination[0] ||
        !backup || !backup[0] || std::strcmp(temporary, destination) == 0 ||
        std::strcmp(temporary, backup) == 0 ||
        std::strcmp(destination, backup) == 0 ||
        !operations.statPath || !operations.removePath || !operations.renamePath) {
        errno = EINVAL;
        return ReplaceStatus::Failed;
    }

    struct stat destinationStat{};
    bool hasDestination = false;
    if (operations.statPath(operations.context, destination, &destinationStat) == 0) {
        hasDestination = true;
    } else if (errno != ENOENT) {
        return ReplaceStatus::Failed;
    }

    // A backup left by a previous failed rollback is the only surviving copy
    // if destination is missing. Restore it before attempting another write.
    if (!hasDestination) {
        struct stat backupStat{};
        if (operations.statPath(operations.context, backup, &backupStat) == 0) {
            if (operations.renamePath(operations.context, backup, destination) != 0)
                return ReplaceStatus::RollbackFailed;
            hasDestination = true;
        } else if (errno != ENOENT) {
            return ReplaceStatus::Failed;
        }
    }

    if (hasDestination) {
        if (operations.removePath(operations.context, backup) != 0 && errno != ENOENT)
            return ReplaceStatus::Failed;
        if (operations.renamePath(operations.context, destination, backup) != 0)
            return ReplaceStatus::Failed;
    }

    if (operations.renamePath(operations.context, temporary, destination) != 0) {
        const int publishError = errno;
        if (hasDestination &&
            operations.renamePath(operations.context, backup, destination) != 0)
            return ReplaceStatus::RollbackFailed;
        errno = publishError;
        return ReplaceStatus::Failed;
    }

    if (hasDestination && !keepBackupOnSuccess)
        (void)operations.removePath(operations.context, backup);
    return ReplaceStatus::Success;
}

inline ReplaceStatus replaceFilePreservingOnFailure(
    const char* temporary, const char* destination, const char* backup,
    bool keepBackupOnSuccess = false) {
    const ReplaceOperations operations = defaultReplaceOperations();
    return replaceFilePreservingOnFailureWithOps(
        temporary, destination, backup, operations, keepBackupOnSuccess);
}

} // namespace desknx::safeio
