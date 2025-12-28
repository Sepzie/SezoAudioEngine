#pragma once

#include <memory>
#include <cstdint>
#include <atomic>
#include <vector>

// Forward declaration to avoid including the large Signalsmith header here
namespace signalsmith {
namespace stretch {
template<typename Sample, class RandomEngine>
struct SignalsmithStretch;
}
}

namespace sezo {
namespace playback {

/**
 * TimeStretch - Wrapper around Signalsmith Stretch library for real-time
 * pitch shifting and time stretching.
 *
 * This class provides a thread-safe interface to the Signalsmith Stretch
 * library, allowing for independent control of pitch and playback speed.
 *
 * Thread Safety:
 * - SetPitchSemitones() and SetStretchFactor() can be called from any thread
 * - Process() is designed to be called from the real-time audio thread
 * - Reset() should only be called when audio processing is stopped
 */
class TimeStretch {
 public:
  /**
   * Constructs a TimeStretch instance.
   *
   * @param sample_rate The audio sample rate (e.g., 44100, 48000)
   * @param channels Number of audio channels (typically 2 for stereo)
   */
  explicit TimeStretch(int32_t sample_rate, int32_t channels = 2);

  /**
   * Destructor - handles cleanup of Signalsmith instance
   */
  ~TimeStretch();

  // Non-copyable, non-movable (contains unique_ptr)
  TimeStretch(const TimeStretch&) = delete;
  TimeStretch& operator=(const TimeStretch&) = delete;
  TimeStretch(TimeStretch&&) = delete;
  TimeStretch& operator=(TimeStretch&&) = delete;

  /**
   * Sets the pitch shift amount in semitones.
   *
   * @param semitones Pitch shift amount (range: -12.0 to +12.0)
   *                  0 = no pitch shift
   *                  +12 = one octave up
   *                  -12 = one octave down
   *
   * Thread-safe: Can be called from any thread
   */
  void SetPitchSemitones(float semitones);

  /**
   * Sets the time-stretch factor (playback speed).
   *
   * @param factor Stretch factor (range: 0.5 to 2.0)
   *               1.0 = normal speed
   *               2.0 = 2x faster (half duration)
   *               0.5 = 2x slower (double duration)
   *
   * Thread-safe: Can be called from any thread
   */
  void SetStretchFactor(float factor);

  /**
   * Gets the current pitch shift in semitones.
   */
  float GetPitchSemitones() const;

  /**
   * Gets the current stretch factor.
   */
  float GetStretchFactor() const;

  /**
   * Processes audio through the time-stretch/pitch-shift algorithm.
   *
   * This method is designed to be called from the real-time audio thread.
   * It applies pitch shifting and time stretching based on current parameters.
   *
   * @param input Interleaved audio samples (e.g., [L, R, L, R, ...])
   * @param input_frames Number of input frames (samples per channel)
   * @param output Output buffer (can be the same as input for in-place processing)
   * @param output_frames Number of output frames (samples per channel)
   *
   * Note: For stereo, total samples = frames * 2
   *
   * Thread-safe: Should only be called from audio callback thread
   */
  void Process(const float* input, size_t input_frames, float* output, size_t output_frames);

  /**
   * Resets the internal state of the time-stretcher.
   *
   * Call this after seeking or when there's a discontinuity in the audio stream.
   * This clears internal buffers to prevent artifacts.
   *
   * NOT thread-safe: Only call when audio processing is paused/stopped
   */
  void Reset();

  /**
   * Checks if effects are currently active (pitch != 0 or speed != 1.0)
   */
  bool IsActive() const;

 private:
  [[maybe_unused]] int32_t sample_rate_;
  int32_t channels_;

  // Atomic parameters for thread-safe access
  std::atomic<float> pitch_semitones_{0.0f};
  std::atomic<float> stretch_factor_{1.0f};

  // Signalsmith Stretch instance (using unique_ptr to hide implementation)
  using StretcherType = signalsmith::stretch::SignalsmithStretch<float, void>;
  std::unique_ptr<StretcherType> stretcher_;

  // Internal buffers for processing
  std::vector<float> input_buffers_[2];   // Separate buffers per channel
  std::vector<float> output_buffers_[2];  // Separate buffers per channel
  std::vector<float> temp_buffer_;        // Temporary working buffer

  // Latency compensation
  int32_t input_latency_ = 0;
  int32_t output_latency_ = 0;

  // Last applied parameters (to detect changes)
  float last_pitch_ = 0.0f;
  float last_stretch_ = 1.0f;
};

}  // namespace playback
}  // namespace sezo
