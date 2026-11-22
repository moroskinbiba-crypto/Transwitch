#pragma once

#include <SDL2/SDL.h>
#include <desknx/Artwork.hpp>
#include <desknx/TileVisualRules.hpp>
#include "VisualEngine.hpp"
#include "UiComponents.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace desknx::ui {

// Shared renderer for all artwork-backed tiles: workspace covers and library
// cards. It owns image fit/clipping, rounded artwork corners, label scrims,
// badges and the elevated/focused surface. Text shaping and hitboxes stay with
// the caller so this renderer never changes app identity or interaction rules.
class TileRenderer final {
public:
    void bind(desknx::visual::VisualEngine* engine,
              UiComponents* components,
              SDL_Renderer* renderer) noexcept {
        if (renderer_ && renderer_ != renderer) releaseMask();
        engine_ = engine;
        components_ = components;
        renderer_ = renderer;
    }

    void shutdown() noexcept {
        releaseMask();
        renderer_ = nullptr;
        engine_ = nullptr;
        components_ = nullptr;
    }

    bool artworkFrame(const SDL_Rect& bounds,
                      SDL_Color fill,
                      SDL_Color edge,
                      SDL_Color accent,
                      bool selected,
                      int surfaceOpacity = 255,
                      int radius = 10) {
        if (!valid(bounds)) return false;
        bool ok = true;
        if (selected && engine_ && engine_->ready()) {
            SDL_Color glow = accent;
            glow.a = scaleAlpha(glow.a, 62);
            const SDL_Rect glowBounds{
                bounds.x + bounds.w / 8,
                bounds.y - 6,
                std::max(24, bounds.w * 3 / 4),
                std::max(24, bounds.h / 2)
            };
            ok = engine_->drawRadialGlow(glowBounds, glow) && ok;
        }

        if (components_) {
            ok = components_->surface(bounds, fill, edge, radius, true,
                                      surfaceOpacity) && ok;
        } else if (renderer_) {
            ok = fillRounded(bounds, radius, fill) && ok;
            ok = outlineRounded(bounds, radius, edge) && ok;
        } else {
            return false;
        }
        return ok;
    }

    // Draw an already-decoded texture into the tile viewport. Cover/contain/
    // stretch retain the same pure fit policy as the launcher previously used.
    // The previous clip rectangle is restored exactly, including disabled state.
    bool artworkImage(SDL_Texture* texture,
                      const SDL_Rect& viewport,
                      desknx::artwork::FitMode fitMode,
                      SDL_Color backdrop,
                      int radius = 10) {
        if (!valid(viewport) || !renderer_) return false;
        bool ok = fillRounded(viewport, radius, backdrop);
        if (!texture) return false;

        int sourceWidth = 0, sourceHeight = 0;
        if (SDL_QueryTexture(texture, nullptr, nullptr, &sourceWidth, &sourceHeight) != 0 ||
            sourceWidth <= 0 || sourceHeight <= 0)
            return false;

        // Treat invalid persisted fit modes as Cover rather than generating an
        // empty destination. The parser normally clamps this, but old layouts
        // and hand-edited files must not blank the artwork.
        if (fitMode != desknx::artwork::FitMode::Cover &&
            fitMode != desknx::artwork::FitMode::Contain &&
            fitMode != desknx::artwork::FitMode::Stretch)
            fitMode = desknx::artwork::FitMode::Cover;
        const auto fit = desknx::artwork::destination(
            viewport.x, viewport.y, viewport.w, viewport.h,
            sourceWidth, sourceHeight, fitMode);
        if (fit.w <= 0 || fit.h <= 0) return false;

        // Contain keeps the whole image visible over a subdued backing surface.
        if (fitMode == desknx::artwork::FitMode::Contain) {
            SDL_Color inset = backdrop;
            inset.r = static_cast<Uint8>(inset.r / 3u);
            inset.g = static_cast<Uint8>(inset.g / 3u);
            inset.b = static_cast<Uint8>(inset.b / 3u);
            ok = fillRounded(viewport, radius, inset) && ok;
        }

        SDL_Rect savedClip{};
        const bool savedClipEnabled = SDL_RenderIsClipEnabled(renderer_) == SDL_TRUE;
        SDL_RenderGetClipRect(renderer_, &savedClip);
        if (SDL_RenderSetClipRect(renderer_, &viewport) != 0) return false;

        const SDL_Rect target{fit.x, fit.y, fit.w, fit.h};
        const bool copied = SDL_RenderCopy(renderer_, texture, nullptr, &target) == 0;
        const bool clipRestored =
            SDL_RenderSetClipRect(renderer_, savedClipEnabled ? &savedClip : nullptr) == 0;
        return ok && copied && clipRestored;
    }

