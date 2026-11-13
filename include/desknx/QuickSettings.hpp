#pragma once

// Quick Settings overlay domain: the state machine behind HomeNX's
// QuickSettingsOverlay — sliders, toggles, power actions, and the music
// transport controls.
//
// Mirrors HomeNX QuickSettingsOverlay.hpp: ItemIndex (the panel rows),
// MusicControl (the transport buttons), and PowerAction (the power menu).
// This file owns the state and the navigation rules; it does not own
// rendering, input polling, or the audio device.
//
// Threading: all mutators are single-caller (the main update loop).
// The render thread reads only const accessors.

#include <cstdint>
#include <string>

namespace desknx::quicksettings {

// ---- Panel items (matches HomeNX ItemIndex) ------------------------------

enum class ItemIndex : unsigned char {
    MusicTransport = 0,
    MusicProgress,
    BgmVolume,
    SfxVolume,
    Brightness,
    AirplaneMode,
    Wifi,
    PowerActions,
    Count
};

constexpr int kItemCount = static_cast<int>(ItemIndex::Count);

// ---- Music transport controls --------------------------------------------

enum class MusicControl : unsigned char {
    Prev = 0,
    PlayPause,
    Next,
    Repeat,
    Shuffle,
    Playlist,
    Count
};

constexpr int kMusicControlCount = static_cast<int>(MusicControl::Count);

// ---- Power actions -------------------------------------------------------

enum class PowerAction : unsigned char {
    Sleep = 0,
    Reboot,
    Shutdown,
    Count
};

constexpr int kPowerActionCount = static_cast<int>(PowerAction::Count);

// ---- Repeat mode (HomeNX MusicRepeatMode) --------------------------------

enum class MusicRepeatMode : unsigned char {
    Off = 0,
    All,    // advance to next track on end
    One,    // repeat the current track
};

// ---- UI state snapshot (read by the render thread) -----------------------

struct MusicUiState {
    std::string nowPlaying;
    bool        playing{false};
    bool        shuffle{false};
    MusicRepeatMode repeat{MusicRepeatMode::Off};
    float       volume{0.4f};       // 0..1
    float       positionSeconds{0.f};
    float       durationSeconds{0.f};
    int         currentIndex{0};
};

// ---- Quick Settings state machine ----------------------------------------

class QuickSettingsState final {
public:
    // ---- Visibility -------------------------------------------------------

    void show()    { m_active = true; m_animating = true; m_animProgress = 0.f; }
    void hide()    { m_animating = true; m_animProgress = 0.f; }
    void finishShow() { m_animating = false; m_animProgress = 1.f; }
    void finishHide() { m_active = false; m_animating = false; m_animProgress = 0.f; }

    bool isActive()       const { return m_active || m_animating; }
    bool isFullyVisible() const { return m_active && !m_animating; }
    float animProgress()  const { return m_animProgress; }
    void setAnimProgress(float p) { m_animProgress = p; }

    // ---- Navigation ------------------------------------------------------

    ItemIndex selectedItem() const { return m_selectedItem; }

    void moveVertical(int dir) {
        int idx = static_cast<int>(m_selectedItem);
        idx += dir;
        if (idx < 0) idx = 0;
        if (idx >= kItemCount) idx = kItemCount - 1;
        m_selectedItem = static_cast<ItemIndex>(idx);
    }

    void selectItem(ItemIndex item) {
        if (static_cast<int>(item) >= 0 && static_cast<int>(item) < kItemCount)
            m_selectedItem = item;
    }

    // ---- Music transport navigation -------------------------------------

    MusicControl selectedMusicControl() const { return m_selectedMusicControl; }

    void cycleMusicControl(int dir) {
        int idx = static_cast<int>(m_selectedMusicControl);
        idx += dir;
        // wrap around
        while (idx < 0) idx += kMusicControlCount;
        idx %= kMusicControlCount;
        m_selectedMusicControl = static_cast<MusicControl>(idx);
    }

    // ---- Power action navigation ----------------------------------------

    PowerAction selectedPower() const { return m_selectedPower; }

