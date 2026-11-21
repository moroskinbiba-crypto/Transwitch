#include "nextendo_api.hpp"
#include <nextendo/parser.hpp>
#include <nextendo/nacp_titles.hpp>

#include <switch.h>
#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace nextendo {
namespace {
constexpr const char* kBaseUrl = "https://nextendo.network";
constexpr std::size_t kMaxResponseBytes = 512u * 1024u;
constexpr auto kFetchBudget = std::chrono::seconds(12);

struct HttpBody {
    std::string data;
    bool overflow{false};
};

size_t receiveBody(char* data, size_t size, size_t count, void* opaque) {
    auto* body = static_cast<HttpBody*>(opaque);
    if (size != 0 && count > static_cast<size_t>(-1) / size) return 0;
    const std::size_t bytes = size * count;
    if (bytes > kMaxResponseBytes || body->data.size() > kMaxResponseBytes - bytes) {
        body->overflow = true;
        return 0;
    }
    body->data.append(data, bytes);
    return bytes;
}

int abortOnShutdown(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* running = static_cast<const std::atomic<bool>*>(opaque);
    return running && !running->load() ? 1 : 0;
}

bool request(CURL* curl, const std::string& url, std::string& body,
             std::chrono::steady_clock::time_point deadline,
             const std::atomic<bool>* running) {
    if (!curl) return false;
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    const long remaining = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    if (remaining <= 0) return false;

    HttpBody response;
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Transwitch-Nextendo-Overlay/0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min(2500L, remaining));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, std::min(4500L, remaining));
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (running) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abortOnShutdown);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, running);
    }

    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    if (result == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK || status != 200 || response.overflow) return false;
    body.swap(response.data);
    return true;
}

bool readSpecialGame(CURL* curl, const std::string& path, TitleId id,
                     bool requiresOnline, PlayerSnapshot& snapshot,
                     std::chrono::steady_clock::time_point deadline,
                     const std::atomic<bool>* running) {
    std::string body;
    if (!request(curl, std::string(kBaseUrl) + path, body, deadline, running)) return false;
    if (requiresOnline && !parser::readBoolField(body, "online", false)) return false;
    unsigned connected = 0;
    if (!parser::readConnected(body, connected)) return false;
    snapshot.perTitle[id] = connected;
    return true;
}

std::string titleIdString(TitleId id) {
    char value[17]{};
    std::snprintf(value, sizeof(value), "%016llX", static_cast<unsigned long long>(id));
    return value;
}

std::string knownTitleName(TitleId id) {
    switch (id) {
        case 0x010028600EBDA000ull: return "Super Mario 3D World + Bowser's Fury";
        case 0x0100965017338000ull: return "Super Mario Party Jamboree";
        case 0x0100E65002BB8000ull: return "Stardew Valley";
        case 0x0100A8E016236000ull: return "Kirby";
        case 0x0100EA6014BB8000ull: return "Just Dance 2022";
        case 0x0100B6E012EBE000ull: return "UNO";
        default: return "Title " + titleIdString(id);
    }
}

template <typename T, typename = void>
struct HasLangData : std::false_type {};

template <typename T>
struct HasLangData<T, std::void_t<decltype(std::declval<const T&>().lang_data.lang)>>
    : std::true_type {};

template <typename T>
const NacpLanguageEntry* languageEntries(const T& nacp) {
    if constexpr (HasLangData<T>::value) return nacp.lang_data.lang;
    else return nacp.lang;
}

std::string resolveNacpName(const NacpStruct& source) {
    NacpStruct nacp = source;
    if (!nacp::normalizeCompressedTitleBlock(&nacp, sizeof(nacp))) return {};

    NacpLanguageEntry* preferred = nullptr;
    if (R_SUCCEEDED(nacpGetLanguageEntry(&nacp, &preferred)) &&
        preferred && preferred->name[0] != '\0') {
        return std::string(preferred->name);
    }

    const NacpLanguageEntry* entries = languageEntries(nacp);
    if (!entries) return {};
    if (entries[1].name[0] != '\0') return std::string(entries[1].name);
    for (std::size_t i = 0; i < 16; ++i)
        if (entries[i].name[0] != '\0') return std::string(entries[i].name);
    return {};
}

} // namespace

