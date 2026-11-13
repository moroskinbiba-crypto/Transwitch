#include <desknx/QuickSettings.hpp>
#include <cassert>
#include <cstdio>

using namespace desknx::quicksettings;

void testInit() {
    QuickSettingsState qs;
    assert(!qs.isActive());
    assert(!qs.isFullyVisible());
    assert(qs.brightness() == 0.5f);
    assert(qs.bgmVolume() == 0.5f);
    assert(qs.sfxVolume() == 0.5f);
    assert(!qs.airplaneMode());
    assert(qs.wifiEnabled());
    assert(!qs.isPlaylistOpen());
}

void testShowHide() {
    QuickSettingsState qs;
    qs.show();
    assert(qs.isActive());
    qs.finishShow();
    assert(qs.isFullyVisible());
    qs.hide();
    assert(qs.isActive());
    assert(!qs.isFullyVisible());
    qs.finishHide();
    assert(!qs.isActive());
}

void testSliders() {
    QuickSettingsState qs;
    qs.adjustSlider(ItemIndex::Brightness, 0.2f);
    assert(qs.brightness() == 0.7f);
    qs.setSlider(ItemIndex::Brightness, 0.3f);
    assert(qs.brightness() == 0.3f);
    qs.adjustSlider(ItemIndex::BgmVolume, -0.6f);
    assert(qs.bgmVolume() == 0.f);  // clamped
    qs.setSlider(ItemIndex::SfxVolume, 2.0f);
    assert(qs.sfxVolume() == 1.f);  // clamped
}

void testToggles() {
    QuickSettingsState qs;
    assert(!qs.airplaneMode());
    qs.toggleAirplaneMode();
    assert(qs.airplaneMode());
    assert(qs.wifiEnabled());
    qs.toggleWifi();
    assert(!qs.wifiEnabled());
}

void testNavigation() {
    QuickSettingsState qs;
    assert(qs.selectedItem() == ItemIndex::MusicTransport);
    qs.moveVertical(1);
    assert(qs.selectedItem() == ItemIndex::MusicProgress);
    qs.moveVertical(-1);
    assert(qs.selectedItem() == ItemIndex::MusicTransport);
    qs.moveVertical(-1);
    assert(qs.selectedItem() == ItemIndex::MusicTransport);  // clamped at top
    qs.selectItem(ItemIndex::PowerActions);
    assert(qs.selectedItem() == ItemIndex::PowerActions);
}

void testMusicControlCycle() {
    QuickSettingsState qs;
    assert(qs.selectedMusicControl() == MusicControl::PlayPause);  // 1
    qs.cycleMusicControl(1);
    assert(qs.selectedMusicControl() == MusicControl::Next);       // 2
    qs.cycleMusicControl(2);
    assert(qs.selectedMusicControl() == MusicControl::Shuffle);    // 4
    qs.cycleMusicControl(-1);
    assert(qs.selectedMusicControl() == MusicControl::Repeat);     // 3
    // wrap: Prev(0) -> Count-1 backward from Prev
    qs.selectItem(ItemIndex::MusicTransport);  // just reset nav, not music
    qs.cycleMusicControl(-1);                 // from Repeat(3) -> Next(2)
    assert(qs.selectedMusicControl() == MusicControl::Next);       // 2
    // wrap forward past Playlist(5)
    qs.cycleMusicControl(4);                  // 2+4=6 -> 6%6=0 = Prev
    assert(qs.selectedMusicControl() == MusicControl::Prev);       // 0
}

void testPowerActionCycle() {
    QuickSettingsState qs;
    assert(qs.selectedPower() == PowerAction::Sleep);
    qs.cyclePowerAction(1);
    assert(qs.selectedPower() == PowerAction::Reboot);
    qs.cyclePowerAction(1);
    assert(qs.selectedPower() == PowerAction::Shutdown);
    qs.cyclePowerAction(1);
    assert(qs.selectedPower() == PowerAction::Sleep);  // wrapped
}

void testMusicState() {
    QuickSettingsState qs;
    MusicUiState m;
    m.nowPlaying = "Test Track";
    m.playing = true;
    m.shuffle = true;
    m.repeat = MusicRepeatMode::One;
    m.volume = 0.8f;
    m.positionSeconds = 30.f;
    m.durationSeconds = 200.f;
    m.currentIndex = 5;
    qs.setMusicState(m);
    const auto& s = qs.musicState();
    assert(s.nowPlaying == "Test Track");
    assert(s.playing);
    assert(s.shuffle);
    assert(s.repeat == MusicRepeatMode::One);
    assert(s.volume == 0.8f);
    assert(s.positionSeconds == 30.f);
    assert(s.durationSeconds == 200.f);
    assert(s.currentIndex == 5);
}

