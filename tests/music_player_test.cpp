#include <desknx/MusicPlayer.hpp>

#include <cassert>
#include <cstdio>

using namespace desknx::audio;

// ----- PlaylistController ---------------------------------------------------

void testEmptyPlaylist() {
    PlaylistController p;
    assert(p.empty());
    assert(p.size() == 0);
    assert(p.current() == -1);
    assert(p.trackName().empty());
    assert(p.duration() == 0.f);
}

void testSetTracksSelectsFirst() {
    PlaylistController p;
    p.setTracks({ {"sdmc:/a.mp3", "A", 180.f}, {"sdmc:/b.mp3", "B", 240.f} });
    assert(p.size() == 2);
    assert(p.current() == 0);
    assert(p.trackName() == "A");
    assert(p.duration() == 180.f);
}

void testAdvanceLinear() {
    PlaylistController p;
    p.setTracks({ {"a", "A", 0}, {"b", "B", 0}, {"c", "C", 0} });
    assert(p.current() == 0);
    p.advance(false, false, 0);
    assert(p.current() == 1);
    p.advance(false, false, 0);
    assert(p.current() == 2);
    p.advance(false, false, 0);
    // wraps around
    assert(p.current() == 0);
}

void testAdvanceRepeat() {
    PlaylistController p;
    p.setTracks({ {"a", "A", 0}, {"b", "B", 0}, {"c", "C", 0} });
    p.playTrack(1);
    assert(p.current() == 1);
    p.advance(false, true, 0);  // repeat: stay
    assert(p.current() == 1);
}

void testAdvanceShuffleNeverReturnsCurrent() {
    PlaylistController p;
    p.setTracks({ {"a", "A", 0}, {"b", "B", 0}, {"c", "C", 0}, {"d", "D", 0}, {"e", "E", 0} });
    p.playTrack(2);
    for (int i = 0; i < 100; ++i) {
        const long before = p.current();
        p.advance(true, false, static_cast<std::uint64_t>(i * 12345 + 7));
        assert(p.current() != before);  // shuffled advance must never return same track
        assert(p.current() >= 0 && p.current() < 5);
    }
}

void testSkipNextAndPrev() {
    PlaylistController p;
    p.setTracks({ {"a", "A", 0}, {"b", "B", 0}, {"c", "C", 0} });
    p.playTrack(1);
    p.skip(1, false, 0);
    assert(p.current() == 2);
    p.skip(-1, false, 0);
    assert(p.current() == 1);
    p.skip(-1, false, 0);
    assert(p.current() == 0);
    p.skip(-1, false, 0);
    // wraps around backward
    assert(p.current() == 2);
}

void testPlayTrackOutOfRange() {
    PlaylistController p;
    p.setTracks({ {"a", "A", 0}, {"b", "B", 0} });
    assert(!p.playTrack(5));
    assert(p.current() == 0);  // unchanged
}

// ----- MusicPlayer state machine -------------------------------------------

void testPlayerStartsStopped() {
    MusicPlayer mp;
    assert(mp.isStopped());
    assert(!mp.isPlaying());
    assert(!mp.isPaused());
    assert(mp.trackName().empty());
    assert(mp.duration() == 0.f);
}

void testPlayEmptyReturnsFalse() {
    MusicPlayer mp;
    assert(!mp.play());
    assert(mp.isStopped());
}

void testPlayStartsPlaying() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 180.f}, {"b", "B", 240.f} });
    assert(mp.play());
    assert(mp.isPlaying());
    assert(mp.trackName() == "A");
    assert(mp.duration() == 180.f);
}

void testStop() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 100.f} });
    mp.play();
    mp.stop();
    assert(mp.isStopped());
    assert(mp.position() == 0.f);
}

void testUserPauseToggle() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 100.f} });
    mp.play();
    assert(mp.isPlaying());
    mp.togglePause();
    assert(mp.isPaused());
    assert(mp.state() == PlayState::UserPaused);
    mp.togglePause();
    assert(mp.isPlaying());
    assert(mp.state() == PlayState::Playing);
}

void testSysPauseResume() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 100.f} });
    mp.play();
    mp.sysPause();
    assert(mp.isPaused());
    assert(mp.state() == PlayState::SysPaused);
    // user toggle must not override system pause
    mp.togglePause();
    assert(mp.state() == PlayState::SysPaused);
    mp.sysResume();
    assert(mp.isPlaying());
}

void testOnTrackEndedAdvances() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 10.f}, {"b", "B", 20.f} });
    mp.play();
    assert(mp.trackName() == "A");
    assert(mp.onTrackEnded());
    assert(mp.trackName() == "B");
    assert(mp.position() == 0.f);
}

