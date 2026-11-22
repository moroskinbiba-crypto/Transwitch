#include "../include/desknx/RendererStateRecovery.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

void assertPixel(SDL_Surface* surface, int x, int y,
                 Uint8 expectedRed, Uint8 expectedGreen, Uint8 expectedBlue) {
    bool locked = false;
    if (SDL_MUSTLOCK(surface)) {
        assert(SDL_LockSurface(surface) == 0);
        locked = true;
    }

    const auto* row = static_cast<const Uint8*>(surface->pixels) +
        static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch);
    Uint32 pixel = 0;
    std::memcpy(&pixel, row + static_cast<std::size_t>(x) *
        static_cast<std::size_t>(surface->format->BytesPerPixel), sizeof(pixel));

    Uint8 red = 0, green = 0, blue = 0, alpha = 0;
    SDL_GetRGBA(pixel, surface->format, &red, &green, &blue, &alpha);

    if (locked) SDL_UnlockSurface(surface);
    assert(red == expectedRed);
    assert(green == expectedGreen);
    assert(blue == expectedBlue);
}

} // namespace

int main() {
    assert(SDL_Init(0) == 0);

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface != nullptr);

    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer != nullptr);

    SDL_Texture* offscreen = nullptr;
    if (SDL_RenderTargetSupported(renderer) == SDL_TRUE) {
        offscreen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET, 64, 64);
        // The software renderer reports target support only when it can create
        // a target texture. A failed allocation is a test failure, not a skip.
        assert(offscreen != nullptr);
    }

    const SDL_Rect leakedClip{0, 0, 6, 6};
    const SDL_Rect fullFrame{0, 0, 64, 64};

    for (int cycle = 0; cycle < 8; ++cycle) {
        // Start every synthetic modal-frame cycle from a known, black frame.
        const auto initialRecovery =
            desknx::ui::RestoreRendererFrameBaseline(renderer);
        assert(initialRecovery.recovered());
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        assert(SDL_RenderClear(renderer) == 0);
        SDL_RenderPresent(renderer);
        assertPixel(surface, 63, 63, 0, 0, 0);

        const bool useOffscreen = offscreen != nullptr && (cycle % 2 == 0);
        if (useOffscreen) {
            assert(SDL_SetRenderTarget(renderer, offscreen) == 0);
            SDL_SetRenderDrawColor(renderer, 0, 180, 60, 255);
            assert(SDL_RenderClear(renderer) == 0);
        }

        assert(SDL_RenderSetClipRect(renderer, &leakedClip) == 0);
        assert(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) == 0);

        const auto recovery =
            desknx::ui::RestoreRendererFrameBaseline(renderer);
        assert(recovery.recovered());
        assert(recovery.hadActiveTarget == useOffscreen);
        assert(SDL_GetRenderTarget(renderer) == nullptr);
        assert(SDL_RenderIsClipEnabled(renderer) == SDL_FALSE);

        SDL_BlendMode blendMode = SDL_BLENDMODE_NONE;
        assert(SDL_GetRenderDrawBlendMode(renderer, &blendMode) == 0);
        assert(blendMode == SDL_BLENDMODE_BLEND);

        // This pixel is outside the leaked 6x6 clip. It must still be painted
        // after recovery, otherwise the next frame remains partially clipped.
        SDL_SetRenderDrawColor(renderer, 231, 74, 59, 255);
        assert(SDL_RenderFillRect(renderer, &fullFrame) == 0);
        SDL_RenderPresent(renderer);
        assertPixel(surface, 63, 63, 231, 74, 59);
    }

    if (offscreen) SDL_DestroyTexture(offscreen);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    std::puts("renderer frame recovery: PASS");
    return 0;
}
