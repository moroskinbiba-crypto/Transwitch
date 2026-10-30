#pragma once

#include <cstdint>

namespace desknx::catalog {

constexpr std::int32_t ApplicationPageCapacity = 128;

struct ApplicationPageRequest {
    std::int32_t offset;
    std::int32_t capacity;
};

// nsListApplicationRecord uses its count pointer as an input/output value:
// initialize it to the output-buffer capacity before every request.
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
