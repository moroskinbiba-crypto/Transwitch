#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <desknx/IconRegistry.hpp>
#include <array>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

// Small renderer-thread cache for the launcher's own SVG glyphs. The SVG files
// in assets/icons are the editable source; a compact embedded fallback keeps
// the UI usable on existing SD-card installations that have not copied assets.
class VectorIconCache {
public:
    void setRenderer(SDL_Renderer* renderer) {
        if (renderer_ == renderer && initialized_) return;
        shutdown();
        renderer_ = renderer;
        if (!renderer_) return;
        for (std::size_t i = 0; i < icons_.size(); ++i) {
            std::string source = readPackagedSvg(desknx::ui::IconManifest[i].asset);
            if (!source.empty()) icons_[i] = createTexture(source);
            // A missing or malformed user/package asset never removes the
            // built-in vector control from the launcher UI.
            if (!icons_[i]) icons_[i] = createTexture(fallbacks_[i]);
        }
        initialized_ = true;
    }

    bool draw(desknx::ui::IconId kind, int x, int y, int size, SDL_Color color) {
        const std::size_t index = desknx::ui::iconIndex(kind);
        if (!renderer_ || !desknx::ui::isValidIcon(kind) ||
            index >= icons_.size() || !icons_[index] || size <= 0)
            return false;
        SDL_Texture* texture = icons_[index];
        SDL_SetTextureColorMod(texture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(texture, color.a);
        const SDL_Rect destination{x, y, size, size};
        const int rc = SDL_RenderCopy(renderer_, texture, nullptr, &destination);
        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
        return rc == 0;
    }

    void shutdown() {
        for (auto*& texture : icons_) {
            if (texture) SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        renderer_ = nullptr;
        initialized_ = false;
    }

private:
    static constexpr std::size_t MaxSvgBytes = 128u * 1024u;
    static constexpr int MaxSvgDimension = 512;

    static const std::array<const char*,
        static_cast<std::size_t>(desknx::ui::IconId::Count)> fallbacks_;

    static std::string readPackagedSvg(const char* name) {
        const std::string path =
            std::string("sdmc:/switch/DeskNX/media/icons/") + name;
        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
            st.st_size <= 0 ||
            static_cast<std::uint64_t>(st.st_size) > MaxSvgBytes)
            return {};
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        std::string source(static_cast<std::size_t>(st.st_size), '\0');
        input.read(&source[0], static_cast<std::streamsize>(source.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(source.size()))
            return {};
        return source;
    }

    SDL_Surface* loadSvgSurface(const std::string& source) {
        if (source.empty() || source.size() > MaxSvgBytes ||
            source.size() > static_cast<std::size_t>(INT_MAX))
            return nullptr;
        SDL_RWops* rw = SDL_RWFromConstMem(
            source.data(), static_cast<int>(source.size()));
        if (!rw) return nullptr;
        SDL_Surface* surface = IMG_Load_RW(rw, 1);
        if (!surface) return nullptr;
        const std::uint64_t pixelCount =
            static_cast<std::uint64_t>(std::max(0, surface->w)) *
            static_cast<std::uint64_t>(std::max(0, surface->h));
        if (surface->w <= 0 || surface->h <= 0 ||
            surface->w > MaxSvgDimension || surface->h > MaxSvgDimension ||
            pixelCount > 1024u * 1024u) {
            SDL_FreeSurface(surface);
            return nullptr;
        }
        return surface;
    }

    SDL_Texture* createTexture(const std::string& source) {
        SDL_Surface* surface = loadSvgSurface(source);
        if (!surface) return nullptr;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!texture) return nullptr;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        return texture;
    }

    SDL_Renderer* renderer_{nullptr};
    bool initialized_{false};
    std::array<SDL_Texture*,
        static_cast<std::size_t>(desknx::ui::IconId::Count)> icons_{};
};

inline const std::array<const char*,
    static_cast<std::size_t>(desknx::ui::IconId::Count)> VectorIconCache::fallbacks_{{
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3.8"/><path d="M12 2.5v2.2m0 14.6v2.2M2.5 12h2.2m14.6 0h2.2M5.3 5.3l1.6 1.6m10.2 10.2 1.6 1.6m0-13.4-1.6 1.6M6.9 17.1l-1.6 1.6"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="M6 8h12a3.4 3.4 0 0 1 3.3 2.7l.8 4.4a2.3 2.3 0 0 1-3.8 2.1l-2.2-1.9H8l-2.2 1.9A2.3 2.3 0 0 1 2 15.1l.8-4.4A3.4 3.4 0 0 1 6 8Z"/><path d="M7 10.5v4m-2-2h4m7-1h.1m2 3h.1"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="15" height="15" rx="2"/><path d="m4 16 4-4 3 3 2-2 5 4M14 4V3h5a2 2 0 0 1 2 2v12h-3"/><circle cx="8" cy="8.5" r="1.2"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 2.8v8.5"/><path d="M6.1 5.9a8.3 8.3 0 1 0 11.8 0"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 5h16v15H4zM8 8h8M8 11.5h8M8 15h5"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><path d="m3 10.8 9-7.3 9 7.3M5.5 9.3v11h13v-11M9.5 20.3v-6.8h5v6.8"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="8.5"/><path d="M12 7v5l3.5 2.2M3.5 5.8v4h4"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="7.8" r="3.4"/><path d="M4 20a8 8 0 0 1 16 0"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6h7m4 0h5M4 12h3m4 0h9M4 18h9m4 0h3"/><circle cx="13" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="15" cy="18" r="2"/></g></svg>)svg",
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><g fill="none" stroke="#fff" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="3.5" y="7.5" width="16.5" height="9" rx="2"/><path d="M20 10.2h1.8v3.6H20"/><path d="M6.5 10.5h3.3v3H6.5z" fill="#fff" stroke="none"/></g></svg>)svg"
}};

