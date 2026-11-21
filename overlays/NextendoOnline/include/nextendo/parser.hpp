#pragma once
#include "snapshot.hpp"
#include <cstdlib>
#include <string>

namespace nextendo::parser {
inline void skipSpace(const std::string& text, std::size_t& pos) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
           text[pos] == '\r' || text[pos] == '\n')) ++pos;
}
inline bool readJsonString(const std::string& text, std::size_t& pos, std::string& out) {
    if (pos >= text.size() || text[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[pos++]);
        if (ch == '"') return true;
        if (ch == '\\') {
            if (pos >= text.size()) return false;
            const char escaped = text[pos++];
            switch (escaped) {
                case '"': case '\\': case '/': out.push_back(escaped); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: return false;
            }
        } else {
            if (ch < 0x20 || ch >= 0x80) return false;
            out.push_back(static_cast<char>(ch));
        }
    }
    return false;
}
inline bool parseTitleId(const std::string& key, TitleId& id) {
    if (key.size() != 16) return false;
    id = 0;
    for (const unsigned char ch : key) {
        unsigned digit = 0;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else return false;
        id = (id << 4) | digit;
    }
    return id != 0;
}
inline bool parseUnsigned(const std::string& text, std::size_t& pos, unsigned& value) {
    const std::size_t start = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
    if (start == pos) return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str() + start, &end, 10);
    if (end != text.c_str() + pos || parsed > MaxPlayers) return false;
    value = static_cast<unsigned>(parsed);
    return true;
}
// Parses the public Nextendo online-counts payload. onlyNew preserves values
// from the primary endpoint when merging the service-catalog endpoint.
inline bool parseCounts(const std::string& body, PlayerSnapshot& snapshot, bool onlyNew = false) {
    if (body.empty() || body.size() > 512u * 1024u) return false;
    const std::size_t key = body.find("\"counts\"");
    if (key == std::string::npos) return false;
    std::size_t pos = body.find(':', key + 8);
    if (pos == std::string::npos) return false;
    ++pos;
    skipSpace(body, pos);
    if (pos >= body.size() || body[pos++] != '{') return false;
    std::size_t parsedRows = 0;
    for (;;) {
        skipSpace(body, pos);
        if (pos >= body.size()) return false;
        if (body[pos] == '}') return true;
        std::string keyText;
        if (!readJsonString(body, pos, keyText)) return false;
        skipSpace(body, pos);
        if (pos >= body.size() || body[pos++] != ':') return false;
        skipSpace(body, pos);
        unsigned count = 0;
        if (!parseUnsigned(body, pos, count)) return false;
        TitleId titleId = 0;
        if (parseTitleId(keyText, titleId)) {
            const auto found = snapshot.perTitle.find(titleId);
            if (!onlyNew || found == snapshot.perTitle.end()) {
                if (snapshot.perTitle.size() >= MaxTitles && found == snapshot.perTitle.end())
                    return false;
                snapshot.perTitle[titleId] = count;
            }
        }
        if (++parsedRows > MaxTitles * 4u) return false;
        skipSpace(body, pos);
        if (pos < body.size() && body[pos] == ',') { ++pos; continue; }
        if (pos < body.size() && body[pos] == '}') return true;
        return false;
    }
}
inline bool readBoolField(const std::string& body, const char* field, bool fallback = false) {
    const std::string key = std::string("\"") + field + "\"";
    const std::size_t at = body.find(key);
    if (at == std::string::npos) return fallback;
    std::size_t pos = body.find(':', at + key.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    skipSpace(body, pos);
    if (body.compare(pos, 4, "true") == 0) return true;
    if (body.compare(pos, 5, "false") == 0) return false;
    return fallback;
}
inline bool readConnected(const std::string& body, unsigned& connected) {
    const std::string key = "\"connected\"";
    const std::size_t at = body.find(key);
    if (at == std::string::npos) return false;
    std::size_t pos = body.find(':', at + key.size());
    if (pos == std::string::npos) return false;
    ++pos;
    skipSpace(body, pos);
    return parseUnsigned(body, pos, connected);
}
inline bool plausible(const PlayerSnapshot& snapshot) {
    if (snapshot.perTitle.size() > MaxTitles) return false;
    std::uint64_t sum = 0;
    for (const auto& entry : snapshot.perTitle) {
        if (entry.first == 0 || entry.second > MaxPlayers) return false;
        sum += entry.second;
        if (sum > MaxPlayers) return false;
    }
    return true;
}
} // namespace nextendo::parser
