#pragma once

#include <cstdint>

namespace desknx::catalog {

// Match SLauncher/libnx's conservative NS page size.
constexpr std::int32_t ApplicationPageCapacity = 30;

struct ApplicationPageRequest {
    std::int32_t offset;
    std::int32_t capacity;
};

// nsListApplicationRecord's count pointer is an output parameter. Start at zero;
// the output-buffer capacity is supplied separately as the second argument.
constexpr ApplicationPageRequest MakeApplicationPageRequest(std::int32_t offset) {
    return {offset, ApplicationPageCapacity};
}

constexpr bool IsValidApplicationPageCount(std::int32_t requested,
                                           std::int32_t returned) {
    return requested > 0 && returned >= 0 && returned <= requested;
}

constexpr std::int32_t NextApplicationPageOffset(std::int32_t offset,
                                                  std::int32_t returned) {
    return offset + returned;
}

} // namespace desknx::catalog
