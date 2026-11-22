#pragma once

#include <SDL2/SDL.h>

namespace desknx::ui {

// Results from restoring the renderer to the full-frame composition baseline.
// A zero SDL result means the corresponding state was restored successfully.
struct RendererRecoveryResult {
    bool hadActiveTarget{false};
    int renderTargetResult{0};
    int clipResult{0};
    int blendResult{0};

    constexpr bool recovered() const noexcept {
        return renderTargetResult == 0 && clipResult == 0 && blendResult == 0;
    }
};

// Call at frame boundaries, after window composition and before Present.
// The order is intentional: leave an off-screen target before removing its
// clip, then establish the draw blend mode expected by the normal UI renderer.
inline RendererRecoveryResult RestoreRendererFrameBaseline(
    SDL_Renderer* renderer) noexcept {
    RendererRecoveryResult result{};
    if (!renderer) {
        result.renderTargetResult = -1;
        result.clipResult = -1;
        result.blendResult = -1;
        return result;
    }

    if (SDL_GetRenderTarget(renderer) != nullptr) {
        result.hadActiveTarget = true;
        result.renderTargetResult = SDL_SetRenderTarget(renderer, nullptr);
    }
    result.clipResult = SDL_RenderSetClipRect(renderer, nullptr);
    result.blendResult = SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return result;
}

} // namespace desknx::ui
