#pragma once

#include "SafeReplace.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace desknx::persist {

enum class FileSource { None, Primary, Backup };

enum class WriteStatus {
    Success,
    InvalidArgument,
    OpenFailed,
    WriteFailed,
    TempCommitFailed,
    ReplaceFailed,
    RollbackFailed,
    PublishCommitFailed
};

using Validator = bool (*)(const std::string& contents, void* context);
using CommitCallback = bool (*)(void* context, const char* phase);

inline bool readBoundedFile(const char* path, std::size_t maxBytes,
                            std::string& output) {
    if (!path || !path[0] || maxBytes == 0) {
        errno = EINVAL;
        return false;
    }

    struct stat st{};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || static_cast<unsigned long long>(st.st_size) > maxBytes)
        return false;

    FILE* input = std::fopen(path, "rb");
    if (!input) return false;

    std::string contents;
    contents.resize(static_cast<std::size_t>(st.st_size));
    const std::size_t read = std::fread(&contents[0], 1, contents.size(), input);
    const bool readOk = read == contents.size() && std::ferror(input) == 0;
    const int extra = readOk ? std::fgetc(input) : EOF;
    const bool exactSize = readOk && extra == EOF && std::feof(input) != 0;
    const bool closeOk = std::fclose(input) == 0;
    if (!exactSize || !closeOk) return false;

    output.swap(contents);
    return true;
}

inline FileSource readValidatedWithBackup(
    const char* primary, const char* backup, std::size_t maxBytes,
    Validator validator, void* context, std::string& output) {
    if (!primary || !backup || !validator) return FileSource::None;

    std::string candidate;
    if (readBoundedFile(primary, maxBytes, candidate) &&
        validator(candidate, context)) {
        output.swap(candidate);
        return FileSource::Primary;
    }

    candidate.clear();
    if (readBoundedFile(backup, maxBytes, candidate) &&
        validator(candidate, context)) {
        output.swap(candidate);
        return FileSource::Backup;
    }
    return FileSource::None;
}

inline WriteStatus writeAtomically(
    const char* temporary, const char* destination, const char* backup,
    const std::string& contents, CommitCallback commit, void* context,
    const char* tempCommitPhase, const char* publishCommitPhase,
    bool commitBeforePublish = true) {
    if (!temporary || !temporary[0] || !destination || !destination[0] ||
        !backup || !backup[0] || !commit ||
        !tempCommitPhase || !publishCommitPhase) {
        errno = EINVAL;
        return WriteStatus::InvalidArgument;
    }

    FILE* output = std::fopen(temporary, "wb");
    if (!output) return WriteStatus::OpenFailed;

    bool written = contents.empty() ||
        std::fwrite(contents.data(), 1, contents.size(), output) == contents.size();
    if (std::fflush(output) != 0) written = false;
    if (std::fclose(output) != 0) written = false;
    if (!written) {
        (void)std::remove(temporary);
        return WriteStatus::WriteFailed;
    }

    if (commitBeforePublish && !commit(context, tempCommitPhase)) {
        (void)std::remove(temporary);
        return WriteStatus::TempCommitFailed;
    }

    const auto replaced = safeio::replaceFilePreservingOnFailure(
        temporary, destination, backup, true);
    if (replaced == safeio::ReplaceStatus::RollbackFailed) {
        (void)std::remove(temporary);
        return WriteStatus::RollbackFailed;
    }
    if (replaced != safeio::ReplaceStatus::Success) {
        (void)std::remove(temporary);
        return WriteStatus::ReplaceFailed;
    }

    if (!commit(context, publishCommitPhase))
        return WriteStatus::PublishCommitFailed;
    return WriteStatus::Success;
}

} // namespace desknx::persist
