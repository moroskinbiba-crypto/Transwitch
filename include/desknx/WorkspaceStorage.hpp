#pragma once

#include "Layout.hpp"
#include "ProductionPersistence.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace desknx::workspace_storage {

// One path contract for the standalone launcher and the optional HOME applet.
// The on-disk DESKNX1..DESKNX4 formats remain owned by layout::parse/serialize.
struct Paths {
    std::string directory{"sdmc:/switch/DeskNX"};
    std::string layout{"sdmc:/switch/DeskNX/layout.tsv"};
    std::string layoutTemp{"sdmc:/switch/DeskNX/layout.tmp"};
    std::string layoutBackup{"sdmc:/switch/DeskNX/layout.bak"};
    std::string active{"sdmc:/switch/DeskNX/active-workspace.dat"};
    std::string activeTemp{"sdmc:/switch/DeskNX/active-workspace.tmp"};
    std::string activeBackup{"sdmc:/switch/DeskNX/active-workspace.bak"};

    static Paths under(const std::string& root) {
        Paths paths;
        paths.directory = root;
        const std::string slash = !root.empty() && root.back() == '/' ? "" : "/";
        paths.layout = root + slash + "layout.tsv";
        paths.layoutTemp = root + slash + "layout.tmp";
        paths.layoutBackup = root + slash + "layout.bak";
        paths.active = root + slash + "active-workspace.dat";
        paths.activeTemp = root + slash + "active-workspace.tmp";
        paths.activeBackup = root + slash + "active-workspace.bak";
        return paths;
    }
};

struct Snapshot {
    layout::LoadResult parsed{};
    persist::FileSource layoutSource{persist::FileSource::None};
    persist::FileSource activeSource{persist::FileSource::None};
};

struct SnapshotWriteResult {
    persist::WriteStatus status{persist::WriteStatus::InvalidArgument};
    bool activeMarkerPublished{false};
    bool layoutPublished{false};

    bool success() const noexcept {
        return status == persist::WriteStatus::Success &&
               activeMarkerPublished && layoutPublished;
    }
};

namespace detail {

struct LayoutValidation {
    layout::LoadResult parsed{};
};

inline bool validateLayout(const std::string& bytes, void* opaque) {
    auto* context = static_cast<LayoutValidation*>(opaque);
    return context && layout::parse(bytes, context->parsed);
}

struct ActiveValidation {
    const std::vector<model::Workspace>* workspaces{nullptr};
    int index{0};
};

inline bool validateActive(const std::string& bytes, void* opaque) {
    auto* context = static_cast<ActiveValidation*>(opaque);
    if (!context || !context->workspaces)
        return false;
    int candidate = context->index;
    if (!layout::parseActiveWorkspaceState(bytes, *context->workspaces, candidate))
        return false;
    context->index = candidate;
    return true;
}

inline bool commitNothing(void*, const char*) {
    return true;
}

inline void removeTemps(const Paths& paths) {
    if (!paths.layoutTemp.empty())
        (void)std::remove(paths.layoutTemp.c_str());
    if (!paths.activeTemp.empty())
        (void)std::remove(paths.activeTemp.c_str());
}

inline safeio::ReplaceStatus replace(
    const std::string& temporary, const std::string& destination,
    const std::string& backup, const safeio::ReplaceOperations* operations) {
    if (operations) {
        return safeio::replaceFilePreservingOnFailureWithOps(
            temporary.c_str(), destination.c_str(), backup.c_str(),
            *operations, true);
    }
    return safeio::replaceFilePreservingOnFailure(
        temporary.c_str(), destination.c_str(), backup.c_str(), true);
}

inline persist::WriteStatus mapReplaceStatus(safeio::ReplaceStatus status) {
    if (status == safeio::ReplaceStatus::Success)
        return persist::WriteStatus::Success;
    if (status == safeio::ReplaceStatus::RollbackFailed)
        return persist::WriteStatus::RollbackFailed;
    return persist::WriteStatus::ReplaceFailed;
}

} // namespace detail

