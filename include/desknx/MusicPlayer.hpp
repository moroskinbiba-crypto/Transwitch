#pragma once

// Music player domain: the playlist model, play-state machine, and the rules
// for every player action.
//
// Modelled closely on HomeNX cards::Audio (projects/cards/src/Audio.hpp),
// translated into a pure-state data model so it can be tested without
// SDL_mixer and without an audio device.
//
// What is NOT here:
//   - SDL_mixer calls (belong in AudioDevice / platform service)
//   - File I/O, track discovery (belong in TrackScanner)
//   - Spectrum decode (data already computed by SDL_mixer; pass float* in)
//   - Rendering (belongs in MusicWidget / QuickSettingsOverlay)
//
// Threading contract:
//   The audio device thread calls setPosition() / onTrackEnded().
//   The render thread calls only const accessors.
//   All mutators that change play state are assumed single-caller (the host
//   application serialises calls via its main-thread update loop, as HomeNX
//   does in Audio::update()).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace desknx::audio {

// ---- Playlist model -------------------------------------------------------

struct Track {
    std::string path;    // absolute SD-card path
    std::string title;   // display name (filename without extension)
    float       length{0.f};  // seconds; 0 = unknown until decoded
};

// ---- Player configuration -------------------------------------------------

struct PlayerConfig {
    bool  musicEnabled{true};
    bool  soundsEnabled{true};
    bool  shuffle{false};
    bool  repeatTrack{false};   // repeat the current track instead of advancing
    int   musicVolume{60};      // 0..100
    int   soundVolume{80};      // 0..100
};

// ---- Play state -----------------------------------------------------------
//
// Exactly the state HomeNX tracks across m_isPlaying / m_userPaused /
// m_sysPaused, made explicit.

enum class PlayState : unsigned char {
    Stopped,     // no track loaded / audio device not open
    Playing,
    UserPaused,  // the user pressed pause
    SysPaused,   // the console is sleeping or an applet is active
};

// ---- Playlist controller --------------------------------------------------
//
// Owns the playlist and decides what plays next.  Does not own SDL handles.

class PlaylistController final {
public:
    // Load a fresh playlist; stops playback.
    void setTracks(std::vector<Track> tracks) {
        m_tracks  = std::move(tracks);
        m_current = m_tracks.empty() ? -1L : 0L;
    }

    bool empty() const { return m_tracks.empty(); }
    std::size_t size() const { return m_tracks.size(); }

    const std::vector<Track>& tracks() const { return m_tracks; }

    long current() const { return m_current; }  // -1 = none

    const Track* currentTrack() const {
        if (m_current < 0 || static_cast<std::size_t>(m_current) >= m_tracks.size())
            return nullptr;
        return &m_tracks[static_cast<std::size_t>(m_current)];
    }

    std::string trackName() const {
        const Track* t = currentTrack();
        return t ? t->title : std::string{};
    }

    float duration() const {
        const Track* t = currentTrack();
        return t ? t->length : 0.f;
    }

    // Advance to the next track. Returns false if the playlist is empty.
    // With shuffle, picks a random index != current using the provided seed.
    // With repeat, stays on the current track.
    bool advance(bool shuffle, bool repeat, std::uint64_t randomSeed) {
        if (m_tracks.empty()) return false;
        if (repeat) return true;  // same track
        if (m_tracks.size() == 1) { m_current = 0; return true; }
        if (shuffle) {
            m_current = static_cast<long>(
                shuffledIndex(static_cast<std::size_t>(m_current),
                              m_tracks.size(), randomSeed));
        } else {
            m_current = (m_current + 1) % static_cast<long>(m_tracks.size());
        }
        return true;
    }

    // skip(+1) = next, skip(-1) = previous. Clamps on empty playlist.
    bool skip(int dir, bool shuffle, std::uint64_t randomSeed) {
        if (m_tracks.empty()) return false;
        if (dir > 0) return advance(shuffle, false, randomSeed);
        // previous
        if (m_tracks.size() == 1) return true;
        m_current = (m_current - 1 + static_cast<long>(m_tracks.size()))
                    % static_cast<long>(m_tracks.size());
        return true;
    }

    bool playTrack(std::size_t index) {
        if (index >= m_tracks.size()) return false;
        m_current = static_cast<long>(index);
        return true;
    }

