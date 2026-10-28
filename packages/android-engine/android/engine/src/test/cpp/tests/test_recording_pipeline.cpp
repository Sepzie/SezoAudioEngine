// Host tests for RecordingPipeline: the encoder must consume audio while the
// take is being captured, so StopRecording() costs the same for a 1-second and
// a 10-minute take and memory never holds the whole recording.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "audio/WAVDecoder.h"
#include "recording/RecordingPipeline.h"
#include "test_helpers.h"
#include "tone_source.h"

namespace sezo {
namespace recording {
namespace {

using test::ProducerStats;
using test::ToneSample;
using test::ToneSource;
using test::WaitFor;

RecordingConfig WavConfig() {
  RecordingConfig config;
  // Deliberately wrong: the pipeline must take the format from the source.
  config.sample_rate = 44100;
  config.channels = 2;
  config.format = "wav";
  config.bits_per_sample = 16;
  return config;
}

std::vector<float> DecodeWav(const std::string& path, int32_t* sample_rate, int32_t* channels) {
  audio::WAVDecoder decoder;
  if (!decoder.Open(path)) {
    return {};
  }
  const auto& format = decoder.GetFormat();
  *sample_rate = format.sample_rate;
  *channels = format.channels;

  std::vector<float> samples;
  std::vector<float> chunk(4096 * static_cast<size_t>(format.channels));
  while (true) {
    const size_t frames = decoder.Read(chunk.data(), 4096);
    if (frames == 0) {
      break;
    }
    samples.insert(samples.end(), chunk.begin(),
                   chunk.begin() + frames * static_cast<size_t>(format.channels));
  }
  return samples;
}

}  // namespace

TEST(RecordingPipelineStreamingTest, EncodesEveryFrameAndStopIsBounded) {
  // 10 s of audio delivered at 10x real time through a ring that only holds
  // 0.5 s: the pipeline can only succeed if it encodes while capturing.
  const int32_t kSampleRate = 48000;
  const int32_t kChannels = 1;
  const size_t kTotalFrames = static_cast<size_t>(kSampleRate) * 10;
  const size_t kRingFrames = static_cast<size_t>(kSampleRate) / 2;

  auto stats = std::make_shared<ProducerStats>();
  auto source = std::make_unique<ToneSource>(kSampleRate, kChannels, kRingFrames, stats);
  ToneSource* source_ptr = source.get();  // valid until StopRecording()

  RecordingPipeline pipeline;
  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_stream_", ".wav"));
  ASSERT_TRUE(pipeline.StartRecording(std::move(source), temp_file.path(), WavConfig()));
  ASSERT_TRUE(pipeline.IsRecording());

  source_ptr->StartProducer(4800, std::chrono::milliseconds(10), kTotalFrames);
  WaitFor([&]() { return stats->finished.load(); }, std::chrono::seconds(30));
  ASSERT_EQ(stats->frames_produced.load(), kTotalFrames);

  // Everything the producer pushed must already be in the file (minus what is
  // still in flight in the ring) before stop is even called.
  const auto size_before_stop = std::filesystem::file_size(temp_file.path());
  EXPECT_GT(size_before_stop, static_cast<uintmax_t>(44 + (kTotalFrames - kRingFrames) * 2));

  const auto stop_begin = std::chrono::steady_clock::now();
  const RecordingResult result = pipeline.StopRecording();
  const auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - stop_begin)
                           .count();

  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(pipeline.IsRecording());
  EXPECT_LT(stop_ms, 500);
  EXPECT_EQ(result.duration_samples, static_cast<int64_t>(kTotalFrames));
  EXPECT_EQ(result.sample_rate, kSampleRate);
  EXPECT_EQ(result.channels, kChannels);
  EXPECT_EQ(result.file_size, static_cast<int64_t>(44 + kTotalFrames * 2));
  EXPECT_EQ(result.output_path, temp_file.path());

