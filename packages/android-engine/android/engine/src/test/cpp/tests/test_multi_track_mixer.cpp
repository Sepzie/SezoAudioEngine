#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "playback/MultiTrackMixer.h"
#include "test_helpers.h"

namespace sezo {
namespace playback {

namespace {

std::vector<float> MixWithRetry(MultiTrackMixer& mixer,
                                size_t frames,
                                int64_t timeline_start,
                                int attempts = 50) {
  std::vector<float> output(frames * 2, 0.0f);
  for (int i = 0; i < attempts; ++i) {
    std::fill(output.begin(), output.end(), 0.0f);
    mixer.Mix(output.data(), frames, timeline_start);
    if (test::Rms(output.data(), output.size()) > 1e-4f) {
      return output;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return output;
}

float SegmentMaxAbs(const std::vector<float>& samples, size_t start, size_t count) {
  float max_value = 0.0f;
  const size_t end = std::min(samples.size(), start + count);
  for (size_t i = start; i < end; ++i) {
    const float value = std::abs(samples[i]);
    if (value > max_value) {
      max_value = value;
    }
  }
  return max_value;
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

TEST(MultiTrackMixerTest, SoloOverridesMute) {
  const std::string path = test::FixturePath("stereo_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  auto solo_track = std::make_shared<Track>("solo", path);
  auto other_track = std::make_shared<Track>("other", path);
  ASSERT_TRUE(solo_track->Load());
  ASSERT_TRUE(other_track->Load());
  solo_track->SetSolo(true);
  solo_track->SetPan(-1.0f);
  other_track->SetPan(1.0f);

  MultiTrackMixer mixer;
  mixer.AddTrack(solo_track);
  mixer.AddTrack(other_track);

  const size_t frames = 512;
  auto output = MixWithRetry(mixer, frames, 0);
  const float left_rms = ChannelRms(output, 0, 2);
  const float right_rms = ChannelRms(output, 1, 2);
  EXPECT_GT(left_rms, 0.01f);
  EXPECT_LT(right_rms, 1e-3f);
}

TEST(MultiTrackMixerTest, TrackOffsetsRespected) {
  const std::string path = test::FixturePath("stereo_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  auto track = std::make_shared<Track>("offset", path);
  ASSERT_TRUE(track->Load());
  track->SetStartTimeSamples(256);

  MultiTrackMixer mixer;
  mixer.AddTrack(track);

  const size_t frames = 512;
  auto output = MixWithRetry(mixer, frames, 0);
  const size_t silent_samples = 256 * 2;
  EXPECT_LT(SegmentMaxAbs(output, 0, silent_samples), 1e-4f);
  EXPECT_GT(test::Rms(output.data() + silent_samples,
                      output.size() - silent_samples),
            0.01f);
}

TEST(MultiTrackMixerTest, MasterVolumeAppliedAndClipped) {
  const std::string path = test::FixturePath("stereo_1khz_1s.wav");
  if (!test::FileExists(path)) {
    GTEST_SKIP() << "Missing fixture: " << path;
  }

  auto track = std::make_shared<Track>("master", path);
  ASSERT_TRUE(track->Load());
  track->SetVolume(2.0f);

  MultiTrackMixer mixer;
  mixer.AddTrack(track);
  mixer.SetMasterVolume(2.0f);

  const size_t frames = 512;
  auto output = MixWithRetry(mixer, frames, 0);
  const float max_abs = test::MaxAbs(output.data(), output.size());
  EXPECT_LE(max_abs, 1.0f);
  EXPECT_GT(max_abs, 0.9f);
}

}  // namespace playback
}  // namespace sezo