    // Deterministic shuffle for testability: different from `current`,
    // uniformly distributed over the rest using an LCG.
    static std::size_t shuffledIndex(std::size_t current, std::size_t count,
                                     std::uint64_t seed) {
        if (count <= 1) return 0;
        // LCG constants from Knuth MMIX.
        std::uint64_t s = seed ^ (static_cast<std::uint64_t>(current) * 6364136223846793005ull);
        for (int i = 0; i < 4; ++i)
            s = s * 6364136223846793005ull + 1442695040888963407ull;
        const std::size_t candidate = static_cast<std::size_t>(s % (count - 1));
        // Map around current so the result != current.
        return candidate >= current ? candidate + 1 : candidate;
    }

private:
    std::vector<Track> m_tracks;
    long               m_current{-1};
};

// ---- Player state machine -------------------------------------------------
//
// Owns PlaylistController and PlayerConfig; drives state transitions.
// The audio device reads m_state / m_position only; it does not call mutators.

class MusicPlayer final {
public:
    // ---- Configuration ----------------------------------------------------

    void setConfig(const PlayerConfig& cfg) { m_cfg = cfg; }
    const PlayerConfig& config() const { return m_cfg; }

    void setMusicEnabled(bool on) { m_cfg.musicEnabled = on; }
    void setSoundsEnabled(bool on) { m_cfg.soundsEnabled = on; }
    void setShuffle(bool on) { m_cfg.shuffle = on; }
    void setRepeat(bool on) { m_cfg.repeatTrack = on; }

    void setMusicVolume(int v) { m_cfg.musicVolume = clamp(v, 0, 100); }
    void setSoundVolume(int v) { m_cfg.soundVolume = clamp(v, 0, 100); }

    // ---- Playlist ---------------------------------------------------------

    void setTracks(std::vector<Track> tracks) {
        m_playlist.setTracks(std::move(tracks));
        if (m_state != PlayState::Stopped) m_state = PlayState::Stopped;
        m_position = 0.f;
    }

    const PlaylistController& playlist() const { return m_playlist; }

    // ---- Transport --------------------------------------------------------

    // Start playing; returns false if there is nothing to play.
    bool play(std::uint64_t randomSeed = 0) {
        if (!m_cfg.musicEnabled || m_playlist.empty()) return false;
        if (m_playlist.current() < 0) m_playlist.advance(m_cfg.shuffle, false, randomSeed);
        m_state    = PlayState::Playing;
        m_position = 0.f;
        return true;
    }

    void stop() {
        m_state    = PlayState::Stopped;
        m_position = 0.f;
    }

    // User pause/resume toggle (HomeNX: togglePause()).
    void togglePause() {
        if (m_state == PlayState::Playing)   m_state = PlayState::UserPaused;
        else if (m_state == PlayState::UserPaused) m_state = PlayState::Playing;
        // SysPaused state is not toggled by user action.
    }

    // System suspend/resume (sleep, applet) — HomeNX pause()/resume().
    void sysPause() {
        if (m_state == PlayState::Playing) m_state = PlayState::SysPaused;
    }
    void sysResume() {
        if (m_state == PlayState::SysPaused) m_state = PlayState::Playing;
    }

    // skip(+1) or skip(-1) — HomeNX skip(dir).
    void skip(int dir, std::uint64_t randomSeed = 0) {
        if (m_playlist.empty()) return;
        m_playlist.skip(dir, m_cfg.shuffle, randomSeed);
        m_position = 0.f;
        // Advance implies Playing, same as HomeNX behaviour.
        if (m_state == PlayState::UserPaused) m_state = PlayState::Playing;
    }

    void playTrack(std::size_t index, std::uint64_t randomSeed = 0) {
        if (!m_playlist.playTrack(index)) return;
        m_position = 0.f;
        m_state    = PlayState::Playing;
        (void)randomSeed;
    }

    // Called by the audio device thread when the current track finishes.
    // Returns true when a new track should start (device side loads next).
    bool onTrackEnded(std::uint64_t randomSeed = 0) {
        if (m_state != PlayState::Playing) return false;
        const bool ok = m_playlist.advance(m_cfg.shuffle, m_cfg.repeatTrack, randomSeed);
        if (!ok) { m_state = PlayState::Stopped; return false; }
        m_position = 0.f;
        return true;
    }