void testOnTrackEndedWhenNotPlayingIsNoop() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 10.f}, {"b", "B", 20.f} });
    mp.play();
    mp.togglePause();
    assert(!mp.onTrackEnded());  // paused: do not advance
    assert(mp.trackName() == "A");
}

void testOnTrackEndedRepeatStays() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 10.f}, {"b", "B", 20.f} });
    mp.setRepeat(true);
    mp.play();
    assert(mp.trackName() == "A");
    mp.onTrackEnded();
    assert(mp.trackName() == "A");  // repeat: same track
}

void testOnTrackEndedShuffleNeverPicksCurrent() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 0}, {"b", "B", 0}, {"c", "C", 0}, {"d", "D", 0} });
    mp.setShuffle(true);
    mp.playTrack(2);
    mp.play();
    for (int i = 0; i < 100; ++i) {
        const std::string before = mp.trackName();
        mp.onTrackEnded(static_cast<std::uint64_t>(i * 77 + 3));
        // shuffle must never advance to the same track that was playing.
        assert(mp.trackName() != before);
    }
}

void testSkipChangesTrack() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 0}, {"b", "B", 0}, {"c", "C", 0} });
    mp.play();
    mp.skip(1);
    assert(mp.trackName() == "B");
    mp.skip(-1);
    assert(mp.trackName() == "A");
}

void testSkipResumesFromUserPause() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 0}, {"b", "B", 0}, {"c", "C", 0} });
    mp.play();
    mp.togglePause();
    assert(mp.isPaused());
    mp.skip(1);
    // skip(+) implies play, matching HomeNX.
    assert(mp.isPlaying());
}

void testFadeOutOnlyWhenPlaying() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 100.f} });
    // Stopped → no fade
    assert(mp.fadeOutMs() == 0);
    mp.play();
    assert(mp.fadeOutMs() == MusicPlayer::kFadeOutMs);
    mp.togglePause();
    // Paused → no fade (nothing is playing)
    assert(mp.fadeOutMs() == 0);
}

void testVolumeClamping() {
    MusicPlayer mp;
    mp.setMusicVolume(150);
    assert(mp.config().musicVolume == 100);
    mp.setMusicVolume(-5);
    assert(mp.config().musicVolume == 0);
    mp.setMusicVolume(42);
    assert(mp.config().musicVolume == 42);
}

void testSetTracksStopsPlayback() {
    MusicPlayer mp;
    mp.setTracks({ {"a", "A", 100.f} });
    mp.play();
    assert(mp.isPlaying());
    mp.setTracks({ {"b", "B", 200.f} });
    assert(mp.isStopped());
    assert(mp.position() == 0.f);
}

void testMusicDisabledPreventsPlay() {
    MusicPlayer mp;
    mp.setMusicEnabled(false);
    mp.setTracks({ {"a", "A", 100.f} });
    assert(!mp.play());
    assert(mp.isStopped());
}

void testShuffledIndexDeterministic() {
    // Same seed + current must produce the same result.
    auto a = PlaylistController::shuffledIndex(0, 5, 12345);
    auto b = PlaylistController::shuffledIndex(0, 5, 12345);
    assert(a == b);
    // Must never equal current.
    assert(a != 0);
    auto c = PlaylistController::shuffledIndex(3, 5, 999);
    assert(c != 3);
    assert(c < 5);
}

void testShuffledIndexSingleTrack() {
    assert(PlaylistController::shuffledIndex(0, 1, 42) == 0);
}

int main() {
    testEmptyPlaylist();
    testSetTracksSelectsFirst();
    testAdvanceLinear();
    testAdvanceRepeat();
    testAdvanceShuffleNeverReturnsCurrent();
    testSkipNextAndPrev();
    testPlayTrackOutOfRange();
    testPlayerStartsStopped();
    testPlayEmptyReturnsFalse();
    testPlayStartsPlaying();
    testStop();
    testUserPauseToggle();
    testSysPauseResume();
    testOnTrackEndedAdvances();
    testOnTrackEndedWhenNotPlayingIsNoop();
    testOnTrackEndedRepeatStays();
    testOnTrackEndedShuffleNeverPicksCurrent();
    testSkipChangesTrack();
    testSkipResumesFromUserPause();
    testFadeOutOnlyWhenPlaying();
    testVolumeClamping();
    testSetTracksStopsPlayback();
    testMusicDisabledPreventsPlay();
    testShuffledIndexDeterministic();
    testShuffledIndexSingleTrack();
    std::puts("music_player_test: ok");
    return 0;
}
