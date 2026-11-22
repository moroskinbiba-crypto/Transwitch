#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace desknx::application_size {

struct Result {
    bool available{false};
    std::uint64_t bytes{0};
};

inline std::uint64_t readLe64(const std::uint8_t* data) {
    std::uint64_t value=0;
    for(unsigned i=0;i<8;++i)
        value|=static_cast<std::uint64_t>(data[i])<<(i*8);
    return value;
}

// NS returns four 0x20-byte storage entities in a 0x80-byte block.
// Each entity contains AppSize, PatchSize and AocSize at offsets 0x08,
// 0x10 and 0x18. Decode bytes explicitly instead of depending on an
// undocumented C struct field name or host alignment.
inline Result decode(const std::uint8_t* data,std::size_t size) {
    constexpr std::size_t EntitySize=0x20;
    constexpr std::size_t EntityCount=4;
    constexpr std::size_t RequiredSize=EntitySize*EntityCount;
    if(!data||size<RequiredSize)return {};
    std::uint64_t total=0;
    for(std::size_t i=0;i<EntityCount;++i) {
        const std::uint8_t* entity=data+i*EntitySize;
        const std::uint64_t values[]={readLe64(entity+0x08),
            readLe64(entity+0x10),readLe64(entity+0x18)};
        for(const std::uint64_t value:values) {
            if(value>std::numeric_limits<std::uint64_t>::max()-total)return {};
            total+=value;
        }
    }
    return {total>0,total};
}

} // namespace desknx::application_size