    // Cover art is drawn with a rectangular SDL clip for portability. Four
    // cached alpha wedges restore the rounded silhouette afterwards.
    bool maskRoundedCorners(const SDL_Rect& bounds, int radius,
                            SDL_Color cornerFill) {
        if (!valid(bounds) || !renderer_ || radius <= 0) return true;
        const int safeRadius = std::max(0, std::min(
            radius, std::min(bounds.w, bounds.h) / 2));
        if (safeRadius == 0 || !ensureCornerMask(safeRadius)) return false;

        if (SDL_SetTextureColorMod(cornerMask_, cornerFill.r,
                                   cornerFill.g, cornerFill.b) != 0 ||
            SDL_SetTextureAlphaMod(cornerMask_, cornerFill.a) != 0) {
            resetCornerMaskModulation();
            return false;
        }

        const SDL_Rect corners[] = {
            {bounds.x, bounds.y, safeRadius, safeRadius},
            {bounds.x + bounds.w - safeRadius, bounds.y, safeRadius, safeRadius},
            {bounds.x, bounds.y + bounds.h - safeRadius, safeRadius, safeRadius},
            {bounds.x + bounds.w - safeRadius, bounds.y + bounds.h - safeRadius,
             safeRadius, safeRadius}
        };
        const SDL_RendererFlip flips[] = {
            SDL_FLIP_NONE, SDL_FLIP_HORIZONTAL, SDL_FLIP_VERTICAL,
            static_cast<SDL_RendererFlip>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)
        };

        bool ok = true;
        for (int i = 0; i < 4; ++i)
            ok = SDL_RenderCopyEx(renderer_, cornerMask_, nullptr, &corners[i],
                                  0.0, nullptr, flips[i]) == 0 && ok;
        resetCornerMaskModulation();
        return ok;
    }

    // Cached bottom-up alpha gradient behind tile labels. The direct-SDL
    // fallback preserves the previous blend mode and draw colour.
    bool labelScrim(const SDL_Rect& tileBounds, bool twoLines,
                    int opacity, int radius = 10) {
        if (!valid(tileBounds) || opacity <= 0) return true;
        const TileLabelLayout layout = measureTileLabel(
            tileBounds.w, tileBounds.h, twoLines, radius);
        const SDL_Rect strip{
            tileBounds.x,
            tileBounds.y + tileBounds.h - layout.stripHeight,
            tileBounds.w,
            layout.stripHeight
        };
        const int alpha = clampTileLabelOpacity(opacity);
        if (engine_ && engine_->ready()) {
            const SDL_Color top{5,9,16,0};
            const SDL_Color bottom{5,9,16,static_cast<Uint8>(alpha)};
            if (engine_->drawLinearGradient(strip, top, bottom,
                    desknx::visual::VisualEngine::GradientAxis::Vertical))
                return true;
        }

        if (!renderer_) return false;
        SDL_BlendMode savedBlend = SDL_BLENDMODE_BLEND;
        Uint8 r = 0, g = 0, b = 0, a = 255;
        if (SDL_GetRenderDrawBlendMode(renderer_, &savedBlend) != 0 ||
            SDL_GetRenderDrawColor(renderer_, &r, &g, &b, &a) != 0)
            return false;
        bool ok = SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND) == 0;
        if (ok) ok = SDL_SetRenderDrawColor(renderer_, 5, 9, 16,
                                            static_cast<Uint8>(alpha)) == 0;
        if (ok) ok = SDL_RenderFillRect(renderer_, &strip) == 0;
        const bool colorRestored = SDL_SetRenderDrawColor(renderer_, r, g, b, a) == 0;
        const bool blendRestored = SDL_SetRenderDrawBlendMode(renderer_, savedBlend) == 0;
        return ok && colorRestored && blendRestored;
    }

    // Reusable badge shell. The caller owns label text and semantic action.
    bool badgeSurface(const SDL_Rect& bounds, SDL_Color fill,
                      SDL_Color edge, int radius = 10) {
        if (!valid(bounds)) return false;
        if (components_)
            return components_->surface(bounds, fill, edge, radius, false, 255);
        if (!renderer_) return false;
        bool ok = fillRounded(bounds, radius, fill);
        ok = outlineRounded(bounds, radius, edge) && ok;
        return ok;
    }

    static TileLabelLayout labelLayout(int width, int height,
                                       bool twoLines, int radius = 10) noexcept {
        return measureTileLabel(width, height, twoLines, radius);
    }

