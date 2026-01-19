#pragma once

#include "MicrophoneCapture.h"
#include "audio/AudioEncoder.h"
#include "audio/AACEncoder.h"
#include "audio/M4AEncoder.h"
#include "audio/MP3Encoder.h"
#include "audio/WAVEncoder.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
  int64_t duration_samples = 0;
  int64_t start_time_samples = 0;
  double start_time_ms = 0.0;
  int64_t file_size = 0;
  std::string error_message;
};

/**
 * Recording pipeline that captures audio and encodes to file.
 */
class RecordingPipeline {
 public:
  RecordingPipeline();
  ~RecordingPipeline();

  using RecordingCallback = std::function<void(const RecordingResult&)>;

  /**
   * Start recording audio.
   * @param output_path Path to output file
   * @param config Recording configuration
   * @param callback Completion callback (called when recording stops)
   * @return true if recording started successfully
   */
  bool StartRecording(
      const std::string& output_path,
      const RecordingConfig& config,
      RecordingCallback callback = nullptr);

  /**
   * Stop recording and save to file.
   * @return Recording result
   */
  RecordingResult StopRecording();

  /**
   * Check if currently recording.
   * @return true if recording
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
   * Get duration of current recording in samples.
   * @return Sample count
   */
  int64_t GetRecordedSamples() const;

 private:
  void RecordingWorkerLoop();
  void StopWorker();
  RecordingResult EncodeRecording();

  std::unique_ptr<MicrophoneCapture> microphone_;
  std::unique_ptr<audio::AudioEncoder> encoder_;

  std::string output_path_;
  RecordingConfig config_;
  RecordingCallback callback_;

  std::atomic<bool> is_recording_{false};
  std::atomic<int64_t> recorded_samples_{0};

  // Worker thread for reading from microphone and encoding
  std::thread worker_thread_;
  std::atomic<bool> worker_running_{false};
  std::atomic<bool> worker_shutdown_{false};
  std::mutex worker_mutex_;
  std::condition_variable worker_cv_;

  // Temporary buffer for recorded audio
  std::vector<float> recording_buffer_;
};

}  // namespace recording
}  // namespace sezo
