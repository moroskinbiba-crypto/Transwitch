#pragma once

#include "Model.hpp"
#include <string>

namespace desknx::layout {

struct LoadResult {
    std::vector<model::Workspace> workspaces;
    int activeWorkspace{0};
    bool needsMigration{false};
};

bool parse(const std::string& input, LoadResult& result);
std::string serialize(const std::vector<model::Workspace>& workspaces, int activeWorkspace);

} // namespace desknx::layout
