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
#include <unordered_map>
#include <vector>

class IconCache {
public:
    void setRenderer(SDL_Renderer* renderer) { renderer_ = renderer; }
    void setCustomIconPackDir(const std::string& path) {
        if(path==customIconPackDir_)return;
        shutdown();
        resolvedPaths_.clear();
        failedUntil_.clear();
        customIconPackDir_=path;
    }
    SDL_Texture* get(const desknx::model::Tile& tile) {
        const std::string defaultPath = desknx::model::iconCachePath(tile.kind, tile.data);
        if (defaultPath.empty() || !renderer_) return nullptr;
        const std::string lookupKey=desknx::model::typeCode(tile)+"\t"+tile.data;
        std::string path;
        const auto resolved=resolvedPaths_.find(lookupKey);
        if(resolved!=resolvedPaths_.end()) {
            path=resolved->second;
        } else {
            path=customPathFor(tile);
            if(path.empty())path=defaultPath;
            // The path resolution checks up to three SD paths. Remember both
            // hits and misses so this work never happens on every redraw.
            resolvedPaths_.emplace(lookupKey,path);
        }
        const bool isCustom=(path!=defaultPath);
        const Uint32 now=SDL_GetTicks();
        const auto failed=failedUntil_.find(path);
        if(failed!=failedUntil_.end()) {
            if(static_cast<Sint32>(now-failed->second)<0)return nullptr;
            failedUntil_.erase(failed);
        }
        auto rememberFailure=[&]() {
            if(failedUntil_.size()>=128)failedUntil_.erase(failedUntil_.begin());
            failedUntil_[path]=now+5000;
        };
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
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            rememberFailure();
            return nullptr;
        }
        if (st.st_size <= 0 || st.st_size > MaxCompressedIconBytes ||
            !imageDimensionsWithinLimit(path)) {
            // Never delete user-owned icon-pack files. Only the generated cache
            // may be removed so the next catalogue refresh can rebuild it.
            if(!isCustom)std::remove(path.c_str());
            rememberFailure();
            return nullptr;
        }
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface) {
            if(!isCustom)std::remove(path.c_str());
            rememberFailure();
            return nullptr;
        }
        constexpr int MaxIconDimension = 1024;
        const bool dimensionsOk = surface->w > 0 && surface->h > 0 &&
            surface->w <= MaxIconDimension && surface->h <= MaxIconDimension &&
            static_cast<std::uint64_t>(surface->w) * static_cast<std::uint64_t>(surface->h) <=
                static_cast<std::uint64_t>(MaxIconDimension) * MaxIconDimension;
        if (!dimensionsOk) {
            SDL_FreeSurface(surface);
            if(!isCustom)std::remove(path.c_str());
            rememberFailure();
            return nullptr;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!texture) {
            rememberFailure();
            return nullptr;
        }
        failedUntil_.erase(path);
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
    std::string customPathFor(const desknx::model::Tile& tile) const {
        if(customIconPackDir_.empty())return {};
        std::string stem;
        if(tile.kind==desknx::model::Kind::Game) {
            if(!desknx::model::isTitleId(tile.data))return {};
            stem=tile.data;
            for(char& ch:stem)if(ch>='a'&&ch<='f')ch=static_cast<char>(ch-'a'+'A');
        } else if(tile.kind==desknx::model::Kind::Nro) {
            const size_t slash=tile.data.find_last_of('/');
            const size_t dot=tile.data.find_last_of('.');
            if(slash!=std::string::npos) {
                const size_t nameStart=slash+1;
                const size_t nameEnd=(dot!=std::string::npos&&dot>nameStart)?dot:tile.data.size();
                stem=tile.data.substr(nameStart,nameEnd-nameStart);
            }
            if(stem.empty()) {
                char key[32]{};
                std::snprintf(key,sizeof(key),"nro-%016llX",
                    static_cast<unsigned long long>(desknx::model::stableHash(tile.data)));
                stem=key;
            }
        } else return {};
        static const char* const exts[]={".jpg",".png",".jpeg"};
        auto findFile=[&](const std::string& base)->std::string {
            for(const char* ext:exts) {
                const std::string candidate=customIconPackDir_+"/"+base+ext;
                struct stat st{};
                if(stat(candidate.c_str(),&st)==0&&S_ISREG(st.st_mode)&&st.st_size>0)
                    return candidate;
            }
            return {};
        };
        std::string found=findFile(stem);
        if(!found.empty())return found;
        if(tile.kind==desknx::model::Kind::Nro&&stem.rfind("nro-",0)!=0) {
            char key[32]{};
            std::snprintf(key,sizeof(key),"nro-%016llX",
                static_cast<unsigned long long>(desknx::model::stableHash(tile.data)));
            return findFile(key);
        }
        return {};
    }

    static bool imageDimensionsWithinLimit(const std::string& path) {
        std::ifstream input(path,std::ios::binary);
        unsigned char header[24]{};
        if(!input.read(reinterpret_cast<char*>(header),8))return false;
        static const unsigned char pngSig[]={137,80,78,71,13,10,26,10};
        bool png=true;
        for(size_t i=0;i<8;++i)if(header[i]!=pngSig[i])png=false;
        if(png) {
            if(!input.read(reinterpret_cast<char*>(header+8),16))return false;
            const std::uint32_t width=(static_cast<std::uint32_t>(header[16])<<24)|
                (static_cast<std::uint32_t>(header[17])<<16)|
                (static_cast<std::uint32_t>(header[18])<<8)|header[19];
            const std::uint32_t height=(static_cast<std::uint32_t>(header[20])<<24)|
                (static_cast<std::uint32_t>(header[21])<<16)|
                (static_cast<std::uint32_t>(header[22])<<8)|header[23];
            return width>0&&height>0&&width<=1024&&height<=1024&&width*height<=1024u*1024u;
        }
        return jpegDimensionsWithinLimit(path);
    }

    // Cached and user-supplied JPEGs are dimension-checked before IMG_Load.
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
    std::string customIconPackDir_;
    std::unordered_map<std::string,std::string> resolvedPaths_;
    std::unordered_map<std::string,Uint32> failedUntil_;
    unsigned long long clock_{0};
    std::vector<Entry> entries_;
};
