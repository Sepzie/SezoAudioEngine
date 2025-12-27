#include "Track.h"
#include "audio/MP3Decoder.h"
#include "audio/WAVDecoder.h"

#include <algorithm>

namespace sezo {
namespace playback {

Track::Track(const std::string& id, const std::string& file_path)
    : id_(id), file_path_(file_path) {}

Track::~Track() {
  Unload();
}

bool Track::Load() {
  if (is_loaded_) {
    return true;
  }

  // Determine decoder type based on file extension
  if (file_path_.find(".mp3") != std::string::npos) {
    decoder_ = std::make_unique<audio::MP3Decoder>();
  } else if (file_path_.find(".wav") != std::string::npos) {
    decoder_ = std::make_unique<audio::WAVDecoder>();
  } else {
    return false;  // Unsupported format
  }

  if (!decoder_->Open(file_path_)) {
    decoder_.reset();
    return false;
  }

  // Create circular buffer (e.g., 1 second of audio)
  const size_t buffer_size = decoder_->GetFormat().sample_rate * decoder_->GetFormat().channels;
  buffer_ = std::make_unique<core::CircularBuffer>(buffer_size);

  is_loaded_ = true;
  return true;
}

void Track::Unload() {
  if (is_loaded_) {
    decoder_->Close();
    decoder_.reset();
    buffer_.reset();
    is_loaded_ = false;
  }
}

size_t Track::ReadSamples(float* output, size_t frames) {
  if (!is_loaded_ || muted_.load(std::memory_order_acquire)) {
    // If muted, fill with silence
    std::fill_n(output, frames * decoder_->GetFormat().channels, 0.0f);
    return frames;
  }

  // Read from buffer
  const size_t samples_needed = frames * decoder_->GetFormat().channels;
  const size_t samples_read = buffer_->Read(output, samples_needed);

  // Apply volume
  const float vol = volume_.load(std::memory_order_acquire);
  if (vol != 1.0f) {
    for (size_t i = 0; i < samples_read; ++i) {
      output[i] *= vol;
    }
  }

  // TODO: Apply pan

  return samples_read / decoder_->GetFormat().channels;
}

bool Track::Seek(int64_t frame) {
  if (!is_loaded_) {
    return false;
  }

  buffer_->Reset();
  return decoder_->Seek(frame);
}

int64_t Track::GetDuration() const {
  return is_loaded_ ? decoder_->GetFormat().total_frames : 0;
}

int32_t Track::GetSampleRate() const {
  return is_loaded_ ? decoder_->GetFormat().sample_rate : 0;
}

int32_t Track::GetChannels() const {
  return is_loaded_ ? decoder_->GetFormat().channels : 0;
}

void Track::SetVolume(float volume) {
  volume_.store(std::clamp(volume, 0.0f, 2.0f), std::memory_order_release);
}

float Track::GetVolume() const {
  return volume_.load(std::memory_order_acquire);
}

void Track::SetMuted(bool muted) {
  muted_.store(muted, std::memory_order_release);
}

bool Track::IsMuted() const {
  return muted_.load(std::memory_order_acquire);
}

void Track::SetSolo(bool solo) {
  solo_.store(solo, std::memory_order_release);
}

bool Track::IsSolo() const {
  return solo_.load(std::memory_order_acquire);
}

void Track::SetPan(float pan) {
  pan_.store(std::clamp(pan, -1.0f, 1.0f), std::memory_order_release);
}

float Track::GetPan() const {
  return pan_.load(std::memory_order_acquire);
}

}  // namespace playback
}  // namespace sezo
