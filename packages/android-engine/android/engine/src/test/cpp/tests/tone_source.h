#pragma once

// Generated-audio AudioSource shared by the RecordingPipeline tests (host and
// Android). A producer thread pushes a 440 Hz tone into a ring buffer the way
// the Oboe input callback does on a device; the pipeline's worker drains it
// through the AudioSource interface.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "core/CircularBuffer.h"
#include "recording/AudioSource.h"

namespace sezo {
namespace test {

constexpr double kTonePi = 3.14159265358979323846;
constexpr double kToneHz = 440.0;
constexpr float kToneAmplitude = 0.5f;

inline float ToneSample(int64_t frame, int32_t sample_rate) {
  const double phase = 2.0 * kTonePi * kToneHz * static_cast<double>(frame) /
                       static_cast<double>(sample_rate);
  return kToneAmplitude * static_cast<float>(std::sin(phase));
}

// Counters a test keeps after the pipeline has destroyed the source (the
// pipeline owns its source and releases it in StopRecording()).
struct ProducerStats {
  std::atomic<size_t> frames_produced{0};
  std::atomic<bool> finished{false};
};

class ToneSource : public recording::AudioSource {
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

inline void WaitFor(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

}  // namespace test
}  // namespace sezo
