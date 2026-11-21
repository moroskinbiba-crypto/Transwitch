#include <desknx/NextendoPlayers.hpp>

// Nextendo endpoint selection and the per-title merge strategy are adapted
// from HomeNX (GPL-3.0), projects/cards/src/Library.cpp. The polling cadence
// and card presentation follow projects/cards/src/CardsActivity.cpp.

#include <switch.h>
#include <curl/curl.h>
#include "NetworkRuntime.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace desknx::nextendo {
namespace {

constexpr const char* kBaseUrl = "https://nextendo.network";
constexpr std::size_t kMaxResponseBytes = 512u * 1024u;

struct Response {
    std::string body;
    bool overflow{false};
};

size_t receive(char* data, size_t size, size_t count, void* opaque) {
    auto* response = static_cast<Response*>(opaque);
    const size_t bytes = size * count;
    if (bytes > kMaxResponseBytes || response->body.size() > kMaxResponseBytes - bytes) {
        response->overflow = true;
        return 0;
    }
    response->body.append(data, bytes);
    return bytes;
}

struct CancelInfo {
    CancelCheck check{nullptr};
    void* context{nullptr};
};
int transferProgress(void* opaque, curl_off_t, curl_off_t,
                     curl_off_t, curl_off_t) {
    const auto* cancel=static_cast<const CancelInfo*>(opaque);
    return cancel && cancel->check && cancel->check(cancel->context) ? 1 : 0;
}

bool request(CURL* curl, const std::string& url, std::string& body,
             CancelCheck shouldCancel, void* cancelContext) {
    if (!curl || (shouldCancel && shouldCancel(cancelContext))) return false;
    Response response;
    CancelInfo cancellation{shouldCancel,cancelContext};
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Transwitch/DeskNX");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (shouldCancel) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transferProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancellation);
    }
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    if (result == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK || status != 200 || response.overflow) return false;
    body.swap(response.body);
    return true;
}

void skipSpace(const std::string& s, std::size_t& p) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
}

bool readString(const std::string& s, std::size_t& p, std::string& out) {
    if (p >= s.size() || s[p] != '"') return false;
    ++p;
    out.clear();
    while (p < s.size()) {
        const char c = s[p++];
        if (c == '"') return true;
        if (c == '\\') {
            if (p >= s.size()) return false;
            const char escaped = s[p++];
            switch (escaped) {
                case '"': case '\\': case '/': out.push_back(escaped); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: return false; // keys in this endpoint are ASCII title IDs
            }
        } else {
            if (static_cast<unsigned char>(c) < 0x20) return false;
            out.push_back(c);
        }
    }
    return false;
}

bool parseTitleId(const std::string& key, TitleId& id) {
    if (key.size() != 16) return false;
    id = 0;
    for (unsigned char c : key) {
        unsigned digit = 0;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        id = (id << 4) | digit;
    }
    return id != 0;
}

bool parseCounts(const std::string& body, PlayerSnapshot& snapshot, bool onlyNew) {
    const std::size_t key = body.find("\"counts\"");
    if (key == std::string::npos) return false;
    std::size_t p = body.find(':', key + 8);
    if (p == std::string::npos) return false;
    ++p;
    skipSpace(body, p);
    if (p >= body.size() || body[p++] != '{') return false;
    for (;;) {
        skipSpace(body, p);
        if (p >= body.size()) return false;
        if (body[p] == '}') return true;
        std::string title;
        if (!readString(body, p, title)) return false;
        skipSpace(body, p);
        if (p >= body.size() || body[p++] != ':') return false;
        skipSpace(body, p);
        if (p >= body.size()) return false;
        const std::size_t start = p;
        if (body[p] == '-') ++p;
        while (p < body.size() && body[p] >= '0' && body[p] <= '9') ++p;
        if (p == start || (p == start + 1 && body[start] == '-')) return false;
        char* end = nullptr;
        const long long value = std::strtoll(body.c_str() + start, &end, 10);
        if (end == body.c_str() + start || value < 0 ||
            static_cast<unsigned long long>(value) > MaxPlayersPerTitle) return false;
        TitleId id = 0;
        if (parseTitleId(title, id) && (!onlyNew || snapshot.perTitle.find(id) == snapshot.perTitle.end())) {
            snapshot.perTitle[id] = static_cast<unsigned>(value);
        }
        p = static_cast<std::size_t>(end - body.c_str());
        skipSpace(body, p);
        if (p < body.size() && body[p] == ',') { ++p; continue; }
        if (p < body.size() && body[p] == '}') return true;
        return false;
    }
}

