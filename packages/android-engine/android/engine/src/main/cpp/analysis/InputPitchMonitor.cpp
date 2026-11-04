#include "InputPitchMonitor.h"

#include <chrono>
#include <utility>

namespace sezo {
namespace analysis {

namespace {

// Idle sleep while the ring is empty. Oboe input callbacks arrive every few
// milliseconds, so this bounds the extra event latency to about 5 ms.
constexpr auto kIdleSleep = std::chrono::milliseconds(5);

int64_t NowEpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

InputPitchMonitor::InputPitchMonitor(int32_t sample_rate,
                                     int32_t channels,
                                     const InputMonitorConfig& config,
                                     ReadFn read,
                                     PositionFn position,
                                     EventFn on_event)
    : config_(config),
      channels_(channels > 0 ? channels : 1),
      analyzer_(sample_rate, channels_, config),
      read_(std::move(read)),
      position_(std::move(position)),
      on_event_(std::move(on_event)) {
  read_buffer_.assign(kReadFrames * static_cast<size_t>(channels_), 0.0f);
  window_callback_ = [this](const PitchEstimate& estimate, int64_t window_end_frame) {
    level_.store(estimate.level, std::memory_order_relaxed);
    if (!on_event_) {
      return;
    }
    InputPitchEvent event;
    event.hz = estimate.hz;
    event.confidence = estimate.confidence;
    event.level = estimate.level;
    event.position_ms = position_ ? position_() : -1.0;
    event.timestamp_ms = NowEpochMs();
    event.window_end_frame = window_end_frame;
    on_event_(event);
  };
}

InputPitchMonitor::~InputPitchMonitor() {
  Stop();
}

void InputPitchMonitor::Start() {
  if (running_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  shutdown_.store(false, std::memory_order_release);
  worker_ = std::thread(&InputPitchMonitor::WorkerLoop, this);
}

void InputPitchMonitor::Stop() {
  shutdown_.store(true, std::memory_order_release);
  if (worker_.joinable()) {
    worker_.join();
  }
  running_.store(false, std::memory_order_release);
}

size_t InputPitchMonitor::Pump() {
  if (!read_) {
    return 0;
  }
  size_t total = 0;
  while (true) {
    const size_t frames = read_(read_buffer_.data(), kReadFrames);
    if (frames == 0) {
      break;
    }
    analyzer_.Process(read_buffer_.data(), frames, window_callback_);
    total += frames;
    if (shutdown_.load(std::memory_order_acquire)) {
      break;
    }
  }
  return total;
}

void InputPitchMonitor::WorkerLoop() {
  while (!shutdown_.load(std::memory_order_acquire)) {
    if (Pump() == 0) {
      std::this_thread::sleep_for(kIdleSleep);
    }
  }
}

}  // namespace analysis
}  // namespace sezo
