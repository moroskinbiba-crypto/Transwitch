#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace desknx::strict {

// Parse a complete base-10 signed 32-bit integer. No whitespace, suffixes,
// partial parses, or out-of-range values are accepted.
inline bool parseInt32(const std::string& text, int& value) {
    if (text.empty()) return false;
    std::size_t firstDigit = 0;
    if (text[0] == '-' || text[0] == '+') firstDigit = 1;
    if (firstDigit == text.size()) return false;
    for (std::size_t i = firstDigit; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
    }

    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || !end || *end != '\0' ||
        parsed < -2147483647LL - 1LL || parsed > 2147483647LL)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

} // namespace desknx::strict
