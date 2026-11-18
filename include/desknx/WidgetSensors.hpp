#pragma once

#include <algorithm>
#include <cstdint>

namespace desknx::widget_sensors {

struct AtmosphereVersion {
    unsigned major{0};
    unsigned minor{0};
    unsigned patch{0};
    bool available{false};
};

// Exosphere config item 65000 returns Atmosphere semver in bits 40..63,
// encoded as 0xMMmmpp. The remaining bits contain keygen/HOS metadata.
inline AtmosphereVersion decodeAtmosphereConfig(std::uint64_t value) {
    const std::uint32_t packed=static_cast<std::uint32_t>((value>>40)&0xFFFFFFu);
    AtmosphereVersion out{};
    out.major=(packed>>16)&0xFFu;
    out.minor=(packed>>8)&0xFFu;
    out.patch=packed&0xFFu;
    out.available=out.major!=0;
    return out;
}

// libnx HidPowerInfo uses a coarse five-step 0..4 battery scale, not exact %.
inline int joyconPercentFromLevel(unsigned level) {
    return static_cast<int>(std::min(level,4u))*25;
}

} // namespace desknx::widget_sensors
