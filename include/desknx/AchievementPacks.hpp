#pragma once

// Achievement packs domain: pack file format, achievement state,
// memory-watch rules, and unlock/progress tracking.
//
// Mirrors HomeNX projects/achievements/packs/*.txt format:
//
//   game <TitleId> <Name>           -- one game per file
//   a <id> | <Name> | <Description> -- achievement declaration
//   build <BuildId>                  -- CPU build ID (hex, may repeat)
//   c <id> | <type> <addr> <op> <value>  -- condition for the achievement
//
// The memory-watch engine reads cheat-db-style addresses (`heap+OFFSET`),
// compares a typed value against a threshold, and sets the achievement
// unlocked when the condition is first met.
//
// What is NOT here:
//   - DMNT access (belong in a cheat-service wrapper)
//   - Tesla overlay rendering
//   - File I/O (pack loading belongs in PackScanner)

#include <cstdint>
#include <string>
#include <vector>

namespace desknx::achievements {

using TitleId = std::uint64_t;

// ---- Memory watch types --------------------------------------------------
//
// Matches HomeNX pack condition types: u8, u16, u32, u64 at heap+offset.

enum class WatchType : unsigned char {
    U8, U16, U32, U64,
};

enum class WatchOp : unsigned char {
    GreaterOrEqual,  // ">= value" (most common in packs)
    Equal,           // "== value"
    NotEqual,
};

struct WatchCondition {
    WatchType   type{WatchType::U8};
    WatchOp     op{WatchOp::GreaterOrEqual};
    std::string addressExpr;  // "heap+OFFSET" as parsed from the pack
    std::uint64_t threshold{0};
    std::string buildId;      // the CPU build this condition applies to
};

// ---- Achievement declaration ---------------------------------------------

struct AchievementDecl {
    std::string              id;           // "purple.cap"
    std::string              name;         // "Cap collector"
    std::string              description;  // "Find all 50 purple coins..."
    std::vector<WatchCondition> conditions; // any condition satisfies unlock
};

// ---- Pack (one .txt file, one game) --------------------------------------

struct Pack {
    TitleId                       titleId{0};
    std::string                   gameName;
    std::vector<AchievementDecl>  achievements;

    const AchievementDecl* findById(const std::string& id) const {
        for (const auto& a : achievements)
            if (a.id == id) return &a;
        return nullptr;
    }
};

// ---- Achievement state (per title, persisted) ----------------------------

struct AchievementState {
    std::string id;
    bool        unlocked{false};
    std::int64_t unlockedAt{0};  // POSIX seconds

    void unlock(std::int64_t when) { unlocked = true; unlockedAt = when; }
};

// ---- Progress tracker (in memory, fed by the memory-watch engine) --------

class AchievementTracker final {
public:
    void setPack(const Pack* pack) {
        m_pack = pack;
        m_states.clear();
        if (!pack) return;
        for (const auto& a : pack->achievements) {
            AchievementState s;
            s.id = a.id;
            m_states.push_back(s);
        }
    }

    const Pack* pack() const { return m_pack; }
    std::size_t total()    const { return m_states.size(); }

    std::size_t unlockedCount() const {
        std::size_t n = 0;
        for (const auto& s : m_states) if (s.unlocked) ++n;
        return n;
    }

    // Returns true when the achievement was just unlocked (first time).
    bool tryUnlock(const std::string& id, std::int64_t when) {
        for (auto& s : m_states) {
            if (s.id == id) {
                if (s.unlocked) return false;  // already unlocked
                s.unlock(when);
                return true;
            }
        }
        return false;  // unknown id
    }

    bool isUnlocked(const std::string& id) const {
        for (const auto& s : m_states)
            if (s.id == id) return s.unlocked;
        return false;
    }

    const std::vector<AchievementState>& states() const { return m_states; }

    // Reset all unlocks (for testing / "reset progress").
    void reset() {
        for (auto& s : m_states) { s.unlocked = false; s.unlockedAt = 0; }
    }

private:
    const Pack*                    m_pack{nullptr};
    std::vector<AchievementState>  m_states;
};

// ---- Address parsing helpers ---------------------------------------------
//
// A pack address is `heap+HEXOFFSET`.  The parser produces a numeric offset
// and a region tag so the memory-watch engine can look up the right segment.

enum class AddressRegion : unsigned char { Heap, Unknown };

struct ParsedAddress {
    AddressRegion region{AddressRegion::Unknown};
    std::uint64_t offset{0};
    bool          valid{false};
};

inline ParsedAddress parseAddress(const std::string& expr) {
    ParsedAddress r;
    if (expr.substr(0, 5) == "heap+") {
        r.region = AddressRegion::Heap;
        try {
            std::size_t pos;
            r.offset = std::stoull(expr.substr(5), &pos, 16);
            r.valid = (pos == expr.size() - 5);
        } catch (...) {}
    }
    return r;
}

} // namespace desknx::achievements
