#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace sezo {
namespace analysis {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Lags are capped at half the window so the difference function always
// integrates over at least half of it.
constexpr size_t kMaxLag = kAnalysisWindowSize / 2;
// Smallest lag searched: a period of two samples is the Nyquist limit.
constexpr size_t kMinLag = 2;
// Range the config is clamped to (lag 512 at 16 kHz is 31.25 Hz; lag 4 is 4 kHz).
constexpr float kLowestMinHz = 32.0f;
constexpr float kHighestMaxHz = 4000.0f;

float SilenceThresholdLinear() {
  return std::pow(10.0f, kSilenceThresholdDbfs / 20.0f);
}

}  // namespace

bool NormalizeInputMonitorConfig(InputMonitorConfig* config) {
  if (!config) {
    return false;
  }
  if (config->interval_ms < kMinIntervalMs) {
    config->interval_ms = kMinIntervalMs;
  }
  // Written as positive range checks (false for NaN without -ffast-math); the
  // engine is built with -ffast-math, so callers also reject NaN before this.
  const bool min_ok = config->min_hz > 0.0f && config->min_hz < 1.0e6f;
  const bool max_ok = config->max_hz > 0.0f && config->max_hz < 1.0e6f;
  if (!min_ok || !max_ok) {
    return false;
  }
  config->min_hz = std::max(config->min_hz, kLowestMinHz);
  config->max_hz = std::min(config->max_hz, kHighestMaxHz);
  return config->min_hz < config->max_hz;
}

// ---------------------------------------------------------------------------
// PitchDetector

PitchDetector::PitchDetector(float min_hz, float max_hz)
    : min_hz_(min_hz), max_hz_(max_hz) {
  const float rate = static_cast<float>(kAnalysisSampleRate);
  // Shortest period searched (highest frequency).
  tau_min_ = std::max(kMinLag, static_cast<size_t>(std::floor(rate / max_hz_)));
  // Longest period searched, plus one lag so the minimum can be interpolated.
  tau_max_ = std::min(kMaxLag, static_cast<size_t>(std::ceil(rate / min_hz_)) + 1);
  tau_min_ = std::min(tau_min_, tau_max_ - 1);
  integration_size_ = kAnalysisWindowSize - tau_max_;
  difference_.assign(tau_max_ + 1, 0.0f);
  cmnd_.assign(tau_max_ + 1, 1.0f);
}

PitchEstimate PitchDetector::Analyze(const float* window) {
  PitchEstimate result;

  // 1. Level of the whole window; quiet windows report no pitch.
  double energy = 0.0;
  for (size_t i = 0; i < kAnalysisWindowSize; ++i) {
    energy += static_cast<double>(window[i]) * static_cast<double>(window[i]);
  }
  result.level = static_cast<float>(std::sqrt(energy / static_cast<double>(kAnalysisWindowSize)));
  if (result.level < SilenceThresholdLinear()) {
    return result;
  }

  // 2. Difference function d(tau).
  for (size_t tau = 1; tau <= tau_max_; ++tau) {
    float sum = 0.0f;
    for (size_t j = 0; j < integration_size_; ++j) {
      const float delta = window[j] - window[j + tau];
      sum += delta * delta;
    }
    difference_[tau] = sum;
  }

  // 3. Cumulative mean normalised difference d'(tau).
  cmnd_[0] = 1.0f;
  float running = 0.0f;
  for (size_t tau = 1; tau <= tau_max_; ++tau) {
    running += difference_[tau];
    cmnd_[tau] = running > 0.0f
        ? difference_[tau] * static_cast<float>(tau) / running
        : 1.0f;
  }

  // 4. Absolute threshold: first dip under the threshold, then down to its minimum.
  size_t tau = 0;
  for (size_t candidate = tau_min_; candidate < tau_max_; ++candidate) {
    if (cmnd_[candidate] < kYinThreshold) {
      while (candidate + 1 < tau_max_ && cmnd_[candidate + 1] < cmnd_[candidate]) {
        ++candidate;
      }
      tau = candidate;
      break;
    }
  }
  if (tau == 0) {
    return result;
  }

  // 5. Parabolic interpolation of the minimum.
  float refined = static_cast<float>(tau);
  const float s0 = cmnd_[tau - 1];
  const float s1 = cmnd_[tau];
  const float s2 = cmnd_[tau + 1];
  const float denominator = s0 + s2 - 2.0f * s1;
  if (std::fabs(denominator) > 1e-12f) {
    const float shift = 0.5f * (s0 - s2) / denominator;
    if (std::fabs(shift) < 1.0f) {
      refined += shift;
    }
  }

  // 6. Frequency and confidence.
  const float hz = static_cast<float>(kAnalysisSampleRate) / refined;
  if (hz < min_hz_ || hz > max_hz_) {
    return result;
  }
  result.hz = hz;
  result.confidence = std::clamp(1.0f - cmnd_[tau], 0.0f, 1.0f);
  return result;
}

// ---------------------------------------------------------------------------
// Decimator

