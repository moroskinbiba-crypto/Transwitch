#pragma once

#include "../include/desknx/Model.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>

namespace desknx::catalog {

struct ScanResult {
    std::vector<desknx::model::Candidate> items;
    bool truncated{false};
    bool fromCache{false};
    bool complete{false};
    bool nsAvailable{false};
    std::uint32_t nsErrorCode{0};
    std::size_t gameCount{0};
    std::size_t nroCount{0};
};

using ProgressCallback = void (*)(const ScanResult& snapshot, void* context);
ScanResult Scan(ProgressCallback progress = nullptr, void* context = nullptr);

} // namespace desknx::catalog
