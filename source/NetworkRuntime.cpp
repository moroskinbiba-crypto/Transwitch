#include "NetworkRuntime.hpp"

#include <switch.h>
#include <curl/curl.h>
#include <SDL2/SDL.h>

#include <cstddef>

namespace desknx::network {
namespace {
SDL_SpinLock gLock = 0;
std::size_t gLeases = 0;
bool gSocketReady = false;
bool gCurlReady = false;
}

bool acquire() {
    SDL_AtomicLock(&gLock);
    if (gLeases > 0) {
        ++gLeases;
        SDL_AtomicUnlock(&gLock);
        return true;
    }

    const Result socketRc = socketInitializeDefault();
    if (R_FAILED(socketRc)) {
        SDL_AtomicUnlock(&gLock);
        return false;
    }
    gSocketReady = true;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        socketExit();
        gSocketReady = false;
        SDL_AtomicUnlock(&gLock);
        return false;
    }

    gCurlReady = true;
    gLeases = 1;
    SDL_AtomicUnlock(&gLock);
    return true;
}

void release() {
    SDL_AtomicLock(&gLock);
    if (gLeases == 0) {
        SDL_AtomicUnlock(&gLock);
        return;
    }
    --gLeases;
    if (gLeases != 0) {
        SDL_AtomicUnlock(&gLock);
        return;
    }

    if (gCurlReady) {
        curl_global_cleanup();
        gCurlReady = false;
    }
    if (gSocketReady) {
        socketExit();
        gSocketReady = false;
    }
    SDL_AtomicUnlock(&gLock);
}

} // namespace desknx::network
