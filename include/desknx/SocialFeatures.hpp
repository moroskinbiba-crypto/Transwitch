#pragma once

// Social feature domain: friends list, Mii store, Discord presence.
//
// Mirrors HomeNX FriendStore, MiiStore, DiscordService without side effects.
// No IPC to friend:a / mii:e / nimble-authnet, no HTTP to Discord Gateway.

#include <cstdint>
#include <string>
#include <vector>

namespace desknx::social {

// ---- Friends (FriendStore.hpp + SocialScreen) -----------------------------

enum class FriendStatus : unsigned char {
    Offline,
    Online,
    PlayingOnline,
};

struct Friend {
    std::uint64_t networkId{0};
    std::string   nickname;
    FriendStatus  status{FriendStatus::Offline};
    std::int64_t  lastOnline{0};  // POSIX seconds, 0 = unknown
    bool          favourite{false};
    std::vector<std::uint8_t> avatar;  // JPEG from friends service
};

class FriendList final {
public:
    enum class LoadStatus : unsigned char { Idle, Loading, Ready, Failed };

    LoadStatus           status{LoadStatus::Idle};
    std::vector<Friend>  friends;
    std::string          error;
    int                  selectedIndex{0};

    bool isLoading() const { return status == LoadStatus::Loading; }
    bool isReady()   const { return status == LoadStatus::Ready; }

    void setLoading() { status = LoadStatus::Loading; friends.clear(); error.clear(); }
    void setReady(std::vector<Friend> f) { status = LoadStatus::Ready; friends = std::move(f); }
    void setFailed(const std::string& msg) { status = LoadStatus::Failed; error = msg; }

    void moveSelection(int dir) {
        if (friends.empty()) return;
        selectedIndex += dir;
        if (selectedIndex < 0) selectedIndex = static_cast<int>(friends.size()) - 1;
        if (selectedIndex >= static_cast<int>(friends.size())) selectedIndex = 0;
    }

    const Friend* selected() const {
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(friends.size()))
            return nullptr;
        return &friends[static_cast<std::size_t>(selectedIndex)];
    }
};

struct PlayRecord {
    std::uint64_t titleId{0};
    std::uint64_t minutes{0};
    std::uint32_t launches{0};
    std::int64_t  firstPlayed{0};  // POSIX seconds
    std::int64_t  lastPlayed{0};
    bool          haveTime{false};
};

// ---- Miis (MiiStore.hpp) --------------------------------------------------

// A Mii: the Nintendo CharInfo blob (96 B) + UTF-8 name + Mii Studio face URL.
struct Mii {
    char        charInfo[96]{};  // MiiCharInfo
    std::string name;
    std::string faceUrl;
};

class MiiCollection final {
public:
    void add(Mii mii) { m_miis.push_back(std::move(mii)); }
    void remove(std::size_t index) {
        if (index < m_miis.size()) m_miis.erase(m_miis.begin() + static_cast<long>(index));
    }
    void rename(std::size_t index, const std::string& name) {
        if (index < m_miis.size()) m_miis[index].name = name;
    }

    const std::vector<Mii>& all() const { return m_miis; }
    std::size_t count() const { return m_miis.size(); }

private:
    std::vector<Mii> m_miis;
};

// ---- Discord (DiscordService.hpp + SocialScreen) --------------------------

enum class DiscordState : unsigned char {
    SignedOut,
    QrLogin,
    TokenLogin,
    Connecting,
    Ready,
    Failed,
};

struct DiscordAccount {
    std::string id;
    std::string username;
    std::string globalName;
    std::string avatar;
    std::string token;

    std::string displayName() const {
        return globalName.empty() ? username : globalName;
    }
};

struct DiscordDm {
    std::uint64_t channelId{0};
    std::string   name;
    std::string   avatarUrl;
    std::string   status;     // "online" / "idle" / "dnd" / "offline"
    int           members{0}; // group DMs
    bool          unread{false};
    int           mentions{0};
};

struct DiscordServer {
    std::uint64_t id{0};
    std::string   name;
    std::string   iconUrl;
    std::string   initials;  // for servers without an icon
    bool          unread{false};
    int           mentions{0};
};

