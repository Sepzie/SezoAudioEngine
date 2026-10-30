#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "AudioEngine.h"
#include "audio/WAVDecoder.h"
#include "test_helpers.h"

#include <algorithm>
#include <vector>
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
namespace {

// Renders the track with its effects and returns the tone's frequency.
float ExtractedFrequencyHz(AudioEngine& engine, const std::string& track_id) {
  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_engine_pitch_", ".wav"));
  AudioEngine::ExtractionOptions options;
  options.format = "wav";
  const auto result = engine.ExtractTrack(track_id, temp_file.path(), options);
  if (!result.success) {
    ADD_FAILURE() << "Extraction failed: " << result.error_message;
    return 0.0f;
  }
  audio::WAVDecoder decoder;
  if (!decoder.Open(temp_file.path())) {
    ADD_FAILURE() << "Cannot open " << temp_file.path();
    return 0.0f;
  }
  const auto& format = decoder.GetFormat();
  std::vector<float> samples(48000 * static_cast<size_t>(format.channels));
  const size_t frames = decoder.Read(samples.data(), 48000);
  // Skip the stretcher's start-up and the tail.
  const size_t skip = std::min<size_t>(4096, frames / 4);
  const size_t count = frames > 2 * skip ? frames - 2 * skip : 0;
  return test::EstimateFrequencyHz(samples.data() + skip * format.channels,
                                   count, format.channels, format.sample_rate);
}

}  // namespace

TEST(AudioEngineTest, MasterPitchComposesWithTrackPitch) {
  const std::string path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  AudioEngine engine;
  ASSERT_TRUE(engine.Initialize(48000, 4));
  ASSERT_TRUE(engine.LoadTrack("track_1", path));

  engine.SetPitch(2.0f);
  engine.SetTrackPitch("track_1", -2.0f);
  EXPECT_FLOAT_EQ(engine.GetPitch(), 2.0f);
  EXPECT_FLOAT_EQ(engine.GetTrackPitch("track_1"), -2.0f);
  EXPECT_NEAR(ExtractedFrequencyHz(engine, "track_1"), 1000.0f, 10.0f);

  // A master change after the track value keeps the track's offset.
  engine.SetPitch(-3.0f);
  EXPECT_FLOAT_EQ(engine.GetTrackPitch("track_1"), -2.0f);
  EXPECT_NEAR(ExtractedFrequencyHz(engine, "track_1"),
              1000.0f * test::SemitonesToRatio(-5.0f), 15.0f);
  engine.Release();
}

TEST(AudioEngineTest, TrackLoadedAfterMasterPitchGetsIt) {
  const std::string path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  AudioEngine engine;
  ASSERT_TRUE(engine.Initialize(48000, 4));
  engine.SetPitch(3.0f);
  ASSERT_TRUE(engine.LoadTrack("track_1", path));

  EXPECT_FLOAT_EQ(engine.GetTrackPitch("track_1"), 0.0f);
  EXPECT_NEAR(ExtractedFrequencyHz(engine, "track_1"),
              1000.0f * test::SemitonesToRatio(3.0f), 15.0f);
  engine.Release();
}

TEST(AudioEngineTest, MasterSpeedMultipliesTrackSpeed) {
  const std::string path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  AudioEngine engine;
  ASSERT_TRUE(engine.Initialize(48000, 4));
  engine.SetSpeed(2.0f);
  ASSERT_TRUE(engine.LoadTrack("track_1", path));
  engine.SetTrackSpeed("track_1", 0.5f);
  EXPECT_FLOAT_EQ(engine.GetSpeed(), 2.0f);
  EXPECT_FLOAT_EQ(engine.GetTrackSpeed("track_1"), 0.5f);

  AudioEngine::ExtractionOptions options;
  options.format = "wav";
  test::ScopedTempFile composed(test::MakeTempPath("sezo_engine_speed_", ".wav"));
  const auto at_one = engine.ExtractTrack("track_1", composed.path(), options);
  ASSERT_TRUE(at_one.success) << at_one.error_message;
  // 0.5 * 2.0 = 1.0: the full second.
  EXPECT_NEAR(static_cast<double>(at_one.duration_samples), 48000.0, 48000.0 * 0.05);

  engine.SetTrackSpeed("track_1", 1.0f);
  test::ScopedTempFile master_only(test::MakeTempPath("sezo_engine_speed_", ".wav"));
  const auto at_two = engine.ExtractTrack("track_1", master_only.path(), options);
  ASSERT_TRUE(at_two.success) << at_two.error_message;
  EXPECT_NEAR(static_cast<double>(at_two.duration_samples), 24000.0, 48000.0 * 0.05);
  engine.Release();
}
#else
TEST(AudioEngineTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only AudioEngine tests.";
}
#endif

}  // namespace sezo
