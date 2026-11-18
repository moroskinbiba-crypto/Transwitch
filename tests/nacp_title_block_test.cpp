#include "../include/desknx/NacpTitleBlock.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <zlib.h>

namespace {
std::vector<std::uint8_t> rawDeflate(const std::vector<std::uint8_t>& input) {
    z_stream stream{};
    assert(deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 8,
                        Z_DEFAULT_STRATEGY) == Z_OK);
    std::vector<std::uint8_t> output(compressBound(input.size()));
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    output.resize(stream.total_out);
    deflateEnd(&stream);
    return output;
}

std::vector<std::uint8_t> makeNacp(const std::vector<std::uint8_t>& expanded) {
    const auto compressed = rawDeflate(expanded);
    std::vector<std::uint8_t> nacp(desknx::nacp::NacpBytes);
    const auto size = static_cast<std::uint16_t>(compressed.size());
    std::memcpy(nacp.data(), &size, sizeof(size));
    std::memcpy(nacp.data() + sizeof(size), compressed.data(), compressed.size());
    nacp[desknx::nacp::TitlesDataFormatOffset] = 1;
    return nacp;
}
}

int main() {
    using namespace desknx::nacp;
    std::vector<std::uint8_t> titleBlock(ExtendedBlockBytes);
    const char title[] = "Compressed Switch Title";
    std::memcpy(titleBlock.data(), title, sizeof(title));

    auto nacp = makeNacp(titleBlock);
    assert(normalizeCompressedTitleBlock(nacp.data(), nacp.size()));
    assert(nacp[TitlesDataFormatOffset] == 0);
    assert(std::strcmp(reinterpret_cast<const char*>(nacp.data()), title) == 0);

    std::fill(titleBlock.begin(), titleBlock.end(), 0);
    const char extendedOnly[] = "Extended Language Title";
    std::memcpy(titleBlock.data() + 20 * TitleEntryBytes,
                extendedOnly, sizeof(extendedOnly));
    nacp = makeNacp(titleBlock);
    assert(normalizeCompressedTitleBlock(nacp.data(), nacp.size()));
    assert(std::strcmp(reinterpret_cast<const char*>(nacp.data()),
                       extendedOnly) == 0);

    std::vector<std::uint8_t> legacy(NacpBytes);
    legacy[0] = 'O';
    assert(normalizeCompressedTitleBlock(legacy.data(), legacy.size()));
    assert(legacy[0] == 'O');

    std::vector<std::uint8_t> invalid(NacpBytes);
    invalid[TitlesDataFormatOffset] = 1;
    assert(!normalizeCompressedTitleBlock(invalid.data(), invalid.size()));
    assert(invalid[TitlesDataFormatOffset] == 0);
    assert(invalid[0] == 0);
    return 0;
}
