#include "TimingManager.h"

namespace sezo {
namespace core {

TimingManager::TimingManager(int32_t sample_rate) : sample_rate_(sample_rate) {}

void TimingManager::SetDuration(int64_t duration_samples) {
  duration_samples_.store(duration_samples, std::memory_order_release);
}

int64_t TimingManager::GetDurationSamples() const {
  return duration_samples_.load(std::memory_order_acquire);
}

double TimingManager::GetDurationMs() const {
  return SamplesToMs(GetDurationSamples());
}

double TimingManager::SamplesToMs(int64_t samples) const {
  return (static_cast<double>(samples) * 1000.0) / static_cast<double>(sample_rate_);
}

int64_t TimingManager::MsToSamples(double ms) const {
  return static_cast<int64_t>((ms * static_cast<double>(sample_rate_)) / 1000.0);
}

}  // namespace core
}  // namespace sezo
