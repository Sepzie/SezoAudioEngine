// On-device tests for RecordingPipeline with the MediaCodec encoders (M4A and
// ADTS AAC). A generated tone is pushed far faster than real time through a
// one-second ring, so the take only reaches the file in full if the pipeline
// encodes while capturing. StopRecording() must return in bounded time and
// cost the same for a 10-second and a 5-minute take.

#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "audio/M4ADecoder.h"
#include "recording/RecordingPipeline.h"
#include "test_helpers.h"
#include "tone_source.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#endif

namespace sezo {
namespace recording {

#if defined(__ANDROID__)
namespace {

using test::ProducerStats;
using test::ToneSource;
using test::WaitFor;

constexpr int32_t kSampleRate = 48000;
constexpr int32_t kChannels = 1;
constexpr int32_t kBitrate = 128000;

// One second of ring. The producer offers a second of audio every 10 ms (100x
// real time) and blocks while the ring is full, so the encoder is the
// bottleneck for the whole take and the ring is full when stop is called.
constexpr size_t kRingFrames = static_cast<size_t>(kSampleRate);
constexpr size_t kFramesPerTick = static_cast<size_t>(kSampleRate);
constexpr auto kTick = std::chrono::milliseconds(10);

constexpr int64_t kStopBudgetMs = 500;

// AAC adds encoder priming at the start and pads the last 1024-frame packet.
constexpr int64_t kAacFrameTolerance = 8192;

// RMS of a sine with amplitude kToneAmplitude.
const double kExpectedRms = test::kToneAmplitude / std::sqrt(2.0);

// Frames skipped at the start of the decoded signal (priming silence and the
// encoder's ramp-in) before measuring level and pitch.
constexpr int64_t kEdgeFrames = 8192;

struct Take {
  RecordingResult result;
  int64_t frames_pushed = 0;
  int64_t backlog_frames_at_stop = 0;  // still in the ring when stop was called
  int64_t stop_ms = 0;
  double capture_wall_s = 0.0;  // start to last frame pushed
};

// Records `seconds` of tone through the pipeline in `format` and stops.
void RecordTake(const std::string& format, const std::string& path, int seconds, Take* take) {
  const size_t total_frames = static_cast<size_t>(kSampleRate) * static_cast<size_t>(seconds);

  auto stats = std::make_shared<ProducerStats>();
  auto source = std::make_unique<ToneSource>(kSampleRate, kChannels, kRingFrames, stats);
  ToneSource* source_ptr = source.get();  // valid until StopRecording()

  RecordingConfig config;
  config.sample_rate = 44100;  // the source's rate must win
  config.channels = 2;         // the source's channel count must win
  config.format = format;
  config.bitrate = kBitrate;

  RecordingPipeline pipeline;
  ASSERT_TRUE(pipeline.StartRecording(std::move(source), path, config));
  ASSERT_TRUE(pipeline.IsRecording());

  const auto capture_begin = std::chrono::steady_clock::now();
  source_ptr->StartProducer(kFramesPerTick, kTick, total_frames);
  // The encoder runs well above real time on any device that can record; the
  // timeout only guards against a hang.
  WaitFor([&]() { return stats->finished.load(); },
          std::chrono::milliseconds(static_cast<int64_t>(seconds) * 1000 + 120000));
  take->capture_wall_s = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - capture_begin)
                             .count();
  ASSERT_TRUE(stats->finished.load()) << "producer did not finish";
  ASSERT_EQ(stats->frames_produced.load(), total_frames);

  take->frames_pushed = static_cast<int64_t>(total_frames);
  take->backlog_frames_at_stop = take->frames_pushed - pipeline.GetRecordedSamples();

