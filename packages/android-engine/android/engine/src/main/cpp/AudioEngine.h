#pragma once

#include "core/MasterClock.h"
#include "core/TimingManager.h"
#include "core/TransportController.h"
#include "playback/MultiTrackMixer.h"
#include "playback/OboePlayer.h"
#include "playback/Track.h"

#include <map>
#include <memory>
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

  // TODO: Effects (Phase 2)
  void SetPitch(float semitones);
  float GetPitch() const;
  void SetSpeed(float rate);
  float GetSpeed() const;

 private:
  bool initialized_ = false;
  int32_t sample_rate_ = 44100;

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
};

}  // namespace sezo
