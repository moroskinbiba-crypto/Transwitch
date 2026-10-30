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
    static_assert(!IsValidPayloadSize(0x100000000ULL));

    static_assert(IsBusyEndSignal(0));
    static_assert(!IsBusyEndSignal(1));
    static_assert(!IsBusyEndSignal(120));
    static_assert(!IsValidBusySeconds(0));
    static_assert(IsValidBusySeconds(1));
    static_assert(IsValidBusySeconds(120));
    static_assert(!IsValidBusySeconds(121));

    assert(IsSupportedVersion(1));
    assert(IsSupportedVersion(2));
    assert(!IsSupportedVersion(65535));
    assert(IsValidPayloadSize(0x3F00));
    assert(!IsValidPayloadSize(0x3F01));
    assert(!IsValidPayloadSize(0x100000000ULL));
    assert(IsBusyEndSignal(0));
    assert(!IsBusyEndSignal(1));
    assert(!IsBusyEndSignal(120));
    assert(IsValidBusySeconds(1));
    assert(IsValidBusySeconds(120));
    assert(!IsValidBusySeconds(0));
    assert(!IsValidBusySeconds(121));

    // AppletId values from libnx. 0x0A..0x0D are Auth/Cabinet/Controller/DataErase,
    // not Web/Shop/PhotoViewer/Settings.
    static_assert(IsAllowedSystemApplet(0x13));  // LibraryAppletWeb
    static_assert(IsAllowedSystemApplet(0x14));  // LibraryAppletShop
    static_assert(IsAllowedSystemApplet(0x15));  // LibraryAppletPhotoViewer
    static_assert(!IsAllowedSystemApplet(0x0A)); // LibraryAppletAuth
    static_assert(!IsAllowedSystemApplet(0x0B)); // LibraryAppletCabinet
    static_assert(!IsAllowedSystemApplet(0x16)); // unavailable LibraryAppletSet
    static_assert(IsAllowedSystemApplet(0x17));  // LibraryAppletOfflineWeb
    static_assert(IsAllowedSystemApplet(0x18));  // LibraryAppletLoginShare
    static_assert(IsAllowedSystemApplet(0x19));  // LibraryAppletWifiWebAuth
    static_assert(IsAllowedSystemApplet(0x1A));  // LibraryAppletMyPage
    static_assert(!IsAllowedSystemApplet(0x1B)); // unknown/unsupported id

    // Shared command/payload validation is used by production ParseMenuCommand.
    static_assert(IsValidCommandPayloadSize(1, 1, 0));      // MenuReady v1
    static_assert(IsValidCommandPayloadSize(1, 2, 8));      // legacy LaunchTitle
    static_assert(!IsValidCommandPayloadSize(1, 2, 32));    // v2 payload needs v2
    static_assert(IsValidCommandPayloadSize(2, 2, 32));     // current LaunchTitle
    static_assert(IsValidCommandPayloadSize(2, 2, 8));      // legacy-size compatibility
    static_assert(!IsValidCommandPayloadSize(2, 2, 16));    // malformed title payload
    static_assert(!IsValidCommandPayloadSize(1, 6, 4));     // Busy requires v2
    static_assert(IsValidCommandPayloadSize(2, 6, 4));
    static_assert(!IsValidCommandPayloadSize(2, 7, 0xFFFFFFFFULL));
    static_assert(!IsValidCommandPayloadSize(2, 11, 0));    // empty log line
    static_assert(IsValidCommandPayloadSize(2, 11, 1));
    static_assert(IsValidCommandPayloadSize(2, 11, MaxLogLineBytes));
    static_assert(!IsValidCommandPayloadSize(2, 11, MaxLogLineBytes + 1));
    static_assert(!IsValidCommandPayloadSize(2, 999, 0));   // unknown command
}
