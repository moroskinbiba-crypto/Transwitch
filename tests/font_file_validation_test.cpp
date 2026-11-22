#include <desknx/FontFileValidation.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}
void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}
std::vector<std::uint8_t> makeSfnt(std::size_t faceOffset = 0) {
    constexpr std::array<std::uint32_t,5> tags{
        0x68656164u, 0x6D617870u, 0x636D6170u, 0x68686561u, 0x686D7478u
    };
    std::vector<std::uint8_t> bytes(faceOffset + 12 + tags.size()*16 + tags.size()*4, 0);
    put32(bytes, faceOffset, 0x00010000u);
    put16(bytes, faceOffset + 4, static_cast<std::uint16_t>(tags.size()));
    for (std::size_t i=0; i<tags.size(); ++i) {
        const std::size_t entry = faceOffset + 12 + i*16;
        put32(bytes, entry, tags[i]);
        put32(bytes, entry + 8, static_cast<std::uint32_t>(
            faceOffset + 12 + tags.size()*16 + i*4));
        put32(bytes, entry + 12, 4);
    }
    return bytes;
}
}

int main() {
    auto valid = makeSfnt();
    assert(desknx::font_file::validate(valid.data(), valid.size()));
    assert(!desknx::font_file::validate(nullptr, valid.size()));
    assert(!desknx::font_file::validate(valid.data(), 11));
    assert(!desknx::font_file::validate(valid.data(), valid.size(), valid.size()-1));

    auto badSignature = valid;
    put32(badSignature, 0, 0x12345678u);
    assert(!desknx::font_file::validate(badSignature.data(), badSignature.size()));

    auto badTableOffset = valid;
    put32(badTableOffset, 12 + 8, 0xFFFFFFF0u);
    assert(!desknx::font_file::validate(badTableOffset.data(), badTableOffset.size()));

    auto tooManyTables = valid;
    put16(tooManyTables, 4, 257);
    assert(!desknx::font_file::validate(tooManyTables.data(), tooManyTables.size()));

    auto collectionFace = makeSfnt(16);
    std::vector<std::uint8_t> ttc(16 + collectionFace.size() - 16, 0);
    put32(ttc, 0, 0x74746366u); // ttcf
    put32(ttc, 4, 0x00010000u);
    put32(ttc, 8, 1);
    put32(ttc, 12, 16);
    for (std::size_t i=16; i<collectionFace.size(); ++i) ttc[i] = collectionFace[i];
    assert(desknx::font_file::validate(ttc.data(), ttc.size()));

    auto invalidCollection = ttc;
    put32(invalidCollection, 12, 0xFFFFFFF0u);
    assert(!desknx::font_file::validate(invalidCollection.data(), invalidCollection.size()));
    return 0;
}