struct DiscordMessage {
    std::uint32_t color{0};       // role colour, 0 = default
    std::uint64_t authorId{0};
    std::string   author;
    std::string   avatarUrl;
    std::string   text;
    std::string   time;           // e.g. "18:05"
    std::int64_t  timestampMs{0};
    bool          mine{false};
};

class DiscordPresence final {
public:
    DiscordState                 state{DiscordState::SignedOut};
    std::vector<DiscordAccount>  accounts;
    int                          activeAccountIndex{0};
    std::vector<DiscordDm>       dms;
    std::vector<DiscordServer>   servers;
    std::vector<DiscordMessage>  messages;  // the current DM / channel
    std::uint64_t                activeChannelId{0};
    std::string                  errorMessage;

    void setState(DiscordState s) { state = s; if (s != DiscordState::Failed) errorMessage.clear(); }
    void setError(const std::string& msg) { state = DiscordState::Failed; errorMessage = msg; }

    const DiscordAccount* activeAccount() const {
        if (activeAccountIndex < 0 || activeAccountIndex >= static_cast<int>(accounts.size()))
            return nullptr;
        return &accounts[static_cast<std::size_t>(activeAccountIndex)];
    }

    void switchAccount(int index) {
        if (index >= 0 && index < static_cast<int>(accounts.size())) {
            activeAccountIndex = index;
        }
    }

    void loadDms(std::vector<DiscordDm> dm) { dms = std::move(dm); }
    void loadServers(std::vector<DiscordServer> s) { servers = std::move(s); }
    void loadMessages(std::uint64_t channelId, std::vector<DiscordMessage> msgs) {
        activeChannelId = channelId;
        messages = std::move(msgs);
    }
};

} // namespace desknx::social

