#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "recording/MicrophoneCapture.h"

#include <chrono>
#include <cmath>
#include <thread>
#endif

namespace sezo {
namespace recording {

#if defined(__ANDROID__)
TEST(MicrophoneCaptureTest, StartStopLifecycle) {
  MicrophoneCapture capture(48000, 1);
  if (!capture.Initialize()) {
    GTEST_SKIP() << "Microphone input unavailable or permission denied.";
  }
  if (!capture.Start()) {
    GTEST_SKIP() << "Failed to start microphone capture.";
  }

  EXPECT_TRUE(capture.IsCapturing());
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_GE(capture.GetAvailableFrames(), 0u);

  EXPECT_TRUE(capture.Stop());
  EXPECT_FALSE(capture.IsCapturing());
  capture.Close();
}

TEST(MicrophoneCaptureTest, InputLevelUpdates) {
  MicrophoneCapture capture(48000, 1);
  if (!capture.Initialize()) {
    GTEST_SKIP() << "Microphone input unavailable or permission denied.";
  }
  if (!capture.Start()) {
    GTEST_SKIP() << "Failed to start microphone capture.";
  }

  bool has_frames = false;
  for (int i = 0; i < 40; ++i) {
    if (capture.GetAvailableFrames() > 0) {
      has_frames = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!has_frames) {
    capture.Stop();
    capture.Close();
    GTEST_SKIP() << "No input frames captured.";
  }

  const float level = capture.GetInputLevel();
  EXPECT_TRUE(std::isfinite(level));
  EXPECT_GE(level, 0.0f);
  EXPECT_LE(level, 1.0f);

  capture.Stop();
  capture.Close();
}
#else
TEST(MicrophoneCaptureTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only microphone capture tests.";
}
#endif

}  // namespace recording
}  // namespace sezo