  const auto stop_begin = std::chrono::steady_clock::now();
  take->result = pipeline.StopRecording();
  take->stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - stop_begin)
                      .count();
  EXPECT_FALSE(pipeline.IsRecording());
}

struct Decoded {
  int32_t sample_rate = 0;
  int32_t channels = 0;
  int64_t container_frames = 0;  // from the container's duration
  int64_t frames = 0;            // actually decoded
  double rms = 0.0;              // after kEdgeFrames
  double tone_hz = 0.0;          // from upward zero crossings after kEdgeFrames
};

void DecodeFile(const std::string& path, Decoded* out) {
  audio::M4ADecoder decoder;
  ASSERT_TRUE(decoder.Open(path)) << "decoder could not open " << path;
  const auto& format = decoder.GetFormat();
  out->container_frames = format.total_frames;

  const size_t kChunkFrames = 4096;
  std::vector<float> chunk;
  double sum_squares = 0.0;
  int64_t measured_frames = 0;
  int64_t crossings = 0;
  float previous = 0.0f;
  bool have_previous = false;

  while (true) {
    // The decoder may report its real channel count only after the first
    // output buffer, so size the chunk on every read.
    const size_t channels = static_cast<size_t>(decoder.GetFormat().channels);
    chunk.resize(kChunkFrames * channels);
    const size_t frames = decoder.Read(chunk.data(), kChunkFrames);
    if (frames == 0) {
      break;
    }
    for (size_t i = 0; i < frames; ++i) {
      const int64_t frame = out->frames + static_cast<int64_t>(i);
      if (frame < kEdgeFrames) {
        continue;
      }
      const float value = chunk[i * channels];
      sum_squares += static_cast<double>(value) * value;
      ++measured_frames;
      if (have_previous && previous < 0.0f && value >= 0.0f) {
        ++crossings;
      }
      previous = value;
      have_previous = true;
    }
    out->frames += static_cast<int64_t>(frames);
  }

  out->sample_rate = decoder.GetFormat().sample_rate;
  out->channels = decoder.GetFormat().channels;
  if (measured_frames > 0) {
    out->rms = std::sqrt(sum_squares / static_cast<double>(measured_frames));
    out->tone_hz = static_cast<double>(crossings) * out->sample_rate /
                   static_cast<double>(measured_frames);
  }
}

struct AdtsStats {
  bool well_formed = false;
  int64_t packets = 0;
  int64_t bytes_parsed = 0;
  int sample_rate_index = -1;
  int channel_config = -1;
};

// Walks the ADTS headers of a .aac file: every frame must start with a sync
// word and the frame lengths must add up to the file size exactly.
AdtsStats ParseAdts(const std::string& path) {
  AdtsStats stats;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) {
    return stats;
  }
  std::vector<uint8_t> data;
  uint8_t buffer[65536];
  size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    data.insert(data.end(), buffer, buffer + read);
  }
  std::fclose(file);

  size_t offset = 0;
  while (offset + 7 <= data.size()) {
    const uint8_t* h = data.data() + offset;
    if (h[0] != 0xFF || (h[1] & 0xF6) != 0xF0) {
      return stats;
    }
    const int sample_rate_index = (h[2] >> 2) & 0x0F;
    const int channel_config = ((h[2] & 0x01) << 2) | (h[3] >> 6);
    const size_t frame_length =
        (static_cast<size_t>(h[3] & 0x03) << 11) | (static_cast<size_t>(h[4]) << 3) | (h[5] >> 5);
    if (frame_length < 7) {
      return stats;
    }
    if (stats.packets == 0) {
      stats.sample_rate_index = sample_rate_index;
      stats.channel_config = channel_config;
    } else if (sample_rate_index != stats.sample_rate_index ||
               channel_config != stats.channel_config) {
      return stats;
    }
    stats.packets += (h[6] & 0x03) + 1;
    offset += frame_length;
  }
  stats.bytes_parsed = static_cast<int64_t>(offset);
  stats.well_formed = offset == data.size();
  return stats;
}

void ReportTake(const std::string& label, const Take& take) {
  const double audio_s = static_cast<double>(take.frames_pushed) / kSampleRate;
  std::cout << "[ METRIC   ] " << label << ": stop " << take.stop_ms << " ms with "
            << take.backlog_frames_at_stop << " frames (" << (take.backlog_frames_at_stop * 1000 / kSampleRate)
            << " ms) still in the ring, file " << take.result.file_size << " bytes, "
            << audio_s << " s of audio captured in " << take.capture_wall_s << " s ("
            << (audio_s / take.capture_wall_s) << "x real time)" << std::endl;
  ::testing::Test::RecordProperty("stop_ms", static_cast<int>(take.stop_ms));
  ::testing::Test::RecordProperty("backlog_frames_at_stop",
                                  static_cast<int>(take.backlog_frames_at_stop));
  ::testing::Test::RecordProperty("file_size", static_cast<int>(take.result.file_size));
}

