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

{"content":"#include <desknx/MusicPlayer.hpp>\n\n#include <cassert>\n#include <cstdio>\n\nusing namespace desknx::audio;\n\n// ----- PlaylistController ---------------------------------------------------\n\nvoid testEmptyPlaylist() {\n    PlaylistController p;\n    assert(p.empty());\n    assert(p.size() == 0);\n    assert(p.current() == -1);\n    assert(p.trackName().empty());\n    assert(p.duration() == 0.f);\n}\n\nvoid testSetTracksSelectsFirst() {\n    PlaylistController p;\n    p.setTracks({ {\"sdmc:/a.mp3\", \"A\", 180.f}, {\"sdmc:/b.mp3\", \"B\", 240.f} });\n    assert(p.size() == 2);\n    assert(p.current() == 0);\n    assert(p.trackName() == \"A\");\n    assert(p.duration() == 180.f);\n}\n\nvoid testAdvanceLinear() {\n    PlaylistController p;\n    p.setTracks({ {\"a\", \"A\", 0}, {\"b\", \"B\", 0}, {\"c\", \"C\", 0} });\n    assert(p.current() == 0);\n    p.advance(false, false, 0);\n    assert(p.current() == 1);\n    p.advance(false, false, 0);\n    assert(p.current() == 2);\n    p.advance(false, false, 0);\n    // wraps around\n    assert(p.current() == 0);\n}\n\nvoid testAdvanceRepeat() {\n    PlaylistController p;\n    p.setTracks({ {\"a\", \"A\", 0}, {\"b\", \"B\", 0}, {\"c\", \"C\", 0} });\n    p.playTrack(1);\n    assert(p.current() == 1);\n    p.advance(false, true, 0);  // repeat: stay\n    assert(p.current() == 1);\n}\n\nvoid testAdvanceShuffleNeverReturnsCurrent() {\n    PlaylistController p;\n    p.setTracks({ {\"a\", \"A\", 0}, {\"b\", \"B\", 0}, {\"c\", \"C\", 0}, {\"d\", \"D\", 0}, {\"e\", \"E\", 0} });\n    p.playTrack(2);\n    for (int i = 0; i < 100; ++i) {\n        const long before = p.current();\n        p.advance(true, false, static_cast<std::uint64_t>(i * 12345 + 7));\n        assert(p.current() != before);  // shuffled advance must never return same track\n        assert(p.current() >= 0 && p.current() < 5);\n    }\n}\n\nvoid testSkipNextAndPrev() {\n    PlaylistController p;\n    p.setTracks({ {\"a\", \"A\", 0}, {\"b\", \"B\", 0}, {\"c\", \"C\", 0} });\n    p.playTrack(1);\n    p.skip(1, false, 0);\n    assert(p.current() == 2);\n    p.skip(-1, false, 0);\n    assert(p.current() == 1);\n    p.skip(-1, false, 0);\n    assert(p.current() == 0);\n    p.skip(-1, false, 0);\n    // wraps around backward\n    assert(p.current() == 2);\n}\n\nvoid testPlayTrackOutOfRange() {\n    PlaylistController p;\n    p.setTracks({ {\"a\", \"A\", 0}, {\"b\", \"B\", 0} });\n    assert(!p.playTrack(5));\n    assert(p.current() == 0);  // unchanged\n}\n\n// ----- MusicPlayer state machine -------------------------------------------\n\nvoid testPlayerStartsStopped() {\n    MusicPlayer mp;\n    assert(mp.isStopped());\n    assert(!mp.isPlaying());\n    assert(!mp.isPaused());\n    assert(mp.trackName().empty());\n    assert(mp.duration() == 0.f);\n}\n\nvoid testPlayEmptyReturnsFalse() {\n    MusicPlayer mp;\n    assert(!mp.play());\n    assert(mp.isStopped());\n}\n\nvoid testPlayStartsPlaying() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 180.f}, {\"b\", \"B\", 240.f} });\n    assert(mp.play());\n    assert(mp.isPlaying());\n    assert(mp.trackName() == \"A\");\n    assert(mp.duration() == 180.f);\n}\n\nvoid testStop() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 100.f} });\n    mp.play();\n    mp.stop();\n    assert(mp.isStopped());\n    assert(mp.position() == 0.f);\n}\n\nvoid testUserPauseToggle() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 100.f} });\n    mp.play();\n    assert(mp.isPlaying());\n    mp.togglePause();\n    assert(mp.isPaused());\n    assert(mp.state() == PlayState::UserPaused);\n    mp.togglePause();\n    assert(mp.isPlaying());\n    assert(mp.state() == PlayState::Playing);\n}\n\nvoid testSysPauseResume() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 100.f} });\n    mp.play();\n    mp.sysPause();\n    assert(mp.isPaused());\n    assert(mp.state() == PlayState::SysPaused);\n    // user toggle must not override system pause\n    mp.togglePause();\n    assert(mp.state() == PlayState::SysPaused);\n    mp.sysResume();\n    assert(mp.isPlaying());\n}\n\nvoid testOnTrackEndedAdvances() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 10.f}, {\"b\", \"B\", 20.f} });\n    mp.play();\n    assert(mp.trackName() == \"A\");\n    assert(mp.onTrackEnded());\n    assert(mp.trackName() == \"B\");\n    assert(mp.position() == 0.f);\n}\n\nvoid testOnTrackEndedWhenNotPlayingIsNoop() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 10.f}, {\"b\", \"B\", 20.f} });\n    mp.play();\n    mp.togglePause();\n    assert(!mp.onTrackEnded());  // paused: do not advance\n    assert(mp.trackName() == \"A\");\n}\n\nvoid testOnTrackEndedRepeatStays() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 10.f}, {\"b\", \"B\", 20.f} });\n    mp.setRepeat(true);\n    mp.play();\n    assert(mp.trackName() == \"A\");\n    mp.onTrackEnded();\n    assert(mp.trackName() == \"A\");  // repeat: same track\n}\n\nvoid testOnTrackEndedShuffleNeverPicksCurrent() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 0}, {\"b\", \"B\", 0}, {\"c\", \"C\", 0}, {\"d\", \"D\", 0} });\n    mp.setShuffle(true);\n    mp.playTrack(2);\n    mp.play();\n    for (int i = 0; i < 100; ++i) {\n        const std::string before = mp.trackName();\n        mp.onTrackEnded(static_cast<std::uint64_t>(i * 77 + 3));\n        // shuffle must never advance to the same track that was playing.\n        assert(mp.trackName() != before);\n    }\n}\n\nvoid testSkipChangesTrack() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 0}, {\"b\", \"B\", 0}, {\"c\", \"C\", 0} });\n    mp.play();\n    mp.skip(1);\n    assert(mp.trackName() == \"B\");\n    mp.skip(-1);\n    assert(mp.trackName() == \"A\");\n}\n\nvoid testSkipResumesFromUserPause() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 0}, {\"b\", \"B\", 0}, {\"c\", \"C\", 0} });\n    mp.play();\n    mp.togglePause();\n    assert(mp.isPaused());\n    mp.skip(1);\n    // skip(+) implies play, matching HomeNX.\n    assert(mp.isPlaying());\n}\n\nvoid testFadeOutOnlyWhenPlaying() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 100.f} });\n    // Stopped → no fade\n    assert(mp.fadeOutMs() == 0);\n    mp.play();\n    assert(mp.fadeOutMs() == MusicPlayer::kFadeOutMs);\n    mp.togglePause();\n    // Paused → no fade (nothing is playing)\n    assert(mp.fadeOutMs() == 0);\n}\n\nvoid testVolumeClamping() {\n    MusicPlayer mp;\n    mp.setMusicVolume(150);\n    assert(mp.config().musicVolume == 100);\n    mp.setMusicVolume(-5);\n    assert(mp.config().musicVolume == 0);\n    mp.setMusicVolume(42);\n    assert(mp.config().musicVolume == 42);\n}\n\nvoid testSetTracksStopsPlayback() {\n    MusicPlayer mp;\n    mp.setTracks({ {\"a\", \"A\", 100.f} });\n    mp.play();\n    assert(mp.isPlaying());\n    mp.setTracks({ {\"b\", \"B\", 200.f} });\n    assert(mp.isStopped());\n    assert(mp.position() == 0.f);\n}\n\nvoid testMusicDisabledPreventsPlay() {\n    MusicPlayer mp;\n    mp.setMusicEnabled(false);\n    mp.setTracks({ {\"a\", \"A\", 100.f} });\n    assert(!mp.play());\n    assert(mp.isStopped());\n}\n\nvoid testShuffledIndexDeterministic() {\n    // Same seed + current must produce the same result.\n    auto a = PlaylistController::shuffledIndex(0, 5, 12345);\n    auto b = PlaylistController::shuffledIndex(0, 5, 12345);\n    assert(a == b);\n    // Must never equal current.\n    assert(a != 0);\n    auto c = PlaylistController::shuffledIndex(3, 5, 999);\n    assert(c != 3);\n    assert(c < 5);\n}\n\nvoid testShuffledIndexSingleTrack() {\n    assert(PlaylistController::shuffledIndex(0, 1, 42) == 0);\n}\n\nint main() {\n    testEmptyPlaylist();\n    testSetTracksSelectsFirst();\n    testAdvanceLinear();\n    testAdvanceRepeat();\n    testAdvanceShuffleNeverReturnsCurrent();\n    testSkipNextAndPrev();\n    testPlayTrackOutOfRange();\n    testPlayerStartsStopped();\n    testPlayEmptyReturnsFalse();\n    testPlayStartsPlaying();\n    testStop();\n    testUserPauseToggle();\n    testSysPauseResume();\n    testOnTrackEndedAdvances();\n    testOnTrackEndedWhenNotPlayingIsNoop();\n    testOnTrackEndedRepeatStays();\n    testOnTrackEndedShuffleNeverPicksCurrent();\n    testSkipChangesTrack();\n    testSkipResumesFromUserPause();\n    testFadeOutOnlyWhenPlaying();\n    testVolumeClamping();\n    testSetTracksStopsPlayback();\n    testMusicDisabledPreventsPlay();\n    testShuffledIndexDeterministic();\n    testShuffledIndexSingleTrack();\n    std::puts(\"music_player_test: ok\");\n    return 0;\n}\n"}