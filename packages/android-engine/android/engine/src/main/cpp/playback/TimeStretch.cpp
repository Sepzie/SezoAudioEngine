#include "TimeStretch.h"
#include "signalsmith-stretch.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>

#define LOG_TAG "TimeStretch"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace playback {

TimeStretch::TimeStretch(int32_t sample_rate, int32_t channels)
    : sample_rate_(sample_rate), channels_(channels) {

  // Create the Signalsmith Stretch instance
  stretcher_ = std::make_unique<StretcherType>();

  // Configure with default preset
  // Using the "cheaper" preset for better real-time performance
  stretcher_->presetCheaper(channels, static_cast<float>(sample_rate), false);

  // Get latency values
  input_latency_ = stretcher_->inputLatency();
  output_latency_ = stretcher_->outputLatency();

  // Pre-allocate buffers (max expected frame size ~2048)
  const size_t max_frames = 4096;
  for (int c = 0; c < 2; ++c) {
    input_buffers_[c].resize(max_frames);
    output_buffers_[c].resize(max_frames);
  }
  temp_buffer_.resize(max_frames * channels);

  LOGI("TimeStretch initialized: %d Hz, %d channels, input latency: %d, output latency: %d",
       sample_rate, channels, input_latency_, output_latency_);
}

TimeStretch::~TimeStretch() {
  LOGI("TimeStretch destroyed");
}

void TimeStretch::SetPitchSemitones(float semitones) {
  // Clamp to reasonable range
  semitones = std::clamp(semitones, -12.0f, 12.0f);
  pitch_semitones_.store(semitones, std::memory_order_release);
}

void TimeStretch::SetStretchFactor(float factor) {
  // Clamp to reasonable range
  factor = std::clamp(factor, 0.5f, 2.0f);
  stretch_factor_.store(factor, std::memory_order_release);
}

float TimeStretch::GetPitchSemitones() const {
  return pitch_semitones_.load(std::memory_order_acquire);
}

float TimeStretch::GetStretchFactor() const {
  return stretch_factor_.load(std::memory_order_acquire);
}

bool TimeStretch::IsActive() const {
  const float pitch = pitch_semitones_.load(std::memory_order_acquire);
  const float stretch = stretch_factor_.load(std::memory_order_acquire);

  // Consider effects active if pitch is non-zero or stretch is not 1.0
  return std::abs(pitch) > 0.01f || std::abs(stretch - 1.0f) > 0.01f;
}

void TimeStretch::Process(const float* input, float* output, size_t frames) {
  // Quick path: if no effects are active, just copy input to output
  if (!IsActive()) {
    if (input != output) {
      std::copy(input, input + frames * channels_, output);
    }
    return;
  }

  // Load current parameters
  const float pitch = pitch_semitones_.load(std::memory_order_acquire);
  const float stretch = stretch_factor_.load(std::memory_order_acquire);

  // Update stretcher parameters if they changed
  if (std::abs(pitch - last_pitch_) > 0.001f) {
    stretcher_->setTransposeSemitones(pitch);
    last_pitch_ = pitch;
  }

  // Note: The time-stretch rate is controlled by the ratio of input/output samples
  // For now, we'll process with a 1:1 ratio and let the caller handle playback rate
  // In a more advanced implementation, we could use different input/output sizes

  // Ensure buffers are large enough
  for (int c = 0; c < channels_; ++c) {
    if (input_buffers_[c].size() < frames) {
      input_buffers_[c].resize(frames);
      output_buffers_[c].resize(frames);
    }
  }

  // De-interleave input samples into separate channel buffers
  if (channels_ == 2) {
    for (size_t i = 0; i < frames; ++i) {
      input_buffers_[0][i] = input[i * 2];      // Left
      input_buffers_[1][i] = input[i * 2 + 1];  // Right
    }
  } else {
    // Mono or other channel configs
    for (int c = 0; c < channels_; ++c) {
      for (size_t i = 0; i < frames; ++i) {
        input_buffers_[c][i] = input[i * channels_ + c];
      }
    }
  }

  // Create array of pointers for Signalsmith API
  float* input_ptrs[2] = {input_buffers_[0].data(), input_buffers_[1].data()};
  float* output_ptrs[2] = {output_buffers_[0].data(), output_buffers_[1].data()};

  // Process through Signalsmith Stretch
  // For real-time processing with time-stretch, we typically want:
  // - stretch < 1.0: fewer output samples than input (faster playback)
  // - stretch > 1.0: more output samples than input (slower playback)
  // However, for simplicity in real-time, we'll process 1:1 and handle rate elsewhere

  int input_samples = static_cast<int>(frames);
  int output_samples = static_cast<int>(frames / stretch);

  // Clamp output samples to avoid buffer overflow
  output_samples = std::min(output_samples, static_cast<int>(frames));

  stretcher_->process(input_ptrs, input_samples, output_ptrs, output_samples);

  // Re-interleave output samples
  if (channels_ == 2) {
    for (int i = 0; i < output_samples; ++i) {
      output[i * 2] = output_buffers_[0][i];      // Left
      output[i * 2 + 1] = output_buffers_[1][i];  // Right
    }

    // Fill remaining samples with silence if we produced fewer outputs
    for (size_t i = output_samples; i < frames; ++i) {
      output[i * 2] = 0.0f;
      output[i * 2 + 1] = 0.0f;
    }
  } else {
    // Mono or other channel configs
    for (int c = 0; c < channels_; ++c) {
      for (int i = 0; i < output_samples; ++i) {
        output[i * channels_ + c] = output_buffers_[c][i];
      }
    }

    // Fill remaining with silence
    for (size_t i = output_samples; i < frames; ++i) {
      for (int c = 0; c < channels_; ++c) {
        output[i * channels_ + c] = 0.0f;
      }
    }
  }
}

void TimeStretch::Reset() {
  stretcher_->reset();
  last_pitch_ = 0.0f;
  last_stretch_ = 1.0f;

  LOGI("TimeStretch reset");
}

}  // namespace playback
}  // namespace sezo
