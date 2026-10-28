#pragma once

#include "AudioSource.h"
#include "audio/AudioEncoder.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace sezo {
namespace recording {

/**
 * Configuration for audio recording.
 */
struct RecordingConfig {
  int32_t sample_rate = 44100;
  int32_t channels = 1;  // 1 = mono, 2 = stereo
  std::string format = "aac";  // "aac", "m4a", "mp3", "wav"
  int32_t bitrate = 128000;  // For compressed formats
  int32_t bits_per_sample = 16;  // For WAV
  bool enable_noise_gate = false;
  bool enable_normalization = false;
};

/**
 * Result of a recording operation.
 */
struct RecordingResult {
  bool success = false;
  std::string output_path;
  int64_t duration_samples = 0;  // Frames written to the file
  int32_t sample_rate = 0;       // Actual capture sample rate
  int32_t channels = 0;          // Actual capture channel count
  int64_t start_time_samples = 0;
  double start_time_ms = 0.0;
  int64_t file_size = 0;
  std::string error_message;
};

/**
 * Captures audio from an AudioSource and encodes it to a file.
 *
 * Encoding runs concurrently with capture on a worker thread: every chunk read
 * from the source is written to the encoder immediately, so memory stays
 * bounded by the source's buffer and StopRecording() only has to drain what is
 * left in that buffer and finalise the container. Its cost therefore does not
 * depend on the length of the take.
 */
class RecordingPipeline {
 public:
  RecordingPipeline();
  ~RecordingPipeline();

  using RecordingCallback = std::function<void(const RecordingResult&)>;

  /**
   * Start recording from the device microphone (Android only).
   * @param output_path Path to output file
   * @param config Recording configuration; sample rate and channels are
   *        replaced by what the input stream actually provides
   * @param callback Completion callback (called when recording stops)
   * @return true if recording started successfully
   */
  bool StartRecording(
      const std::string& output_path,
      const RecordingConfig& config,
      RecordingCallback callback = nullptr);

  /**
   * Start recording from an arbitrary source. The pipeline takes ownership of
   * the source and starts it; the source's sample rate and channel count
   * override the ones in config.
   */
  bool StartRecording(
      std::unique_ptr<AudioSource> source,
      const std::string& output_path,
      const RecordingConfig& config,
      RecordingCallback callback = nullptr);

  /**
   * Stop recording and finalise the file.
   * Returns once the file is complete and playable.
   */
  RecordingResult StopRecording();

  /**
   * Check if currently recording.
   */
  bool IsRecording() const;

  /**
   * Get current input level.
   * @return Peak amplitude (0.0 to 1.0)
   */
  float GetInputLevel() const;

  /**
   * Set recording volume/gain.
   * @param volume Volume multiplier (0.0 to 2.0)
   */
  void SetVolume(float volume);

  /**
   * Frames written to the file so far.
   */
  int64_t GetRecordedSamples() const;

 private:
  bool OpenEncoder(const std::string& output_path);
  void RecordingWorkerLoop();
  void StopWorker();

  std::unique_ptr<AudioSource> source_;
  std::unique_ptr<audio::AudioEncoder> encoder_;

  std::string output_path_;
  RecordingConfig config_;
  RecordingCallback callback_;

  std::atomic<bool> is_recording_{false};
  std::atomic<int64_t> recorded_samples_{0};
  std::atomic<bool> encode_failed_{false};

  // Worker thread that reads from the source and feeds the encoder
  std::thread worker_thread_;
  std::atomic<bool> worker_shutdown_{false};
};

}  // namespace recording
}  // namespace sezo
