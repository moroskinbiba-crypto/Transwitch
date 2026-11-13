#pragma once

// Profile carousel domain: the state machine for the HOME profile picker.
//
// Mirrors HomeNX ProfileCarouselScreen: a scrolling row of profile tiles
// (every account on the console + an "add user" tile), a currently-selected
// and optionally-locked profile, and the presentation rules for
// accessibility announcements.
//
// What is NOT here:
//   - Switch account service calls (AccountUid from libnx)
//   - Avatar textures (belong in the renderer)
//   - Animation timers (belong in nxui::AnimatedFloat)

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace desknx::profile {

// Lightweight account ID wrapper so this header does not need libnx.
struct Uid {
    std::uint64_t lo{0};
    std::uint64_t hi{0};
    bool operator==(const Uid& o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const Uid& o) const { return !(*this == o); }
};

struct Profile {
    Uid         uid{};
    std::string nickname;
    // avatar is owned by the renderer — only its cache key lives here.
    std::string avatarCacheKey;
};

enum class CardKind : unsigned char {
    Profile,   // a real console account
    AddUser,   // the trailing "+" card
};

class CarouselState final {
public:
    void setProfiles(std::vector<Profile> profiles, bool addUserAvailable) {
        m_profiles         = std::move(profiles);
        m_addUserAvailable = addUserAvailable;
        // Clamp selection in case the new list is shorter.
        if (m_selected >= tileCount()) m_selected = tileCount() - 1;
        if (m_selected < 0) m_selected = 0;
    }

    // ---- Visibility -------------------------------------------------------

    void show() { m_active = true; m_animatingOut = false; }
    void hide() { m_animatingOut = true; }
    void finishHide() { m_active = false; m_animatingOut = false; }

    bool isActive()  const { return m_active || m_animatingOut; }
    bool isOpen()    const { return m_active && !m_animatingOut; }

    // ---- Navigation -------------------------------------------------------

    int      selected()     const { return m_selected; }
    CardKind selectedKind() const { return tileKind(m_selected); }
    bool     selectedIsLocked() const {
        return tileKind(m_selected) == CardKind::Profile
            && m_lockedUid.has_value()
            && selectedProfile()
            && selectedProfile()->uid == *m_lockedUid;
    }

    void moveHorizontal(int dir) {
        const int n = tileCount();
        if (n == 0) return;
        m_selected = (m_selected + dir + n) % n;
    }

    void selectTile(int index) {
        if (index >= 0 && index < tileCount())
            m_selected = index;
    }

    // ---- Lock / unlock ----------------------------------------------------

    void setLockedUid(std::optional<Uid> uid) { m_lockedUid = uid; }
    std::optional<Uid> lockedUid() const { return m_lockedUid; }

    void toggleLockSelected() {
        if (tileKind(m_selected) != CardKind::Profile) return;
        const Profile* p = selectedProfile();
        if (!p) return;
        if (m_lockedUid.has_value() && *m_lockedUid == p->uid)
            m_lockedUid = std::nullopt;  // unlock
        else
            m_lockedUid = p->uid;        // lock to this profile
    }

    // ---- Accessors --------------------------------------------------------

    int tileCount() const {
        const int n = static_cast<int>(m_profiles.size());
        return n > 0 ? n + (m_addUserAvailable ? 1 : 0)
                     : (m_addUserAvailable ? 1 : 0);
    }

    CardKind tileKind(int index) const {
        if (index < static_cast<int>(m_profiles.size())) return CardKind::Profile;
        return CardKind::AddUser;
    }

    const Profile* selectedProfile() const {
        if (tileKind(m_selected) != CardKind::Profile) return nullptr;
        if (m_selected < 0 || m_selected >= static_cast<int>(m_profiles.size()))
            return nullptr;
        return &m_profiles[static_cast<std::size_t>(m_selected)];
    }

    const Profile* profileAt(int index) const {
        if (index < 0 || index >= static_cast<int>(m_profiles.size())) return nullptr;
        return &m_profiles[static_cast<std::size_t>(index)];
    }

    const std::vector<Profile>& profiles() const { return m_profiles; }
    bool addUserAvailable() const { return m_addUserAvailable; }

    // Accessibility announcement: "Profile N of M: <name>", or "Add user".
    std::string announcement() const {
        if (tileKind(m_selected) == CardKind::AddUser) return "Add user";
        const Profile* p = selectedProfile();
        if (!p) return {};
        const int pos = m_selected + 1;
        const int total = static_cast<int>(m_profiles.size());
        return p->nickname + " (" + std::to_string(pos) + " of " + std::to_string(total) + ")";
    }

private:
    std::vector<Profile>  m_profiles;
    bool                  m_addUserAvailable{false};
    bool                  m_active{false};
    bool                  m_animatingOut{false};
    int                   m_selected{0};
    std::optional<Uid>    m_lockedUid;
};

} // namespace desknx::profile

