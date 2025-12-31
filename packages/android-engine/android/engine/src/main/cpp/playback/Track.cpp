#include "Track.h"
#include "audio/MP3Decoder.h"
#include "audio/WAVDecoder.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <android/log.h>

#define LOG_TAG "Track"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace sezo {
namespace playback {

Track::Track(const std::string& id, const std::string& file_path)
    : id_(id), file_path_(file_path) {}

Track::~Track() {
  Unload();
}

bool Track::Load() {
  if (is_loaded_.load(std::memory_order_acquire)) {
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

  // Phase 2: Create time-stretcher
  time_stretcher_ = std::make_unique<TimeStretch>(
      decoder_->GetFormat().sample_rate,
      decoder_->GetFormat().channels);

  // Start streaming thread
  streaming_active_.store(true, std::memory_order_release);
  streaming_thread_ = std::make_unique<std::thread>(&Track::StreamingThreadFunc, this);

  is_loaded_.store(true, std::memory_order_release);
  LOGD("Track loaded: %s", id_.c_str());
  return true;
}

void Track::Unload() {
  if (is_loaded_.load(std::memory_order_acquire)) {
    // Stop streaming thread
    streaming_active_.store(false, std::memory_order_release);
    streaming_cv_.notify_all();

    if (streaming_thread_ && streaming_thread_->joinable()) {
      streaming_thread_->join();
    }
    streaming_thread_.reset();

    {
      std::lock_guard<std::mutex> lock(decoder_mutex_);
      decoder_->Close();
      decoder_.reset();
    }
    buffer_.reset();
    time_stretcher_.reset();
    is_loaded_.store(false, std::memory_order_release);
    LOGD("Track unloaded: %s", id_.c_str());
  }
}

size_t Track::ReadSamples(float* output, size_t frames) {
  if (!is_loaded_.load(std::memory_order_acquire) || muted_.load(std::memory_order_acquire)) {
    // If muted, fill with silence
    const int32_t channels = decoder_ ? decoder_->GetFormat().channels : 2;
    std::fill_n(output, frames * channels, 0.0f);
    return frames;
  }

  const int32_t channels = decoder_->GetFormat().channels;
  const float vol = volume_.load(std::memory_order_acquire);
  const float pan_value = pan_.load(std::memory_order_acquire);
  const bool use_time_stretch =
      time_stretcher_ && time_stretcher_->IsActive() && (channels == 1 || channels == 2);
  size_t frames_processed = frames;

  // Phase 2: Apply time-stretch/pitch-shift effects BEFORE volume/pan
  if (use_time_stretch) {
    const float stretch = time_stretcher_->GetStretchFactor();
    const float pitch = time_stretcher_->GetPitchSemitones();
    const double requested_input = static_cast<double>(frames) * stretch + stretch_input_fraction_;
    size_t input_frames = static_cast<size_t>(requested_input);
    stretch_input_fraction_ = requested_input - static_cast<double>(input_frames);
    if (input_frames < 1) {
      input_frames = 1;
    }

    const size_t input_samples = input_frames * channels;
    if (stretch_input_buffer_.size() < input_samples) {
      stretch_input_buffer_.resize(input_samples);
    }

    const size_t available_samples = buffer_->Available();
    const size_t samples_read = buffer_->Read(stretch_input_buffer_.data(), input_samples);
    if (samples_read < input_samples) {
      std::fill_n(stretch_input_buffer_.data() + samples_read,
                  input_samples - samples_read,
                  0.0f);
      if (++underrun_log_counter_ % 50 == 0) {
        LOGW("Track %s stretch underrun: need=%zu read=%zu avail=%zu out_frames=%zu in_frames=%zu stretch=%.3f pitch=%.2f",
             id_.c_str(),
             input_samples,
             samples_read,
             available_samples,
             frames,
             input_frames,
             stretch,
             pitch);
      }
    }

    time_stretcher_->Process(stretch_input_buffer_.data(), input_frames, output, frames);
    frames_processed = frames;
    if (++stretch_log_counter_ % 200 == 0) {
      LOGD("Track %s stretch: out_frames=%zu in_frames=%zu stretch=%.3f pitch=%.2f avail=%zu read=%zu",
           id_.c_str(),
           frames,
           input_frames,
           stretch,
           pitch,
           available_samples,
           samples_read);
    }
  } else {
    stretch_input_fraction_ = 0.0;
    const size_t samples_needed = frames * channels;
    const size_t available_samples = buffer_->Available();
    const size_t samples_read = buffer_->Read(output, samples_needed);
    if (samples_read < samples_needed) {
      std::fill_n(output + samples_read, samples_needed - samples_read, 0.0f);
      if (++underrun_log_counter_ % 50 == 0) {
        LOGW("Track %s buffer underrun: need=%zu read=%zu avail=%zu frames=%zu",
             id_.c_str(),
             samples_needed,
             samples_read,
             available_samples,
             frames);
      }
    }
    frames_processed = samples_read / channels;
  }

  // Apply volume and pan (for stereo)
  if (channels == 2) {
    // Calculate pan gains (equal power panning)
    const float left_gain = vol * std::cos((pan_value + 1.0f) * 0.25f * M_PI);
    const float right_gain = vol * std::sin((pan_value + 1.0f) * 0.25f * M_PI);

    for (size_t i = 0; i < frames_processed * 2; i += 2) {
      output[i] *= left_gain;       // Left channel
      output[i + 1] *= right_gain;  // Right channel
    }
  } else if (channels == 1 && vol != 1.0f) {
    // Mono, just apply volume
    for (size_t i = 0; i < frames_processed; ++i) {
      output[i] *= vol;
    }
  }

  // Notify streaming thread that buffer has space
  streaming_cv_.notify_one();

  return frames_processed;
}

bool Track::Seek(int64_t frame) {
  if (!is_loaded_.load(std::memory_order_acquire) || !decoder_) {
    return false;
  }

  std::lock_guard<std::mutex> lock(decoder_mutex_);
  const int64_t total_frames = decoder_->GetFormat().total_frames;
  int64_t clamped_frame = frame;
  if (total_frames > 0) {
    clamped_frame = std::clamp(frame, int64_t{0}, total_frames);
  } else if (frame < 0) {
    clamped_frame = 0;
  }

  buffer_->Reset();

  // Phase 2: Reset time-stretcher after seek to avoid artifacts
  if (time_stretcher_) {
    time_stretcher_->Reset();
  }
  stretch_input_fraction_ = 0.0;

  const bool result = decoder_->Seek(clamped_frame);
  streaming_cv_.notify_all();
  return result;
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

void Track::SetStartTimeSamples(int64_t start_time_samples) {
  start_time_samples_.store(std::max<int64_t>(0, start_time_samples), std::memory_order_release);
}

int64_t Track::GetStartTimeSamples() const {
  return start_time_samples_.load(std::memory_order_acquire);
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

// Phase 2: Real-time effects methods
void Track::SetPitchSemitones(float semitones) {
  if (time_stretcher_) {
    time_stretcher_->SetPitchSemitones(semitones);
  }
}

float Track::GetPitchSemitones() const {
  return time_stretcher_ ? time_stretcher_->GetPitchSemitones() : 0.0f;
}

void Track::SetStretchFactor(float factor) {
  if (time_stretcher_) {
    time_stretcher_->SetStretchFactor(factor);
  }
}

float Track::GetStretchFactor() const {
  return time_stretcher_ ? time_stretcher_->GetStretchFactor() : 1.0f;
}

void Track::StreamingThreadFunc() {
  // Buffer for reading from decoder (e.g., 4096 frames)
  const size_t chunk_frames = 4096;
  const int32_t channels = decoder_->GetFormat().channels;
  std::vector<float> temp_buffer(chunk_frames * channels);

  LOGD("Streaming thread started for track: %s", id_.c_str());

  while (streaming_active_.load(std::memory_order_acquire)) {
    // Check if buffer needs filling (has space for at least one chunk)
    const size_t free_space = buffer_->FreeSpace();
    const size_t samples_per_chunk = chunk_frames * channels;

    if (free_space >= samples_per_chunk) {
      // Read from decoder
      size_t frames_read = 0;
      {
        std::lock_guard<std::mutex> lock(decoder_mutex_);
        if (decoder_) {
          frames_read = decoder_->Read(temp_buffer.data(), chunk_frames);
        }
      }

      if (frames_read > 0) {
        // Write to circular buffer
        const size_t samples_to_write = frames_read * channels;
        const size_t samples_written = buffer_->Write(temp_buffer.data(), samples_to_write);

        if (samples_written < samples_to_write) {
          LOGD("Warning: Buffer full, dropped %zu samples",
               samples_to_write - samples_written);
        }
      } else {
        // End of file or error
        // For now, just wait - seeking will reset position
        std::unique_lock<std::mutex> lock(streaming_mutex_);
        streaming_cv_.wait_for(lock, std::chrono::milliseconds(100));
      }
    } else {
      // Buffer is full enough, wait for consumption
      std::unique_lock<std::mutex> lock(streaming_mutex_);
      streaming_cv_.wait_for(lock, std::chrono::milliseconds(10));
    }
  }

  LOGD("Streaming thread stopped for track: %s", id_.c_str());
}

}  // namespace playback
}  // namespace sezo
