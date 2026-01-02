#pragma once

#include "Track.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace sezo {
namespace playback {

/**
 * Mixes multiple audio tracks together.
 * Handles solo/mute logic and master volume.
 */
class MultiTrackMixer {
 public:
  MultiTrackMixer();
  ~MultiTrackMixer();

  /**
   * Add a track to the mixer.
   * @param track Track to add
   */
  void AddTrack(std::shared_ptr<Track> track);

  /**
   * Remove a track from the mixer.
   * @param track_id ID of track to remove
   * @return true if found and removed
   */
  bool RemoveTrack(const std::string& track_id);

  /**
   * Remove all tracks.
   */
  void ClearTracks();

  /**
   * Get a track by ID.
   * @param track_id Track ID
   * @return Track pointer or nullptr if not found
   */
  std::shared_ptr<Track> GetTrack(const std::string& track_id);

  /**
   * Get all tracks.
   * @return Vector of tracks
   */
  std::vector<std::shared_ptr<Track>> GetTracks();

  /**
   * Mix all tracks and write to output buffer.
   * @param output Output buffer (stereo interleaved)
   * @param frames Number of frames to render
   * @param timeline_start_sample Timeline position for the first frame
   */
  void Mix(float* output, size_t frames, int64_t timeline_start_sample);

  /**
   * Set master volume.
   * @param volume Volume level (0.0 to 2.0)
   */
  void SetMasterVolume(float volume);

  /**
   * Get master volume.
   * @return Current master volume
   */
  float GetMasterVolume() const;

 private:
  std::vector<std::shared_ptr<Track>> tracks_;
  std::mutex tracks_mutex_;
  std::atomic<float> master_volume_{1.0f};

  // Temporary mix buffer
  std::vector<float> mix_buffer_;
  std::vector<float> mono_buffer_;
};

}  // namespace playback
}  // namespace sezo
