#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "playback/OboePlayer.h"
#endif

namespace sezo {
namespace playback {

#if defined(__ANDROID__)
TEST(OboePlayerTest, InitializeStartStopLifecycle) {
  GTEST_SKIP() << "TODO: init/start/stop/close and verify IsRunning.";
}

TEST(OboePlayerTest, CallbackAdvancesClock) {
  GTEST_SKIP() << "TODO: run callback and confirm master clock moves.";
}
#else
TEST(OboePlayerTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only OboePlayer tests.";
}
#endif

}  // namespace playback
}  // namespace sezo