// Read a single explicitly selected layout file. Used by startup recovery so
// the caller can preserve a corrupt primary before promoting a valid backup.
inline bool readLayoutFile(const char* path, layout::LoadResult& output) {
    std::string bytes;
    if (!persist::readBoundedFile(path, 1024u * 1024u, bytes))
        return false;
    layout::LoadResult parsed{};
    if (!layout::parse(bytes, parsed))
        return false;
    output = std::move(parsed);
    return true;
}

inline persist::FileSource readLayoutWithBackup(
    const Paths& paths, layout::LoadResult& output) {
    detail::LayoutValidation validation{};
    std::string bytes;
    const auto source = persist::readValidatedWithBackup(
        paths.layout.c_str(), paths.layoutBackup.c_str(), 1024u * 1024u,
        detail::validateLayout, &validation, bytes);
    if (source != persist::FileSource::None)
        output = std::move(validation.parsed);
    return source;
}

inline persist::FileSource readActiveWorkspace(
    const Paths& paths, const std::vector<model::Workspace>& workspaces,
    int& activeWorkspace) {
    detail::ActiveValidation validation{&workspaces, activeWorkspace};
    std::string bytes;
    const auto source = persist::readValidatedWithBackup(
        paths.active.c_str(), paths.activeBackup.c_str(), 512,
        detail::validateActive, &validation, bytes);
    if (source != persist::FileSource::None)
        activeWorkspace = validation.index;
    return source;
}

// Load the layout first, then apply only an active marker whose shape hash and
// workspace name match that exact layout generation. This prevents a newer
// sidecar from selecting a desk in an older backup (or vice versa).
inline bool loadSnapshot(const Paths& paths, Snapshot& output) {
    Snapshot candidate{};
    candidate.layoutSource = readLayoutWithBackup(paths, candidate.parsed);
    if (candidate.layoutSource == persist::FileSource::None)
        return false;

    int active = candidate.parsed.activeWorkspace;
    candidate.activeSource = readActiveWorkspace(
        paths, candidate.parsed.workspaces, active);
    if (candidate.activeSource != persist::FileSource::None)
        candidate.parsed.activeWorkspace = active;

    output = std::move(candidate);
    return true;
}

inline persist::WriteStatus writeActiveWorkspaceBytes(
    const Paths& paths, const std::string& markerBytes,
    persist::CommitCallback commit, void* context,
    const char* tempCommitPhase, const char* publishCommitPhase,
    bool commitBeforePublish = false, bool keepBackupOnSuccess = true,
    const safeio::ReplaceOperations* replaceOperations = nullptr,
    persist::TemporaryWriteCallback writeTemporary = persist::writeTemporaryFile,
    void* writeContext = nullptr) {
    if (markerBytes.empty())
        return persist::WriteStatus::InvalidArgument;
    if (!commit)
        commit = detail::commitNothing;
    return persist::writeAtomically(
        paths.activeTemp.c_str(), paths.active.c_str(), paths.activeBackup.c_str(),
        markerBytes, commit, context, tempCommitPhase, publishCommitPhase,
        commitBeforePublish, keepBackupOnSuccess, replaceOperations,
        writeTemporary, writeContext);
}

inline persist::WriteStatus writeActiveWorkspace(
    const Paths& paths, const std::vector<model::Workspace>& workspaces,
    int activeWorkspace, persist::CommitCallback commit, void* context,
    const char* tempCommitPhase, const char* publishCommitPhase,
    bool commitBeforePublish = false, bool keepBackupOnSuccess = true,
    const safeio::ReplaceOperations* replaceOperations = nullptr,
    persist::TemporaryWriteCallback writeTemporary = persist::writeTemporaryFile,
    void* writeContext = nullptr) {
    const std::string bytes =
        layout::serializeActiveWorkspaceState(workspaces, activeWorkspace);
    return writeActiveWorkspaceBytes(
        paths, bytes, commit, context, tempCommitPhase, publishCommitPhase,
        commitBeforePublish, keepBackupOnSuccess, replaceOperations,
        writeTemporary, writeContext);
}

