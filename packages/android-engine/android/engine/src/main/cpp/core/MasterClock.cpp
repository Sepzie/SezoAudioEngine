#include "MasterClock.h"

namespace sezo {
namespace core {

MasterClock::MasterClock() = default;

void MasterClock::Reset() {
  position_.store(0, std::memory_order_release);
}

void MasterClock::Advance(int64_t frames) {
  position_.fetch_add(frames, std::memory_order_acq_rel);
}

int64_t MasterClock::GetPosition() const {
  return position_.load(std::memory_order_acquire);
}

void MasterClock::SetPosition(int64_t position) {
  position_.store(position, std::memory_order_release);
}

}  // namespace core
}  // namespace sezo
