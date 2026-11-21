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
        if (!active || !renderer || width <= 0 || height <= 0) return false;
        if (!SDL_RenderTargetSupported(renderer) || !ensureTextures()) return false;

        SDL_RenderSetClipRect(renderer, nullptr);
        if (SDL_SetRenderTarget(renderer, base_) != 0) {
            SDL_SetRenderTarget(renderer, nullptr);
            return false;
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        captured_ = true;
        return true;
    }

    // Call once after the desktop has been drawn and before any modal contents.
    // A small multi-sample downscaled pass approximates a frosted backdrop;
    // unsupported render-target devices still receive the same dim layer.
    void compose(std::uint8_t dimAlpha = 174) {
        if (!renderer_ || !active_) return;
        if (captured_ && blur_) {
            if (SDL_SetRenderTarget(renderer_, blur_) == 0) {
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
                    SDL_Rect dst{
                        offset[0], offset[1],
                        BlurWidth, BlurHeight
                    };
                    SDL_RenderCopy(renderer_, base_, nullptr, &dst);
                }
                SDL_SetTextureAlphaMod(base_, 255);
                SDL_SetRenderTarget(renderer_, nullptr);
                SDL_RenderSetClipRect(renderer_, nullptr);
                SDL_SetTextureBlendMode(blur_, SDL_BLENDMODE_NONE);
                SDL_Rect full{0, 0, width_, height_};
                SDL_RenderCopy(renderer_, blur_, nullptr, &full);
                SDL_SetTextureBlendMode(blur_, SDL_BLENDMODE_BLEND);
            } else {
                SDL_SetRenderTarget(renderer_, nullptr);
            }
        } else {
            SDL_SetRenderTarget(renderer_, nullptr);
        }

        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 3, 8, 15, dimAlpha);
        SDL_Rect full{0, 0, width_, height_};
        SDL_RenderFillRect(renderer_, &full);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
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
        width_ = height_ = 0;
    }

    bool blurAvailable() const { return captured_ || blur_ != nullptr; }

private:
    static constexpr int BlurWidth = 320;
    static constexpr int BlurHeight = 180;

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
    int width_{0};
    int height_{0};
    int baseWidth_{0};
    int baseHeight_{0};
    bool active_{false};
    bool captured_{false};
};

} // namespace desknx::ui
