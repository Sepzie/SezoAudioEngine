#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "AudioEngine.h"
#endif

namespace sezo {

#if defined(__ANDROID__)
TEST(AudioEngineTest, InitializeReleaseIdempotent) {
  GTEST_SKIP() << "TODO: initialize twice and release twice without errors.";
}

TEST(AudioEngineTest, LoadTrackUpdatesDuration) {
  GTEST_SKIP() << "TODO: load fixture track and verify duration is set.";
}

TEST(AudioEngineTest, PlaybackAndSeekTransitions) {
  GTEST_SKIP() << "TODO: play/pause/stop/seek and validate state and position.";
}
#else
TEST(AudioEngineTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only AudioEngine tests.";
}
#endif

}  // namespace sezo
