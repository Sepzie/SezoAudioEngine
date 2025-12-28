#pragma once

#include "core/ErrorCodes.h"
#include "core/MasterClock.h"
#include "core/TimingManager.h"
#include "core/TransportController.h"
#include "playback/MultiTrackMixer.h"
#include "playback/OboePlayer.h"
#include "playback/Track.h"

#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
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
  bool LoadTrack(const std::string& track_id, const std::string& file_path);
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

  // Phase 6: Extraction
  struct ExtractionOptions {
    std::string format = "wav";  // "wav", "aac", "mp3"
    int32_t bitrate = 128000;
    int32_t bits_per_sample = 16;
    bool include_effects = true;
  };

  using ExtractionProgressCallback = std::function<void(float progress)>;

  struct ExtractionResult {
    bool success = false;
    std::string track_id;
    std::string output_path;
    int64_t duration_samples = 0;
    int64_t file_size = 0;
    std::string error_message;
  };

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
      ExtractionProgressCallback progress_callback = nullptr);

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
      ExtractionProgressCallback progress_callback = nullptr);

 private:
  void ReportError(core::ErrorCode code, const std::string& message);

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

  // Track management
  std::map<std::string, std::shared_ptr<playback::Track>> tracks_;

  // Effects state (for Phase 2)
  float pitch_ = 0.0f;
  float speed_ = 1.0f;

  mutable std::mutex error_mutex_;
  ErrorCallback error_callback_;
  core::ErrorCode last_error_ = core::ErrorCode::kOk;
  std::string last_error_message_;
};

}  // namespace sezo
