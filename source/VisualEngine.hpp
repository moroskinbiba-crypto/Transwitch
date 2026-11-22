#pragma once

#include <SDL2/SDL.h>
#include <desknx/VisualGeometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace desknx::visual {

// Renderer-owned primitives for DeskNX. The engine never owns the SDL window or
// renderer; it owns only its small, non-target SDL textures. All renderer calls
// and texture lifetime operations must stay on the UI/render thread.
class VisualEngine final {
public:
    struct RendererProfile {
        std::string name{"unknown"};
        Uint32 flags{0};
        int maxTextureWidth{0};
        int maxTextureHeight{0};
        bool accelerated{false};
        bool software{false};
        bool presentVsync{false};
        bool targetTextures{false};
    };

    struct FrameStats {
        std::uint32_t filledRectCalls{0};
        std::uint32_t filledRectBatches{0};
        std::uint32_t batchedRectPrimitives{0};
        std::uint32_t roundedRectCalls{0};
        std::uint32_t roundedFillBatches{0};
        std::uint32_t outlineCalls{0};
        std::uint32_t gradientDrawCalls{0};
        std::uint32_t gradientTextureRebuilds{0};
        std::uint32_t textureDrawCalls{0};
        std::uint32_t radialGlowDrawCalls{0};
        std::uint32_t shadowLayers{0};
        std::uint32_t failedCalls{0};
    };

    enum class GradientAxis : std::uint8_t { Vertical, Horizontal };

