#pragma once

#include "../include/desknx/Model.hpp"
#include <vector>

namespace desknx::catalog {

struct ScanResult {
    std::vector<desknx::model::Candidate> items;
    bool truncated{false};
    bool fromCache{false};
};

ScanResult Scan();

} // namespace desknx::catalog
