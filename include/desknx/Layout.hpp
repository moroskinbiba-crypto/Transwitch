#pragma once

#include "Model.hpp"
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
bool parseActiveWorkspaceState(const std::string& input, const std::vector<model::Workspace>& workspaces, int& activeWorkspace);

} // namespace desknx::layout
