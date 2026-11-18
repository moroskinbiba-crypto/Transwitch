#include "../include/desknx/ApplicationSize.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

void writeLe64(std::uint8_t* out,std::uint64_t value) {
    for(unsigned i=0;i<8;++i)out[i]=static_cast<std::uint8_t>(value>>(i*8));
}

int main() {
    std::array<std::uint8_t,0x80> raw{};
    writeLe64(raw.data()+0x08,1024);
    writeLe64(raw.data()+0x10,2048);
    writeLe64(raw.data()+0x18,4096);
    writeLe64(raw.data()+0x20+0x08,8192);
    writeLe64(raw.data()+0x60+0x10,16384);
    const auto decoded=desknx::application_size::decode(raw.data(),raw.size());
    assert(decoded.available);
    assert(decoded.bytes==31744);
    assert(!desknx::application_size::decode(raw.data(),0x7F).available);
    std::array<std::uint8_t,0x80> empty{};
    assert(!desknx::application_size::decode(empty.data(),empty.size()).available);
    std::puts("application_size_test: ok");
    return 0;
}