bool readBool(const std::string& body, const char* field, bool fallback) {
    const std::string key = std::string("\"") + field + "\"";
    const std::size_t at = body.find(key);
    if (at == std::string::npos) return fallback;
    std::size_t p = body.find(':', at + key.size());
    if (p == std::string::npos) return fallback;
    ++p;
    skipSpace(body, p);
    if (body.compare(p, 4, "true") == 0) return true;
    if (body.compare(p, 5, "false") == 0) return false;
    return fallback;
}

bool readConnected(const std::string& body, unsigned& connected) {
    const std::string key = "\"connected\"";
    const std::size_t at = body.find(key);
    if (at == std::string::npos) return false;
    std::size_t p = body.find(':', at + key.size());
    if (p == std::string::npos) return false;
    ++p;
    skipSpace(body, p);
    const std::size_t start = p;
    while (p < body.size() && body[p] >= '0' && body[p] <= '9') ++p;
    if (start == p) return false;
    const unsigned long value = std::strtoul(body.c_str() + start, nullptr, 10);
    if (value > MaxPlayersPerTitle) return false;
    connected = static_cast<unsigned>(value);
    return true;
}

} // namespace

bool initializeNetwork() {
    return desknx::network::acquire();
}

void shutdownNetwork() {
    desknx::network::release();
}

bool fetchSnapshot(PlayerSnapshot& snapshot, CancelCheck shouldCancel,
                   void* cancelContext) {
    snapshot = PlayerSnapshot{};
    if (shouldCancel && shouldCancel(cancelContext)) return false;
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    bool any = false;
    std::string body;

    if (request(curl, std::string(kBaseUrl) + "/api/online-counts", body,
                 shouldCancel,cancelContext)) {
        PlayerSnapshot parsed;
        if (parseCounts(body, parsed, false)) {
            snapshot.perTitle = std::move(parsed.perTitle);
            any = true;
        }
    }
    if (shouldCancel && shouldCancel(cancelContext)) {
        curl_easy_cleanup(curl);
        return false;
    }
    if (request(curl, std::string(kBaseUrl) + "/service-catalog/v1/online-counts", body,
                shouldCancel,cancelContext)) {
        if (parseCounts(body, snapshot, true)) any = true;
    }

    struct OwnGame { const char* path; TitleId id; bool requiresOnline; };
    static const OwnGame ownGames[] = {
        {"/game-stats/sm3dw", 0x010028600EBDA000ull, false},
        {"/game-stats/jamboree", 0x0100965017338000ull, true},
        {"/game-stats/stardew", 0x0100E65002BB8000ull, true},
        {"/game-stats/kirby", 0x0100A8E016236000ull, true},
        {"/game-stats/justdance22", 0x0100EA6014BB8000ull, true},
        {"/game-stats/uno-count", 0x0100B6E012EBE000ull, false},
    };
    for (const auto& game : ownGames) {
        if (shouldCancel && shouldCancel(cancelContext)) break;
        if (!request(curl, std::string(kBaseUrl) + game.path, body,
                     shouldCancel,cancelContext)) continue;
        if (game.requiresOnline && !readBool(body, "online", false)) continue;
        unsigned connected = 0;
        if (readConnected(body, connected)) {
            snapshot.perTitle[game.id] = connected;
            any = true;
        }
    }
    curl_easy_cleanup(curl);
    if ((shouldCancel && shouldCancel(cancelContext)) || !any) return false;

    std::uint64_t total = 0;
    for (const auto& entry : snapshot.perTitle) total += entry.second;
    if (total > MaxPlayersPerTitle) return false;
    snapshot.total = static_cast<unsigned>(total);
    return PresenceFeed::plausible(snapshot);
}

} // namespace desknx::nextendo
