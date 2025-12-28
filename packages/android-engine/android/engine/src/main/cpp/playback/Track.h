#pragma once

#include "audio/AudioDecoder.h"
#include "core/CircularBuffer.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <condition_variable>
#include <mutex>

namespace sezo {
namespace playback {

/**
 * Represents a single audio track with its own controls and buffer.
 */
class Track {
 public:
  /**
   * Constructor.
   * @param id Unique track identifier
   * @param file_path Path to the audio file
   */
  Track(const std::string& id, const std::string& file_path);
  ~Track();

  /**
   * Load the track (open file and start buffering).
   * @return true if successful
   */
  bool Load();

  /**
   * Unload the track and release resources.
   */
  void Unload();

  /**
   * Read audio samples from the track buffer.
   * @param output Output buffer
   * @param frames Number of frames to read
   * @return Number of frames actually read
   */
  size_t ReadSamples(float* output, size_t frames);

  /**
   * Seek to a specific position.
   * @param frame Frame position
   * @return true if successful
   */
  bool Seek(int64_t frame);

  // Getters
  const std::string& GetId() const { return id_; }
  bool IsLoaded() const { return is_loaded_; }
  int64_t GetDuration() const;
  int32_t GetSampleRate() const;
  int32_t GetChannels() const;

  // Per-track controls
  void SetVolume(float volume);
  float GetVolume() const;
  void SetMuted(bool muted);
  bool IsMuted() const;
  void SetSolo(bool solo);
  bool IsSolo() const;
  void SetPan(float pan);
  float GetPan() const;

 private:
  void StreamingThreadFunc();

  std::string id_;
  std::string file_path_;
  std::unique_ptr<audio::AudioDecoder> decoder_;
  std::unique_ptr<core::CircularBuffer> buffer_;
  bool is_loaded_ = false;

  // Streaming thread
  std::unique_ptr<std::thread> streaming_thread_;
  std::atomic<bool> streaming_active_{false};
  std::mutex streaming_mutex_;
  std::condition_variable streaming_cv_;

  // Per-track controls (atomic for thread safety)
  std::atomic<float> volume_{1.0f};
  std::atomic<bool> muted_{false};
  std::atomic<bool> solo_{false};
  std::atomic<float> pan_{0.0f};
};

}  // namespace playback
}  // namespace sezo
