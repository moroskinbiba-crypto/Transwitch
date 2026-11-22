#pragma once

#include <desknx/SafeReplace.hpp>
#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace desknx::persist {

enum class FileSource { None, Primary, Backup };
enum class WriteStatus {
    Success, InvalidArgument, OpenFailed, WriteFailed, TempCommitFailed,
    ReplaceFailed, RollbackFailed, PublishCommitFailed
};
using Validator = bool (*)(const std::string&, void*);
using CommitCallback = bool (*)(void*, const char*);
using TemporaryWriteCallback = WriteStatus (*)(void*, const char*, const std::string&);

inline bool readBoundedFile(const char* path, std::size_t maxBytes,
                            std::string& output) {
    if (!path || !path[0] || maxBytes == 0) { errno = EINVAL; return false; }
    struct stat st{};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        static_cast<unsigned long long>(st.st_size) > maxBytes) return false;
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
    if (readBoundedFile(primary, maxBytes, candidate) && validator(candidate, context)) {
        output.swap(candidate);
        return FileSource::Primary;
    }
    candidate.clear();
    if (readBoundedFile(backup, maxBytes, candidate) && validator(candidate, context)) {
        output.swap(candidate);
        return FileSource::Backup;
    }
    return FileSource::None;
}

inline WriteStatus writeTemporaryFile(void*, const char* path,
                                      const std::string& contents) {
    FILE* output = std::fopen(path, "wb");
    if (!output) return WriteStatus::OpenFailed;
    bool written = contents.empty() ||
        std::fwrite(contents.data(), 1, contents.size(), output) == contents.size();
    if (std::fflush(output) != 0) written = false;
    if (std::fclose(output) != 0) written = false;
    return written ? WriteStatus::Success : WriteStatus::WriteFailed;
}


// Called only after the authoritative layout file has been durably published.
// Its active-workspace marker is now redundant: leaving a previous marker
// backup would let startup restore an older desk over the committed layout.
inline bool discardFallbackPairAfterAuthoritativeCommit(
    const char* primary, const char* backup) {
    if(!primary||!primary[0]||!backup||!backup[0]||
       std::string(primary)==std::string(backup)) {
        errno=EINVAL;
        return false;
    }
    bool primaryOk=true;
    bool backupOk=true;
    if(std::remove(primary)!=0&&errno!=ENOENT)primaryOk=false;
    if(std::remove(backup)!=0&&errno!=ENOENT)backupOk=false;
    return primaryOk&&backupOk;
}

inline WriteStatus writeAtomically(
    const char* temporary, const char* destination, const char* backup,
    const std::string& contents, CommitCallback commit, void* context,
    const char* tempCommitPhase, const char* publishCommitPhase,
    bool commitBeforePublish = true, bool keepBackupOnSuccess = true,
    const safeio::ReplaceOperations* replaceOperations = nullptr,
    TemporaryWriteCallback writeTemporary = writeTemporaryFile,
    void* writeContext = nullptr) {
    if (!temporary || !temporary[0] || !destination || !destination[0] ||
        !backup || !backup[0] || !commit || !tempCommitPhase || !publishCommitPhase ||
        !writeTemporary) {
        errno = EINVAL;
        return WriteStatus::InvalidArgument;
    }

    const WriteStatus written = writeTemporary(writeContext, temporary, contents);
    if (written != WriteStatus::Success) {
        (void)std::remove(temporary);
        return written;
    }
    if (commitBeforePublish && !commit(context, tempCommitPhase)) {
        (void)std::remove(temporary);
        return WriteStatus::TempCommitFailed;
    }

    const safeio::ReplaceStatus replaced = replaceOperations
        ? safeio::replaceFilePreservingOnFailureWithOps(
              temporary, destination, backup, *replaceOperations, keepBackupOnSuccess)
        : safeio::replaceFilePreservingOnFailure(
              temporary, destination, backup, keepBackupOnSuccess);
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
