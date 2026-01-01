#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "recording/RecordingPipeline.h"
#endif

namespace sezo {
namespace recording {

#if defined(__ANDROID__)
TEST(RecordingPipelineTest, StartStopProducesFile) {
  GTEST_SKIP() << "TODO: record 1s and verify output file size > 0.";
}

TEST(RecordingPipelineTest, StartWhileRecordingFails) {
  GTEST_SKIP() << "TODO: call StartRecording twice and expect failure.";
}

TEST(RecordingPipelineTest, NoInitialBufferNoise) {
  GTEST_SKIP() << "TODO: record silence and ensure low noise floor.";
}
#else
TEST(RecordingPipelineTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only recording pipeline tests.";
}
#endif

}  // namespace recording
}  // namespace sezo