    // ---- Accessors (render thread) ----------------------------------------

    PlayState   state()     const { return m_state; }
    bool        isPlaying() const { return m_state == PlayState::Playing; }
    bool        isPaused()  const { return m_state == PlayState::UserPaused
                                        || m_state == PlayState::SysPaused; }
    bool        isStopped() const { return m_state == PlayState::Stopped; }

    float       position()  const { return m_position; }
    float       duration()  const { return m_playlist.duration(); }
    std::string trackName() const { return m_playlist.trackName(); }

    // Called from the audio device at every decode buffer: advances position.
    void setPosition(float pos) { m_position = pos; }

    // ---- Fade rule --------------------------------------------------------
    //
    // Returns the requested fade-out duration in ms before launching a game,
    // matching HomeNX fadeOut(ms). 0 = music was not playing; caller skips it.
    int fadeOutMs() const {
        if (m_state != PlayState::Playing) return 0;
        return kFadeOutMs;
    }

    static constexpr int kFadeOutMs = 1000;

private:
    static int clamp(int v, int lo, int hi) {
        return v < lo ? lo : v > hi ? hi : v;
    }

    PlayerConfig       m_cfg{};
    PlaylistController m_playlist{};
    PlayState          m_state{PlayState::Stopped};
    float              m_position{0.f};
};

// ---- Spectrum data --------------------------------------------------------
//
// 48-band spectrum, mirroring HomeNX Audio::kBands.
// Filled by the audio device thread, read by the render thread.
// Values normalised 0..1, low-pass smoothed by the device each frame.

constexpr int kSpectrumBands = 48;

struct SpectrumFrame {
    float bands[kSpectrumBands]{};
};

} // namespace desknx::audio

