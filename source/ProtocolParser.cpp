#include "desknx/ProtocolParser.hpp"
#include "desknx/ProtocolRules.hpp"

#include <cstring>

namespace desknx::ipc {
namespace {

std::uint32_t LoadU32(const std::uint8_t* at) {
    std::uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value; // little-endian target; host tests run little-endian too
}

std::uint64_t LoadU64(const std::uint8_t* at) {
    std::uint64_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

} // namespace

bool ParseMessage(const void* data, std::size_t total_size,
                  ParseStatus& status, ParsedMessage& message) {
    message = ParsedMessage{};
    status = ParseStatus::Truncated;
    if (!data || total_size < sizeof(ParserHeader)) return false;

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    ParserHeader header{};
    header.magic = LoadU32(bytes);
    header.version = static_cast<std::uint16_t>(LoadU32(bytes + 4) & 0xFFFFu);
    header.header_size = static_cast<std::uint16_t>(LoadU32(bytes + 4) >> 16);
    header.command = LoadU32(bytes + 8);
    header.payload_size = LoadU32(bytes + 12);

    if (header.magic != ParserMagic) { status = ParseStatus::BadMagic; return false; }
    if (!rules::IsSupportedVersion(header.version)) { status = ParseStatus::BadVersion; return false; }
    if (header.header_size != sizeof(ParserHeader)) { status = ParseStatus::BadHeaderSize; return false; }
    if (!rules::IsValidPayloadSize(header.payload_size)) { status = ParseStatus::BadPayloadSize; return false; }
    // The declared payload must fit the bytes actually delivered.
    if (static_cast<std::uint64_t>(header.header_size) + header.payload_size > total_size) {
        status = ParseStatus::Truncated;
        return false;
    }

    const bool known = rules::IsKnownCommand(header.command);
    if (known && !rules::IsValidCommandPayloadSize(
            header.version, header.command, header.payload_size)) {
        status = ParseStatus::BadPayloadSize;
        return false;
    }

    message.command = header.command;
    message.version = header.version;
    message.payload = bytes + header.header_size;
    message.payload_size = header.payload_size;
    // Unknown commands parse cleanly but are reported as ignored so callers
    // can count them without treating the menu as broken (IPC-04).
    status = known ? ParseStatus::Accepted : ParseStatus::IgnoredUnknownCommand;
    return known;
}

bool ReadU32(const ParsedMessage& message, std::size_t offset, std::uint32_t& out) {
    if (!message.payload || offset > message.payload_size ||
        sizeof(std::uint32_t) > message.payload_size - offset)
        return false;
    out = LoadU32(message.payload + offset);
    return true;
}

bool ReadU64(const ParsedMessage& message, std::size_t offset, std::uint64_t& out) {
    if (!message.payload || offset > message.payload_size ||
        sizeof(std::uint64_t) > message.payload_size - offset)
        return false;
    out = LoadU64(message.payload + offset);
    return true;
}

bool ReadUid(const ParsedMessage& message, std::size_t offset,
             std::uint64_t& uid_high, std::uint64_t& uid_low) {
    return ReadU64(message, offset, uid_high) && ReadU64(message, offset + 8, uid_low);
}

bool ReadString(const ParsedMessage& message, std::size_t offset, std::size_t max_length,
                char* out, std::size_t capacity) {
    if (!message.payload || !out || capacity == 0 || offset > message.payload_size) return false;
    const std::size_t available = message.payload_size - offset;
    const std::size_t window = available < max_length ? available : max_length;
    for (std::size_t i = 0; i < window; ++i) {
        if (message.payload[offset + i] == 0) {
            if (i + 1 > capacity) return false; // would not fit with terminator
            std::memcpy(out, message.payload + offset, i);
            out[i] = '\0';
            return true;
        }
    }
    return false; // no NUL terminator within bounds (IPC-02)
}

} // namespace desknx::ipc