Decimator::Decimator(int32_t input_rate)
    : input_rate_(std::max<int32_t>(1, input_rate)),
      filter_enabled_(input_rate > kAnalysisSampleRate) {
  if (!filter_enabled_) {
    return;
  }
  // Windowed-sinc low-pass (Blackman), unity gain at DC.
  const double cutoff = static_cast<double>(kDecimatorCutoffHz) / static_cast<double>(input_rate_);
  const double middle = static_cast<double>(kDecimatorTaps - 1) / 2.0;
  taps_.assign(kDecimatorTaps, 0.0f);
  double sum = 0.0;
  for (size_t n = 0; n < kDecimatorTaps; ++n) {
    const double x = static_cast<double>(n) - middle;
    const double sinc = x == 0.0
        ? 2.0 * cutoff
        : std::sin(2.0 * kPi * cutoff * x) / (kPi * x);
    const double phase = 2.0 * kPi * static_cast<double>(n) / static_cast<double>(kDecimatorTaps - 1);
    const double blackman = 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
    const double tap = sinc * blackman;
    taps_[n] = static_cast<float>(tap);
    sum += tap;
  }
  for (size_t n = 0; n < kDecimatorTaps; ++n) {
    taps_[n] = static_cast<float>(static_cast<double>(taps_[n]) / sum);
  }
  history_.assign(kDecimatorTaps * 2, 0.0f);
}

size_t Decimator::MaxOutputFor(size_t count) const {
  return count * static_cast<size_t>(kAnalysisSampleRate) / static_cast<size_t>(input_rate_) + 2;
}

float Decimator::Filter(float sample) {
  history_[history_pos_] = sample;
  history_[history_pos_ + kDecimatorTaps] = sample;
  history_pos_ = (history_pos_ + 1) % kDecimatorTaps;
  // history_[history_pos_ ... + kDecimatorTaps - 1] is oldest -> newest.
  const float* line = history_.data() + history_pos_;
  float acc = 0.0f;
  for (size_t i = 0; i < kDecimatorTaps; ++i) {
    acc += taps_[i] * line[i];
  }
  return acc;
}

size_t Decimator::Process(const float* in, size_t count, float* out) {
  size_t written = 0;
  const int64_t rate = static_cast<int64_t>(input_rate_);
  const int64_t target = static_cast<int64_t>(kAnalysisSampleRate);
  for (size_t i = 0; i < count; ++i) {
    const float current = filter_enabled_ ? Filter(in[i]) : in[i];
    const int64_t n = input_count_;
    if (n >= 1) {
      // Emit every output instant that lies in [n - 1, n).
      while (true) {
        const int64_t position = output_count_ * rate;
        const int64_t index = position / target;
        if (index > n - 1) {
          break;
        }
        const float fraction =
            static_cast<float>(position - index * target) / static_cast<float>(target);
        out[written++] = previous_ + fraction * (current - previous_);
        ++output_count_;
      }
    }
    previous_ = current;
    ++input_count_;
  }
  return written;
}

// ---------------------------------------------------------------------------
// InputPitchAnalyzer

InputPitchAnalyzer::InputPitchAnalyzer(
    int32_t input_rate, int32_t channels, const InputMonitorConfig& config)
    : input_rate_(std::max<int32_t>(1, input_rate)),
      channels_(std::max<int32_t>(1, channels)),
      hop_size_(static_cast<size_t>(std::max(config.interval_ms, kMinIntervalMs)) *
                static_cast<size_t>(kAnalysisSampleRate) / 1000),
      decimator_(input_rate_),
      detector_(config.min_hz, config.max_hz) {
  mono_.assign(kChunkFrames, 0.0f);
  decimated_.assign(decimator_.MaxOutputFor(kChunkFrames), 0.0f);
  ring_.assign(kAnalysisWindowSize, 0.0f);
  window_.assign(kAnalysisWindowSize, 0.0f);
}

void InputPitchAnalyzer::Process(
    const float* interleaved, size_t frames, const WindowCallback& on_window) {
  const size_t channels = static_cast<size_t>(channels_);
  const float scale = 1.0f / static_cast<float>(channels_);
  size_t offset = 0;
  while (offset < frames) {
    const size_t chunk = std::min(kChunkFrames, frames - offset);
    const float* source = interleaved + offset * channels;
    // Mono mix.
    for (size_t i = 0; i < chunk; ++i) {
      float sum = 0.0f;
      for (size_t c = 0; c < channels; ++c) {
        sum += source[i * channels + c];
      }
      mono_[i] = sum * scale;
    }
    // Low-pass + resample to 16 kHz.
    const size_t produced = decimator_.Process(mono_.data(), chunk, decimated_.data());
    for (size_t i = 0; i < produced; ++i) {
      Push(decimated_[i], on_window);
    }
    offset += chunk;
  }
}

void InputPitchAnalyzer::Push(float sample, const WindowCallback& on_window) {
  ring_[ring_pos_] = sample;
  ring_pos_ = (ring_pos_ + 1) % kAnalysisWindowSize;
  ++decimated_count_;

  bool due = false;
  if (!first_window_done_) {
    due = decimated_count_ >= static_cast<int64_t>(kAnalysisWindowSize);
  } else {
    ++since_last_window_;
    due = since_last_window_ >= hop_size_;
  }
  if (!due) {
    return;
  }
  first_window_done_ = true;
  since_last_window_ = 0;

  // ring_[ring_pos_] is the oldest sample.
  const size_t tail = kAnalysisWindowSize - ring_pos_;
  std::copy(ring_.begin() + static_cast<std::ptrdiff_t>(ring_pos_), ring_.end(), window_.begin());
  std::copy(ring_.begin(), ring_.begin() + static_cast<std::ptrdiff_t>(ring_pos_),
            window_.begin() + static_cast<std::ptrdiff_t>(tail));

  const PitchEstimate estimate = detector_.Analyze(window_.data());
  const int64_t window_end_frame =
      decimated_count_ * static_cast<int64_t>(input_rate_) / static_cast<int64_t>(kAnalysisSampleRate);
  if (on_window) {
    on_window(estimate, window_end_frame);
  }
}

}  // namespace analysis
}  // namespace sezo
