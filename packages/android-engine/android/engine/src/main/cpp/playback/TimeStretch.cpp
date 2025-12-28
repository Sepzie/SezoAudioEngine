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
  // Prefer quality for debugging artifacting; split computation to reduce spikes.
  const float sample_rate_f = static_cast<float>(sample_rate);
  stretcher_->presetDefault(channels, sample_rate_f, true);
  tonality_limit_ = sample_rate_f > 0.0f ? (8000.0f / sample_rate_f) : 0.0f;

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

  LOGI("TimeStretch initialized: %d Hz, %d channels, input latency: %d, output latency: %d, block: %d, interval: %d, split: %d",
       sample_rate,
       channels,
       input_latency_,
       output_latency_,
       stretcher_->blockSamples(),
       stretcher_->intervalSamples(),
       stretcher_->splitComputation() ? 1 : 0);
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

void TimeStretch::Process(const float* input, size_t input_frames, float* output, size_t output_frames) {
  if (!input || !output || output_frames == 0) {
    return;
  }

  if (input_frames == 0) {
    std::fill_n(output, output_frames * channels_, 0.0f);
    return;
  }

  if (channels_ != 1 && channels_ != 2) {
    const size_t frames_to_copy = std::min(input_frames, output_frames);
    const size_t samples_to_copy = frames_to_copy * channels_;
    if (input != output && samples_to_copy > 0) {
      std::copy(input, input + samples_to_copy, output);
    }
    if (frames_to_copy < output_frames) {
      std::fill(output + samples_to_copy, output + output_frames * channels_, 0.0f);
    }
    return;
  }

  // Quick path: if no effects are active, just copy input to output
  if (!IsActive()) {
    const size_t frames_to_copy = std::min(input_frames, output_frames);
    const size_t samples_to_copy = frames_to_copy * channels_;
    if (input != output && samples_to_copy > 0) {
      std::copy(input, input + samples_to_copy, output);
    }
    if (frames_to_copy < output_frames) {
      std::fill(output + samples_to_copy, output + output_frames * channels_, 0.0f);
    }
    return;
  }

  // Load current parameters
  const float pitch = pitch_semitones_.load(std::memory_order_acquire);

  // Update stretcher parameters if they changed
  if (std::abs(pitch - last_pitch_) > 0.001f) {
    stretcher_->setTransposeSemitones(pitch, tonality_limit_);
    last_pitch_ = pitch;
  }

  // Ensure buffers are large enough
  for (int c = 0; c < channels_; ++c) {
    if (input_buffers_[c].size() < input_frames) {
      input_buffers_[c].resize(input_frames);
    }
    if (output_buffers_[c].size() < output_frames) {
      output_buffers_[c].resize(output_frames);
    }
  }

  // De-interleave input samples into separate channel buffers
  if (channels_ == 2) {
    for (size_t i = 0; i < input_frames; ++i) {
      input_buffers_[0][i] = input[i * 2];      // Left
      input_buffers_[1][i] = input[i * 2 + 1];  // Right
    }
  } else {
    // Mono or other channel configs
    for (int c = 0; c < channels_; ++c) {
      for (size_t i = 0; i < input_frames; ++i) {
        input_buffers_[c][i] = input[i * channels_ + c];
      }
    }
  }

  // Create array of pointers for Signalsmith API
  float* input_ptrs[2] = {input_buffers_[0].data(), input_buffers_[1].data()};
  float* output_ptrs[2] = {output_buffers_[0].data(), output_buffers_[1].data()};

  // Process through Signalsmith Stretch
  const int input_samples = static_cast<int>(input_frames);
  const int output_samples = static_cast<int>(output_frames);
  stretcher_->process(input_ptrs, input_samples, output_ptrs, output_samples);

  // Re-interleave output samples
  if (channels_ == 2) {
    for (int i = 0; i < output_samples; ++i) {
      output[i * 2] = output_buffers_[0][i];      // Left
      output[i * 2 + 1] = output_buffers_[1][i];  // Right
    }
  } else {
    // Mono or other channel configs
    for (int c = 0; c < channels_; ++c) {
      for (int i = 0; i < output_samples; ++i) {
        output[i * channels_ + c] = output_buffers_[c][i];
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
