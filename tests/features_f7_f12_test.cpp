#include <desknx/ThemeShop.hpp>
#include <desknx/SocialFeatures.hpp>
#include <desknx/AchievementPacks.hpp>
#include <desknx/BluetoothSink.hpp>

#include <cassert>
#include <cstdio>

// ===== F7: ThemeShop =====================================================

void testInstallStateTransitions() {
    using namespace desknx::themeshop;
    InstallState s;
    assert(s.status == InstallStatus::Idle);
    assert(!s.isActive());
    s.setDownloading("theme-001");
    assert(s.isActive());
    assert(s.status == InstallStatus::Downloading);
    s.setUnpacking(0.5f);
    assert(s.isActive());
    s.setApplying(0.9f);
    assert(s.isActive());
    s.setDone("/sdmc:/switch/desknx/themes/theme-001");
    assert(s.isDone());
    assert(s.installed);
    assert(!s.isActive());
    s.reset();
    assert(s.status == InstallStatus::Idle);
}

void testInstallFailed() {
    using namespace desknx::themeshop;
    InstallState s;
    s.setDownloading("x");
    s.setFailed("network error");
    assert(s.isFailed());
    assert(!s.installed);
}

void testInstalledThemeList() {
    using namespace desknx::themeshop;
    InstalledThemeList list;
    list.add({ "t1", "Theme A", "Alice", "1.0", "/path/t1", false });
    list.add({ "t2", "Theme B", "Bob",   "2.0", "/path/t2", false });
    assert(list.count() == 2);
    list.setActive("t1");
    assert( list.isActive("t1"));
    assert(!list.isActive("t2"));
    list.setActive("t2");
    assert(!list.isActive("t1"));
    assert( list.isActive("t2"));
    // Update (same id)
    list.add({ "t1", "Theme A v2", "Alice", "1.1", "/path/t1v2", false });
    assert(list.count() == 2);
    assert(list.findById("t1")->version == "1.1");
    list.remove("t2");
    assert(list.count() == 1);
    assert(list.findById("t2") == nullptr);
}

void testCatalogState() {
    using namespace desknx::themeshop;
    CatalogState c;
    assert(c.status == CatalogState::LoadStatus::Idle);
    c.setLoading();
    assert(c.isLoading());
    std::vector<CatalogEntry> entries;
    entries.push_back({ "e1", "Entry 1", "Alice", "1.0", "", "", "", "", {} });
    entries.push_back({ "e2", "Entry 2", "Bob",   "2.0", "", "", "", "", {} });
    c.setReady(entries);
    assert(c.isReady());
    assert(c.entries.size() == 2);
    assert(c.selected() != nullptr && c.selected()->id == "e1");
    c.moveSelection(1);
    assert(c.selected()->id == "e2");
    c.moveSelection(1);
    assert(c.selected()->id == "e1");  // wrapped
    c.setFailed("timeout");
    assert(c.status == CatalogState::LoadStatus::Failed);
}

// ===== F8: Friends (social) ===============================================

void testFriendList() {
    using namespace desknx::social;
    FriendList fl;
    assert(!fl.isReady());
    fl.setLoading();
    assert(fl.isLoading());
    fl.setReady({
        { 1001, "Alice", FriendStatus::PlayingOnline, 0, true,  {} },
        { 1002, "Bob",   FriendStatus::Offline,       0, false, {} },
    });
    assert(fl.isReady());
    assert(fl.friends.size() == 2);
    assert(fl.selected() && fl.selected()->nickname == "Alice");
    fl.moveSelection(1);
    assert(fl.selected()->nickname == "Bob");
    fl.moveSelection(1);
    assert(fl.selected()->nickname == "Alice");  // wrapped
    fl.setFailed("service unavailable");
    assert(!fl.isReady() && !fl.isLoading());
}

// ===== F9: Mii ===========================================================

void testMiiCollection() {
    using namespace desknx::social;
    MiiCollection col;
    col.add({ {}, "Mii A", "url_a" });
    col.add({ {}, "Mii B", "url_b" });
    assert(col.count() == 2);
    col.rename(0, "Mii Alpha");
    assert(col.all()[0].name == "Mii Alpha");
    col.remove(0);
    assert(col.count() == 1);
    assert(col.all()[0].name == "Mii B");
}

// ===== F10: Discord presence ==============================================

void testDiscordPresence() {
    using namespace desknx::social;
    DiscordPresence d;
    assert(d.state == DiscordState::SignedOut);
    d.setState(DiscordState::Connecting);
    assert(d.state == DiscordState::Connecting);
    d.accounts.push_back({ "u1", "alice", "Alice", "", "tok1" });
    d.accounts.push_back({ "u2", "bob",   "Bob",   "", "tok2" });
    d.switchAccount(1);
    assert(d.activeAccount()->username == "bob");
    d.setState(DiscordState::Ready);
    assert(d.errorMessage.empty());
    d.setError("connection reset");
    assert(d.state == DiscordState::Failed);
    assert(!d.errorMessage.empty());
    d.loadDms({ { 1, "Eve", "", "online", 0, false, 0 } });
    assert(d.dms.size() == 1);
    d.loadServers({ { 42, "GameServer", "", "GS", false, 0 } });
    assert(d.servers.size() == 1);
    d.loadMessages(1, { { 0xFFFFFFFF, 999, "Eve", "", "hi", "10:00", 0, false } });
    assert(d.messages.size() == 1 && d.activeChannelId == 1);
}