bool initializeNetwork() {
    const Result nifmResult = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(nifmResult)) return false;

    const Result socketResult = socketInitializeDefault();
    if (R_FAILED(socketResult)) {
        nifmExit();
        return false;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        socketExit();
        nifmExit();
        return false;
    }
    return true;
}

void shutdownNetwork() {
    curl_global_cleanup();
    socketExit();
    nifmExit();
}

bool fetchSnapshot(PlayerSnapshot& snapshot, const std::atomic<bool>* running) {
    PlayerSnapshot candidate;
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    const auto deadline = std::chrono::steady_clock::now() + kFetchBudget;
    bool receivedValidDocument = false;
    std::string body;

    // Same public feed used by Transwitch's desktop cards.
    if (request(curl, std::string(kBaseUrl) + "/api/online-counts", body, deadline, running) &&
        parser::parseCounts(body, candidate, false)) {
        receivedValidDocument = true;
    }

    // Preserve primary endpoint values when filling gaps from the catalogue.
    if (std::chrono::steady_clock::now() < deadline &&
        request(curl, std::string(kBaseUrl) + "/service-catalog/v1/online-counts", body, deadline, running) &&
        parser::parseCounts(body, candidate, true)) {
        receivedValidDocument = true;
    }

    struct SpecialFeed { const char* path; TitleId id; bool requiresOnline; };
    static constexpr SpecialFeed specialFeeds[] = {
        {"/game-stats/sm3dw",      0x010028600EBDA000ull, false},
        {"/game-stats/jamboree",   0x0100965017338000ull, true},
        {"/game-stats/stardew",    0x0100E65002BB8000ull, true},
        {"/game-stats/kirby",      0x0100A8E016236000ull, true},
        {"/game-stats/justdance22",0x0100EA6014BB8000ull, true},
        {"/game-stats/uno-count",  0x0100B6E012EBE000ull, false},
    };
    for (const auto& feed : specialFeeds) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        if (readSpecialGame(curl, feed.path, feed.id, feed.requiresOnline, candidate, deadline, running))
            receivedValidDocument = true;
    }
    curl_easy_cleanup(curl);

    if (!receivedValidDocument || !parser::plausible(candidate)) return false;
    std::uint64_t total = 0;
    for (const auto& entry : candidate.perTitle) {
        total += entry.second;
        if (total > MaxPlayers) return false;
    }
    candidate.total = static_cast<unsigned>(total);
    candidate.fromCache = false;
    snapshot = std::move(candidate);
    return true;
}

void resolveInstalledNames(PlayerSnapshot& snapshot, bool nsReady) {
    for (const auto& entry : snapshot.perTitle)
        snapshot.titleNames[entry.first] = knownTitleName(entry.first);

    if (!nsReady || snapshot.perTitle.empty()) return;

    std::map<TitleId, bool> unresolved;
    for (const auto& entry : snapshot.perTitle) unresolved.emplace(entry.first, true);

    constexpr s32 PageCapacity = 30;
    s32 cursor = 0;
    for (;;) {
        if (unresolved.empty()) break;
        NsApplicationRecord page[PageCapacity]{};
        s32 returned = 0;
        const Result listResult = nsListApplicationRecord(page, PageCapacity, cursor, &returned);
        if (R_FAILED(listResult) || returned <= 0 || returned > PageCapacity) break;

        for (s32 i = 0; i < returned; ++i) {
            const TitleId id = page[i].application_id;
            const auto pending = unresolved.find(id);
            if (pending == unresolved.end()) continue;

            NsApplicationControlData control{};
            u64 bytesWritten = 0;
            if (R_FAILED(nsGetApplicationControlData(NsApplicationControlSource_Storage,
                    id, &control, sizeof(control), &bytesWritten))) continue;

            const std::string name = resolveNacpName(control.nacp);
            if (!name.empty()) snapshot.titleNames[id] = name;
            unresolved.erase(pending);
        }
        cursor += returned;
        if (returned < PageCapacity) break;
    }
}

} // namespace nextendo
