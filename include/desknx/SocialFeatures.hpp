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
