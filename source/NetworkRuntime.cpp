#include "NetworkRuntime.hpp"
#include <switch.h>
#include <curl/curl.h>
#include <SDL2/SDL.h>
#include <cstddef>
namespace desknx::network {
namespace {
SDL_SpinLock gLock = 0;
std::size_t gLeases=0;
bool gSocketReady=false;
bool gCurlReady=false;
}
bool acquire() {
    SDL_AtomicLock(&gLock);
    if(gLeases>0){++gLeases;return true;}
    const Result rc=socketInitializeDefault();
    if(R_FAILED(rc))return false;
    gSocketReady=true;
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){
        socketExit();gSocketReady=false;return false;
    }
    gCurlReady=true;gLeases=1;return true;
}
void release() {
    std::lock_guard<std::mutex> lock(gMutex);
    if(gLeases==0)return;
    if(--gLeases!=0)return;
    if(gCurlReady){curl_global_cleanup();gCurlReady=false;}
    if(gSocketReady){socketExit();gSocketReady=false;}
}
} // namespace desknx::network
