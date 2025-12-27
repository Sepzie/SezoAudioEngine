#pragma once

#include <atomic>

namespace sezo {
namespace core {

/**
 * Playback state enumeration.
 */
enum class PlaybackState {
  kStopped,
  kPlaying,
  kPaused,
  kRecording
};

/**
 * Controls playback transport (play, pause, stop).
 * Thread-safe for use from both UI and audio threads.
 */
class TransportController {
 public:
  TransportController();
  ~TransportController() = default;

  /**
   * Start playback.
   */
  void Play();

  /**
   * Pause playback.
   */
  void Pause();

  /**
   * Stop playback and reset position.
   */
  void Stop();

  /**
   * Get current playback state.
   * @return Current state
   */
  PlaybackState GetState() const;

  /**
   * Check if currently playing.
   * @return true if playing or recording
   */
  bool IsPlaying() const;

 private:
  std::atomic<PlaybackState> state_{PlaybackState::kStopped};
};

}  // namespace core
}  // namespace sezo
