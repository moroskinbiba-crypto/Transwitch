#pragma once

#include <SDL2/SDL.h>

#include <cstdint>

namespace desknx::ui {

// Composes the modal scrim directly on the launcher's logical window target.
// It never switches render targets, changes the viewport or scale, or allocates
// textures. Only draw color/blend and the temporary clip are saved and restored.
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

    // Called after the desktop and before modal contents. The SDL logical-size
    // mapping remains untouched: the full scrim is expressed in the same 1280x720
    // coordinates as all of the launcher's other drawing calls.
    bool compose(std::uint8_t dimAlpha = 174) noexcept {
        if (!renderer_ || !active_) return true;

        SDL_BlendMode savedBlendMode = SDL_BLENDMODE_BLEND;
        Uint8 savedR = 0, savedG = 0, savedB = 0, savedA = 255;
        SDL_Rect savedClip{};
        if (SDL_GetRenderDrawBlendMode(renderer_, &savedBlendMode) != 0 ||
            SDL_GetRenderDrawColor(renderer_, &savedR, &savedG, &savedB, &savedA) != 0)
            return false;

        const bool savedClipEnabled = SDL_RenderIsClipEnabled(renderer_) == SDL_TRUE;
        SDL_RenderGetClipRect(renderer_, &savedClip);

        bool ok = SDL_RenderSetClipRect(renderer_, nullptr) == 0;
        if (ok) ok = SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND) == 0;
        if (ok) ok = SDL_SetRenderDrawColor(renderer_, 3, 8, 15, dimAlpha) == 0;
        if (ok) {
            const SDL_Rect full{0, 0, width_, height_};
            ok = SDL_RenderFillRect(renderer_, &full) == 0;
        }

        // Restore in reverse order even when the fill fails. Do not reset the
        // viewport or scale here: with SDL_RenderSetLogicalSize active, those
        // values are renderer-owned and changing them can desynchronize later
        // modal/card draws on Switch renderers.
        const bool colorRestored =
            SDL_SetRenderDrawColor(renderer_, savedR, savedG, savedB, savedA) == 0;
        const bool blendRestored =
            SDL_SetRenderDrawBlendMode(renderer_, savedBlendMode) == 0;
        const bool clipRestored =
            SDL_RenderSetClipRect(renderer_, savedClipEnabled ? &savedClip : nullptr) == 0;
        return ok && colorRestored && blendRestored && clipRestored;
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
