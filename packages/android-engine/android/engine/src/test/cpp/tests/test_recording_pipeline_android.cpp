#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "recording/RecordingPipeline.h"
#include "audio/WAVDecoder.h"
#include "test_helpers.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>
#endif

namespace sezo {
namespace recording {

#if defined(__ANDROID__)
namespace {

float ComputeRms(const std::vector<float>& samples) {
  if (samples.empty()) {
    return 0.0f;
  }
  double sum = 0.0;
  for (float value : samples) {
    const double v = static_cast<double>(value);
    sum += v * v;
  }
  return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
}

}  // namespace

TEST(RecordingPipelineTest, StartStopProducesFile) {
  RecordingPipeline pipeline;
  RecordingConfig config;
  config.sample_rate = 48000;
  config.channels = 1;
  config.format = "wav";
  config.bits_per_sample = 16;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_record_", ".wav"));
  if (!pipeline.StartRecording(temp_file.path(), config)) {
    GTEST_SKIP() << "Recording start failed (mic permission or input unavailable).";
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto result = pipeline.StopRecording();
  if (!result.success && result.error_message == "No audio data recorded") {
    GTEST_SKIP() << "No audio data captured.";
  }

  ASSERT_TRUE(result.success);
  EXPECT_GT(result.file_size, 44);
  EXPECT_GT(result.duration_samples, 0);
}

TEST(RecordingPipelineTest, StartWhileRecordingFails) {
  RecordingPipeline pipeline;
  RecordingConfig config;
  config.sample_rate = 48000;
  config.channels = 1;
  config.format = "wav";
  config.bits_per_sample = 16;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_record_", ".wav"));
  if (!pipeline.StartRecording(temp_file.path(), config)) {
    GTEST_SKIP() << "Recording start failed (mic permission or input unavailable).";
  }

  test::ScopedTempFile temp_file2(test::MakeTempPath("sezo_record_", ".wav"));
  EXPECT_FALSE(pipeline.StartRecording(temp_file2.path(), config));

  (void)pipeline.StopRecording();
}

TEST(RecordingPipelineTest, NoInitialBufferNoise) {
  RecordingPipeline pipeline;
  RecordingConfig config;
  config.sample_rate = 48000;
  config.channels = 1;
  config.format = "wav";
  config.bits_per_sample = 16;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_record_silence_", ".wav"));
  if (!pipeline.StartRecording(temp_file.path(), config)) {
    GTEST_SKIP() << "Recording start failed (mic permission or input unavailable).";
  }

  pipeline.SetVolume(0.0f);
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  auto result = pipeline.StopRecording();
  if (!result.success && result.error_message == "No audio data recorded") {
    GTEST_SKIP() << "No audio data captured.";
  }
  ASSERT_TRUE(result.success);

  audio::WAVDecoder decoder;
  ASSERT_TRUE(decoder.Open(temp_file.path()));
  const auto& format = decoder.GetFormat();
  std::vector<float> buffer(1024 * format.channels);
  std::vector<float> samples;

  while (true) {
    const size_t frames_read = decoder.Read(buffer.data(), 1024);
    if (frames_read == 0) {
      break;
    }
    samples.insert(samples.end(), buffer.begin(),
                   buffer.begin() + frames_read * format.channels);
  }

  const float rms = ComputeRms(samples);
  EXPECT_LT(rms, 1e-3f);
}
#else
TEST(RecordingPipelineTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only recording pipeline tests.";
}
#endif

}  // namespace recording
}  // namespace sezo
