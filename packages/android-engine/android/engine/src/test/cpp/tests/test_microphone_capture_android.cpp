#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "recording/MicrophoneCapture.h"
#endif

namespace sezo {
namespace recording {

#if defined(__ANDROID__)
TEST(MicrophoneCaptureTest, StartStopLifecycle) {
  GTEST_SKIP() << "TODO: initialize, start, stop, close without errors.";
}

TEST(MicrophoneCaptureTest, InputLevelUpdates) {
  GTEST_SKIP() << "TODO: verify input level is finite and within [0,1].";
}
#else
TEST(MicrophoneCaptureTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only microphone capture tests.";
}
#endif

}  // namespace recording
}  // namespace sezo