{"content":"#pragma once\n\n// Profile carousel domain: the state machine for the HOME profile picker.\n//\n// Mirrors HomeNX ProfileCarouselScreen: a scrolling row of profile tiles\n// (every account on the console + an \"add user\" tile), a currently-selected\n// and optionally-locked profile, and the presentation rules for\n// accessibility announcements.\n//\n// What is NOT here:\n//   - Switch account service calls (AccountUid from libnx)\n//   - Avatar textures (belong in the renderer)\n//   - Animation timers (belong in nxui::AnimatedFloat)\n\n#include <cstdint>\n#include <optional>\n#include <string>\n#include <vector>\n\nnamespace desknx::profile {\n\n// Lightweight account ID wrapper so this header does not need libnx.\nstruct Uid {\n    std::uint64_t lo{0};\n    std::uint64_t hi{0};\n    bool operator==(const Uid& o) const { return lo == o.lo && hi == o.hi; }\n    bool operator!=(const Uid& o) const { return !(*this == o); }\n};\n\nstruct Profile {\n    Uid         uid{};\n    std::string nickname;\n    // avatar is owned by the renderer — only its cache key lives here.\n    std::string avatarCacheKey;\n};\n\nenum class CardKind : unsigned char {\n    Profile,   // a real console account\n    AddUser,   // the trailing \"+\" card\n};\n\nclass CarouselState final {\npublic:\n    void setProfiles(std::vector<Profile> profiles, bool addUserAvailable) {\n        m_profiles         = std::move(profiles);\n        m_addUserAvailable = addUserAvailable;\n        // Clamp selection in case the new list is shorter.\n        if (m_selected >= tileCount()) m_selected = tileCount() - 1;\n        if (m_selected < 0) m_selected = 0;\n    }\n\n    // ---- Visibility -------------------------------------------------------\n\n    void show() { m_active = true; m_animatingOut = false; }\n    void hide() { m_animatingOut = true; }\n    void finishHide() { m_active = false; m_animatingOut = false; }\n\n    bool isActive()  const { return m_active || m_animatingOut; }\n    bool isOpen()    const { return m_active && !m_animatingOut; }\n\n    // ---- Navigation -------------------------------------------------------\n\n    int      selected()     const { return m_selected; }\n    CardKind selectedKind() const { return tileKind(m_selected); }\n    bool     selectedIsLocked() const {\n        return tileKind(m_selected) == CardKind::Profile\n            && m_lockedUid.has_value()\n            && selectedProfile()\n            && selectedProfile()->uid == *m_lockedUid;\n    }\n\n    void moveHorizontal(int dir) {\n        const int n = tileCount();\n        if (n == 0) return;\n        m_selected = (m_selected + dir + n) % n;\n    }\n\n    void selectTile(int index) {\n        if (index >= 0 && index < tileCount())\n            m_selected = index;\n    }\n\n    // ---- Lock / unlock ----------------------------------------------------\n\n    void setLockedUid(std::optional<Uid> uid) { m_lockedUid = uid; }\n    std::optional<Uid> lockedUid() const { return m_lockedUid; }\n\n    void toggleLockSelected() {\n        if (tileKind(m_selected) != CardKind::Profile) return;\n        const Profile* p = selectedProfile();\n        if (!p) return;\n        if (m_lockedUid.has_value() && *m_lockedUid == p->uid)\n            m_lockedUid = std::nullopt;  // unlock\n        else\n            m_lockedUid = p->uid;        // lock to this profile\n    }\n\n    // ---- Accessors --------------------------------------------------------\n\n    int tileCount() const {\n        const int n = static_cast<int>(m_profiles.size());\n        return n > 0 ? n + (m_addUserAvailable ? 1 : 0)\n                     : (m_addUserAvailable ? 1 : 0);\n    }\n\n    CardKind tileKind(int index) const {\n        if (index < static_cast<int>(m_profiles.size())) return CardKind::Profile;\n        return CardKind::AddUser;\n    }\n\n    const Profile* selectedProfile() const {\n        if (tileKind(m_selected) != CardKind::Profile) return nullptr;\n        if (m_selected < 0 || m_selected >= static_cast<int>(m_profiles.size()))\n            return nullptr;\n        return &m_profiles[static_cast<std::size_t>(m_selected)];\n    }\n\n    const Profile* profileAt(int index) const {\n        if (index < 0 || index >= static_cast<int>(m_profiles.size())) return nullptr;\n        return &m_profiles[static_cast<std::size_t>(index)];\n    }\n\n    const std::vector<Profile>& profiles() const { return m_profiles; }\n    bool addUserAvailable() const { return m_addUserAvailable; }\n\n    // Accessibility announcement: \"Profile N of M: <name>\", or \"Add user\".\n    std::string announcement() const {\n        if (tileKind(m_selected) == CardKind::AddUser) return \"Add user\";\n        const Profile* p = selectedProfile();\n        if (!p) return {};\n        const int pos = m_selected + 1;\n        const int total = static_cast<int>(m_profiles.size());\n        return p->nickname + \" (\" + std::to_string(pos) + \" of \" + std::to_string(total) + \")\";\n    }\n\nprivate:\n    std::vector<Profile>  m_profiles;\n    bool                  m_addUserAvailable{false};\n    bool                  m_active{false};\n    bool                  m_animatingOut{false};\n    int                   m_selected{0};\n    std::optional<Uid>    m_lockedUid;\n};\n\n} // namespace desknx::profile\n"}