// Host unit tests for the raw-bytes IPC parser (IPC-01, IPC-02, IPC-04, IPC-07).
// Covers: bad magic, unsupported version, wrong header size, payload-size
// mismatches, truncation, unknown commands, boundary values and unterminated
// strings. Must compile without libnx.

#include "desknx/ProtocolParser.hpp"
#include "desknx/ProtocolRules.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace desknx::ipc;

namespace {

std::vector<std::uint8_t> makeMessage(std::uint32_t command, std::uint16_t version,
                                      const void* payload, std::uint32_t payload_size,
                                      std::uint32_t declared_size = 0xFFFFFFFFu,
                                      std::uint16_t header_size = 16,
                                      std::uint32_t magic = ParserMagic) {
    if (declared_size == 0xFFFFFFFFu) declared_size = payload_size;
    std::vector<std::uint8_t> buf(16 + payload_size, 0);
    std::uint32_t m = magic;      std::memcpy(buf.data() + 0, &m, 4);
    std::uint32_t vh = (static_cast<std::uint32_t>(header_size) << 16) | version;
    std::memcpy(buf.data() + 4, &vh, 4);
    std::memcpy(buf.data() + 8, &command, 4);
    std::memcpy(buf.data() + 12, &declared_size, 4);
    if (payload && payload_size) std::memcpy(buf.data() + 16, payload, payload_size);
    return buf;
}

ParseStatus status{};
ParsedMessage msg{};

bool parse(const std::vector<std::uint8_t>& buf, std::size_t delivered = 0) {
    const std::size_t n = delivered ? delivered : buf.size();
    return ParseMessage(buf.data(), n, status, msg);
}

void testHappyPathV2() {
    const std::uint64_t title = 0x0100000000012345ULL;
    auto buf = makeMessage(2 /*LaunchTitle*/, 2, &title, sizeof(title));
    assert(parse(buf));
    assert(status == ParseStatus::Accepted);
    assert(msg.command == 2);
    assert(msg.version == 2);
    assert(msg.payload_size == 8);
    std::uint64_t got = 0;
    assert(ReadU64(msg, 0, got));
    assert(got == title);
    // Reading past the payload must fail, never overrun.
    assert(!ReadU64(msg, 4, got));
    assert(!ReadU32(msg, 6, *reinterpret_cast<std::uint32_t*>(&got)));
}

void testLegacyV1LaunchTitle() {
    const std::uint64_t title = 42;
    auto buf = makeMessage(2, 1, &title, sizeof(title));
    assert(parse(buf));
    assert(status == ParseStatus::Accepted);
    assert(msg.version == 1);
}

void testLaunchTitleV2WithUid() {
    // v2 layout: title_id(8) + flags(8) + AccountUid(16) = 32 bytes total.
    struct { std::uint64_t title_id, flags, uid_hi, uid_lo; } p{7, 3, 0xDEAD, 0xBEEF};
    static_assert(sizeof(p) == 32);
    auto buf = makeMessage(2, 2, &p, sizeof(p));
    assert(parse(buf));
    assert(status == ParseStatus::Accepted);
    std::uint64_t title = 0, flags = 0, hi = 0, lo = 0;
    assert(ReadU64(msg, 0, title));
    assert(ReadU64(msg, 8, flags));
    assert(ReadUid(msg, 16, hi, lo));
    assert(title == 7 && flags == 3 && hi == 0xDEAD && lo == 0xBEEF);
}

void testLaunchTitleV2WrongSize() {
    // A 24-byte LaunchTitle payload must be rejected (IPC-01 strict sizes).
    std::uint8_t p[24] = {0};
    auto buf = makeMessage(2, 2, &p, sizeof(p));
    assert(!parse(buf));
    assert(status == ParseStatus::BadPayloadSize);
}

void testEmptyPayloadCommands() {
    for (std::uint32_t cmd : {1u /*MenuReady*/, 3u /*ReturnToGame*/, 4u /*Heartbeat*/, 7u /*CloseApp*/}) {
        auto buf = makeMessage(cmd, 2, nullptr, 0);
        assert(parse(buf));
        assert(status == ParseStatus::Accepted);
        assert(msg.payload_size == 0);
    }
}

void testBadMagic() {
    auto buf = makeMessage(1, 2, nullptr, 0, 0, 16, 0x12345678u);
    assert(!parse(buf));
    assert(status == ParseStatus::BadMagic);
}

void testBadVersion() {
    auto buf = makeMessage(1, 3, nullptr, 0);
    assert(!parse(buf));
    assert(status == ParseStatus::BadVersion);
    buf = makeMessage(1, 0, nullptr, 0);
    assert(!parse(buf));
    assert(status == ParseStatus::BadVersion);
}

void testBadHeaderSize() {
    auto buf = makeMessage(1, 2, nullptr, 0, 0, 20);
    assert(!parse(buf));
    assert(status == ParseStatus::BadHeaderSize);
}

void testOversizedPayload() {
    auto buf = makeMessage(1, 2, nullptr, 0, desknx::ipc::rules::MaxPayloadSize + 1);
    assert(!parse(buf));
    assert(status == ParseStatus::BadPayloadSize);
    // Exactly at the limit is size-valid but fails on delivery/truncation.
    buf = makeMessage(4, 2, nullptr, 0, desknx::ipc::rules::MaxPayloadSize);
    assert(!parse(buf));
    assert(status == ParseStatus::Truncated);
}

void testTruncatedDelivery() {
    const char s[] = "hello";
    auto buf = makeMessage(11 /*Log*/, 2, s, sizeof(s));
    assert(!parse(buf, 16 + 3)); // declared 6, only 3 payload bytes delivered
    assert(status == ParseStatus::Truncated);
    assert(!parse(buf, 10));     // not even a full header
    assert(status == ParseStatus::Truncated);
}

void testWrongFixedPayloadSize() {
    const std::uint32_t seconds = 5;
    auto buf = makeMessage(6 /*Busy*/, 2, &seconds, 4);
    assert(parse(buf));
    buf = makeMessage(6, 2, &seconds, 3);
    assert(!parse(buf));
    assert(status == ParseStatus::BadPayloadSize);
    // LaunchTitle with an illegal intermediate size.
    std::uint64_t title = 1;
    buf = makeMessage(2, 2, &title, 16);
    assert(!parse(buf));
    assert(status == ParseStatus::BadPayloadSize);
}

void testUnknownCommandIgnored() {
    auto buf = makeMessage(999, 2, nullptr, 0);
    assert(!parse(buf));               // returns false...
    assert(status == ParseStatus::IgnoredUnknownCommand); // ...but not an error
    // A future backend sending a new big command id beyond known range.
    buf = makeMessage(5000, 2, nullptr, 0);
    assert(!parse(buf));
    assert(status == ParseStatus::IgnoredUnknownCommand);
}

void testStringsMustTerminate() {
    char out[64];
    const char good[] = "sdmc:/switch/a.nro";
    auto buf = makeMessage(11, 2, good, sizeof(good)); // includes NUL
    assert(parse(buf));
    assert(ReadString(msg, 0, 200, out, sizeof(out)));
    assert(!std::strcmp(out, "sdmc:/switch/a.nro"));

    // No terminator within the window -> reject (IPC-02).
    char nasty[32];
    std::memset(nasty, 'A', sizeof(nasty));
    buf = makeMessage(11, 2, nasty, sizeof(nasty));
    assert(parse(buf));
    assert(!ReadString(msg, 0, 200, out, sizeof(out)));

    // Terminator exactly at capacity boundary.
    const char edge[] = "abcde";
    buf = makeMessage(11, 2, edge, sizeof(edge));
    assert(parse(buf));
    assert(ReadString(msg, 0, 200, out, 6));   // "abcde" + NUL fits
    assert(!ReadString(msg, 0, 200, out, 5));  // would truncate -> reject
}

void testLogBounds() {
    char line[201];
    std::memset(line, 'x', sizeof(line));
    line[200] = 0;
    auto buf = makeMessage(11, 2, line, 200); // exactly MaxLogLineBytes
    assert(parse(buf));
    assert(status == ParseStatus::Accepted);
    buf = makeMessage(11, 2, line, 201);      // one over MaxLogLineBytes
    assert(!parse(buf));
    assert(status == ParseStatus::BadPayloadSize);
    buf = makeMessage(11, 2, nullptr, 0);     // empty Log payload rejected
    assert(!parse(buf));
    assert(status == ParseStatus::BadPayloadSize);
}

void testNullAndTinyInput() {
    assert(!ParseMessage(nullptr, 100, status, msg));
    assert(status == ParseStatus::Truncated);
    std::uint8_t tiny[15] = {0};
    assert(!ParseMessage(tiny, sizeof(tiny), status, msg));
    assert(status == ParseStatus::Truncated);
}

} // namespace

int main() {
    testHappyPathV2();
    testLegacyV1LaunchTitle();
    testLaunchTitleV2WithUid();
    testLaunchTitleV2WrongSize();
    testEmptyPayloadCommands();
    testBadMagic();
    testBadVersion();
    testBadHeaderSize();
    testOversizedPayload();
    testTruncatedDelivery();
    testWrongFixedPayloadSize();
    testUnknownCommandIgnored();
    testStringsMustTerminate();
    testLogBounds();
    testNullAndTinyInput();
    std::puts("protocol_parser_test: all assertions passed");
    return 0;
}
