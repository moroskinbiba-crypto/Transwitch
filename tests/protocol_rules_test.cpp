#include "../projects/common/include/desknx/ProtocolRules.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using namespace desknx::ipc::rules;

    static_assert(IsSupportedVersion(1));
    static_assert(IsSupportedVersion(2));
    static_assert(!IsSupportedVersion(0));
    static_assert(!IsSupportedVersion(3));

    static_assert(IsValidPayloadSize(0));
    static_assert(IsValidPayloadSize(MaxPayloadSize));
    static_assert(!IsValidPayloadSize(MaxPayloadSize + 1));

    static_assert(!IsValidBusySeconds(0));
    static_assert(IsValidBusySeconds(1));
    static_assert(IsValidBusySeconds(120));
    static_assert(!IsValidBusySeconds(121));

    assert(IsSupportedVersion(1));
    assert(IsSupportedVersion(2));
    assert(!IsSupportedVersion(65535));
    assert(IsValidPayloadSize(0x3F00));
    assert(!IsValidPayloadSize(0x3F01));
    assert(IsValidBusySeconds(1));
    assert(IsValidBusySeconds(120));
    assert(!IsValidBusySeconds(0));
    assert(!IsValidBusySeconds(121));
}
