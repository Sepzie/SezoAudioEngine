// Host tests for RecordingPipeline: the encoder must consume audio while the
// take is being captured, so StopRecording() costs the same for a 1-second and
// a 10-minute take and memory never holds the whole recording.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "audio/WAVDecoder.h"
#include "core/CircularBuffer.h"
#include "recording/RecordingPipeline.h"
#include "test_helpers.h"

namespace sezo {
namespace recording {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kToneHz = 440.0;
constexpr float kToneAmplitude = 0.5f;

float ToneSample(int64_t frame, int32_t sample_rate) {
  const double phase = 2.0 * kPi * kToneHz * static_cast<double>(frame) /
                       static_cast<double>(sample_rate);
  return kToneAmplitude * static_cast<float>(std::sin(phase));
}

// Counters a test keeps after the pipeline has destroyed the source (the
// pipeline owns its source and releases it in StopRecording()).
struct ProducerStats {
  std::atomic<size_t> frames_produced{0};
  std::atomic<bool> finished{false};
};

// Feeds a 440 Hz tone through a ring buffer, the way the Oboe input callback
// does on a device. Frames are pushed from a producer thread at a configurable
// pace; the pipeline's worker reads them through the AudioSource interface.
class ToneSource : public AudioSource {
 public:
  ToneSource(int32_t sample_rate, int32_t channels, size_t ring_frames,
             std::shared_ptr<ProducerStats> stats)
      : sample_rate_(sample_rate),
        channels_(channels),
        buffer_(ring_frames * static_cast<size_t>(channels)),
        stats_(std::move(stats)) {}

  ~ToneSource() override { StopProducer(); }

  bool Start() override { return true; }

  bool Stop() override {
    StopProducer();
    return true;
  }

  int32_t GetSampleRate() const override { return sample_rate_; }
  int32_t GetChannelCount() const override { return channels_; }

  size_t GetAvailableFrames() const override {
    return buffer_.Available() / static_cast<size_t>(channels_);
  }

  size_t ReadData(float* data, size_t frame_count) override {
    return buffer_.Read(data, frame_count * static_cast<size_t>(channels_)) /
           static_cast<size_t>(channels_);
  }

  /**
   * Push `frames_per_tick` frames every `tick` from a producer thread until
   * `total_frames` have been pushed (0 = until Stop()). Waits for ring space
   * instead of dropping, so a frame count mismatch in a test means the
   * pipeline lost data, not the source.
   */
  void StartProducer(size_t frames_per_tick, std::chrono::milliseconds tick, size_t total_frames) {
    producer_running_.store(true);
    producer_ = std::thread([this, frames_per_tick, tick, total_frames]() {
      std::vector<float> chunk;
      while (producer_running_.load()) {
        if (total_frames > 0 && stats_->frames_produced.load() >= total_frames) {
          break;
        }
        size_t frames = frames_per_tick;
        if (total_frames > 0) {
          frames = std::min(frames, total_frames - stats_->frames_produced.load());
        }
        PushBlocking(frames, chunk);
        std::this_thread::sleep_for(tick);
      }
      stats_->finished.store(true);
    });
  }

 private:
  void PushBlocking(size_t frames, std::vector<float>& chunk) {
    const size_t channels = static_cast<size_t>(channels_);
    chunk.resize(frames * channels);
    const int64_t first_frame = static_cast<int64_t>(stats_->frames_produced.load());
    for (size_t i = 0; i < frames; ++i) {
      const float value = ToneSample(first_frame + static_cast<int64_t>(i), sample_rate_);
      for (size_t c = 0; c < channels; ++c) {
        chunk[i * channels + c] = value;
      }
    }

    size_t samples_written = 0;
    const size_t samples_total = chunk.size();
    while (samples_written < samples_total && producer_running_.load()) {
      const size_t written =
          buffer_.Write(chunk.data() + samples_written, samples_total - samples_written);
      samples_written += written;
      if (written == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    stats_->frames_produced.fetch_add(samples_written / channels);
  }

  void StopProducer() {
    producer_running_.store(false);
    if (producer_.joinable()) {
      producer_.join();
    }
  }

  int32_t sample_rate_;
  int32_t channels_;
  core::CircularBuffer buffer_;
  std::shared_ptr<ProducerStats> stats_;
  std::atomic<bool> producer_running_{false};
  std::thread producer_;
};

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

void WaitFor(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
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
