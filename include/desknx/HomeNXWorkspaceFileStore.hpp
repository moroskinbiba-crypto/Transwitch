#pragma once

#include "HomeNXWorkspaceProvider.hpp"
#include "WorkspaceStorage.hpp"

#include <functional>
#include <string>
#include <utility>

namespace desknx::homenx {

using WorkspaceFilePaths = workspace_storage::Paths;

struct WorkspaceLoadInfo {
    bool restoredLayoutBackup{false};
    bool restoredActiveBackup{false};
    bool usedActiveSidecar{false};
};

class WorkspaceFileStore final {
public:
    using CommitCallback = std::function<bool()>;

    explicit WorkspaceFileStore(WorkspaceFilePaths paths = {})
        : m_paths(std::move(paths)) {}

    const WorkspaceFilePaths& paths() const { return m_paths; }

    bool load(WorkspaceProvider& provider, WorkspaceLoadInfo* info = nullptr) const {
        workspace_storage::Snapshot snapshot{};
        if (!workspace_storage::loadSnapshot(m_paths, snapshot))
            return false;

        if (!provider.load(snapshot.parsed.workspaces,
                           snapshot.parsed.activeWorkspace))
            return false;

        if (info) {
            WorkspaceLoadInfo loaded{};
            loaded.restoredLayoutBackup =
                snapshot.layoutSource == persist::FileSource::Backup;
            loaded.restoredActiveBackup =
                snapshot.activeSource == persist::FileSource::Backup;
            loaded.usedActiveSidecar =
                snapshot.activeSource != persist::FileSource::None;
            *info = loaded;
        }
        return true;
    }

    bool save(const WorkspaceProvider& provider,
              const CommitCallback& commit = {}) const {
        struct CommitContext {
            const CommitCallback* callback;
        } context{&commit};

        const auto commitBridge = [](void* opaque, const char*) -> bool {
            auto* current = static_cast<CommitContext*>(opaque);
            return current && current->callback &&
                   (!*current->callback || (*current->callback)());
        };

        const auto result = workspace_storage::writeSnapshot(
            m_paths, provider.workspaces(), provider.activeIndex(),
            commitBridge, &context, "workspace-temp", "workspace-publish");
        return result.success();
    }

    bool saveActiveState(const WorkspaceProvider& provider,
                         const CommitCallback& commit = {}) const {
        struct CommitContext {
            const CommitCallback* callback;
        } context{&commit};

        const auto commitBridge = [](void* opaque, const char*) -> bool {
            auto* current = static_cast<CommitContext*>(opaque);
            return current && current->callback &&
                   (!*current->callback || (*current->callback)());
        };

        return workspace_storage::writeActiveWorkspace(
            m_paths, provider.workspaces(), provider.activeIndex(),
            commitBridge, &context, "workspace-marker-temp",
            "workspace-marker-publish", true) == persist::WriteStatus::Success;
    }

private:
    WorkspaceFilePaths m_paths;
};

} // namespace desknx::homenx
