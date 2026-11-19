#pragma once

#include <curl/curl.h>
#include <atomic>

namespace desknx::network {

// Shared, reference-counted libcurl lifetime. Nextendo and SteamGridDB may
// perform requests on separate workers; one worker must never tear curl down
// while the other is still using it.
inline std::atomic_flag curlLifecycleLock = ATOMIC_FLAG_INIT;
inline unsigned int curlLifecycleUsers = 0;

inline bool acquireCurl() {
    while(curlLifecycleLock.test_and_set(std::memory_order_acquire)) {}
    bool ok=true;
    if(curlLifecycleUsers==0)
        ok=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
    if(ok)++curlLifecycleUsers;
    curlLifecycleLock.clear(std::memory_order_release);
    return ok;
}

inline void releaseCurl() {
    while(curlLifecycleLock.test_and_set(std::memory_order_acquire)) {}
    if(curlLifecycleUsers>0&&--curlLifecycleUsers==0)
        curl_global_cleanup();
    curlLifecycleLock.clear(std::memory_order_release);
}

} // namespace desknx::network
