#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "playback/TimeStretch.h"
#include "test_helpers.h"

namespace sezo {
namespace playback {

TEST(TimeStretchTest, PassThroughWhenInactive) {
  TimeStretch stretch(48000, 2);
  const size_t frames = 256;
  std::vector<float> input(frames * 2);
  std::vector<float> output(frames * 2, 0.0f);

  for (size_t i = 0; i < frames; ++i) {
    const float value = static_cast<float>(i) / static_cast<float>(frames);
    input[i * 2] = value;
    input[i * 2 + 1] = 1.0f - value;
  }

  stretch.Process(input.data(), frames, output.data(), frames);
  for (size_t i = 0; i < input.size(); ++i) {
    EXPECT_FLOAT_EQ(output[i], input[i]);
  }
}

TEST(TimeStretchTest, PitchDoesNotChangeDuration) {
  TimeStretch stretch(48000, 2);
  stretch.SetPitchSemitones(5.0f);
  EXPECT_TRUE(stretch.IsActive());

  const size_t frames = 512;
  std::vector<float> input(frames * 2);
  std::vector<float> output(frames * 2, 0.0f);
  const double kPi = 3.14159265358979323846;

  for (size_t i = 0; i < frames; ++i) {
    const float phase = static_cast<float>(2.0 * kPi * 440.0 * static_cast<double>(i) /
                                           48000.0);
    const float value = 0.5f * std::sin(phase);
    input[i * 2] = value;
    input[i * 2 + 1] = value;
  }

  stretch.Process(input.data(), frames, output.data(), frames);
  EXPECT_TRUE(test::AllFinite(output.data(), output.size()));
  EXPECT_GT(test::Rms(output.data(), output.size()), 0.01f);

  float diff_sum = 0.0f;
  for (size_t i = 0; i < output.size(); ++i) {
    diff_sum += std::abs(output[i] - input[i]);
  }
  const float diff_mean = diff_sum / static_cast<float>(output.size());
  EXPECT_GT(diff_mean, 1e-4f);
}

TEST(TimeStretchTest, StretchFactorChangesInputRatio) {
  TimeStretch stretch(48000, 2);
  stretch.SetStretchFactor(2.0f);
  EXPECT_TRUE(stretch.IsActive());

  const size_t frames = 512;
  std::vector<float> input(frames * 2);
  std::vector<float> output(frames * 2, 0.0f);
  for (size_t i = 0; i < frames; ++i) {
    const float value = static_cast<float>(i) / static_cast<float>(frames);
    input[i * 2] = value;
    input[i * 2 + 1] = value;
  }

  stretch.Process(input.data(), frames, output.data(), frames);
  EXPECT_TRUE(test::AllFinite(output.data(), output.size()));

  float diff_sum = 0.0f;
  for (size_t i = 0; i < output.size(); ++i) {
    diff_sum += std::abs(output[i] - input[i]);
  }
  const float diff_mean = diff_sum / static_cast<float>(output.size());
  EXPECT_GT(diff_mean, 1e-4f);
}

TEST(TimeStretchTest, SilenceProducesSilenceWithoutNaNs) {
  TimeStretch stretch(48000, 2);
  stretch.SetPitchSemitones(3.0f);
  stretch.SetStretchFactor(1.5f);

  const size_t frames = 512;
  std::vector<float> input(frames * 2, 0.0f);
  std::vector<float> output(frames * 2, 1.0f);

  stretch.Process(input.data(), frames, output.data(), frames);
  EXPECT_TRUE(test::AllFinite(output.data(), output.size()));
  EXPECT_LT(test::MaxAbs(output.data(), output.size()), 1e-4f);
}

}  // namespace playback
}  // namespace sezo
