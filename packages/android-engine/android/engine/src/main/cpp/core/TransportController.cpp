#include "TransportController.h"

namespace sezo {
namespace core {

TransportController::TransportController() = default;

void TransportController::Play() {
  state_.store(PlaybackState::kPlaying, std::memory_order_release);
}

void TransportController::Pause() {
  auto current = state_.load(std::memory_order_acquire);
  if (current == PlaybackState::kPlaying || current == PlaybackState::kRecording) {
    state_.store(PlaybackState::kPaused, std::memory_order_release);
  }
}

void TransportController::Stop() {
  state_.store(PlaybackState::kStopped, std::memory_order_release);
}

PlaybackState TransportController::GetState() const {
  return state_.load(std::memory_order_acquire);
}

bool TransportController::IsPlaying() const {
  auto current = GetState();
  return current == PlaybackState::kPlaying || current == PlaybackState::kRecording;
}

}  // namespace core
}  // namespace sezo
