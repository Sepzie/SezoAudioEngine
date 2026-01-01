#include <gtest/gtest.h>

#include "playback/Track.h"

namespace sezo {
namespace playback {

TEST(TrackTest, MuteOutputsSilence) {
  GTEST_SKIP() << "TODO: load fixture, mute track, verify output is zero.";
}

TEST(TrackTest, SeekResetsTimeStretchState) {
  GTEST_SKIP() << "TODO: apply stretch, seek, ensure output has no artifacts.";
}

TEST(TrackTest, VolumeAndPanApplied) {
  GTEST_SKIP() << "TODO: verify per-channel gain matches pan/volume settings.";
}

}  // namespace playback
}  // namespace sezo
