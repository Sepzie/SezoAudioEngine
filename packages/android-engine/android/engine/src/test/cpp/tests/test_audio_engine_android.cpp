#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "AudioEngine.h"
#include "test_helpers.h"
#endif

namespace sezo {

#if defined(__ANDROID__)
TEST(AudioEngineTest, InitializeReleaseIdempotent) {
  AudioEngine engine;
  EXPECT_TRUE(engine.Initialize(48000, 4));
  EXPECT_TRUE(engine.Initialize(48000, 4));

  engine.Release();
  engine.Release();
}

TEST(AudioEngineTest, LoadTrackUpdatesDuration) {
  const std::string path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  AudioEngine engine;
  ASSERT_TRUE(engine.Initialize(48000, 4));
  EXPECT_TRUE(engine.LoadTrack("track_1", path));

  const double duration_ms = engine.GetDuration();
  EXPECT_NEAR(duration_ms, 1000.0, 1.0);
  engine.Release();
}

TEST(AudioEngineTest, SeekUpdatesPosition) {
  const std::string path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  AudioEngine engine;
  ASSERT_TRUE(engine.Initialize(48000, 4));
  ASSERT_TRUE(engine.LoadTrack("track_1", path));

  engine.Seek(250.0);
  EXPECT_NEAR(engine.GetCurrentPosition(), 250.0, 2.0);
  engine.Release();
}

TEST(AudioEngineTest, PlaybackAndSeekTransitions) {
  const std::string path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  AudioEngine engine;
  ASSERT_TRUE(engine.Initialize(48000, 4));
  ASSERT_TRUE(engine.LoadTrack("track_1", path));

  engine.Seek(100.0);
  EXPECT_NEAR(engine.GetCurrentPosition(), 100.0, 2.0);

  engine.Play();
  if (!engine.IsPlaying()) {
    if (engine.GetLastErrorCode() == core::ErrorCode::kStreamError) {
      GTEST_SKIP() << "Audio stream failed to start on this device.";
    }
    FAIL() << "Expected playback to start.";
  }

  engine.Pause();
  EXPECT_FALSE(engine.IsPlaying());

  engine.Play();
  EXPECT_TRUE(engine.IsPlaying());

  engine.Stop();
  EXPECT_FALSE(engine.IsPlaying());
  EXPECT_NEAR(engine.GetCurrentPosition(), 0.0, 0.1);

  engine.Release();
}
#else
TEST(AudioEngineTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only AudioEngine tests.";
}
#endif

}  // namespace sezo
