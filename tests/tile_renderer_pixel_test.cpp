#include "TileRenderer.hpp"
#include "../include/desknx/ThemeVisualPolicy.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace {

struct Pixel {
    Uint8 r{0};
    Uint8 g{0};
    Uint8 b{0};
    Uint8 a{0};
};

struct TileCase {
    int x;
    int y;
    int width;
    int height;
    const char* name;
};

struct TileSamples {
    Pixel corner;
    Pixel artwork;
    Pixel label;
    Pixel outside;
    SDL_Color fill;
    const char* name;
};

Pixel pixelAt(SDL_Surface* surface, int x, int y) {
    assert(surface != nullptr);
    assert(x >= 0 && x < surface->w);
    assert(y >= 0 && y < surface->h);

    const bool mustUnlock = SDL_MUSTLOCK(surface) == SDL_TRUE;
    if (mustUnlock) assert(SDL_LockSurface(surface) == 0);

    const auto* row = static_cast<const Uint8*>(surface->pixels) +
        static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch);
    Uint32 packed = 0;
    std::memcpy(&packed,
        row + static_cast<std::size_t>(x) *
            static_cast<std::size_t>(surface->format->BytesPerPixel),
        sizeof(packed));

    Pixel result{};
    SDL_GetRGBA(packed, surface->format,
                &result.r, &result.g, &result.b, &result.a);

    if (mustUnlock) SDL_UnlockSurface(surface);
    return result;
}

void assertNear(Uint8 actual, Uint8 expected, int tolerance = 3) {
    const int delta = static_cast<int>(actual) - static_cast<int>(expected);
    assert(delta >= -tolerance && delta <= tolerance);
}

void assertPixelNear(Pixel actual, SDL_Color expected, int tolerance = 3) {
    assertNear(actual.r, expected.r, tolerance);
    assertNear(actual.g, expected.g, tolerance);
    assertNear(actual.b, expected.b, tolerance);
}

SDL_Texture* makeArtwork(SDL_Renderer* renderer) {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, 64, 32, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface != nullptr);

    const Uint32 color = SDL_MapRGBA(surface->format, 240, 80, 200, 255);
    assert(SDL_FillRect(surface, nullptr, color) == 0);

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    assert(texture != nullptr);
    assert(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) == 0);
    return texture;
}

void clearSurface(SDL_Renderer* renderer, SDL_Color color) {
    assert(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) == 0);
    assert(SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a) == 0);
    assert(SDL_RenderClear(renderer) == 0);
    assert(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) == 0);
}

} // namespace

