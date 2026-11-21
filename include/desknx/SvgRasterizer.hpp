#pragma once

// NanoSVG parses authored SVG paths and rasterizes them only when an icon is
// first requested. The launcher keeps the SVG source as the canonical asset;
// the resulting small RGBA image is uploaded once and cached as an SDL texture.
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#include "vendor/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "vendor/nanosvgrast.h"

namespace desknx::svg {

inline bool Rasterize(const std::string& source, int maxDimension,
                      std::vector<Uint8>& pixels, int& width, int& height) {
    pixels.clear();
    width = height = 0;
    constexpr std::size_t MaxSvgBytes = 128u * 1024u;
    constexpr int HardMaxDimension = 512;
    if (source.empty() || source.size() > MaxSvgBytes ||
        maxDimension < 16 || maxDimension > HardMaxDimension)
        return false;

    // NanoSVG parses in-place, so work on a bounded private copy.
    std::string mutableSource = source;
    mutableSource.push_back('\0');
    NSVGimage* image = nsvgParse(&mutableSource[0], "px", 96.0f);
    if (!image) return false;

    const bool dimensionsValid =
        std::isfinite(image->width) && std::isfinite(image->height) &&
        image->width > 0.0f && image->height > 0.0f &&
        image->width <= 4096.0f && image->height <= 4096.0f;
    if (!dimensionsValid) {
        nsvgDelete(image);
        return false;
    }

    const float scale = std::min(
        static_cast<float>(maxDimension) / image->width,
        static_cast<float>(maxDimension) / image->height);
    width = std::max(1, std::min(maxDimension,
        static_cast<int>(std::lround(image->width * scale))));
    height = std::max(1, std::min(maxDimension,
        static_cast<int>(std::lround(image->height * scale))));
    const std::size_t byteCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (byteCount == 0 || byteCount > 1024u * 1024u) {
        nsvgDelete(image);
        width = height = 0;
        return false;
    }

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    if (!rasterizer) {
        nsvgDelete(image);
        width = height = 0;
        return false;
    }

    pixels.assign(byteCount, 0);
    // The output surface follows the SVG's aspect ratio, so no source pixels crop.
    nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale,
                  pixels.data(), width, height, width * 4);

    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(image);
    return true;
}


} // namespace desknx::svg
