#include <gtest/gtest.h>

#include "playback/MultiTrackMixer.h"

namespace sezo {
namespace playback {

TEST(MultiTrackMixerTest, SoloOverridesMute) {
  GTEST_SKIP() << "TODO: ensure soloed tracks play even if others muted.";
}

TEST(MultiTrackMixerTest, TrackOffsetsRespected) {
  GTEST_SKIP() << "TODO: verify timeline offsets skip samples before start.";
}

TEST(MultiTrackMixerTest, MasterVolumeAppliedAndClipped) {
  GTEST_SKIP() << "TODO: verify master gain and soft clipping behavior.";
}

}  // namespace playback
}  // namespace sezo
