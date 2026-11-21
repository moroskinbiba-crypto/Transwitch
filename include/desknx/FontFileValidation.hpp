#pragma once
#include <cstddef>
#include <cstdint>

namespace desknx::font_file {
inline std::uint16_t readBe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}
inline std::uint32_t readBe32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
            static_cast<std::uint32_t>(p[3]);
}
inline bool rangeInside(std::size_t offset, std::size_t length, std::size_t total) {
    return offset <= total && length <= total - offset;
}

// Bounded SFNT/TTC preflight rejects truncated or out-of-range table directories
// before SDL_ttf/FreeType is asked to parse an untrusted file.
inline bool validate(const std::uint8_t* bytes, std::size_t size,
                     std::size_t maxBytes = 16u * 1024u * 1024u) {
    if (!bytes || size < 12u || size > maxBytes) return false;
    std::size_t faceOffset = 0;
    const std::uint32_t signature = readBe32(bytes);
    if (signature == 0x74746366u) { // 'ttcf'
        if (size < 16u) return false;
        const std::uint32_t version = readBe32(bytes + 4u);
        const std::uint32_t faces = readBe32(bytes + 8u);
        if ((version != 0x00010000u && version != 0x00020000u) ||
            faces == 0u || faces > 64u ||
            !rangeInside(12u, static_cast<std::size_t>(faces) * 4u, size))
            return false;
        faceOffset = static_cast<std::size_t>(readBe32(bytes + 12u));
    }
    if (!rangeInside(faceOffset, 12u, size)) return false;
    const std::uint32_t scaler = readBe32(bytes + faceOffset);
    if (scaler != 0x00010000u && scaler != 0x4F54544Fu &&
        scaler != 0x74727565u && scaler != 0x74797031u)
        return false;
    const std::uint16_t tableCount = readBe16(bytes + faceOffset + 4u);
    if (tableCount == 0u || tableCount > 256u) return false;
    const std::size_t directoryBytes = 12u + static_cast<std::size_t>(tableCount) * 16u;
    if (!rangeInside(faceOffset, directoryBytes, size)) return false;
    bool head=false, maxp=false, cmap=false, hhea=false, hmtx=false;
    for (std::size_t i=0; i<tableCount; ++i) {
        const std::uint8_t* entry = bytes + faceOffset + 12u + i*16u;
        const std::uint32_t tag = readBe32(entry);
        const std::size_t offset = static_cast<std::size_t>(readBe32(entry + 8u));
        const std::size_t length = static_cast<std::size_t>(readBe32(entry + 12u));
        if (!rangeInside(offset, length, size)) return false;
        head |= tag == 0x68656164u;
        maxp |= tag == 0x6D617870u;
        cmap |= tag == 0x636D6170u;
        hhea |= tag == 0x68686561u;
        hmtx |= tag == 0x686D7478u;
    }
    return head && maxp && cmap && hhea && hmtx;
}
} // namespace desknx::font_file
