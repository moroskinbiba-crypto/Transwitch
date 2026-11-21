#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
namespace nextendo {
using TitleId = std::uint64_t;
struct PlayerSnapshot {
    std::map<TitleId, unsigned> perTitle;
    std::map<TitleId, std::string> titleNames;
    unsigned total{0};
    std::uint64_t fetchedAt{0};
    bool fromCache{false};
};
constexpr unsigned MaxPlayers = 100000u;
constexpr std::size_t MaxTitles = 1024u;
} // namespace nextendo
