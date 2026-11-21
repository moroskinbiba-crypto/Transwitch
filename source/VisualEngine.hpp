#pragma once
#include <SDL2/SDL.h>
#include <desknx/VisualGeometry.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <string>

namespace desknx::visual {

// Non-owning facade over the one SDL renderer. Renderer operations and texture
// lifetime stay on the UI thread; worker threads only read/decode CPU data.
class VisualEngine {
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
        std::uint32_t roundedRectCalls{0};
        std::uint32_t roundedFillBatches{0};
        std::uint32_t outlineCalls{0};
        std::uint32_t gradientDrawCalls{0};
        std::uint32_t gradientTextureRebuilds{0};
        std::uint32_t failedCalls{0};
    };

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
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        ready_ = true;
        return true;
    }

    void shutdown() {
        if (gradientTexture_) SDL_DestroyTexture(gradientTexture_);
        gradientTexture_ = nullptr;
        gradientTop_ = {};
        gradientBottom_ = {};
        gradientValid_ = false;
        renderer_ = nullptr;
        logicalWidth_ = logicalHeight_ = 0;
        profile_ = {};
        frameStats_ = {};
        lastFrameStats_ = {};
        ready_ = false;
    }

    bool ready() const { return ready_ && renderer_ != nullptr; }
    const RendererProfile& rendererProfile() const { return profile_; }
    void beginFrame() { frameStats_ = {}; }
    void endFrame() { lastFrameStats_ = frameStats_; }
    const FrameStats& lastFrameStats() const { return lastFrameStats_; }

    bool setDrawColor(SDL_Color color) {
        if (!ready()) return false;
        if (SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) != 0) {
            ++frameStats_.failedCalls;
            return false;
        }
        return true;
    }

    bool fillRect(int x, int y, int width, int height, SDL_Color color) {
        if (!ready() || width <= 0 || height <= 0 || !setDrawColor(color)) return false;
        const SDL_Rect bounds{x, y, width, height};
        ++frameStats_.filledRectCalls;
        if (SDL_RenderFillRect(renderer_, &bounds) != 0) {
            ++frameStats_.failedCalls;
            return false;
        }
        return true;
    }

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
            ++frameStats_.failedCalls;
            return false;
        }
        return true;
    }

    bool drawRoundedOutline(int x, int y, int width, int height, int radius, SDL_Color color) {
        if (!ready() || width <= 0 || height <= 0 || !setDrawColor(color)) return false;
        const int corner = std::max(0, std::min(radius, std::min(width, height) / 2));
        ++frameStats_.outlineCalls;
        if (corner == 0) {
            const SDL_Rect bounds{x, y, width, height};
            if (SDL_RenderDrawRect(renderer_, &bounds) != 0) {
                ++frameStats_.failedCalls;
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
            const int inset = static_cast<int>(std::ceil(static_cast<double>(corner) - std::sqrt(
                std::max(0.0, static_cast<double>(corner * corner) - dy * dy))));
            success = SDL_RenderDrawPoint(renderer_, x + inset, y + row) == 0 && success;
            success = SDL_RenderDrawPoint(renderer_, x + width - 1 - inset, y + row) == 0 && success;
            success = SDL_RenderDrawPoint(renderer_, x + inset, y + height - 1 - row) == 0 && success;
            success = SDL_RenderDrawPoint(renderer_, x + width - 1 - inset,
                y + height - 1 - row) == 0 && success;
        }
        if (!success) ++frameStats_.failedCalls;
        return success;
    }

    // Cached 1x256 ramp: one texture draw per frame, rebuilt only on theme changes.
    bool drawVerticalGradient(SDL_Color top, SDL_Color bottom) {
        if (!ready() || !ensureGradient(top, bottom)) return false;
        const SDL_Rect destination{0, 0, logicalWidth_, logicalHeight_};
        ++frameStats_.gradientDrawCalls;
        if (SDL_RenderCopy(renderer_, gradientTexture_, nullptr, &destination) != 0) {
            ++frameStats_.failedCalls;
            return false;
        }
        return true;
    }

private:
    static constexpr int GradientHeight = 256;
    static bool sameColor(SDL_Color a, SDL_Color b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }
    static Uint8 interpolate(Uint8 from, Uint8 to, int position) {
        const unsigned int inverse = 255u - static_cast<unsigned int>(position);
        return static_cast<Uint8>(
            (static_cast<unsigned int>(from) * inverse +
             static_cast<unsigned int>(to) * static_cast<unsigned int>(position) + 127u) / 255u);
    }
    bool ensureGradient(SDL_Color top, SDL_Color bottom) {
        if (gradientTexture_ && gradientValid_ &&
            sameColor(top, gradientTop_) && sameColor(bottom, gradientBottom_))
            return true;

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
            0, 1, GradientHeight, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface) { ++frameStats_.failedCalls; return false; }
        if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) {
            SDL_FreeSurface(surface);
            ++frameStats_.failedCalls;
            return false;
        }
        for (int y = 0; y < GradientHeight; ++y) {
            const SDL_Color c{
                interpolate(top.r, bottom.r, y),
                interpolate(top.g, bottom.g, y),
                interpolate(top.b, bottom.b, y),
                interpolate(top.a, bottom.a, y)
            };
            auto* row = reinterpret_cast<Uint32*>(
                static_cast<Uint8*>(surface->pixels) + y * surface->pitch);
            row[0] = SDL_MapRGBA(surface->format, c.r, c.g, c.b, c.a);
        }
        if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
        SDL_Texture* replacement = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!replacement) { ++frameStats_.failedCalls; return false; }
        SDL_SetTextureBlendMode(replacement, SDL_BLENDMODE_NONE);
        if (gradientTexture_) SDL_DestroyTexture(gradientTexture_);
        gradientTexture_ = replacement;
        gradientTop_ = top;
        gradientBottom_ = bottom;
        gradientValid_ = true;
        ++frameStats_.gradientTextureRebuilds;
        return true;
    }

    SDL_Renderer* renderer_{nullptr};
    int logicalWidth_{0};
    int logicalHeight_{0};
    bool ready_{false};
    RendererProfile profile_{};
    SDL_Texture* gradientTexture_{nullptr};
    SDL_Color gradientTop_{};
    SDL_Color gradientBottom_{};
    bool gradientValid_{false};
    FrameStats frameStats_{};
    FrameStats lastFrameStats_{};
};
} // namespace desknx::visual