void ExpectCommonResult(const Take& take, const std::string& path) {
  const RecordingResult& result = take.result;
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.output_path, path);
  EXPECT_LT(take.stop_ms, kStopBudgetMs);
  // Every frame the producer pushed went through the encoder.
  EXPECT_EQ(result.duration_samples, take.frames_pushed);
  EXPECT_EQ(result.sample_rate, kSampleRate);
  EXPECT_EQ(result.channels, kChannels);

  // A plausible size for the configured bitrate.
  const double seconds = static_cast<double>(take.frames_pushed) / kSampleRate;
  const double nominal_bytes = kBitrate * seconds / 8.0;
  EXPECT_GT(static_cast<double>(result.file_size), 0.5 * nominal_bytes);
  EXPECT_LT(static_cast<double>(result.file_size), 2.0 * nominal_bytes);
  EXPECT_EQ(static_cast<uintmax_t>(result.file_size), std::filesystem::file_size(path));
}

void ExpectToneDecoded(const Decoded& decoded, int64_t frames_pushed) {
  EXPECT_EQ(decoded.sample_rate, kSampleRate);
  EXPECT_EQ(decoded.channels, kChannels);
  EXPECT_NEAR(static_cast<double>(decoded.frames), static_cast<double>(frames_pushed),
              static_cast<double>(kAacFrameTolerance));
  EXPECT_NEAR(decoded.rms, kExpectedRms, 0.02) << "decoded audio is not the tone";
  EXPECT_NEAR(decoded.tone_hz, test::kToneHz, 1.0);
}

void RunM4aTake(int seconds) {
  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_stream_", ".m4a"));
  Take take;
  ASSERT_NO_FATAL_FAILURE(RecordTake("m4a", temp_file.path(), seconds, &take));
  ReportTake("m4a " + std::to_string(seconds) + " s", take);
  ASSERT_NO_FATAL_FAILURE(ExpectCommonResult(take, temp_file.path()));

  Decoded decoded;
  ASSERT_NO_FATAL_FAILURE(DecodeFile(temp_file.path(), &decoded));
  // The muxer wrote a finished container: its duration covers the take.
  EXPECT_NEAR(static_cast<double>(decoded.container_frames),
              static_cast<double>(take.frames_pushed),
              static_cast<double>(kAacFrameTolerance));
  ExpectToneDecoded(decoded, take.frames_pushed);
}

void RunAacTake(int seconds) {
  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_stream_", ".aac"));
  Take take;
  ASSERT_NO_FATAL_FAILURE(RecordTake("aac", temp_file.path(), seconds, &take));
  ReportTake("aac " + std::to_string(seconds) + " s", take);
  ASSERT_NO_FATAL_FAILURE(ExpectCommonResult(take, temp_file.path()));

  const AdtsStats adts = ParseAdts(temp_file.path());
  EXPECT_TRUE(adts.well_formed) << "ADTS stream broken at byte " << adts.bytes_parsed;
  EXPECT_EQ(adts.sample_rate_index, 3);  // 48 kHz
  EXPECT_EQ(adts.channel_config, kChannels);
  EXPECT_NEAR(static_cast<double>(adts.packets * 1024), static_cast<double>(take.frames_pushed),
              static_cast<double>(kAacFrameTolerance));

  Decoded decoded;
  ASSERT_NO_FATAL_FAILURE(DecodeFile(temp_file.path(), &decoded));
  ExpectToneDecoded(decoded, take.frames_pushed);
}

}  // namespace

TEST(RecordingPipelineMediaCodecTest, M4aTenSecondTake) {
  RunM4aTake(10);
}

TEST(RecordingPipelineMediaCodecTest, M4aFiveMinuteTake) {
  RunM4aTake(300);
}

TEST(RecordingPipelineMediaCodecTest, AacTenSecondTake) {
  RunAacTake(10);
}

TEST(RecordingPipelineMediaCodecTest, AacFiveMinuteTake) {
  RunAacTake(300);
}
#else
TEST(RecordingPipelineMediaCodecTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only MediaCodec recording tests.";
}
#endif

}  // namespace recording
}  // namespace sezo
