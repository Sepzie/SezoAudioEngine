#pragma once

#include <atomic>
#include <cstdint>

namespace sezo {
namespace core {

/**
 * Manages timing information and position tracking.
 * Converts between samples, milliseconds, and provides duration info.
 */
class TimingManager {
 public:
  /**
   * Constructor.
   * @param sample_rate Sample rate in Hz
   */
  explicit TimingManager(int32_t sample_rate);
  ~TimingManager() = default;

  /**
   * Set the total duration in samples.
   * @param duration_samples Total duration
   */
  void SetDuration(int64_t duration_samples);

  /**
   * Get the total duration in samples.
   * @return Duration in samples
   */
  int64_t GetDurationSamples() const;

  /**
   * Get the total duration in milliseconds.
   * @return Duration in milliseconds
   */
  double GetDurationMs() const;

  /**
   * Convert samples to milliseconds.
   * @param samples Number of samples
   * @return Time in milliseconds
   */
  double SamplesToMs(int64_t samples) const;

  /**
   * Convert milliseconds to samples.
   * @param ms Time in milliseconds
   * @return Number of samples
   */
  int64_t MsToSamples(double ms) const;

  /**
   * Get the sample rate.
   * @return Sample rate in Hz
   */
  int32_t GetSampleRate() const { return sample_rate_; }

 private:
  int32_t sample_rate_;
  std::atomic<int64_t> duration_samples_{0};
};

}  // namespace core
}  // namespace sezo