// ===== F11: Achievement packs =============================================

void testTrackerUnlock() {
    using namespace desknx::achievements;
    Pack pack;
    pack.titleId = 0x0100000000010000ull;
    pack.gameName = "Test Game";
    pack.achievements.push_back({ "a.first", "First!", "Do it.", {} });
    pack.achievements.push_back({ "a.second", "Second!", "Do it again.", {} });

    AchievementTracker tracker;
    tracker.setPack(&pack);
    assert(tracker.total() == 2);
    assert(tracker.unlockedCount() == 0);
    assert(tracker.tryUnlock("a.first", 1000));
    assert(!tracker.tryUnlock("a.first", 1001));  // already unlocked
    assert(tracker.isUnlocked("a.first"));
    assert(!tracker.isUnlocked("a.second"));
    assert(tracker.unlockedCount() == 1);
    tracker.reset();
    assert(tracker.unlockedCount() == 0);
}

void testTrackerUnknownId() {
    using namespace desknx::achievements;
    Pack pack;
    pack.achievements.push_back({ "a.real", "Real", "", {} });
    AchievementTracker tracker;
    tracker.setPack(&pack);
    assert(!tracker.tryUnlock("a.fake", 1000));
}

void testTrackerNullPack() {
    using namespace desknx::achievements;
    AchievementTracker tracker;
    assert(tracker.total() == 0);
    assert(!tracker.tryUnlock("x", 0));
}

void testParseAddress() {
    using namespace desknx::achievements;
    auto a = parseAddress("heap+61C77360");
    assert(a.valid);
    assert(a.region == AddressRegion::Heap);
    assert(a.offset == 0x61C77360ull);
    auto b = parseAddress("stack+0");
    assert(!b.valid || b.region == AddressRegion::Unknown);
    auto c = parseAddress("heap+ZZZZ");
    assert(!c.valid);
}

void testFindAchievementInPack() {
    using namespace desknx::achievements;
    Pack pack;
    pack.achievements.push_back({ "a.foo", "Foo", "Bar", {} });
    assert(pack.findById("a.foo") != nullptr);
    assert(pack.findById("a.none") == nullptr);
}

// ===== F12: Bluetooth A2DP sink ===========================================

void testA2dpStateTransitions() {
    using namespace desknx::a2dp;
    A2dpSink sink;
    assert(sink.isIdle());
    assert(!sink.isStreaming());
    sink.startAdvertising();
    assert(sink.state() == SinkState::Advertising);
    sink.setConnecting("AA:BB:CC:DD:EE:FF", "Phone");
    assert(sink.state() == SinkState::Connecting);
    sink.setConnected(Codec::Sbc, 44100, 2, 320000);
    assert(sink.isAvailable());
    assert(!sink.isStreaming());
    assert(sink.session().sampleRate == 44100);
    sink.startStream();
    assert(sink.isStreaming());
    sink.suspendStream();
    assert(sink.state() == SinkState::Paused);
    sink.startStream();
    assert(sink.isStreaming());
    sink.disconnect();
    assert(sink.state() == SinkState::Disconnecting);
    sink.finishDisconnect();
    assert(sink.state() == SinkState::Advertising);
}

void testA2dpFailed() {
    using namespace desknx::a2dp;
    A2dpSink sink;
    sink.startAdvertising();
    sink.setFailed("btdrv: service not available");
    assert(sink.state() == SinkState::Failed);
    assert(!sink.errorMessage().empty());
    // Can try again
    sink.startAdvertising();
    assert(sink.state() == SinkState::Advertising);
    assert(sink.errorMessage().empty());
}

void testA2dpSuspendOnlyFromStreaming() {
    using namespace desknx::a2dp;
    A2dpSink sink;
    sink.suspendStream();  // in Idle: no-op, no crash
    assert(sink.isIdle());
    sink.startAdvertising();
    sink.suspendStream();  // in Advertising: no-op
    assert(sink.state() == SinkState::Advertising);
}

void testA2dpCodecName() {
    using namespace desknx::a2dp;
    assert(std::string(A2dpSink::codecName(Codec::Sbc))  == "SBC");
    assert(std::string(A2dpSink::codecName(Codec::Aac))  == "AAC");
    assert(std::string(A2dpSink::codecName(Codec::Ldac)) == "LDAC");
}

int main() {
    // F7
    testInstallStateTransitions();
    testInstallFailed();
    testInstalledThemeList();
    testCatalogState();
    // F8
    testFriendList();
    // F9
    testMiiCollection();
    // F10
    testDiscordPresence();
    // F11
    testTrackerUnlock();
    testTrackerUnknownId();
    testTrackerNullPack();
    testParseAddress();
    testFindAchievementInPack();
    // F12
    testA2dpStateTransitions();
    testA2dpFailed();
    testA2dpSuspendOnlyFromStreaming();
    testA2dpCodecName();
    std::puts("features_f7_f12_test: ok");
    return 0;
}
