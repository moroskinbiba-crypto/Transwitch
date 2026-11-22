#pragma once

#include <cstdint>

namespace desknx::ui {

enum RendererStateViolation : std::uint8_t {
    RendererStateNone = 0,
    RendererStateRenderTargetActive = 1u << 0,
    RendererStateClipEnabled = 1u << 1,
    RendererStateBlendMismatch = 1u << 2
};

struct RendererStateSnapshot {
    bool renderTargetActive{false};
    bool clipEnabled{false};
    int drawBlendMode{0};
};

constexpr std::uint8_t RendererStateViolations(
    RendererStateSnapshot snapshot,int expectedBlendMode) noexcept {
    std::uint8_t violations=RendererStateNone;
    if(snapshot.renderTargetActive)
        violations|=RendererStateRenderTargetActive;
    if(snapshot.clipEnabled)
        violations|=RendererStateClipEnabled;
    if(snapshot.drawBlendMode!=expectedBlendMode)
        violations|=RendererStateBlendMismatch;
    return violations;
}

constexpr bool RendererStateIsBaseline(
    RendererStateSnapshot snapshot,int expectedBlendMode) noexcept {
    return RendererStateViolations(snapshot,expectedBlendMode)==RendererStateNone;
}

} // namespace desknx::ui
