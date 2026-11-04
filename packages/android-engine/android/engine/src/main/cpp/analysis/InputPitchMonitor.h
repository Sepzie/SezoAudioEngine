#pragma once

#include "PitchDetector.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace sezo {
namespace analysis {

struct InputPitchEvent {
  float hz = 0.0f;
  float confidence = 0.0f;
  float level = 0.0f;
  /** Transport position (ms) when the window was analysed; -1 without a position. */
  double position_ms = -1.0;
  /** Wall clock, ms since the epoch, when the window was analysed. */
  int64_t timestamp_ms = 0;
  /** Input frame (counted from the first frame read) at the end of the window. */
  int64_t window_end_frame = 0;
};

/**
 * Worker that drains captured input and turns it into InputPitchEvents.
 *
 * The capture side writes into its own ring buffer on the audio callback and
 * does nothing else; this class reads that ring through `read` on a worker
 * thread, runs InputPitchAnalyzer and calls `on_event` (on the worker thread)
 * once per analysed window. It is independent of Oboe so the host tests can
 * drive Pump() directly.
 */
class InputPitchMonitor {
 public:
  /** Reads up to `max_frames` interleaved frames into `dst`; returns frames read. */
  using ReadFn = std::function<size_t(float* dst, size_t max_frames)>;
  /** Transport position in ms, or -1 when the transport has none. */
  using PositionFn = std::function<double()>;
  using EventFn = std::function<void(const InputPitchEvent&)>;

  /** Frames read from the ring per Pump() iteration. */
  static constexpr size_t kReadFrames = 1024;

  InputPitchMonitor(int32_t sample_rate,
                    int32_t channels,
                    const InputMonitorConfig& config,
                    ReadFn read,
                    PositionFn position,
                    EventFn on_event);
  ~InputPitchMonitor();

  InputPitchMonitor(const InputPitchMonitor&) = delete;
  InputPitchMonitor& operator=(const InputPitchMonitor&) = delete;

  /** Starts the worker thread. */
  void Start();

  /** Stops and joins the worker thread. No event is delivered after it returns. */
  void Stop();

  bool IsRunning() const { return running_.load(std::memory_order_acquire); }

  /**
   * Drains everything currently readable and analyses it. The worker calls this
   * in a loop; tests call it directly.
   * @return Frames consumed.
   */
  size_t Pump();

  /** RMS of the most recent analysed window. */
  float GetLevel() const { return level_.load(std::memory_order_relaxed); }

  const InputMonitorConfig& GetConfig() const { return config_; }

 private:
  void WorkerLoop();

  InputMonitorConfig config_;
  int32_t channels_;
  InputPitchAnalyzer analyzer_;
  ReadFn read_;
  PositionFn position_;
  EventFn on_event_;
  std::vector<float> read_buffer_;
  InputPitchAnalyzer::WindowCallback window_callback_;

  std::atomic<float> level_{0.0f};
  std::atomic<bool> running_{false};
  std::atomic<bool> shutdown_{false};
  std::thread worker_;
};

}  // namespace analysis
}  // namespace sezo