    void cyclePowerAction(int dir) {
        int idx = static_cast<int>(m_selectedPower);
        idx += dir;
        while (idx < 0) idx += kPowerActionCount;
        idx %= kPowerActionCount;
        m_selectedPower = static_cast<PowerAction>(idx);
    }

    // ---- Sliders (brightness, BGM volume, SFX volume) -------------------

    // All sliders are 0..1 internally (HomeNX uses 0..1 for brightness and
    // 0..0.6 for volume, but the UI normalises to 0..1 for drawing).
    void adjustSlider(ItemIndex item, float delta) {
        float& target = sliderRef(item);
        target += delta;
        if (target < 0.f) target = 0.f;
        if (target > 1.f) target = 1.f;
    }

    void setSlider(ItemIndex item, float value) {
        if (value < 0.f) value = 0.f;
        if (value > 1.f) value = 1.f;
        sliderRef(item) = value;
    }

    float brightness() const { return m_brightness; }
    float bgmVolume()   const { return m_bgmVolume; }
    float sfxVolume()   const { return m_sfxVolume; }

    // ---- Toggles ----------------------------------------------------------

    bool airplaneMode() const { return m_airplaneMode; }
    bool wifiEnabled()  const { return m_wifiEnabled; }

    void toggleAirplaneMode() { m_airplaneMode = !m_airplaneMode; }
    void toggleWifi()         { m_wifiEnabled = !m_wifiEnabled; }

    // ---- Music state (fed by the audio device) ---------------------------

    const MusicUiState& musicState() const { return m_music; }
    void setMusicState(const MusicUiState& state) { m_music = state; }

    // ---- Playlist overlay ------------------------------------------------

    bool isPlaylistOpen() const { return m_playlistOpen; }
    void openPlaylist()  { m_playlistOpen = true; }
    void closePlaylist() { m_playlistOpen = false; }

    // ---- Battery / status (fed by hardware polling) ----------------------

    void setBatteryStatus(int percent, bool charging) {
        m_batteryPercent = percent;
        m_batteryCharging = charging;
    }
    int  batteryPercent()  const { return m_batteryPercent; }
    bool batteryCharging() const { return m_batteryCharging; }

    // ---- Item kinds (for navigation logic) --------------------------------

    static bool isSlider(ItemIndex item) {
        return item == ItemIndex::BgmVolume
            || item == ItemIndex::SfxVolume
            || item == ItemIndex::Brightness;
    }
    static bool isToggle(ItemIndex item) {
        return item == ItemIndex::AirplaneMode || item == ItemIndex::Wifi;
    }
    static bool isPowerActions(ItemIndex item) {
        return item == ItemIndex::PowerActions;
    }
    static bool isMusicTransport(ItemIndex item) {
        return item == ItemIndex::MusicTransport || item == ItemIndex::MusicProgress;
    }

private:
    float& sliderRef(ItemIndex item) {
        if (item == ItemIndex::Brightness) return m_brightness;
        if (item == ItemIndex::BgmVolume)  return m_bgmVolume;
        return m_sfxVolume;  // SfxVolume
    }

    bool        m_active{false};
    bool        m_animating{false};
    float       m_animProgress{0.f};

    ItemIndex     m_selectedItem{ItemIndex::MusicTransport};
    MusicControl m_selectedMusicControl{MusicControl::PlayPause};
    PowerAction  m_selectedPower{PowerAction::Sleep};

    float       m_brightness{0.5f};
    float       m_bgmVolume{0.5f};
    float       m_sfxVolume{0.5f};
    bool        m_airplaneMode{false};
    bool        m_wifiEnabled{true};

    MusicUiState m_music{};

    bool        m_playlistOpen{false};

    int         m_batteryPercent{-1};
    bool        m_batteryCharging{false};
};

} // namespace desknx::quicksettings