{"content":"#pragma once\n\n// Social feature domain: friends list, Mii store, Discord presence.\n//\n// Mirrors HomeNX FriendStore, MiiStore, DiscordService without side effects.\n// No IPC to friend:a / mii:e / nimble-authnet, no HTTP to Discord Gateway.\n\n#include <cstdint>\n#include <string>\n#include <vector>\n\nnamespace desknx::social {\n\n// ---- Friends (FriendStore.hpp + SocialScreen) -----------------------------\n\nenum class FriendStatus : unsigned char {\n    Offline,\n    Online,\n    PlayingOnline,\n};\n\nstruct Friend {\n    std::uint64_t networkId{0};\n    std::string   nickname;\n    FriendStatus  status{FriendStatus::Offline};\n    std::int64_t  lastOnline{0};  // POSIX seconds, 0 = unknown\n    bool          favourite{false};\n    std::vector<std::uint8_t> avatar;  // JPEG from friends service\n};\n\nclass FriendList final {\npublic:\n    enum class LoadStatus : unsigned char { Idle, Loading, Ready, Failed };\n\n    LoadStatus           status{LoadStatus::Idle};\n    std::vector<Friend>  friends;\n    std::string          error;\n    int                  selectedIndex{0};\n\n    bool isLoading() const { return status == LoadStatus::Loading; }\n    bool isReady()   const { return status == LoadStatus::Ready; }\n\n    void setLoading() { status = LoadStatus::Loading; friends.clear(); error.clear(); }\n    void setReady(std::vector<Friend> f) { status = LoadStatus::Ready; friends = std::move(f); }\n    void setFailed(const std::string& msg) { status = LoadStatus::Failed; error = msg; }\n\n    void moveSelection(int dir) {\n        if (friends.empty()) return;\n        selectedIndex += dir;\n        if (selectedIndex < 0) selectedIndex = static_cast<int>(friends.size()) - 1;\n        if (selectedIndex >= static_cast<int>(friends.size())) selectedIndex = 0;\n    }\n\n    const Friend* selected() const {\n        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(friends.size()))\n            return nullptr;\n        return &friends[static_cast<std::size_t>(selectedIndex)];\n    }\n};\n\nstruct PlayRecord {\n    std::uint64_t titleId{0};\n    std::uint64_t minutes{0};\n    std::uint32_t launches{0};\n    std::int64_t  firstPlayed{0};  // POSIX seconds\n    std::int64_t  lastPlayed{0};\n    bool          haveTime{false};\n};\n\n// ---- Miis (MiiStore.hpp) --------------------------------------------------\n\n// A Mii: the Nintendo CharInfo blob (96 B) + UTF-8 name + Mii Studio face URL.\nstruct Mii {\n    char        charInfo[96]{};  // MiiCharInfo\n    std::string name;\n    std::string faceUrl;\n};\n\nclass MiiCollection final {\npublic:\n    void add(Mii mii) { m_miis.push_back(std::move(mii)); }\n    void remove(std::size_t index) {\n        if (index < m_miis.size()) m_miis.erase(m_miis.begin() + static_cast<long>(index));\n    }\n    void rename(std::size_t index, const std::string& name) {\n        if (index < m_miis.size()) m_miis[index].name = name;\n    }\n\n    const std::vector<Mii>& all() const { return m_miis; }\n    std::size_t count() const { return m_miis.size(); }\n\nprivate:\n    std::vector<Mii> m_miis;\n};\n\n// ---- Discord (DiscordService.hpp + SocialScreen) --------------------------\n\nenum class DiscordState : unsigned char {\n    SignedOut,\n    QrLogin,\n    TokenLogin,\n    Connecting,\n    Ready,\n    Failed,\n};\n\nstruct DiscordAccount {\n    std::string id;\n    std::string username;\n    std::string globalName;\n    std::string avatar;\n    std::string token;\n\n    std::string displayName() const {\n        return globalName.empty() ? username : globalName;\n    }\n};\n\nstruct DiscordDm {\n    std::uint64_t channelId{0};\n    std::string   name;\n    std::string   avatarUrl;\n    std::string   status;     // \"online\" / \"idle\" / \"dnd\" / \"offline\"\n    int           members{0}; // group DMs\n    bool          unread{false};\n    int           mentions{0};\n};\n\nstruct DiscordServer {\n    std::uint64_t id{0};\n    std::string   name;\n    std::string   iconUrl;\n    std::string   initials;  // for servers without an icon\n    bool          unread{false};\n    int           mentions{0};\n};\n\nstruct DiscordMessage {\n    std::uint32_t color{0};       // role colour, 0 = default\n    std::uint64_t authorId{0};\n    std::string   author;\n    std::string   avatarUrl;\n    std::string   text;\n    std::string   time;           // e.g. \"18:05\"\n    std::int64_t  timestampMs{0};\n    bool          mine{false};\n};\n\nclass DiscordPresence final {\npublic:\n    DiscordState                 state{DiscordState::SignedOut};\n    std::vector<DiscordAccount>  accounts;\n    int                          activeAccountIndex{0};\n    std::vector<DiscordDm>       dms;\n    std::vector<DiscordServer>   servers;\n    std::vector<DiscordMessage>  messages;  // the current DM / channel\n    std::uint64_t                activeChannelId{0};\n    std::string                  errorMessage;\n\n    void setState(DiscordState s) { state = s; if (s != DiscordState::Failed) errorMessage.clear(); }\n    void setError(const std::string& msg) { state = DiscordState::Failed; errorMessage = msg; }\n\n    const DiscordAccount* activeAccount() const {\n        if (activeAccountIndex < 0 || activeAccountIndex >= static_cast<int>(accounts.size()))\n            return nullptr;\n        return &accounts[static_cast<std::size_t>(activeAccountIndex)];\n    }\n\n    void switchAccount(int index) {\n        if (index >= 0 && index < static_cast<int>(accounts.size())) {\n            activeAccountIndex = index;\n        }\n    }\n\n    void loadDms(std::vector<DiscordDm> dm) { dms = std::move(dm); }\n    void loadServers(std::vector<DiscordServer> s) { servers = std::move(s); }\n    void loadMessages(std::uint64_t channelId, std::vector<DiscordMessage> msgs) {\n        activeChannelId = channelId;\n        messages = std::move(msgs);\n    }\n};\n\n} // namespace desknx::social\n"}