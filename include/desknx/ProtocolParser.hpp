#pragma once

// Host-testable IPC message parser (IPC-01, IPC-02, IPC-07). The backend and
// the menu both feed raw storage bytes through ParseMessage; this file must
// stay free of libnx so the host unit tests can compile it directly.

#include <cstddef>
#include <cstdint>

namespace desknx::ipc {

constexpr std::uint32_t ParserMagic = 0x584E4B44; // "DKNX"

struct ParserHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t header_size;
    std::uint32_t command;
    std::uint32_t payload_size;
};
static_assert(sizeof(ParserHeader) == 16);

enum class ParseStatus : std::uint32_t {
    Accepted = 0,
    IgnoredUnknownCommand, // IPC-04: not an error for the menu, counted separately
    BadMagic,
    BadVersion,
    BadHeaderSize,
    BadPayloadSize,
    Truncated,
};

// Validated view of one message. The pointers alias the input buffer and stay
// valid only while the buffer is alive.
struct ParsedMessage {
    std::uint32_t command{0};
    std::uint16_t version{0};
    const std::uint8_t* payload{nullptr};
    std::uint32_t payload_size{0};
};

bool ParseMessage(const void* data, std::size_t total_size,
                  ParseStatus& status, ParsedMessage& message);

// Field extraction with range checks. All return false on malformed input;
// they never read outside [payload, payload + payload_size).
bool ReadU32(const ParsedMessage&, std::size_t offset, std::uint32_t& out);
bool ReadU64(const ParsedMessage&, std::size_t offset, std::uint64_t& out);
bool ReadUid(const ParsedMessage&, std::size_t offset,
             std::uint64_t& uid_high, std::uint64_t& uid_low);

// Copies a NUL-terminated string of at most capacity-1 characters. Fails when
// the payload region contains no terminator within the given length
// (IPC-02: strings must be terminated).
bool ReadString(const ParsedMessage&, std::size_t offset, std::size_t max_length,
                char* out, std::size_t capacity);

} // namespace desknx::ipc
