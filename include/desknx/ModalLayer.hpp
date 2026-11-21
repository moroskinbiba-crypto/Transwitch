#pragma once

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>

namespace desknx::ui {

class ModalLayer {
public:
    ModalLayer() = default;
    ModalLayer(const ModalLayer&) = delete;
    ModalLayer& operator=(const ModalLayer&) = delete;

    // Capture only while a surface is open. Normal desktop frames are rendered
    // directly and pay no render-target or copy cost.
    bool beginFrame(SDL_Renderer* renderer, bool active, int width, int height) {
        renderer_ = renderer;
        captured_ = false;
        active_ = active;
        width_ = width;
        height_ = height;
        rendererStateSaved_ = false;
        if (!active || !renderer || width <= 0 || height <= 0) return false;

        saveRendererState();
        if (!SDL_RenderTargetSupported(renderer) || !ensureTextures()) return false;

        SDL_RenderSetClipRect(renderer, nullptr);
        if (SDL_SetRenderTarget(renderer, base_) != 0) {
            SDL_SetRenderTarget(renderer, nullptr);
            restoreRendererState();
            return false;
        }

        // RenderTarget textures must be cleared opaquely, but the normal UI
        // uses alpha blending for its glass cards, shadows, icons and text.
        // Restore the caller's state immediately after the clear so the whole
        // desktop is captured exactly as it would be on the window surface.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        restoreCanvasState();
        captured_ = true;
        return true;
    }

    // Call once after the desktop has been drawn and before any modal contents.
    // A small multi-sample downscaled pass approximates a frosted backdrop;
    // unsupported render-target devices still receive the same dim layer.
    void compose(std::uint8_t dimAlpha = 174) {
        if (!renderer_ || !active_) return;

        if (captured_ && blur_ && SDL_SetRenderTarget(renderer_, blur_) == 0) {
            SDL_RenderSetClipRect(renderer_, nullptr);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer_, 8, 14, 22, 255);
            SDL_RenderClear(renderer_);

            SDL_SetTextureBlendMode(base_, SDL_BLENDMODE_BLEND);
            static constexpr int offsets[][2] = {
                {-2, 0}, {2, 0}, {0, -2}, {0, 2},
                {-1, -1}, {1, 1}, {-1, 1}, {1, -1}
            };
            SDL_SetTextureAlphaMod(base_, 28);
            for (const auto& offset : offsets) {
                SDL_Rect dst{offset[0], offset[1], BlurWidth, BlurHeight};
                SDL_RenderCopy(renderer_, base_, nullptr, &dst);
            }
            SDL_SetTextureAlphaMod(base_, 255);

            SDL_SetRenderTarget(renderer_, nullptr);
            resetCanvasState();
            SDL_SetTextureBlendMode(blur_, SDL_BLENDMODE_NONE);
            SDL_Rect full{0, 0, width_, height_};
            SDL_RenderCopy(renderer_, blur_, nullptr, &full);
            SDL_SetTextureBlendMode(blur_, SDL_BLENDMODE_BLEND);
        } else {
            SDL_SetRenderTarget(renderer_, nullptr);
            resetCanvasState();
        }

        // The dim layer is translucent. Keep blending enabled for it and
        // restore the exact pre-modal renderer state afterwards; leaving NONE
        // here made every subsequent glass surface render as an opaque block.
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 3, 8, 15, dimAlpha);
        SDL_Rect full{0, 0, width_, height_};
        SDL_RenderFillRect(renderer_, &full);
        restoreRendererState();
        captured_ = false;
    }

    void shutdown() {
        if (renderer_) SDL_SetRenderTarget(renderer_, nullptr);
        if (blur_) SDL_DestroyTexture(blur_);
        if (base_) SDL_DestroyTexture(base_);
        blur_ = nullptr;
        base_ = nullptr;
        renderer_ = nullptr;
        captured_ = false;
        active_ = false;
        rendererStateSaved_ = false;
        width_ = height_ = 0;
    }

    bool blurAvailable() const { return captured_ || blur_ != nullptr; }

