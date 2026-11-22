#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace desknx::ui {

struct Utf8DecodeResult {
    std::uint32_t codepoint{0xFFFD};
    std::size_t length{1};
    bool valid{false};
};

// Invalid UTF-8 consumes one byte so a damaged sequence cannot hide the
// remainder of the label. Valid sequences are checked for overlong encodings,
// surrogate values and codepoints beyond the Unicode limit.
inline Utf8DecodeResult DecodeUtf8Codepoint(
    const std::string& value, std::size_t offset) noexcept {
    if (offset >= value.size()) return {0, 0, false};

    const auto byte = [&value](std::size_t index) {
        return static_cast<std::uint8_t>(
            static_cast<unsigned char>(value[index]));
    };
    const std::uint8_t lead = byte(offset);
    if (lead < 0x80) return {lead, 1, true};

    std::size_t length = 0;
    std::uint32_t codepoint = 0;
    std::uint32_t minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        codepoint = lead & 0x1F;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        codepoint = lead & 0x0F;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        codepoint = lead & 0x07;
        minimum = 0x10000;
    } else {
        return {0xFFFD, 1, false};
    }

    if (offset + length > value.size()) return {0xFFFD, 1, false};
    for (std::size_t index = 1; index < length; ++index) {
        const std::uint8_t continuation = byte(offset + index);
        if ((continuation & 0xC0) != 0x80) return {0xFFFD, 1, false};
        codepoint = (codepoint << 6) | (continuation & 0x3F);
    }

    if (codepoint < minimum || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return {0xFFFD, 1, false};
    }
    return {codepoint, length, true};
}

inline std::uint32_t NormalizeRussianBitmapCodepoint(
    std::uint32_t codepoint) noexcept;

inline std::uint32_t UppercaseUnicodeInitial(
    std::uint32_t codepoint) noexcept {
    if (codepoint >= 'a' && codepoint <= 'z') return codepoint - 0x20;
    return NormalizeRussianBitmapCodepoint(codepoint);
}

inline void AppendUtf8Codepoint(std::string& output,
                                std::uint32_t codepoint) {
    if (codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        codepoint = 0xFFFD;
    }
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

inline std::uint32_t NormalizeRussianBitmapCodepoint(
    std::uint32_t codepoint) noexcept {
    if (codepoint >= 0x0430 && codepoint <= 0x044F)
        return codepoint - 0x20;
    if (codepoint == 0x0451) return 0x0401;
    return codepoint;
}

inline bool IsRussianBitmapLetter(std::uint32_t codepoint) noexcept {
    codepoint = NormalizeRussianBitmapCodepoint(codepoint);
    return (codepoint >= 0x0410 && codepoint <= 0x042F) ||
        codepoint == 0x0401;
}

// Compact 5x7 uppercase Cyrillic glyphs. Lowercase Russian letters share the
// same bitmap, matching the visual convention of the existing ASCII fallback.
inline std::array<std::uint8_t, 7> RussianBitmapGlyph(
    std::uint32_t input) noexcept {
    const std::uint32_t codepoint = NormalizeRussianBitmapCodepoint(input);
    switch (codepoint) {
        case 0x0410: return {{14,17,17,31,17,17,17}}; // А
        case 0x0411: return {{31,16,16,30,17,17,30}}; // Б
        case 0x0412: return {{30,17,17,30,17,17,30}}; // В
        case 0x0413: return {{31,16,16,16,16,16,16}}; // Г
        case 0x0414: return {{14,17,17,17,17,17,31}}; // Д
        case 0x0415: return {{31,16,16,30,16,16,31}}; // Е
        case 0x0401: return {{10,0,31,16,30,16,31}};   // Ё
        case 0x0416: return {{21,21,14,4,14,21,21}};   // Ж
        case 0x0417: return {{30,1,1,14,1,1,30}};      // З
        case 0x0418: return {{17,19,21,25,17,17,17}};  // И
        case 0x0419: return {{10,0,17,19,21,25,17}};   // Й
        case 0x041A: return {{17,18,20,24,20,18,17}};  // К
        case 0x041B: return {{7,9,9,9,17,17,17}};      // Л
        case 0x041C: return {{17,27,21,21,17,17,17}};  // М
        case 0x041D: return {{17,17,17,31,17,17,17}};  // Н
        case 0x041E: return {{14,17,17,17,17,17,14}};  // О
        case 0x041F: return {{31,17,17,17,17,17,17}};  // П
        case 0x0420: return {{30,17,17,30,16,16,16}};  // Р
        case 0x0421: return {{15,16,16,16,16,16,15}};  // С
        case 0x0422: return {{31,4,4,4,4,4,4}};        // Т
        case 0x0423: return {{17,17,10,4,4,8,16}};     // У
        case 0x0424: return {{4,14,21,21,21,14,4}};    // Ф
        case 0x0425: return {{17,17,10,4,10,17,17}};   // Х
        case 0x0426: return {{17,17,17,17,17,17,15}};  // Ц
        case 0x0427: return {{17,17,17,15,1,1,1}};     // Ч
        case 0x0428: return {{21,21,21,21,21,21,31}};  // Ш
        case 0x0429: return {{21,21,21,21,21,21,29}};  // Щ
        case 0x042A: return {{24,8,8,14,9,9,14}};      // Ъ
        case 0x042B: return {{17,17,17,29,19,19,29}};  // Ы
        case 0x042C: return {{16,16,16,30,17,17,30}};  // Ь
        case 0x042D: return {{14,17,1,7,1,17,14}};     // Э
        case 0x042E: return {{17,21,21,29,21,21,17}};  // Ю
        case 0x042F: return {{15,17,17,15,5,9,17}};    // Я
        case 0x00A0: return {{0,0,0,0,0,0,0}};          // non-breaking space
        case 0x2013:
        case 0x2014: return {{0,0,0,31,0,0,0}};        // en/em dash
        case 0x2026: return {{0,0,0,0,0,21,0}};         // ellipsis
        default:    return {{14,17,1,2,4,0,4}};         // replacement '?'
    }
}

} // namespace desknx::ui