{"content":"#pragma once\n\n// Music player domain: the playlist model, play-state machine, and the rules\n// for every player action.\n//\n// Modelled closely on HomeNX cards::Audio (projects/cards/src/Audio.hpp),\n// translated into a pure-state data model so it can be tested without\n// SDL_mixer and without an audio device.\n//\n// What is NOT here:\n//   - SDL_mixer calls (belong in AudioDevice / platform service)\n//   - File I/O, track discovery (belong in TrackScanner)\n//   - Spectrum decode (data already computed by SDL_mixer; pass float* in)\n//   - Rendering (belongs in MusicWidget / QuickSettingsOverlay)\n//\n// Threading contract:\n//   The audio device thread calls setPosition() / onTrackEnded().\n//   The render thread calls only const accessors.\n//   All mutators that change play state are assumed single-caller (the host\n//   application serialises calls via its main-thread update loop, as HomeNX\n//   does in Audio::update()).\n\n#include <cstddef>\n#include <cstdint>\n#include <string>\n#include <vector>\n\nnamespace desknx::audio {\n\n// ---- Playlist model -------------------------------------------------------\n\nstruct Track {\n    std::string path;    // absolute SD-card path\n    std::string title;   // display name (filename without extension)\n    float       length{0.f};  // seconds; 0 = unknown until decoded\n};\n\n// ---- Player configuration -------------------------------------------------\n\nstruct PlayerConfig {\n    bool  musicEnabled{true};\n    bool  soundsEnabled{true};\n    bool  shuffle{false};\n    bool  repeatTrack{false};   // repeat the current track instead of advancing\n    int   musicVolume{60};      // 0..100\n    int   soundVolume{80};      // 0..100\n};\n\n// ---- Play state -----------------------------------------------------------\n//\n// Exactly the state HomeNX tracks across m_isPlaying / m_userPaused /\n// m_sysPaused, made explicit.\n\nenum class PlayState : unsigned char {\n    Stopped,     // no track loaded / audio device not open\n    Playing,\n    UserPaused,  // the user pressed pause\n    SysPaused,   // the console is sleeping or an applet is active\n};\n\n// ---- Playlist controller --------------------------------------------------\n//\n// Owns the playlist and decides what plays next.  Does not own SDL handles.\n\nclass PlaylistController final {\npublic:\n    // Load a fresh playlist; stops playback.\n    void setTracks(std::vector<Track> tracks) {\n        m_tracks  = std::move(tracks);\n        m_current = m_tracks.empty() ? -1L : 0L;\n    }\n\n    bool empty() const { return m_tracks.empty(); }\n    std::size_t size() const { return m_tracks.size(); }\n\n    const std::vector<Track>& tracks() const { return m_tracks; }\n\n    long current() const { return m_current; }  // -1 = none\n\n    const Track* currentTrack() const {\n        if (m_current < 0 || static_cast<std::size_t>(m_current) >= m_tracks.size())\n            return nullptr;\n        return &m_tracks[static_cast<std::size_t>(m_current)];\n    }\n\n    std::string trackName() const {\n        const Track* t = currentTrack();\n        return t ? t->title : std::string{};\n    }\n\n    float duration() const {\n        const Track* t = currentTrack();\n        return t ? t->length : 0.f;\n    }\n\n    // Advance to the next track. Returns false if the playlist is empty.\n    // With shuffle, picks a random index != current using the provided seed.\n    // With repeat, stays on the current track.\n    bool advance(bool shuffle, bool repeat, std::uint64_t randomSeed) {\n        if (m_tracks.empty()) return false;\n        if (repeat) return true;  // same track\n        if (m_tracks.size() == 1) { m_current = 0; return true; }\n        if (shuffle) {\n            m_current = static_cast<long>(\n                shuffledIndex(static_cast<std::size_t>(m_current),\n                              m_tracks.size(), randomSeed));\n        } else {\n            m_current = (m_current + 1) % static_cast<long>(m_tracks.size());\n        }\n        return true;\n    }\n\n    // skip(+1) = next, skip(-1) = previous. Clamps on empty playlist.\n    bool skip(int dir, bool shuffle, std::uint64_t randomSeed) {\n        if (m_tracks.empty()) return false;\n        if (dir > 0) return advance(shuffle, false, randomSeed);\n        // previous\n        if (m_tracks.size() == 1) return true;\n        m_current = (m_current - 1 + static_cast<long>(m_tracks.size()))\n                    % static_cast<long>(m_tracks.size());\n        return true;\n    }\n\n    bool playTrack(std::size_t index) {\n        if (index >= m_tracks.size()) return false;\n        m_current = static_cast<long>(index);\n        return true;\n    }\n\n    // Deterministic shuffle for testability: different from `current`,\n    // uniformly distributed over the rest using an LCG.\n    static std::size_t shuffledIndex(std::size_t current, std::size_t count,\n                                     std::uint64_t seed) {\n        if (count <= 1) return 0;\n        // LCG constants from Knuth MMIX.\n        std::uint64_t s = seed ^ (static_cast<std::uint64_t>(current) * 6364136223846793005ull);\n        for (int i = 0; i < 4; ++i)\n            s = s * 6364136223846793005ull + 1442695040888963407ull;\n        const std::size_t candidate = static_cast<std::size_t>(s % (count - 1));\n        // Map around current so the result != current.\n        return candidate >= current ? candidate + 1 : candidate;\n    }\n\nprivate:\n    std::vector<Track> m_tracks;\n    long               m_current{-1};\n};\n\n// ---- Player state machine -------------------------------------------------\n//\n// Owns PlaylistController and PlayerConfig; drives state transitions.\n// The audio device reads m_state / m_position only; it does not call mutators.\n\nclass MusicPlayer final {\npublic:\n    // ---- Configuration ----------------------------------------------------\n\n    void setConfig(const PlayerConfig& cfg) { m_cfg = cfg; }\n    const PlayerConfig& config() const { return m_cfg; }\n\n    void setMusicEnabled(bool on) { m_cfg.musicEnabled = on; }\n    void setSoundsEnabled(bool on) { m_cfg.soundsEnabled = on; }\n    void setShuffle(bool on) { m_cfg.shuffle = on; }\n    void setRepeat(bool on) { m_cfg.repeatTrack = on; }\n\n    void setMusicVolume(int v) { m_cfg.musicVolume = clamp(v, 0, 100); }\n    void setSoundVolume(int v) { m_cfg.soundVolume = clamp(v, 0, 100); }\n\n    // ---- Playlist ---------------------------------------------------------\n\n    void setTracks(std::vector<Track> tracks) {\n        m_playlist.setTracks(std::move(tracks));\n        if (m_state != PlayState::Stopped) m_state = PlayState::Stopped;\n        m_position = 0.f;\n    }\n\n    const PlaylistController& playlist() const { return m_playlist; }\n\n    // ---- Transport --------------------------------------------------------\n\n    // Start playing; returns false if there is nothing to play.\n    bool play(std::uint64_t randomSeed = 0) {\n        if (!m_cfg.musicEnabled || m_playlist.empty()) return false;\n        if (m_playlist.current() < 0) m_playlist.advance(m_cfg.shuffle, false, randomSeed);\n        m_state    = PlayState::Playing;\n        m_position = 0.f;\n        return true;\n    }\n\n    void stop() {\n        m_state    = PlayState::Stopped;\n        m_position = 0.f;\n    }\n\n    // User pause/resume toggle (HomeNX: togglePause()).\n    void togglePause() {\n        if (m_state == PlayState::Playing)   m_state = PlayState::UserPaused;\n        else if (m_state == PlayState::UserPaused) m_state = PlayState::Playing;\n        // SysPaused state is not toggled by user action.\n    }\n\n    // System suspend/resume (sleep, applet) — HomeNX pause()/resume().\n    void sysPause() {\n        if (m_state == PlayState::Playing) m_state = PlayState::SysPaused;\n    }\n    void sysResume() {\n        if (m_state == PlayState::SysPaused) m_state = PlayState::Playing;\n    }\n\n    // skip(+1) or skip(-1) — HomeNX skip(dir).\n    void skip(int dir, std::uint64_t randomSeed = 0) {\n        if (m_playlist.empty()) return;\n        m_playlist.skip(dir, m_cfg.shuffle, randomSeed);\n        m_position = 0.f;\n        // Advance implies Playing, same as HomeNX behaviour.\n        if (m_state == PlayState::UserPaused) m_state = PlayState::Playing;\n    }\n\n    void playTrack(std::size_t index, std::uint64_t randomSeed = 0) {\n        if (!m_playlist.playTrack(index)) return;\n        m_position = 0.f;\n        m_state    = PlayState::Playing;\n        (void)randomSeed;\n    }\n\n    // Called by the audio device thread when the current track finishes.\n    // Returns true when a new track should start (device side loads next).\n    bool onTrackEnded(std::uint64_t randomSeed = 0) {\n        if (m_state != PlayState::Playing) return false;\n        const bool ok = m_playlist.advance(m_cfg.shuffle, m_cfg.repeatTrack, randomSeed);\n        if (!ok) { m_state = PlayState::Stopped; return false; }\n        m_position = 0.f;\n        return true;\n    }\n\n    // ---- Accessors (render thread) ----------------------------------------\n\n    PlayState   state()     const { return m_state; }\n    bool        isPlaying() const { return m_state == PlayState::Playing; }\n    bool        isPaused()  const { return m_state == PlayState::UserPaused\n                                        || m_state == PlayState::SysPaused; }\n    bool        isStopped() const { return m_state == PlayState::Stopped; }\n\n    float       position()  const { return m_position; }\n    float       duration()  const { return m_playlist.duration(); }\n    std::string trackName() const { return m_playlist.trackName(); }\n\n    // Called from the audio device at every decode buffer: advances position.\n    void setPosition(float pos) { m_position = pos; }\n\n    // ---- Fade rule --------------------------------------------------------\n    //\n    // Returns the requested fade-out duration in ms before launching a game,\n    // matching HomeNX fadeOut(ms). 0 = music was not playing; caller skips it.\n    int fadeOutMs() const {\n        if (m_state != PlayState::Playing) return 0;\n        return kFadeOutMs;\n    }\n\n    static constexpr int kFadeOutMs = 1000;\n\nprivate:\n    static int clamp(int v, int lo, int hi) {\n        return v < lo ? lo : v > hi ? hi : v;\n    }\n\n    PlayerConfig       m_cfg{};\n    PlaylistController m_playlist{};\n    PlayState          m_state{PlayState::Stopped};\n    float              m_position{0.f};\n};\n\n// ---- Spectrum data --------------------------------------------------------\n//\n// 48-band spectrum, mirroring HomeNX Audio::kBands.\n// Filled by the audio device thread, read by the render thread.\n// Values normalised 0..1, low-pass smoothed by the device each frame.\n\nconstexpr int kSpectrumBands = 48;\n\nstruct SpectrumFrame {\n    float bands[kSpectrumBands]{};\n};\n\n} // namespace desknx::audio\n"}