#pragma once

// Fundamental frequency detection for the input pitch monitor.
//
// YIN (de Cheveigne & Kawahara 2002): difference function, cumulative mean
// normalised difference d'(tau), absolute threshold, parabolic interpolation of
// the minimum, confidence 1 - d'(tau). The input is mixed to mono and resampled
// to 16 kHz (windowed-sinc low-pass, then linear interpolation at the output
// instants); each analysis window is the newest 1024 samples (64 ms), and a new
// window is analysed every `interval_ms`.
//
// The iOS module carries a Swift port of this file
// (packages/expo-module/ios/AudioEngine/InputPitchAnalyzer.swift). Keep the two
// line-for-line comparable: same constants, same steps, same order.
//
// Nothing here allocates after construction, and nothing here is meant to run
// on a real-time audio thread: the monitor calls it from a worker thread.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace sezo {
namespace analysis {

/** Sample rate the detector runs at. */
constexpr int32_t kAnalysisSampleRate = 16000;
/** Analysis window, samples at kAnalysisSampleRate (64 ms). */
constexpr size_t kAnalysisWindowSize = 1024;
/** YIN absolute threshold on d'(tau). */
constexpr float kYinThreshold = 0.15f;
/** Windows whose RMS is below this level (dBFS) report no pitch. */
constexpr float kSilenceThresholdDbfs = -50.0f;
/** Low-pass cutoff applied before resampling to 16 kHz, Hz. */
constexpr float kDecimatorCutoffHz = 7000.0f;
/** Low-pass length, taps (odd, so the filter has an integer delay). */
constexpr size_t kDecimatorTaps = 63;

struct InputMonitorConfig {
  /** Lowest fundamental reported, Hz. */
  float min_hz = 50.0f;
  /** Highest fundamental reported, Hz. */
  float max_hz = 1500.0f;
  /** Target interval between analysed windows, ms (minimum 10). */
  int32_t interval_ms = 25;
};

/** Default / minimum values from the public API contract. */
constexpr float kDefaultMinHz = 50.0f;
constexpr float kDefaultMaxHz = 1500.0f;
constexpr int32_t kDefaultIntervalMs = 25;
constexpr int32_t kMinIntervalMs = 10;

/**
 * Clamps a config to what the detector supports: interval >= 10 ms,
 * 0 < min_hz < max_hz < 8 kHz. Returns false when the values cannot form a
 * usable range (min_hz >= max_hz, non-positive or non-finite values).
 */
bool NormalizeInputMonitorConfig(InputMonitorConfig* config);

struct PitchEstimate {
  /** Fundamental, Hz; 0 when no pitch was found in the window. */
  float hz = 0.0f;
  /** 1 - d'(tau) at the chosen lag, 0..1; 0 when hz is 0. */
  float confidence = 0.0f;
  /** RMS of the analysed window, linear. */
  float level = 0.0f;
};

/** YIN over one window of kAnalysisWindowSize samples at kAnalysisSampleRate. */
class PitchDetector {
 public:
  PitchDetector(float min_hz, float max_hz);

  PitchEstimate Analyze(const float* window);

  size_t GetMinLag() const { return tau_min_; }
  size_t GetMaxLag() const { return tau_max_; }

 private:
  float min_hz_;
  float max_hz_;
  size_t tau_min_;
  size_t tau_max_;
  size_t integration_size_;
  std::vector<float> difference_;
  std::vector<float> cmnd_;
};

/**
 * Mono stream at any input rate -> kAnalysisSampleRate. Output sample k sits at
 * input position k * input_rate / 16000 (exact integer arithmetic, so it does
 * not drift over long sessions).
 */
class Decimator {
 public:
  explicit Decimator(int32_t input_rate);

  /**
   * Consumes `count` mono samples and writes the resampled ones to `out`.
   * `out` must hold at least MaxOutputFor(count) samples.
   * @return Number of samples written.
   */
  size_t Process(const float* in, size_t count, float* out);

  /** Upper bound of output samples produced from `count` input samples. */
  size_t MaxOutputFor(size_t count) const;

  /** Output samples produced so far. */
  int64_t GetOutputCount() const { return output_count_; }

 private:
  float Filter(float sample);

  int32_t input_rate_;
  bool filter_enabled_;
  std::vector<float> taps_;
  // Delay line stored twice so the dot product never wraps.
  std::vector<float> history_;
  size_t history_pos_ = 0;
  int64_t input_count_ = 0;
  int64_t output_count_ = 0;
  float previous_ = 0.0f;
};

/**
 * Frames the stream into hop-spaced windows and runs the detector on each.
 * Feed it interleaved input as it arrives; it calls `on_window` once per
 * analysed window, in order, with the input frame index (counted from the
 * first frame fed) at the end of that window.
 */
class InputPitchAnalyzer {
 public:
  using WindowCallback = std::function<void(const PitchEstimate&, int64_t window_end_frame)>;

  /** Interleaved frames converted per internal step. */
  static constexpr size_t kChunkFrames = 512;

  InputPitchAnalyzer(int32_t input_rate, int32_t channels, const InputMonitorConfig& config);

  void Process(const float* interleaved, size_t frames, const WindowCallback& on_window);

  /** Decimated samples between two windows. */
  size_t GetHopSize() const { return hop_size_; }

  int32_t GetInputRate() const { return input_rate_; }
  int32_t GetChannels() const { return channels_; }

 private:
  void Push(float sample, const WindowCallback& on_window);

  int32_t input_rate_;
  int32_t channels_;
  size_t hop_size_;
  Decimator decimator_;
  PitchDetector detector_;
  std::vector<float> mono_;
  std::vector<float> decimated_;
  // Newest kAnalysisWindowSize decimated samples, circular.
  std::vector<float> ring_;
  size_t ring_pos_ = 0;
  std::vector<float> window_;
  int64_t decimated_count_ = 0;
  size_t since_last_window_ = 0;
  bool first_window_done_ = false;
};

}  // namespace analysis
}  // namespace sezo