  int32_t decoded_rate = 0;
  int32_t decoded_channels = 0;
  const auto samples = DecodeWav(temp_file.path(), &decoded_rate, &decoded_channels);
  ASSERT_EQ(decoded_rate, kSampleRate);
  ASSERT_EQ(decoded_channels, kChannels);
  ASSERT_EQ(samples.size(), kTotalFrames);

  // The file holds the tone in order, with no dropped or duplicated chunks.
  for (size_t i = 0; i < samples.size(); i += 977) {
    EXPECT_NEAR(samples[i], ToneSample(static_cast<int64_t>(i), kSampleRate), 1e-3f)
        << "at frame " << i;
  }
}

TEST(RecordingPipelineStreamingTest, FileGrowsWhileCapturing) {
  const int32_t kSampleRate = 48000;
  const int32_t kChannels = 2;

  auto stats = std::make_shared<ProducerStats>();
  auto source = std::make_unique<ToneSource>(kSampleRate, kChannels, kSampleRate, stats);
  ToneSource* source_ptr = source.get();  // valid until StopRecording()

  RecordingPipeline pipeline;
  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_grow_", ".wav"));
  ASSERT_TRUE(pipeline.StartRecording(std::move(source), temp_file.path(), WavConfig()));

  // Real-time pace: 100 ms of audio every 100 ms.
  source_ptr->StartProducer(4800, std::chrono::milliseconds(100), 0);

  WaitFor([&]() { return pipeline.GetRecordedSamples() >= kSampleRate / 2; },
          std::chrono::seconds(10));
  const int64_t frames_mid = pipeline.GetRecordedSamples();
  EXPECT_GE(frames_mid, kSampleRate / 2);

  // Bytes reach the file during the take, not at stop.
  WaitFor([&]() {
            return std::filesystem::file_size(temp_file.path()) >=
                   static_cast<uintmax_t>(44 + (kSampleRate / 4) * kChannels * 2);
          },
          std::chrono::seconds(5));
  const auto size_mid = std::filesystem::file_size(temp_file.path());
  EXPECT_GE(size_mid, static_cast<uintmax_t>(44 + (kSampleRate / 4) * kChannels * 2));

  const RecordingResult result = pipeline.StopRecording();
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.channels, kChannels);
  EXPECT_EQ(result.duration_samples, static_cast<int64_t>(stats->frames_produced.load()));
  EXPECT_EQ(result.file_size,
            static_cast<int64_t>(44 + result.duration_samples * kChannels * 2));
}

TEST(RecordingPipelineStreamingTest, StopWithoutAudioReportsError) {
  auto source = std::make_unique<ToneSource>(48000, 1, 4800, std::make_shared<ProducerStats>());

  RecordingPipeline pipeline;
  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_empty_", ".wav"));
  ASSERT_TRUE(pipeline.StartRecording(std::move(source), temp_file.path(), WavConfig()));

  const RecordingResult result = pipeline.StopRecording();
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_message, "No audio data recorded");
  EXPECT_EQ(result.duration_samples, 0);
  EXPECT_FALSE(pipeline.IsRecording());
}

TEST(RecordingPipelineStreamingTest, StopWhenIdleReportsNotRecording) {
  RecordingPipeline pipeline;
  const RecordingResult result = pipeline.StopRecording();
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_message, "Not recording");
}

TEST(RecordingPipelineStreamingTest, RejectsUnsupportedFormat) {
  auto source = std::make_unique<ToneSource>(48000, 1, 4800, std::make_shared<ProducerStats>());
  RecordingConfig config = WavConfig();
  config.format = "ogg";

  RecordingPipeline pipeline;
  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_bad_", ".ogg"));
  EXPECT_FALSE(pipeline.StartRecording(std::move(source), temp_file.path(), config));
  EXPECT_FALSE(pipeline.IsRecording());
}

}  // namespace recording
}  // namespace sezo