void testPlaylistToggle() {
    QuickSettingsState qs;
    assert(!qs.isPlaylistOpen());
    qs.openPlaylist();
    assert(qs.isPlaylistOpen());
    qs.closePlaylist();
    assert(!qs.isPlaylistOpen());
}

void testBatteryStatus() {
    QuickSettingsState qs;
    assert(qs.batteryPercent() == -1);
    assert(!qs.batteryCharging());
    qs.setBatteryStatus(75, true);
    assert(qs.batteryPercent() == 75);
    assert(qs.batteryCharging());
}

void testItemKinds() {
    assert(QuickSettingsState::isSlider(ItemIndex::Brightness));
    assert(QuickSettingsState::isSlider(ItemIndex::BgmVolume));
    assert(QuickSettingsState::isSlider(ItemIndex::SfxVolume));
    assert(!QuickSettingsState::isSlider(ItemIndex::MusicTransport));
    assert(QuickSettingsState::isToggle(ItemIndex::AirplaneMode));
    assert(QuickSettingsState::isToggle(ItemIndex::Wifi));
    assert(!QuickSettingsState::isToggle(ItemIndex::Brightness));
    assert(QuickSettingsState::isPowerActions(ItemIndex::PowerActions));
    assert(QuickSettingsState::isMusicTransport(ItemIndex::MusicTransport));
    assert(QuickSettingsState::isMusicTransport(ItemIndex::MusicProgress));
}

int main() {
    testInit();
    testShowHide();
    testSliders();
    testToggles();
    testNavigation();
    testMusicControlCycle();
    testPowerActionCycle();
    testMusicState();
    testPlaylistToggle();
    testBatteryStatus();
    testItemKinds();
    std::puts("quick_settings_test: ok");
    return 0;
}