    bool initialize(SDL_Renderer* renderer, int logicalWidth, int logicalHeight) {
        shutdown();
        if (!renderer || logicalWidth <= 0 || logicalHeight <= 0) return false;

        renderer_ = renderer;
        logicalWidth_ = logicalWidth;
        logicalHeight_ = logicalHeight;

        SDL_RendererInfo info{};
        if (SDL_GetRendererInfo(renderer_, &info) == 0) {
            profile_.name = info.name ? info.name : "unknown";
            profile_.flags = info.flags;
            profile_.maxTextureWidth = info.max_texture_width;
            profile_.maxTextureHeight = info.max_texture_height;
            profile_.accelerated = (info.flags & SDL_RENDERER_ACCELERATED) != 0;
            profile_.software = (info.flags & SDL_RENDERER_SOFTWARE) != 0;
            profile_.presentVsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
            profile_.targetTextures = (info.flags & SDL_RENDERER_TARGETTEXTURE) != 0;
        }

        if (SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND) != 0) {
            ++frameStats_.failedCalls;
        }
        ready_ = true;
        return true;
    }

    void shutdown() noexcept {
        destroyTexture(verticalGradientTexture_);
        destroyTexture(horizontalGradientTexture_);
        destroyTexture(radialGlowTexture_);
        verticalGradientValid_ = false;
        horizontalGradientValid_ = false;
        verticalGradientStart_ = verticalGradientEnd_ = {};
        horizontalGradientStart_ = horizontalGradientEnd_ = {};
        renderer_ = nullptr;
        logicalWidth_ = logicalHeight_ = 0;
        profile_ = {};
        frameStats_ = {};
        lastFrameStats_ = {};
        ready_ = false;
    }

    bool ready() const noexcept { return ready_ && renderer_ != nullptr; }
    const RendererProfile& rendererProfile() const noexcept { return profile_; }

    void beginFrame() noexcept { frameStats_ = {}; }
    void endFrame() noexcept { lastFrameStats_ = frameStats_; }
    const FrameStats& lastFrameStats() const noexcept { return lastFrameStats_; }

    bool setDrawColor(SDL_Color color) {
        if (!ready()) return false;
        if (SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) != 0) {
            fail();
            return false;
        }
        return true;
    }

    bool fillRect(int x, int y, int width, int height, SDL_Color color) {
        if (!ready() || width <= 0 || height <= 0 || !setDrawColor(color)) return false;
        const SDL_Rect bounds{x, y, width, height};
        ++frameStats_.filledRectCalls;
        if (SDL_RenderFillRect(renderer_, &bounds) != 0) {
            fail();
            return false;
        }
        return true;
    }

    // Emit a caller-owned rectangle list with one SDL draw call; no render
    // target textures or additional GPU resource lifetimes are required.
    bool fillRects(const SDL_Rect* rectangles,std::size_t count,SDL_Color color) {
        if(!ready()||!rectangles||count==0||
           count>static_cast<std::size_t>(std::numeric_limits<int>::max()))return false;
        if(!setDrawColor(color))return false;
        ++frameStats_.filledRectBatches;
        frameStats_.batchedRectPrimitives+=static_cast<std::uint32_t>(
            std::min<std::size_t>(count,std::numeric_limits<std::uint32_t>::max()));
        if(SDL_RenderFillRects(renderer_,rectangles,static_cast<int>(count))!=0) {
            fail();return false;
        }
        return true;
    }

    // Rounded fills are emitted as one SDL_RenderFillRects batch. The helper is
    // SDL-independent and caps the radius, so tiny controls cannot overflow its
    // fixed stack buffer or create pathological per-pixel draw loops.
    bool fillRoundedRect(int x, int y, int width, int height, int radius, SDL_Color color) {
        if (!ready() || width <= 0 || height <= 0) return false;
        std::array<geometry::SpanRect, geometry::MaxRoundedFillSpans> spans{};
        const std::size_t count = geometry::roundedFillSpans(
            x, y, width, height, radius, spans.data(), spans.size());
        if (count == 0 || !setDrawColor(color)) return false;

        std::array<SDL_Rect, geometry::MaxRoundedFillSpans> rectangles{};
        for (std::size_t i = 0; i < count; ++i)
            rectangles[i] = {spans[i].x, spans[i].y, spans[i].w, spans[i].h};

        ++frameStats_.roundedRectCalls;
        ++frameStats_.roundedFillBatches;
        if (SDL_RenderFillRects(renderer_, rectangles.data(), static_cast<int>(count)) != 0) {
            fail();
            return false;
        }
        return true;
    }

    bool drawRoundedOutline(int x, int y, int width, int height, int radius, SDL_Color color) {
        if (!ready() || width <= 0 || height <= 0 || !setDrawColor(color)) return false;
        const int corner = std::max(0, std::min(
            radius, std::min({width / 2, height / 2, geometry::MaxCornerRadius})));
        ++frameStats_.outlineCalls;

        if (corner == 0) {
            const SDL_Rect bounds{x, y, width, height};
            if (SDL_RenderDrawRect(renderer_, &bounds) != 0) {
                fail();
                return false;
            }
            return true;
        }

        bool success = true;
        if (width > corner * 2) {
            success = SDL_RenderDrawLine(renderer_, x + corner, y,
                x + width - corner - 1, y) == 0 && success;
            success = SDL_RenderDrawLine(renderer_, x + corner, y + height - 1,
                x + width - corner - 1, y + height - 1) == 0 && success;
        }
        if (height > corner * 2) {
            success = SDL_RenderDrawLine(renderer_, x, y + corner,
                x, y + height - corner - 1) == 0 && success;
            success = SDL_RenderDrawLine(renderer_, x + width - 1, y + corner,
                x + width - 1, y + height - corner - 1) == 0 && success;
        }
        for (int row = 0; row < corner; ++row) {
            const double dy = static_cast<double>(corner - row);
            const int inset = static_cast<int>(std::ceil(
                static_cast<double>(corner) - std::sqrt(std::max(
                    0.0, static_cast<double>(corner * corner) - dy * dy))));
            success = SDL_RenderDrawPoint(renderer_, x + inset, y + row) == 0 && success;
            success = SDL_RenderDrawPoint(renderer_, x + width - 1 - inset, y + row) == 0 && success;
            success = SDL_RenderDrawPoint(renderer_, x + inset, y + height - 1 - row) == 0 && success;
            success = SDL_RenderDrawPoint(renderer_, x + width - 1 - inset,
                y + height - 1 - row) == 0 && success;
        }
        if (!success) fail();
        return success;
    }

    // Three low-cost rounded silhouettes approximate a soft drop shadow without
    // render-target textures, readback, shader dependencies or backend switches.
    bool drawRoundedShadow(const SDL_Rect& bounds, int radius,
                           SDL_Color color, int softness = 6) {
        if (!ready() || bounds.w <= 0 || bounds.h <= 0 || color.a == 0) return false;
        const int spread = std::max(1, std::min(softness, 10));
        const struct Layer { int expansion; int offset; unsigned int weight; } layers[] = {
            {spread, spread / 2 + 2, 42u},
            {std::max(1, spread / 2), spread / 3 + 1, 76u},
            {1, 1, 118u}
        };

        bool ok = true;
        for (const Layer& layer : layers) {
            SDL_Color shade = color;
            shade.a = static_cast<Uint8>(
                (static_cast<unsigned int>(color.a) * layer.weight + 127u) / 255u);
            ok = fillRoundedRect(
                bounds.x - layer.expansion / 2,
                bounds.y + layer.offset,
                bounds.w + layer.expansion,
                bounds.h + layer.expansion / 2,
                radius + layer.expansion / 3,
                shade) && ok;
            ++frameStats_.shadowLayers;
        }
        return ok;
    }

    // A cached 96x96 radial alpha mask provides restrained focus/accent glow.
    // The mask is generated once, tinted per draw and reused; no blur pass runs
    // while a frame is being rendered.
    bool drawRadialGlow(const SDL_Rect& bounds, SDL_Color tint) {
        if (!ready() || bounds.w <= 0 || bounds.h <= 0 || tint.a == 0 ||
            !ensureRadialGlow())
            return false;

        if (SDL_SetTextureColorMod(radialGlowTexture_, tint.r, tint.g, tint.b) != 0 ||
            SDL_SetTextureAlphaMod(radialGlowTexture_, tint.a) != 0) {
            (void)SDL_SetTextureColorMod(radialGlowTexture_, 255, 255, 255);
            (void)SDL_SetTextureAlphaMod(radialGlowTexture_, 255);
            fail();
            return false;
        }

        const int rc = SDL_RenderCopy(renderer_, radialGlowTexture_, nullptr, &bounds);
        (void)SDL_SetTextureColorMod(radialGlowTexture_, 255, 255, 255);
        (void)SDL_SetTextureAlphaMod(radialGlowTexture_, 255);
        ++frameStats_.textureDrawCalls;
        ++frameStats_.radialGlowDrawCalls;
        if (rc != 0) {
            fail();
            return false;
        }
        return true;
    }

    bool drawLinearGradient(const SDL_Rect& bounds, SDL_Color start, SDL_Color end,
                            GradientAxis axis = GradientAxis::Vertical) {
        if (!ready() || bounds.w <= 0 || bounds.h <= 0) return false;
        SDL_Texture*& texture = axis == GradientAxis::Vertical
            ? verticalGradientTexture_ : horizontalGradientTexture_;
        SDL_Color& cachedStart = axis == GradientAxis::Vertical
            ? verticalGradientStart_ : horizontalGradientStart_;
        SDL_Color& cachedEnd = axis == GradientAxis::Vertical
            ? verticalGradientEnd_ : horizontalGradientEnd_;
        bool& valid = axis == GradientAxis::Vertical
            ? verticalGradientValid_ : horizontalGradientValid_;

        if (!ensureGradient(texture, cachedStart, cachedEnd, valid, start, end, axis))
            return false;
        ++frameStats_.gradientDrawCalls;
        ++frameStats_.textureDrawCalls;
        if (SDL_RenderCopy(renderer_, texture, nullptr, &bounds) != 0) {
            fail();
            return false;
        }
        return true;
    }

    // Compatibility helper used by the original first-pass engine.
    bool drawVerticalGradient(SDL_Color top, SDL_Color bottom) {
        const SDL_Rect bounds{0, 0, logicalWidth_, logicalHeight_};
        return drawLinearGradient(bounds, top, bottom, GradientAxis::Vertical);
    }

    bool drawHorizontalGradient(SDL_Color left, SDL_Color right) {
        const SDL_Rect bounds{0, 0, logicalWidth_, logicalHeight_};
        return drawLinearGradient(bounds, left, right, GradientAxis::Horizontal);
    }

