#pragma once

#include "../include/desknx/Model.hpp"
#include <vector>

namespace desknx::catalog {

struct ScanResult {
    std::vector<desknx::model::Candidate> items;
    bool truncated{false};
    bool fromCache{false};
    bool complete{false};
};

using ProgressCallback = void (*)(const ScanResult& snapshot, void* context);
ScanResult Scan(ProgressCallback progress = nullptr, void* context = nullptr);

} // namespace desknx::catalog