private:
    static bool valid(const SDL_Rect& bounds) noexcept {
        return bounds.w > 0 && bounds.h > 0;
    }

    static Uint8 scaleAlpha(Uint8 alpha, int scale) noexcept {
        return static_cast<Uint8>(
            (static_cast<unsigned int>(alpha) *
             static_cast<unsigned int>(std::max(0, std::min(255, scale)))) / 255u);
    }

    bool fillRounded(const SDL_Rect& bounds, int radius, SDL_Color color) {
        if (components_) {
            const SDL_Color transparent{0,0,0,0};
            return components_->surface(bounds, color, transparent, radius, false);
        }
        if (!renderer_) return false;
        if (engine_ && engine_->ready() &&
            engine_->fillRoundedRect(bounds.x, bounds.y, bounds.w, bounds.h, radius, color))
            return true;
        if (SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) != 0)
            return false;
        return SDL_RenderFillRect(renderer_, &bounds) == 0;
    }

    bool outlineRounded(const SDL_Rect& bounds, int radius, SDL_Color color) {
        if (components_) {
            return components_->surface(bounds, SDL_Color{0,0,0,0}, color,
                                         radius, false);
        }
        if (!renderer_) return false;
        if (engine_ && engine_->ready() &&
            engine_->drawRoundedOutline(bounds.x, bounds.y, bounds.w, bounds.h,
                                         radius, color))
            return true;
        if (SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) != 0)
            return false;
        return SDL_RenderDrawRect(renderer_, &bounds) == 0;
    }

    bool ensureCornerMask(int radius) {
        if (cornerMask_ && cornerMaskRadius_ == radius) return true;
        releaseMask();
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
            0, radius, radius, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface) return false;
        if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
            SDL_FreeSurface(surface);
            return false;
        }
        for (int y = 0; y < radius; ++y) {
            Uint32* row = reinterpret_cast<Uint32*>(
                static_cast<Uint8*>(surface->pixels) + y * surface->pitch);
            for (int x = 0; x < radius; ++x) {
                const double dx = radius - (x + 0.5);
                const double dy = radius - (y + 0.5);
                const bool outside = (dx * dx + dy * dy) >
                    static_cast<double>(radius) * radius;
                row[x] = SDL_MapRGBA(surface->format, 255, 255, 255,
                                     outside ? 255 : 0);
            }
        }
        if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
        cornerMask_ = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!cornerMask_) return false;
        if (SDL_SetTextureBlendMode(cornerMask_, SDL_BLENDMODE_BLEND) != 0) {
            releaseMask();
            return false;
        }
        cornerMaskRadius_ = radius;
        return true;
    }

    void resetCornerMaskModulation() noexcept {
        if (!cornerMask_) return;
        (void)SDL_SetTextureColorMod(cornerMask_, 255, 255, 255);
        (void)SDL_SetTextureAlphaMod(cornerMask_, 255);
    }

    void releaseMask() noexcept {
        if (cornerMask_) SDL_DestroyTexture(cornerMask_);
        cornerMask_ = nullptr;
        cornerMaskRadius_ = 0;
    }

    desknx::visual::VisualEngine* engine_{nullptr};
    UiComponents* components_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    SDL_Texture* cornerMask_{nullptr};
    int cornerMaskRadius_{0};
};

} // namespace desknx::ui