private:
    static constexpr int GradientSize = 256;
    static constexpr int GlowSize = 96;

    static bool sameColor(SDL_Color a, SDL_Color b) noexcept {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    static Uint8 interpolate(Uint8 from, Uint8 to, int position) noexcept {
        const unsigned int inverse = 255u - static_cast<unsigned int>(position);
        return static_cast<Uint8>(
            (static_cast<unsigned int>(from) * inverse +
             static_cast<unsigned int>(to) * static_cast<unsigned int>(position) + 127u) / 255u);
    }

    static void destroyTexture(SDL_Texture*& texture) noexcept {
        if (texture) SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    void fail() noexcept { ++frameStats_.failedCalls; }

    bool ensureGradient(SDL_Texture*& texture, SDL_Color& cachedStart,
                        SDL_Color& cachedEnd, bool& valid,
                        SDL_Color start, SDL_Color end, GradientAxis axis) {
        if (texture && valid && sameColor(start, cachedStart) && sameColor(end, cachedEnd))
            return true;

        const int width = axis == GradientAxis::Vertical ? 1 : GradientSize;
        const int height = axis == GradientAxis::Vertical ? GradientSize : 1;
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
            0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface) {
            fail();
            return false;
        }
        if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
            SDL_FreeSurface(surface);
            fail();
            return false;
        }

        for (int i = 0; i < GradientSize; ++i) {
            const SDL_Color color{
                interpolate(start.r, end.r, i),
                interpolate(start.g, end.g, i),
                interpolate(start.b, end.b, i),
                interpolate(start.a, end.a, i)
            };
            const Uint32 pixel = SDL_MapRGBA(
                surface->format, color.r, color.g, color.b, color.a);
            if (axis == GradientAxis::Vertical) {
                auto* row = reinterpret_cast<Uint32*>(
                    static_cast<Uint8*>(surface->pixels) + i * surface->pitch);
                row[0] = pixel;
            } else {
                auto* row = reinterpret_cast<Uint32*>(surface->pixels);
                row[i] = pixel;
            }
        }
        if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);

        SDL_Texture* replacement = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!replacement) {
            fail();
            return false;
        }
        if (SDL_SetTextureBlendMode(replacement, SDL_BLENDMODE_BLEND) != 0) {
            SDL_DestroyTexture(replacement);
            fail();
            return false;
        }

        destroyTexture(texture);
        texture = replacement;
        cachedStart = start;
        cachedEnd = end;
        valid = true;
        ++frameStats_.gradientTextureRebuilds;
        return true;
    }

    bool ensureRadialGlow() {
        if (radialGlowTexture_) return true;
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
            0, GlowSize, GlowSize, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface) {
            fail();
            return false;
        }
        if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
            SDL_FreeSurface(surface);
            fail();
            return false;
        }

        const float center = static_cast<float>(GlowSize - 1) * 0.5f;
        const float inverseRadius = 1.0f / center;
        for (int y = 0; y < GlowSize; ++y) {
            auto* row = reinterpret_cast<Uint32*>(
                static_cast<Uint8*>(surface->pixels) + y * surface->pitch);
            for (int x = 0; x < GlowSize; ++x) {
                const float dx = (static_cast<float>(x) - center) * inverseRadius;
                const float dy = (static_cast<float>(y) - center) * inverseRadius;
                const float distance = std::sqrt(dx * dx + dy * dy);
                const float edge = std::max(0.0f, 1.0f - distance);
                const float smooth = edge * edge * (3.0f - 2.0f * edge);
                const Uint8 alpha = static_cast<Uint8>(
                    std::max(0.0f, std::min(255.0f, smooth * 255.0f)) + 0.5f);
                row[x] = SDL_MapRGBA(surface->format, 255, 255, 255, alpha);
            }
        }
        if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);

        radialGlowTexture_ = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!radialGlowTexture_) {
            fail();
            return false;
        }
        if (SDL_SetTextureBlendMode(radialGlowTexture_, SDL_BLENDMODE_BLEND) != 0) {
            destroyTexture(radialGlowTexture_);
            fail();
            return false;
        }
        return true;
    }

    SDL_Renderer* renderer_{nullptr};
    int logicalWidth_{0};
    int logicalHeight_{0};
    bool ready_{false};
    RendererProfile profile_{};

    SDL_Texture* verticalGradientTexture_{nullptr};
    SDL_Texture* horizontalGradientTexture_{nullptr};
    SDL_Texture* radialGlowTexture_{nullptr};
    SDL_Color verticalGradientStart_{};
    SDL_Color verticalGradientEnd_{};
    SDL_Color horizontalGradientStart_{};
    SDL_Color horizontalGradientEnd_{};
    bool verticalGradientValid_{false};
    bool horizontalGradientValid_{false};

    FrameStats frameStats_{};
    FrameStats lastFrameStats_{};
};

} // namespace desknx::visual
