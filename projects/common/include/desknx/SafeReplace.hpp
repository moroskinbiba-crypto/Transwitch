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

enum class ReplaceStatus {
    Success,
    Failed,
    RollbackFailed
};

inline ReplaceStatus replaceFilePreservingOnFailure(
    const char* temporary, const char* destination, const char* backup) {
    if (!temporary || !temporary[0] || !destination || !destination[0] ||
        !backup || !backup[0] || std::strcmp(temporary, destination) == 0 ||
        std::strcmp(temporary, backup) == 0 ||
        std::strcmp(destination, backup) == 0) {
        errno = EINVAL;
        return ReplaceStatus::Failed;
    }

    struct stat destinationStat{};
    bool hasDestination = false;
    if (stat(destination, &destinationStat) == 0) {
        hasDestination = true;
    } else if (errno != ENOENT) {
        return ReplaceStatus::Failed;
    }

    // A backup left by a previous failed rollback is the only surviving copy
    // if destination is missing. Restore it before attempting another write.
    if (!hasDestination) {
        struct stat backupStat{};
        if (stat(backup, &backupStat) == 0) {
            if (std::rename(backup, destination) != 0)
                return ReplaceStatus::RollbackFailed;
            hasDestination = true;
        } else if (errno != ENOENT) {
            return ReplaceStatus::Failed;
        }
    }

    if (hasDestination) {
        // It is safe to discard an older rollback copy only when a current
        // destination exists and can take its place.
        if (std::remove(backup) != 0 && errno != ENOENT)
            return ReplaceStatus::Failed;
        if (std::rename(destination, backup) != 0)
            return ReplaceStatus::Failed;
    }

    if (std::rename(temporary, destination) != 0) {
        const int publishError = errno;
        if (hasDestination && std::rename(backup, destination) != 0)
            return ReplaceStatus::RollbackFailed;
        errno = publishError;
        return ReplaceStatus::Failed;
    }

    // The new file is now published. A stale backup is no longer required;
    // failure to clean it up does not invalidate the successful replacement.
    if (hasDestination)
        (void)std::remove(backup);
    return ReplaceStatus::Success;
}

} // namespace desknx::safeio
