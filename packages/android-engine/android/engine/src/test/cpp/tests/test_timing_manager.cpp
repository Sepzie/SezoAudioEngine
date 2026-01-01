#include <gtest/gtest.h>

#include "core/TimingManager.h"

namespace sezo {
namespace core {

TEST(TimingManagerTest, MsToSamplesRoundTrip) {
  const int32_t sample_rate = 48000;
  TimingManager timing(sample_rate);
  const double tolerance_ms = 1000.0 / static_cast<double>(sample_rate);
  const double inputs_ms[] = {0.0, 1.0, 10.5, 1234.5};

  for (double input_ms : inputs_ms) {
    const int64_t samples = timing.MsToSamples(input_ms);
    const double round_trip_ms = timing.SamplesToMs(samples);
    EXPECT_NEAR(round_trip_ms, input_ms, tolerance_ms);
  }
}

TEST(TimingManagerTest, DurationUpdates) {
  const int32_t sample_rate = 44100;
  TimingManager timing(sample_rate);

  timing.SetDuration(44100);
  EXPECT_EQ(timing.GetDurationSamples(), 44100);
  EXPECT_NEAR(timing.GetDurationMs(), 1000.0, 1e-6);

  timing.SetDuration(88200);
  EXPECT_EQ(timing.GetDurationSamples(), 88200);
  EXPECT_NEAR(timing.GetDurationMs(), 2000.0, 1e-6);
}

}  // namespace core
}  // namespace sezo
