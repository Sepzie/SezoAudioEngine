#pragma once

#include "core/ErrorCodes.h"
#include "core/MasterClock.h"
#include "core/TimingManager.h"
#include "core/TransportController.h"
#include "playback/MultiTrackMixer.h"
#include "playback/OboePlayer.h"
#include "playback/Track.h"
#include "recording/RecordingPipeline.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sezo {

/**
 * Main audio engine class.
 * Coordinates all components and provides the high-level API.
 */
class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();

  /**
   * Initialize the audio engine.
   * @param sample_rate Desired sample rate (default: 44100)
   * @param max_tracks Maximum number of tracks (default: 8)
   * @return true if successful
   */
  bool Initialize(int32_t sample_rate = 44100, int32_t max_tracks = 8);

  /**
   * Release all resources.
   */
  void Release();

  using ErrorCallback = std::function<void(core::ErrorCode, const std::string&)>;

  void SetErrorCallback(ErrorCallback callback);
  core::ErrorCode GetLastErrorCode() const;
  std::string GetLastErrorMessage() const;

  // Track management
  bool LoadTrack(const std::string& track_id,
                 const std::string& file_path,
                 double start_time_ms = 0.0);
  bool UnloadTrack(const std::string& track_id);
  void UnloadAllTracks();
  std::vector<std::string> GetLoadedTrackIds() const;

  // Playback control
  void Play();
  void Pause();
  void Stop();
  void Seek(double position_ms);
  bool IsPlaying() const;
  double GetCurrentPosition() const;  // in milliseconds
  double GetDuration() const;         // in milliseconds

  // Track controls
  void SetTrackVolume(const std::string& track_id, float volume);
  void SetTrackMuted(const std::string& track_id, bool muted);
  void SetTrackSolo(const std::string& track_id, bool solo);
  void SetTrackPan(const std::string& track_id, float pan);

  // Master controls
  void SetMasterVolume(float volume);
  float GetMasterVolume() const;

  // Phase 2: Effects (per-track and master)
  void SetTrackPitch(const std::string& track_id, float semitones);
  float GetTrackPitch(const std::string& track_id) const;
  void SetTrackSpeed(const std::string& track_id, float rate);
  float GetTrackSpeed(const std::string& track_id) const;

  // Master effects (apply to all tracks)
  void SetPitch(float semitones);
  float GetPitch() const;
  void SetSpeed(float rate);
  float GetSpeed() const;

  // Phase 3: Recording
  using RecordingCompletionCallback = std::function<void(const recording::RecordingResult&)>;

  /**
   * Start recording audio from microphone.
   * @param output_path Path to output file
   * @param config Recording configuration
   * @param callback Completion callback (optional)
   * @return true if recording started successfully
   */
  bool StartRecording(
      const std::string& output_path,
      const recording::RecordingConfig& config,
      RecordingCompletionCallback callback = nullptr);

  /**
   * Stop recording and save to file.
   * @return Recording result
   */
  recording::RecordingResult StopRecording();

  /**
   * Check if currently recording.
   * @return true if recording
   */
  bool IsRecording() const;

  /**
   * Get current input level (for UI metering).
   * @return Peak amplitude (0.0 to 1.0)
   */
  float GetInputLevel() const;

  /**
   * Set recording volume/gain.
   * @param volume Volume multiplier (0.0 to 2.0)
   */
  void SetRecordingVolume(float volume);

  // Phase 6: Extraction
  struct ExtractionOptions {
    std::string format = "wav";  // "wav", "aac", "m4a", "mp3"
    int32_t bitrate = 128000;
    int32_t bits_per_sample = 16;
    bool include_effects = true;
  };

  struct ExtractionResult {
    bool success = false;
    std::string track_id;
    std::string output_path;
    int64_t duration_samples = 0;
    int64_t file_size = 0;
    std::string error_message;
  };

  using ExtractionProgressCallback = std::function<void(float progress)>;
  using ExtractionCompletionCallback =
      std::function<void(int64_t job_id, const ExtractionResult& result)>;

  /**
   * Extract a single track to an audio file with effects applied.
   * @param track_id ID of track to extract
   * @param output_path Path to output file
   * @param options Extraction options
   * @param progress_callback Optional progress callback
   * @return Extraction result
   */
  ExtractionResult ExtractTrack(
      const std::string& track_id,
      const std::string& output_path,
      const ExtractionOptions& options,
      ExtractionProgressCallback progress_callback = nullptr,
      std::atomic<bool>* cancel_flag = nullptr);

  /**
   * Extract all loaded tracks mixed together to an audio file.
   * @param output_path Path to output file
   * @param options Extraction options
   * @param progress_callback Optional progress callback
   * @return Extraction result
   */
  ExtractionResult ExtractAllTracks(
      const std::string& output_path,
      const ExtractionOptions& options,
      ExtractionProgressCallback progress_callback = nullptr,
      std::atomic<bool>* cancel_flag = nullptr);

  int64_t StartExtractTrack(
      const std::string& track_id,
      const std::string& output_path,
      const ExtractionOptions& options,
      ExtractionProgressCallback progress_callback,
      ExtractionCompletionCallback completion_callback);

  int64_t StartExtractAllTracks(
      const std::string& output_path,
      const ExtractionOptions& options,
      ExtractionProgressCallback progress_callback,
      ExtractionCompletionCallback completion_callback);

 bool CancelExtraction(int64_t job_id);
  void CancelAllExtractions();
  bool IsExtractionRunning() const;

 private:
  void RecalculateDuration();
  void ReportError(core::ErrorCode code, const std::string& message);
  void StartExtractionWorker();
  void StopExtractionWorker();
  void ExtractionWorkerLoop();
  int64_t NextExtractionJobId();

  struct ExtractionTask {
    int64_t job_id = 0;
    bool is_mix = false;
    std::string track_id;
    std::string output_path;
    ExtractionOptions options;
    ExtractionProgressCallback progress_callback;
    ExtractionCompletionCallback completion_callback;
    std::shared_ptr<std::atomic<bool>> cancel_flag;
  };

  bool initialized_ = false;
  int32_t sample_rate_ = 44100;
  int32_t max_tracks_ = 8;

  // Core components
  std::shared_ptr<core::MasterClock> clock_;
  std::shared_ptr<core::TimingManager> timing_;
  std::shared_ptr<core::TransportController> transport_;

  // Playback components
  std::shared_ptr<playback::MultiTrackMixer> mixer_;
  std::shared_ptr<playback::OboePlayer> player_;

  // Recording components
  std::unique_ptr<recording::RecordingPipeline> recording_pipeline_;
  std::atomic<int64_t> recording_start_samples_{0};

  // Track management
  std::map<std::string, std::shared_ptr<playback::Track>> tracks_;

  // Effects state (for Phase 2)
  float pitch_ = 0.0f;
  float speed_ = 1.0f;

  mutable std::mutex error_mutex_;
  ErrorCallback error_callback_;
  core::ErrorCode last_error_ = core::ErrorCode::kOk;
  std::string last_error_message_;

  std::thread extraction_thread_;
  std::atomic<bool> extraction_worker_running_{false};
  std::atomic<bool> extraction_shutdown_{false};
  mutable std::mutex extraction_mutex_;
  std::condition_variable extraction_cv_;
  std::deque<ExtractionTask> extraction_queue_;
  std::unordered_map<int64_t, std::shared_ptr<std::atomic<bool>>> extraction_cancel_flags_;
  int64_t next_extraction_job_id_ = 1;
  int64_t current_extraction_job_id_ = 0;
};

}  // namespace sezo