{"content":"#pragma once\n\n// Quick Settings overlay domain: the state machine behind HomeNX's\n// QuickSettingsOverlay — sliders, toggles, power actions, and the music\n// transport controls.\n//\n// Mirrors HomeNX QuickSettingsOverlay.hpp: ItemIndex (the panel rows),\n// MusicControl (the transport buttons), and PowerAction (the power menu).\n// This file owns the state and the navigation rules; it does not own\n// rendering, input polling, or the audio device.\n//\n// Threading: all mutators are single-caller (the main update loop).\n// The render thread reads only const accessors.\n\n#include <cstdint>\n#include <string>\n\nnamespace desknx::quicksettings {\n\n// ---- Panel items (matches HomeNX ItemIndex) ------------------------------\n\nenum class ItemIndex : unsigned char {\n    MusicTransport = 0,\n    MusicProgress,\n    BgmVolume,\n    SfxVolume,\n    Brightness,\n    AirplaneMode,\n    Wifi,\n    PowerActions,\n    Count\n};\n\nconstexpr int kItemCount = static_cast<int>(ItemIndex::Count);\n\n// ---- Music transport controls --------------------------------------------\n\nenum class MusicControl : unsigned char {\n    Prev = 0,\n    PlayPause,\n    Next,\n    Repeat,\n    Shuffle,\n    Playlist,\n    Count\n};\n\nconstexpr int kMusicControlCount = static_cast<int>(MusicControl::Count);\n\n// ---- Power actions -------------------------------------------------------\n\nenum class PowerAction : unsigned char {\n    Sleep = 0,\n    Reboot,\n    Shutdown,\n    Count\n};\n\nconstexpr int kPowerActionCount = static_cast<int>(PowerAction::Count);\n\n// ---- Repeat mode (HomeNX MusicRepeatMode) --------------------------------\n\nenum class MusicRepeatMode : unsigned char {\n    Off = 0,\n    All,    // advance to next track on end\n    One,    // repeat the current track\n};\n\n// ---- UI state snapshot (read by the render thread) -----------------------\n\nstruct MusicUiState {\n    std::string nowPlaying;\n    bool        playing{false};\n    bool        shuffle{false};\n    MusicRepeatMode repeat{MusicRepeatMode::Off};\n    float       volume{0.4f};       // 0..1\n    float       positionSeconds{0.f};\n    float       durationSeconds{0.f};\n    int         currentIndex{0};\n};\n\n// ---- Quick Settings state machine ----------------------------------------\n\nclass QuickSettingsState final {\npublic:\n    // ---- Visibility -------------------------------------------------------\n\n    void show()    { m_active = true; m_animating = true; m_animProgress = 0.f; }\n    void hide()    { m_animating = true; m_animProgress = 0.f; }\n    void finishShow() { m_animating = false; m_animProgress = 1.f; }\n    void finishHide() { m_active = false; m_animating = false; m_animProgress = 0.f; }\n\n    bool isActive()       const { return m_active || m_animating; }\n    bool isFullyVisible() const { return m_active && !m_animating; }\n    float animProgress()  const { return m_animProgress; }\n    void setAnimProgress(float p) { m_animProgress = p; }\n\n    // ---- Navigation ------------------------------------------------------\n\n    ItemIndex selectedItem() const { return m_selectedItem; }\n\n    void moveVertical(int dir) {\n        int idx = static_cast<int>(m_selectedItem);\n        idx += dir;\n        if (idx < 0) idx = 0;\n        if (idx >= kItemCount) idx = kItemCount - 1;\n        m_selectedItem = static_cast<ItemIndex>(idx);\n    }\n\n    void selectItem(ItemIndex item) {\n        if (static_cast<int>(item) >= 0 && static_cast<int>(item) < kItemCount)\n            m_selectedItem = item;\n    }\n\n    // ---- Music transport navigation -------------------------------------\n\n    MusicControl selectedMusicControl() const { return m_selectedMusicControl; }\n\n    void cycleMusicControl(int dir) {\n        int idx = static_cast<int>(m_selectedMusicControl);\n        idx += dir;\n        // wrap around\n        while (idx < 0) idx += kMusicControlCount;\n        idx %= kMusicControlCount;\n        m_selectedMusicControl = static_cast<MusicControl>(idx);\n    }\n\n    // ---- Power action navigation ----------------------------------------\n\n    PowerAction selectedPower() const { return m_selectedPower; }\n\n    void cyclePowerAction(int dir) {\n        int idx = static_cast<int>(m_selectedPower);\n        idx += dir;\n        while (idx < 0) idx += kPowerActionCount;\n        idx %= kPowerActionCount;\n        m_selectedPower = static_cast<PowerAction>(idx);\n    }\n\n    // ---- Sliders (brightness, BGM volume, SFX volume) -------------------\n\n    // All sliders are 0..1 internally (HomeNX uses 0..1 for brightness and\n    // 0..0.6 for volume, but the UI normalises to 0..1 for drawing).\n    void adjustSlider(ItemIndex item, float delta) {\n        float& target = sliderRef(item);\n        target += delta;\n        if (target < 0.f) target = 0.f;\n        if (target > 1.f) target = 1.f;\n    }\n\n    void setSlider(ItemIndex item, float value) {\n        if (value < 0.f) value = 0.f;\n        if (value > 1.f) value = 1.f;\n        sliderRef(item) = value;\n    }\n\n    float brightness() const { return m_brightness; }\n    float bgmVolume()   const { return m_bgmVolume; }\n    float sfxVolume()   const { return m_sfxVolume; }\n\n    // ---- Toggles ----------------------------------------------------------\n\n    bool airplaneMode() const { return m_airplaneMode; }\n    bool wifiEnabled()  const { return m_wifiEnabled; }\n\n    void toggleAirplaneMode() { m_airplaneMode = !m_airplaneMode; }\n    void toggleWifi()         { m_wifiEnabled = !m_wifiEnabled; }\n\n    // ---- Music state (fed by the audio device) ---------------------------\n\n    const MusicUiState& musicState() const { return m_music; }\n    void setMusicState(const MusicUiState& state) { m_music = state; }\n\n    // ---- Playlist overlay ------------------------------------------------\n\n    bool isPlaylistOpen() const { return m_playlistOpen; }\n    void openPlaylist()  { m_playlistOpen = true; }\n    void closePlaylist() { m_playlistOpen = false; }\n\n    // ---- Battery / status (fed by hardware polling) ----------------------\n\n    void setBatteryStatus(int percent, bool charging) {\n        m_batteryPercent = percent;\n        m_batteryCharging = charging;\n    }\n    int  batteryPercent()  const { return m_batteryPercent; }\n    bool batteryCharging() const { return m_batteryCharging; }\n\n    // ---- Item kinds (for navigation logic) --------------------------------\n\n    static bool isSlider(ItemIndex item) {\n        return item == ItemIndex::BgmVolume\n            || item == ItemIndex::SfxVolume\n            || item == ItemIndex::Brightness;\n    }\n    static bool isToggle(ItemIndex item) {\n        return item == ItemIndex::AirplaneMode || item == ItemIndex::Wifi;\n    }\n    static bool isPowerActions(ItemIndex item) {\n        return item == ItemIndex::PowerActions;\n    }\n    static bool isMusicTransport(ItemIndex item) {\n        return item == ItemIndex::MusicTransport || item == ItemIndex::MusicProgress;\n    }\n\nprivate:\n    float& sliderRef(ItemIndex item) {\n        if (item == ItemIndex::Brightness) return m_brightness;\n        if (item == ItemIndex::BgmVolume)  return m_bgmVolume;\n        return m_sfxVolume;  // SfxVolume\n    }\n\n    bool        m_active{false};\n    bool        m_animating{false};\n    float       m_animProgress{0.f};\n\n    ItemIndex     m_selectedItem{ItemIndex::MusicTransport};\n    MusicControl m_selectedMusicControl{MusicControl::PlayPause};\n    PowerAction  m_selectedPower{PowerAction::Sleep};\n\n    float       m_brightness{0.5f};\n    float       m_bgmVolume{0.5f};\n    float       m_sfxVolume{0.5f};\n    bool        m_airplaneMode{false};\n    bool        m_wifiEnabled{true};\n\n    MusicUiState m_music{};\n\n    bool        m_playlistOpen{false};\n\n    int         m_batteryPercent{-1};\n    bool        m_batteryCharging{false};\n};\n\n} // namespace desknx::quicksettings\n"}