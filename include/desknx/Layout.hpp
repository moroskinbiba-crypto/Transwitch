#pragma once

#include "Model.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace desknx::layout {

struct LoadResult {
    std::vector<model::Workspace> workspaces;
    int activeWorkspace{0};
    bool needsMigration{false};
    size_t recoveredTiles{0};
    size_t droppedTiles{0};
};

bool parse(const std::string& input, LoadResult& result);
std::string serialize(const std::vector<model::Workspace>& workspaces, int activeWorkspace);
std::string serializeActiveWorkspaceState(const std::vector<model::Workspace>& workspaces, int activeWorkspace);
// Fast marker builder for callers that already have a validated layout shape hash.
// It avoids walking/copying the complete workspace model for an active-desk switch.
std::string serializeActiveWorkspaceState(int activeWorkspace,
                                          std::size_t workspaceCount,
                                          std::uint64_t shapeHash,
                                          const std::string& activeWorkspaceName);
bool parseActiveWorkspaceState(const std::string& input, const std::vector<model::Workspace>& workspaces, int& activeWorkspace);

} // namespace desknx::layout