// Publish a full workspace snapshot as a two-file generation. The active
// marker is published first and is bound to the new layout by its shape hash;
// the layout follows. A crash between the two renames therefore cannot apply
// the new marker to the previous layout: loadSnapshot falls back to the
// matching marker backup. Both temp files are committed before either rename,
// and the published pair is committed once more.
inline SnapshotWriteResult writeSnapshotBytes(
    const Paths& paths, const std::string& layoutBytes,
    const std::string& activeBytes, persist::CommitCallback commit,
    void* context, const char* tempCommitPhase,
    const char* publishCommitPhase,
    const safeio::ReplaceOperations* replaceOperations = nullptr,
    persist::TemporaryWriteCallback writeTemporary = persist::writeTemporaryFile,
    void* writeContext = nullptr) {
    SnapshotWriteResult result{};
    if (paths.layoutTemp.empty() || paths.layout.empty() ||
        paths.layoutBackup.empty() || paths.activeTemp.empty() ||
        paths.active.empty() || paths.activeBackup.empty() ||
        !tempCommitPhase || !publishCommitPhase || !writeTemporary ||
        layoutBytes.empty() || activeBytes.empty()) {
        result.status = persist::WriteStatus::InvalidArgument;
        return result;
    }
    if (!commit)
        commit = detail::commitNothing;

    const auto layoutWrite =
        writeTemporary(writeContext, paths.layoutTemp.c_str(), layoutBytes);
    if (layoutWrite != persist::WriteStatus::Success) {
        detail::removeTemps(paths);
        result.status = layoutWrite;
        return result;
    }

    const auto activeWrite =
        writeTemporary(writeContext, paths.activeTemp.c_str(), activeBytes);
    if (activeWrite != persist::WriteStatus::Success) {
        detail::removeTemps(paths);
        result.status = activeWrite;
        return result;
    }

    if (!commit(context, tempCommitPhase)) {
        detail::removeTemps(paths);
        result.status = persist::WriteStatus::TempCommitFailed;
        return result;
    }

    const auto activeReplace = detail::replace(
        paths.activeTemp, paths.active, paths.activeBackup, replaceOperations);
    result.status = detail::mapReplaceStatus(activeReplace);
    if (result.status != persist::WriteStatus::Success) {
        detail::removeTemps(paths);
        return result;
    }
    result.activeMarkerPublished = true;

    const auto layoutReplace = detail::replace(
        paths.layoutTemp, paths.layout, paths.layoutBackup, replaceOperations);
    result.status = detail::mapReplaceStatus(layoutReplace);
    if (result.status != persist::WriteStatus::Success) {
        detail::removeTemps(paths);
        return result;
    }
    result.layoutPublished = true;

    if (!commit(context, publishCommitPhase)) {
        result.status = persist::WriteStatus::PublishCommitFailed;
        return result;
    }

    result.status = persist::WriteStatus::Success;
    return result;
}

inline SnapshotWriteResult writeSnapshot(
    const Paths& paths, const std::vector<model::Workspace>& workspaces,
    int activeWorkspace, persist::CommitCallback commit, void* context,
    const char* tempCommitPhase, const char* publishCommitPhase,
    const safeio::ReplaceOperations* replaceOperations = nullptr,
    persist::TemporaryWriteCallback writeTemporary = persist::writeTemporaryFile,
    void* writeContext = nullptr) {
    if (workspaces.empty() || activeWorkspace < 0 ||
        activeWorkspace >= static_cast<int>(workspaces.size())) {
        SnapshotWriteResult invalid{};
        invalid.status = persist::WriteStatus::InvalidArgument;
        return invalid;
    }

    const std::string layoutBytes = layout::serialize(workspaces, activeWorkspace);
    const std::string activeBytes =
        layout::serializeActiveWorkspaceState(workspaces, activeWorkspace);
    return writeSnapshotBytes(
        paths, layoutBytes, activeBytes, commit, context,
        tempCommitPhase, publishCommitPhase, replaceOperations,
        writeTemporary, writeContext);
}

} // namespace desknx::workspace_storage
