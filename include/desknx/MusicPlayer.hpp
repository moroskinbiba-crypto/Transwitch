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
