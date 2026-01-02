#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "extraction/ExtractionPipeline.h"
#include "playback/Track.h"
#include "audio/WAVDecoder.h"
#include "test_helpers.h"

#include <atomic>
#include <cmath>
#include <vector>
#endif

namespace sezo {
namespace extraction {

#if defined(__ANDROID__)
namespace {

float ChannelRms(const std::vector<float>& samples, int channel, int channels) {
  double sum = 0.0;
  size_t count = 0;
  for (size_t i = static_cast<size_t>(channel); i < samples.size(); i += channels) {
    const double value = static_cast<double>(samples[i]);
    sum += value * value;
    ++count;
  }
  return count == 0 ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

}  // namespace

TEST(ExtractionPipelineTest, ExtractSingleTrackAppliesEffects) {
  const std::string path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  auto track = std::make_shared<playback::Track>("track_1", path);
  ASSERT_TRUE(track->Load());
  const int64_t input_frames = track->GetDuration();
  ASSERT_GT(input_frames, 0);

  track->SetStretchFactor(2.0f);

  ExtractionPipeline pipeline;
  ExtractionConfig config;
  config.format = audio::EncoderFormat::kWAV;
  config.sample_rate = 48000;
  config.bits_per_sample = 16;
  config.include_effects = true;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_extract_", ".wav"));
  auto result = pipeline.ExtractTrack(track, temp_file.path(), config);
  EXPECT_TRUE(result.success);
  EXPECT_GT(result.file_size, 0);
  EXPECT_GT(result.duration_samples, 0);
  EXPECT_LT(result.duration_samples, input_frames);
}

TEST(ExtractionPipelineTest, ExportRespectsSoloMute) {
  const std::string path = test::FixturePath("stereo_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  auto solo_track = std::make_shared<playback::Track>("solo", path);
  auto other_track = std::make_shared<playback::Track>("other", path);
  ASSERT_TRUE(solo_track->Load());
  ASSERT_TRUE(other_track->Load());

  solo_track->SetSolo(true);
  solo_track->SetPan(-1.0f);
  other_track->SetPan(1.0f);

  ExtractionPipeline pipeline;
  ExtractionConfig config;
  config.format = audio::EncoderFormat::kWAV;
  config.sample_rate = 48000;
  config.bits_per_sample = 16;
  config.include_effects = true;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_extract_mix_", ".wav"));
  auto result = pipeline.ExtractMixedTracks({solo_track, other_track}, temp_file.path(), config);
  ASSERT_TRUE(result.success);
  EXPECT_GT(result.file_size, 0);

  audio::WAVDecoder decoder;
  ASSERT_TRUE(decoder.Open(temp_file.path()));
  const auto& format = decoder.GetFormat();
  ASSERT_EQ(format.channels, 2);

  std::vector<float> buffer(1024 * 2);
  const size_t frames_read = decoder.Read(buffer.data(), 1024);
  ASSERT_GT(frames_read, 0u);
  buffer.resize(frames_read * 2);

  const float left_rms = ChannelRms(buffer, 0, 2);
  const float right_rms = ChannelRms(buffer, 1, 2);
  EXPECT_GT(left_rms, 0.01f);
  EXPECT_LT(right_rms, 1e-3f);
}

TEST(ExtractionPipelineTest, ProgressIsMonotonic) {
  const std::string path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  auto track = std::make_shared<playback::Track>("track_1", path);
  ASSERT_TRUE(track->Load());

  ExtractionPipeline pipeline;
  ExtractionConfig config;
  config.format = audio::EncoderFormat::kWAV;
  config.sample_rate = 48000;
  config.bits_per_sample = 16;
  config.include_effects = true;

  std::vector<float> progress_values;
  auto progress_cb = [&progress_values](float progress) {
    progress_values.push_back(progress);
  };

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_extract_prog_", ".wav"));
  auto result = pipeline.ExtractTrack(track, temp_file.path(), config, progress_cb);
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(progress_values.empty());

  float last = 0.0f;
  for (float value : progress_values) {
    EXPECT_GE(value, last);
    EXPECT_GE(value, 0.0f);
    EXPECT_LE(value, 1.0f);
    last = value;
  }
  EXPECT_GE(last, 0.99f);
}
#else
TEST(ExtractionPipelineTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only extraction pipeline tests.";
}
#endif

}  // namespace extraction
}  // namespace sezo
