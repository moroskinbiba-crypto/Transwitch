#pragma once

#include "../include/desknx/Model.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

class IconCache {
public:
    void setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }
    SDL_Texture* get(const desknx::model::Tile& tile) {
        const std::string path = desknx::model::iconCachePath(tile.kind, tile.data);
        if (path.empty() || !renderer_) return nullptr;
        // Resolve a cached texture before touching the SD card. The old
        // order performed stat()+JPEG parsing for every visible icon on every
        // frame, even when its SDL texture was already resident. On a Switch
        // SD card that turns a harmless redraw into hundreds of synchronous
        // filesystem operations and causes progressive UI stalls.
        for (auto& entry : entries_) {
            if (entry.path == path) {
                entry.lastUse = ++clock_;
                return entry.texture;
            }
        }
        struct stat st{};
        constexpr long long MaxCompressedIconBytes = 2LL * 1024 * 1024;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return nullptr;
        if (st.st_size <= 0 || st.st_size > MaxCompressedIconBytes ||
            !jpegDimensionsWithinLimit(path)) {
            // Let the next catalogue refresh fetch this icon again from NS/NRO.
            std::remove(path.c_str());
            return nullptr;
        }
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface) {
            std::remove(path.c_str());
            return nullptr;
        }
        constexpr int MaxIconDimension = 1024;
        const bool dimensionsOk = surface->w > 0 && surface->h > 0 &&
            surface->w <= MaxIconDimension && surface->h <= MaxIconDimension &&
            static_cast<std::uint64_t>(surface->w) * static_cast<std::uint64_t>(surface->h) <=
                static_cast<std::uint64_t>(MaxIconDimension) * MaxIconDimension;
        if (!dimensionsOk) {
            SDL_FreeSurface(surface);
            std::remove(path.c_str());
            return nullptr;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!texture) return nullptr;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        if (entries_.size() >= MaxEntries) {
            auto oldest = entries_.begin();
            for (auto it = entries_.begin(); it != entries_.end(); ++it)
                if (it->lastUse < oldest->lastUse) oldest = it;
            if (oldest->texture) SDL_DestroyTexture(oldest->texture);
            entries_.erase(oldest);
        }
        entries_.push_back({path, texture, ++clock_});
        return texture;
    }
    void shutdown() {
        for (auto& entry : entries_) if (entry.texture) SDL_DestroyTexture(entry.texture);
        entries_.clear();
    }
private:
    // Cached icons are JPEGs produced by the catalogue scanner. Inspect SOF
    // dimensions before IMG_Load so a tiny compressed file with absurd image
    // dimensions cannot immediately request an enormous decoded surface.
    static bool jpegDimensionsWithinLimit(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        unsigned char soi[2]{};
        if (!input.read(reinterpret_cast<char*>(soi), sizeof(soi)) ||
            soi[0] != 0xFF || soi[1] != 0xD8) return false;
        constexpr int MaxSegments = 256;
        constexpr std::uint32_t MaxPixels = 1024u * 1024u;
        for (int count = 0; count < MaxSegments; ++count) {
            int byte = input.get();
            while (byte != std::char_traits<char>::eof() && byte != 0xFF)
                byte = input.get();
            if (byte == std::char_traits<char>::eof()) return false;
            do { byte = input.get(); }
            while (byte == 0xFF);
            if (byte == std::char_traits<char>::eof()) return false;
            const unsigned char marker = static_cast<unsigned char>(byte);
            if (marker == 0xD9 || marker == 0xDA) return false;
            if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
            unsigned char lenBytes[2]{};
            if (!input.read(reinterpret_cast<char*>(lenBytes), sizeof(lenBytes))) return false;
            const std::uint16_t length = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(lenBytes[0]) << 8) | lenBytes[1]);
            if (length < 2) return false;
            const bool isSof = marker == 0xC0 || marker == 0xC1 || marker == 0xC2 ||
                marker == 0xC3 || marker == 0xC5 || marker == 0xC6 || marker == 0xC7 ||
                marker == 0xC9 || marker == 0xCA || marker == 0xCB || marker == 0xCD ||
                marker == 0xCE || marker == 0xCF;
            if (isSof) {
                if (length < 7) return false;
                unsigned char sof[5]{};
                if (!input.read(reinterpret_cast<char*>(sof), sizeof(sof))) return false;
                const std::uint32_t height = (static_cast<std::uint32_t>(sof[1]) << 8) | sof[2];
                const std::uint32_t width = (static_cast<std::uint32_t>(sof[3]) << 8) | sof[4];
                return width > 0 && height > 0 && width <= 1024 && height <= 1024 &&
                    width * height <= MaxPixels;
            }
            input.seekg(static_cast<std::streamoff>(length - 2), std::ios::cur);
            if (!input) return false;
        }
        return false;
    }

    struct Entry { std::string path; SDL_Texture* texture; unsigned long long lastUse; };
    static constexpr size_t MaxEntries = 64;
    SDL_Renderer* renderer_{nullptr};
    unsigned long long clock_{0};
    std::vector<Entry> entries_;
};
