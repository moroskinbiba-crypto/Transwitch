#pragma once

#include <SDL2/SDL.h>

#include <cstdint>

namespace desknx::ui {

// Draws a lightweight translucent scrim between the desktop and modal content.
// It deliberately avoids SDL render-target textures: those are inconsistently
// supported by Switch SDL renderers and are unnecessary for modal correctness.
class ModalLayer {
public:
    ModalLayer() = default;
    ModalLayer(const ModalLayer&) = delete;
    ModalLayer& operator=(const ModalLayer&) = delete;

    bool beginFrame(SDL_Renderer* renderer, bool active, int width, int height) noexcept {
        renderer_ = renderer;
        active_ = active && renderer && width > 0 && height > 0;
        width_ = width;
        height_ = height;
        return active_;
    }

    // Called after the desktop and before modal contents. The renderer remains
    // on its normal window target for the entire frame; only draw state changes.
    void compose(std::uint8_t dimAlpha = 174) noexcept {
        if (!renderer_ || !active_) return;

        SDL_BlendMode savedBlendMode = SDL_BLENDMODE_BLEND;
        Uint8 savedR = 0, savedG = 0, savedB = 0, savedA = 255;
        SDL_Rect savedViewport{};
        SDL_Rect savedClip{};
        float savedScaleX = 1.0f, savedScaleY = 1.0f;

        (void)SDL_GetRenderDrawBlendMode(renderer_, &savedBlendMode);
        (void)SDL_GetRenderDrawColor(renderer_, &savedR, &savedG, &savedB, &savedA);
        SDL_RenderGetViewport(renderer_, &savedViewport);
        SDL_RenderGetScale(renderer_, &savedScaleX, &savedScaleY);
        const bool savedClipEnabled = SDL_RenderIsClipEnabled(renderer_) == SDL_TRUE;
        SDL_RenderGetClipRect(renderer_, &savedClip);

        // Use physical renderer coordinates so the scrim always covers the
        // entire display, independent of a control's temporary clip or scale.
        (void)SDL_RenderSetScale(renderer_, 1.0f, 1.0f);
        (void)SDL_RenderSetViewport(renderer_, nullptr);
        (void)SDL_RenderSetClipRect(renderer_, nullptr);
        (void)SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        (void)SDL_SetRenderDrawColor(renderer_, 3, 8, 15, dimAlpha);
        const SDL_Rect full{0, 0, width_, height_};
        (void)SDL_RenderFillRect(renderer_, &full);

        // Restore the exact state so the modal's own glass cards and text keep
        // their normal alpha blending and clipping behavior.
        (void)SDL_RenderSetScale(renderer_, savedScaleX, savedScaleY);
        (void)SDL_RenderSetViewport(renderer_, &savedViewport);
        (void)SDL_RenderSetClipRect(renderer_, savedClipEnabled ? &savedClip : nullptr);
        (void)SDL_SetRenderDrawColor(renderer_, savedR, savedG, savedB, savedA);
        (void)SDL_SetRenderDrawBlendMode(renderer_, savedBlendMode);
    }

    void shutdown() noexcept {
        renderer_ = nullptr;
        active_ = false;
        width_ = height_ = 0;
    }

    // Kept for diagnostics/API compatibility. The Switch-safe path does not
    // allocate or rely on a blur render target.
    bool blurAvailable() const noexcept { return false; }

private:
    SDL_Renderer* renderer_{nullptr};
    int width_{0};
    int height_{0};
    bool active_{false};
};

} // namespace desknx::ui
