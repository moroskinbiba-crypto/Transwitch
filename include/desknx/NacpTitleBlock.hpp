#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <zlib.h>

namespace desknx::nacp {

// Horizon 21.0.0+ may store the 32-entry NACP title block as raw DEFLATE.
// Older libnx nacp helpers still assume the legacy 16-entry uncompressed block,
// so normalize a private copy before reading names.
constexpr std::size_t TitleEntryBytes = 0x300;
constexpr std::size_t LegacyEntries = 16;
constexpr std::size_t ExtendedEntries = 32;
constexpr std::size_t LegacyBlockBytes = TitleEntryBytes * LegacyEntries;
constexpr std::size_t ExtendedBlockBytes = TitleEntryBytes * ExtendedEntries;
constexpr std::size_t TitlesDataFormatOffset = 0x3215;
constexpr std::size_t MaxCompressedTitleBytes = 0x2FFE;
constexpr std::size_t NacpBytes = 0x4000;

inline bool normalizeCompressedTitleBlock(void* nacpData, std::size_t size) {
    if (!nacpData || size < NacpBytes)
        return false;

    auto* bytes = static_cast<std::uint8_t*>(nacpData);
    const std::uint8_t format = bytes[TitlesDataFormatOffset];
    if (format == 0)
        return true;
    if (format != 1) {
        std::memset(bytes, 0, LegacyBlockBytes);
        bytes[TitlesDataFormatOffset] = 0;
        return false;
    }

    std::uint16_t compressedSize = 0;
    std::memcpy(&compressedSize, bytes, sizeof(compressedSize));
    if (compressedSize == 0 || compressedSize > MaxCompressedTitleBytes ||
        static_cast<std::size_t>(compressedSize) > size - sizeof(compressedSize)) {
        std::memset(bytes, 0, LegacyBlockBytes);
        bytes[TitlesDataFormatOffset] = 0;
        return false;
    }

    std::vector<std::uint8_t> expanded(ExtendedBlockBytes);
    z_stream stream{};
    stream.next_in = bytes + sizeof(compressedSize);
    stream.avail_in = static_cast<uInt>(compressedSize);
    stream.next_out = expanded.data();
    stream.avail_out = static_cast<uInt>(expanded.size());

    if (inflateInit2(&stream, -15) != Z_OK) {
        std::memset(bytes, 0, LegacyBlockBytes);
        bytes[TitlesDataFormatOffset] = 0;
        return false;
    }
    const int inflateResult = inflate(&stream, Z_FINISH);
    const bool decoded = inflateResult == Z_STREAM_END ||
        (inflateResult == Z_OK && stream.total_out >= LegacyBlockBytes);
    inflateEnd(&stream);

    std::memset(bytes, 0, LegacyBlockBytes);
    bytes[TitlesDataFormatOffset] = 0;
    if (!decoded || stream.total_out < LegacyBlockBytes)
        return false;

    std::memcpy(bytes, expanded.data(), LegacyBlockBytes);

    // A small number of titles only populate an extended language slot.
    bool hasLegacyName = false;
    for (std::size_t i = 0; i < LegacyEntries; ++i) {
        if (bytes[i * TitleEntryBytes] != 0) {
            hasLegacyName = true;
            break;
        }
    }
    if (!hasLegacyName) {
        for (std::size_t i = LegacyEntries; i < ExtendedEntries; ++i) {
            const std::size_t offset = i * TitleEntryBytes;
            if (expanded[offset] != 0) {
                std::memcpy(bytes, expanded.data() + offset, TitleEntryBytes);
                break;
            }
        }
    }
    return true;
}

} // namespace desknx::nacp