private:
    static constexpr int BlurWidth = 320;
    static constexpr int BlurHeight = 180;

    void saveRendererState() {
        if (!renderer_) return;
        (void)SDL_GetRenderDrawBlendMode(renderer_, &savedBlendMode_);
        (void)SDL_GetRenderDrawColor(renderer_, &savedDrawR_, &savedDrawG_,
                                     &savedDrawB_, &savedDrawA_);
        SDL_RenderGetViewport(renderer_, &savedViewport_);
        SDL_RenderGetScale(renderer_, &savedScaleX_, &savedScaleY_);
        savedClipEnabled_ = SDL_RenderIsClipEnabled(renderer_) == SDL_TRUE;
        SDL_RenderGetClipRect(renderer_, &savedClip_);
        rendererStateSaved_ = true;
    }

    // Restore state while drawing into the full-size capture texture. Its
    // dimensions match the window, so the original viewport/scale/clip remain
    // valid and desktop widgets retain their normal coordinate system.
    void restoreCanvasState() {
        if (!renderer_ || !rendererStateSaved_) return;
        SDL_RenderSetScale(renderer_, savedScaleX_, savedScaleY_);
        SDL_RenderSetViewport(renderer_, &savedViewport_);
        SDL_RenderSetClipRect(renderer_, savedClipEnabled_ ? &savedClip_ : nullptr);
        SDL_SetRenderDrawColor(renderer_, savedDrawR_, savedDrawG_, savedDrawB_, savedDrawA_);
        SDL_SetRenderDrawBlendMode(renderer_, savedBlendMode_);
    }

    // Clear all window-target clipping/scaling for the full-screen blur copy
    // and dim pass. restoreRendererState() is called once the composition ends.
    void resetCanvasState() {
        if (!renderer_) return;
        SDL_RenderSetScale(renderer_, 1.0f, 1.0f);
        SDL_RenderSetViewport(renderer_, nullptr);
        SDL_RenderSetClipRect(renderer_, nullptr);
    }

    void restoreRendererState() {
        if (!renderer_ || !rendererStateSaved_) return;
        SDL_RenderSetScale(renderer_, savedScaleX_, savedScaleY_);
        SDL_RenderSetViewport(renderer_, &savedViewport_);
        SDL_RenderSetClipRect(renderer_, savedClipEnabled_ ? &savedClip_ : nullptr);
        SDL_SetRenderDrawColor(renderer_, savedDrawR_, savedDrawG_, savedDrawB_, savedDrawA_);
        SDL_SetRenderDrawBlendMode(renderer_, savedBlendMode_);
        rendererStateSaved_ = false;
    }

    bool ensureTextures() {
        if (!renderer_) return false;
        if (base_ && baseWidth_ == width_ && baseHeight_ == height_ && blur_)
            return true;
        if (blur_) SDL_DestroyTexture(blur_);
        if (base_) SDL_DestroyTexture(base_);
        blur_ = nullptr;
        base_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET, width_, height_);
        blur_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET, BlurWidth, BlurHeight);
        if (!base_ || !blur_) {
            if (base_) SDL_DestroyTexture(base_);
            if (blur_) SDL_DestroyTexture(blur_);
            base_ = blur_ = nullptr;
            baseWidth_ = baseHeight_ = 0;
            return false;
        }
        SDL_SetTextureBlendMode(base_, SDL_BLENDMODE_NONE);
        SDL_SetTextureBlendMode(blur_, SDL_BLENDMODE_BLEND);
        baseWidth_ = width_;
        baseHeight_ = height_;
        return true;
    }

    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* base_{nullptr};
    SDL_Texture* blur_{nullptr};
    SDL_Rect savedViewport_{};
    SDL_Rect savedClip_{};
    SDL_BlendMode savedBlendMode_{SDL_BLENDMODE_BLEND};
    Uint8 savedDrawR_{0}, savedDrawG_{0}, savedDrawB_{0}, savedDrawA_{255};
    float savedScaleX_{1.0f}, savedScaleY_{1.0f};
    int width_{0};
    int height_{0};
    int baseWidth_{0};
    int baseHeight_{0};
    bool active_{false};
    bool captured_{false};
    bool rendererStateSaved_{false};
    bool savedClipEnabled_{false};
};

} // namespace desknx::ui
