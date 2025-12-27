#pragma once

#include <atomic>
#include <cstdint>

namespace sezo {
namespace core {

/**
 * Master clock for sample-accurate timing across all tracks.
 * Thread-safe and lock-free for use in audio callback.
 */
class MasterClock {
 public:
  MasterClock();
  ~MasterClock() = default;

  /**
   * Reset the clock to zero.
   */
  void Reset();

  /**
   * Advance the clock by the given number of frames.
   * Called from audio callback.
   * @param frames Number of frames to advance
   */
  void Advance(int64_t frames);

  /**
   * Get the current position in frames.
   * @return Current position
   */
  int64_t GetPosition() const;

  /**
   * Set the position (for seeking).
   * @param position New position in frames
   */
  void SetPosition(int64_t position);

 private:
  std::atomic<int64_t> position_{0};
};

}  // namespace core
}  // namespace sezo
