#pragma once

#include <cstdint>

namespace desknx::ui {

inline constexpr std::uint8_t MaxTextTextureBuildsPerFrame=12;

// Counts attempts, not just successful uploads, so repeated SDL failures cannot
// turn a single frame into an unbounded retry loop.
class TextTextureFrameBudget {
public:
    constexpr explicit TextTextureFrameBudget(
        std::uint8_t limit=MaxTextTextureBuildsPerFrame) noexcept
        : limit_(limit) {}

    constexpr void beginFrame() noexcept {
        used_=0;
    }

    constexpr bool tryConsume() noexcept {
        if(used_>=limit_)return false;
        ++used_;
        return true;
    }

    constexpr std::uint8_t used() const noexcept { return used_; }
    constexpr std::uint8_t remaining() const noexcept {
        return used_>=limit_?0:static_cast<std::uint8_t>(limit_-used_);
    }
    constexpr std::uint8_t limit() const noexcept { return limit_; }

private:
    std::uint8_t limit_{MaxTextTextureBuildsPerFrame};
    std::uint8_t used_{0};
};

} // namespace desknx::ui
