#include <gtest/gtest.h>

#include "playback/TimeStretch.h"

namespace sezo {
namespace playback {

TEST(TimeStretchTest, PassThroughWhenInactive) {
  GTEST_SKIP() << "TODO: when pitch=0 and stretch=1, output equals input.";
}

TEST(TimeStretchTest, PitchDoesNotChangeDuration) {
  GTEST_SKIP() << "TODO: pitch-only change should not change output frame count.";
}

TEST(TimeStretchTest, StretchFactorChangesInputRatio) {
  GTEST_SKIP() << "TODO: stretch factor 2.0 should consume ~2x input per output.";
}

TEST(TimeStretchTest, SilenceProducesSilenceWithoutNaNs) {
  GTEST_SKIP() << "TODO: feed silence, verify output finite and near zero.";
}

}  // namespace playback
}  // namespace sezo
