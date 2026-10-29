#pragma once

#include <desknx/Model.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include <vector>

class IconCache {
public:
    void setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }
    SDL_Texture* get(const desknx::model::Tile& tile) {
        const std::string path = desknx::model::iconCachePath(tile.kind, tile.data);
        if (path.empty() || !renderer_) return nullptr;
        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) return nullptr;
        for (auto& entry : entries_) {
            if (entry.path == path) {
                entry.lastUse = ++clock_;
                return entry.texture;
            }
        }
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface) return nullptr;
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
    struct Entry { std::string path; SDL_Texture* texture; unsigned long long lastUse; };
    static constexpr size_t MaxEntries = 64;
    SDL_Renderer* renderer_{nullptr};
    unsigned long long clock_{0};
    std::vector<Entry> entries_;
};
