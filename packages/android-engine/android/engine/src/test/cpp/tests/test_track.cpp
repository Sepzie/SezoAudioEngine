#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "playback/Track.h"
#include "test_helpers.h"

namespace sezo {
namespace playback {

namespace {

std::vector<float> ReadSamplesWithRetry(Track& track,
                                        size_t frames,
                                        int32_t channels,
                                        int attempts = 50) {
  std::vector<float> output(frames * static_cast<size_t>(channels), 0.0f);
  for (int i = 0; i < attempts; ++i) {
    track.ReadSamples(output.data(), frames);
    if (test::Rms(output.data(), output.size()) > 1e-4f) {
      return output;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return output;
}

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

TEST(TrackTest, MuteOutputsSilence) {
  const std::string path = test::FixturePath("stereo_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  Track track("track_mute", path);
  ASSERT_TRUE(track.Load());
  const int32_t channels = track.GetChannels();
  ASSERT_EQ(channels, 2);

  const size_t frames = 512;
  auto output = ReadSamplesWithRetry(track, frames, channels);
  EXPECT_GT(test::Rms(output.data(), output.size()), 0.01f);

  track.SetMuted(true);
  std::vector<float> muted(frames * static_cast<size_t>(channels), 1.0f);
  track.ReadSamples(muted.data(), frames);
  EXPECT_LT(test::MaxAbs(muted.data(), muted.size()), 1e-4f);
}

TEST(TrackTest, SeekResetsTimeStretchState) {
  const std::string path = test::FixturePath("stereo_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  Track baseline_track("baseline", path);
  Track seek_track("seek", path);
  ASSERT_TRUE(baseline_track.Load());
  ASSERT_TRUE(seek_track.Load());
  ASSERT_EQ(baseline_track.GetChannels(), 2);

  baseline_track.SetPitchSemitones(4.0f);
  seek_track.SetPitchSemitones(4.0f);

  const size_t frames = 1024;
  ASSERT_TRUE(baseline_track.Seek(0));
  auto baseline = ReadSamplesWithRetry(baseline_track, frames, 2);

  ReadSamplesWithRetry(seek_track, frames, 2);
  ASSERT_TRUE(seek_track.Seek(0));
  auto after_seek = ReadSamplesWithRetry(seek_track, frames, 2);

  float diff_sum = 0.0f;
  for (size_t i = 0; i < baseline.size(); ++i) {
    diff_sum += std::abs(baseline[i] - after_seek[i]);
  }
  const float diff_mean = diff_sum / static_cast<float>(baseline.size());
  EXPECT_LT(diff_mean, 1e-3f);
}

TEST(TrackTest, VolumeAndPanApplied) {
  const std::string path = test::FixturePath("stereo_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  Track track("track_pan", path);
  ASSERT_TRUE(track.Load());
  ASSERT_EQ(track.GetChannels(), 2);

  const size_t frames = 1024;
  track.SetVolume(1.0f);
  track.SetPan(0.0f);
  ASSERT_TRUE(track.Seek(0));
  auto centered = ReadSamplesWithRetry(track, frames, 2);
  const float centered_left = ChannelRms(centered, 0, 2);
  const float centered_right = ChannelRms(centered, 1, 2);
  EXPECT_NEAR(centered_left, centered_right, 0.02f);

  track.SetVolume(0.5f);
  track.SetPan(0.0f);
  ASSERT_TRUE(track.Seek(0));
  auto half = ReadSamplesWithRetry(track, frames, 2);
  const float half_left = ChannelRms(half, 0, 2);
  EXPECT_NEAR(half_left / centered_left, 0.5f, 0.05f);

  track.SetVolume(1.0f);
  track.SetPan(-1.0f);
  ASSERT_TRUE(track.Seek(0));
  auto hard_left = ReadSamplesWithRetry(track, frames, 2);
  const float hard_left_right = ChannelRms(hard_left, 1, 2);
  EXPECT_LT(hard_left_right, 1e-3f);
}

}  // namespace playback
}  // namespace sezo