{"content":"#include <desknx/QuickSettings.hpp>\n#include <cassert>\n#include <cstdio>\n\nusing namespace desknx::quicksettings;\n\nvoid testInit() {\n    QuickSettingsState qs;\n    assert(!qs.isActive());\n    assert(!qs.isFullyVisible());\n    assert(qs.brightness() == 0.5f);\n    assert(qs.bgmVolume() == 0.5f);\n    assert(qs.sfxVolume() == 0.5f);\n    assert(!qs.airplaneMode());\n    assert(qs.wifiEnabled());\n    assert(!qs.isPlaylistOpen());\n}\n\nvoid testShowHide() {\n    QuickSettingsState qs;\n    qs.show();\n    assert(qs.isActive());\n    qs.finishShow();\n    assert(qs.isFullyVisible());\n    qs.hide();\n    assert(qs.isActive());\n    assert(!qs.isFullyVisible());\n    qs.finishHide();\n    assert(!qs.isActive());\n}\n\nvoid testSliders() {\n    QuickSettingsState qs;\n    qs.adjustSlider(ItemIndex::Brightness, 0.2f);\n    assert(qs.brightness() == 0.7f);\n    qs.setSlider(ItemIndex::Brightness, 0.3f);\n    assert(qs.brightness() == 0.3f);\n    qs.adjustSlider(ItemIndex::BgmVolume, -0.6f);\n    assert(qs.bgmVolume() == 0.f);  // clamped\n    qs.setSlider(ItemIndex::SfxVolume, 2.0f);\n    assert(qs.sfxVolume() == 1.f);  // clamped\n}\n\nvoid testToggles() {\n    QuickSettingsState qs;\n    assert(!qs.airplaneMode());\n    qs.toggleAirplaneMode();\n    assert(qs.airplaneMode());\n    assert(qs.wifiEnabled());\n    qs.toggleWifi();\n    assert(!qs.wifiEnabled());\n}\n\nvoid testNavigation() {\n    QuickSettingsState qs;\n    assert(qs.selectedItem() == ItemIndex::MusicTransport);\n    qs.moveVertical(1);\n    assert(qs.selectedItem() == ItemIndex::MusicProgress);\n    qs.moveVertical(-1);\n    assert(qs.selectedItem() == ItemIndex::MusicTransport);\n    qs.moveVertical(-1);\n    assert(qs.selectedItem() == ItemIndex::MusicTransport);  // clamped at top\n    qs.selectItem(ItemIndex::PowerActions);\n    assert(qs.selectedItem() == ItemIndex::PowerActions);\n}\n\nvoid testMusicControlCycle() {\n    QuickSettingsState qs;\n    assert(qs.selectedMusicControl() == MusicControl::PlayPause);  // 1\n    qs.cycleMusicControl(1);\n    assert(qs.selectedMusicControl() == MusicControl::Next);       // 2\n    qs.cycleMusicControl(2);\n    assert(qs.selectedMusicControl() == MusicControl::Shuffle);    // 4\n    qs.cycleMusicControl(-1);\n    assert(qs.selectedMusicControl() == MusicControl::Repeat);     // 3\n    // wrap: Prev(0) -> Count-1 backward from Prev\n    qs.selectItem(ItemIndex::MusicTransport);  // just reset nav, not music\n    qs.cycleMusicControl(-1);                 // from Repeat(3) -> Next(2)\n    assert(qs.selectedMusicControl() == MusicControl::Next);       // 2\n    // wrap forward past Playlist(5)\n    qs.cycleMusicControl(4);                  // 2+4=6 -> 6%6=0 = Prev\n    assert(qs.selectedMusicControl() == MusicControl::Prev);       // 0\n}\n\nvoid testPowerActionCycle() {\n    QuickSettingsState qs;\n    assert(qs.selectedPower() == PowerAction::Sleep);\n    qs.cyclePowerAction(1);\n    assert(qs.selectedPower() == PowerAction::Reboot);\n    qs.cyclePowerAction(1);\n    assert(qs.selectedPower() == PowerAction::Shutdown);\n    qs.cyclePowerAction(1);\n    assert(qs.selectedPower() == PowerAction::Sleep);  // wrapped\n}\n\nvoid testMusicState() {\n    QuickSettingsState qs;\n    MusicUiState m;\n    m.nowPlaying = \"Test Track\";\n    m.playing = true;\n    m.shuffle = true;\n    m.repeat = MusicRepeatMode::One;\n    m.volume = 0.8f;\n    m.positionSeconds = 30.f;\n    m.durationSeconds = 200.f;\n    m.currentIndex = 5;\n    qs.setMusicState(m);\n    const auto& s = qs.musicState();\n    assert(s.nowPlaying == \"Test Track\");\n    assert(s.playing);\n    assert(s.shuffle);\n    assert(s.repeat == MusicRepeatMode::One);\n    assert(s.volume == 0.8f);\n    assert(s.positionSeconds == 30.f);\n    assert(s.durationSeconds == 200.f);\n    assert(s.currentIndex == 5);\n}\n\nvoid testPlaylistToggle() {\n    QuickSettingsState qs;\n    assert(!qs.isPlaylistOpen());\n    qs.openPlaylist();\n    assert(qs.isPlaylistOpen());\n    qs.closePlaylist();\n    assert(!qs.isPlaylistOpen());\n}\n\nvoid testBatteryStatus() {\n    QuickSettingsState qs;\n    assert(qs.batteryPercent() == -1);\n    assert(!qs.batteryCharging());\n    qs.setBatteryStatus(75, true);\n    assert(qs.batteryPercent() == 75);\n    assert(qs.batteryCharging());\n}\n\nvoid testItemKinds() {\n    assert(QuickSettingsState::isSlider(ItemIndex::Brightness));\n    assert(QuickSettingsState::isSlider(ItemIndex::BgmVolume));\n    assert(QuickSettingsState::isSlider(ItemIndex::SfxVolume));\n    assert(!QuickSettingsState::isSlider(ItemIndex::MusicTransport));\n    assert(QuickSettingsState::isToggle(ItemIndex::AirplaneMode));\n    assert(QuickSettingsState::isToggle(ItemIndex::Wifi));\n    assert(!QuickSettingsState::isToggle(ItemIndex::Brightness));\n    assert(QuickSettingsState::isPowerActions(ItemIndex::PowerActions));\n    assert(QuickSettingsState::isMusicTransport(ItemIndex::MusicTransport));\n    assert(QuickSettingsState::isMusicTransport(ItemIndex::MusicProgress));\n}\n\nint main() {\n    testInit();\n    testShowHide();\n    testSliders();\n    testToggles();\n    testNavigation();\n    testMusicControlCycle();\n    testPowerActionCycle();\n    testMusicState();\n    testPlaylistToggle();\n    testBatteryStatus();\n    testItemKinds();\n    std::puts(\"quick_settings_test: ok\");\n    return 0;\n}\n"}