int main() {
    constexpr int CanvasWidth = 1220;
    constexpr int CanvasHeight = 780;
    constexpr SDL_Color CanvasColor{14, 20, 30, 255};
    constexpr SDL_Color ArtworkColor{240, 80, 200, 255};
    constexpr SDL_Color TileFill{25, 37, 53, 255};
    constexpr SDL_Color TileEdge{89, 125, 162, 255};
    constexpr SDL_Color Accent{83, 157, 255, 255};

    const std::array<TileCase, 6> cases{{
        {20, 20, 154, 112, "1x1 compact"},
        {350, 20, 322, 112, "2x1 banner"},
        {690, 20, 490, 112, "3x1 wide banner"},
        {20, 390, 322, 238, "2x2 standard"},
        {350, 390, 490, 238, "3x2 dashboard"},
        {870, 390, 322, 364, "2x3 portrait"}
    }};

    assert(SDL_Init(0) == 0);
    SDL_Surface* canvas = SDL_CreateRGBSurfaceWithFormat(
        0, CanvasWidth, CanvasHeight, 32, SDL_PIXELFORMAT_RGBA32);
    assert(canvas != nullptr);

    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(canvas);
    assert(renderer != nullptr);
    assert(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) == 0);

    desknx::visual::VisualEngine engine;
    assert(engine.initialize(renderer, CanvasWidth, CanvasHeight));

    desknx::ui::UiComponents components;
    components.bind(&engine, renderer);

    desknx::ui::TileRenderer tiles;
    tiles.bind(&engine, &components, renderer);

    SDL_Texture* artwork = makeArtwork(renderer);
    const SDL_Rect parentClip{8, 8, CanvasWidth - 16, CanvasHeight - 16};

    mkdir("/tmp/desknx-visual-regression", 0777);

    for (int themeIndex = 0; themeIndex < 2; ++themeIndex) {
        const auto policy = desknx::ui::ThemePolicyForIndex(themeIndex);
        desknx::ui::ThemeTokens tokens{};
        tokens.accent = Accent;
        tokens.focus = SDL_Color{246, 248, 252, 220};
        tokens.cornerRadius = std::min(10, policy.maximumSurfaceRadius);
        tokens.focusRingWidth = 1;
        components.setThemeTokens(tokens);

        clearSurface(renderer, CanvasColor);
        std::array<TileSamples, 6> samples{};

        for (std::size_t i = 0; i < cases.size(); ++i) {
            const TileCase& tile = cases[i];
            const SDL_Rect bounds{tile.x, tile.y, tile.width, tile.height};
            const SDL_Rect viewport{
                bounds.x + 1, bounds.y + 1, bounds.w - 2, bounds.h - 2
            };

            assert(components.surface(bounds, TileFill, TileEdge,
                policy.maximumSurfaceRadius, themeIndex == 0, 255));

            const bool selected = (i % 2u) == 0u;
            assert(tiles.artworkFrame(bounds, TileFill, TileEdge, Accent,
                                      selected, 255, 10));

            // A pre-existing clip is deliberately active. The artwork renderer
            // must restore it exactly after drawing its own tile viewport.
            assert(SDL_RenderSetClipRect(renderer, &parentClip) == 0);
            assert(tiles.artworkImage(artwork, viewport,
                desknx::artwork::FitMode::Cover, TileFill, 10));

            SDL_Rect restoredClip{};
            SDL_RenderGetClipRect(renderer, &restoredClip);
            assert(restoredClip.x == parentClip.x);
            assert(restoredClip.y == parentClip.y);
            assert(restoredClip.w == parentClip.w);
            assert(restoredClip.h == parentClip.h);

            assert(tiles.labelScrim(viewport, true, 220, 10));
            assert(tiles.maskRoundedCorners(viewport, 10, TileFill));

            const SDL_Rect badge{
                bounds.x + 12, bounds.y + 12, 58, 24
            };
            assert(tiles.badgeSurface(
                badge, SDL_Color{5, 18, 30, 232},
                SDL_Color{120, 170, 220, 180}, 10));

            samples[i] = {
                pixelAt(canvas, viewport.x + 1, viewport.y + 1),
                pixelAt(canvas, bounds.x + bounds.w * 2 / 3,
                        bounds.y + bounds.h / 3),
                pixelAt(canvas, bounds.x + bounds.w / 2,
                        viewport.y + viewport.h - 2),
                pixelAt(canvas, bounds.x + bounds.w + 8,
                        bounds.y + bounds.h / 2),
                TileFill,
                tile.name
            };
        }

        // Save the synthetic frame before assertions so failed CI runs retain
        // the exact SDL software-rendered image for diagnosis.
        char path[160]{};
        std::snprintf(path, sizeof(path),
            "/tmp/desknx-visual-regression/theme-%d.bmp", themeIndex);
        assert(SDL_RenderSetClipRect(renderer, nullptr) == 0);
        SDL_RenderPresent(renderer);
        assert(SDL_SaveBMP(canvas, path) == 0);

        for (const TileSamples& sample : samples) {
            assertPixelNear(sample.corner, sample.fill);
            assert(sample.artwork.r > 220);
            assert(sample.artwork.g < 110);
            assert(sample.artwork.b > 170);

            // The lower label scrim must visibly darken the cover without
            // changing the artwork above the label strip.
            assert(sample.label.r + 25 < ArtworkColor.r);
            assert(sample.label.b + 25 < ArtworkColor.b);

            // Cover scaling must remain clipped to the tile. A few pixels of
            // shadow are allowed, so inspect beyond the renderer's shadow halo.
            assertPixelNear(sample.outside, CanvasColor);
        }
    }

    tiles.shutdown();
    components.bind(nullptr, nullptr);
    engine.shutdown();
    SDL_DestroyTexture(artwork);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(canvas);
    SDL_Quit();

    std::puts("tile renderer SDL pixel regression: PASS");
    return 0;
}
