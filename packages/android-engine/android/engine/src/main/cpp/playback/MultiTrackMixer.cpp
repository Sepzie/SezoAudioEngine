#include "MultiTrackMixer.h"

#include <algorithm>
#include <cstring>

namespace sezo {
namespace playback {

MultiTrackMixer::MultiTrackMixer() = default;

MultiTrackMixer::~MultiTrackMixer() = default;

void MultiTrackMixer::AddTrack(std::shared_ptr<Track> track) {
  std::lock_guard<std::mutex> lock(tracks_mutex_);
  tracks_.push_back(track);
}

bool MultiTrackMixer::RemoveTrack(const std::string& track_id) {
  std::lock_guard<std::mutex> lock(tracks_mutex_);
  auto it = std::find_if(tracks_.begin(), tracks_.end(),
                         [&track_id](const std::shared_ptr<Track>& t) {
                           return t->GetId() == track_id;
                         });
  if (it != tracks_.end()) {
    tracks_.erase(it);
    return true;
  }
  return false;
}

void MultiTrackMixer::ClearTracks() {
  std::lock_guard<std::mutex> lock(tracks_mutex_);
  tracks_.clear();
}

std::shared_ptr<Track> MultiTrackMixer::GetTrack(const std::string& track_id) {
  std::lock_guard<std::mutex> lock(tracks_mutex_);
  auto it = std::find_if(tracks_.begin(), tracks_.end(),
                         [&track_id](const std::shared_ptr<Track>& t) {
                           return t->GetId() == track_id;
                         });
  return (it != tracks_.end()) ? *it : nullptr;
}

std::vector<std::shared_ptr<Track>> MultiTrackMixer::GetTracks() {
  std::lock_guard<std::mutex> lock(tracks_mutex_);
  return tracks_;
}

void MultiTrackMixer::Mix(float* output, size_t frames, int64_t timeline_start_sample) {
  // Clear output buffer
  std::memset(output, 0, frames * 2 * sizeof(float));  // Assume stereo

  if (tracks_.empty()) {
    return;
  }

  // Check if any track is soloed
  bool has_solo = false;
  {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    for (const auto& track : tracks_) {
      if (track->IsSolo()) {
        has_solo = true;
        break;
      }
    }
  }

  // Mix tracks
  std::lock_guard<std::mutex> lock(tracks_mutex_);
  for (const auto& track : tracks_) {
    if (!track->IsLoaded()) {
      continue;
    }

    // Solo logic: if any track is soloed, only play soloed tracks
    if (has_solo && !track->IsSolo()) {
      continue;
    }

    // Skip muted tracks
    if (track->IsMuted()) {
      continue;
    }

    const int64_t track_start = track->GetStartTimeSamples();
    const int64_t track_frame = timeline_start_sample - track_start;

    if (track_frame < 0 && track_frame + static_cast<int64_t>(frames) <= 0) {
      continue;
    }

    size_t offset_frames = 0;
    if (track_frame < 0) {
      offset_frames = static_cast<size_t>(-track_frame);
    }

    const size_t frames_to_read = (offset_frames >= frames) ? 0 : (frames - offset_frames);
    if (frames_to_read == 0) {
      continue;
    }

    const int32_t channels = track->GetChannels();
    if (channels <= 0) {
      continue;
    }

    // Read track samples
    if (channels == 1) {
      if (mono_buffer_.size() < frames_to_read) {
        mono_buffer_.resize(frames_to_read);
      }
      track->ReadSamples(mono_buffer_.data(), frames_to_read);

      // Mix mono into stereo output
      const size_t output_offset = offset_frames * 2;
      for (size_t i = 0; i < frames_to_read; ++i) {
        const float sample = mono_buffer_[i];
        const size_t out_index = output_offset + i * 2;
        output[out_index] += sample;
        output[out_index + 1] += sample;
      }
    } else if (channels == 2) {
      const size_t samples_needed = frames_to_read * 2;
      if (mix_buffer_.size() < samples_needed) {
        mix_buffer_.resize(samples_needed);
      }
      track->ReadSamples(mix_buffer_.data(), frames_to_read);

      // Mix into output at the offset
      const size_t output_offset = offset_frames * 2;
      for (size_t i = 0; i < frames_to_read * 2; ++i) {
        output[output_offset + i] += mix_buffer_[i];
      }
    }
  }

  // Apply master volume
  const float master_vol = master_volume_.load(std::memory_order_acquire);
  if (master_vol != 1.0f) {
    for (size_t i = 0; i < frames * 2; ++i) {
      output[i] *= master_vol;
    }
  }

  // Clip prevention (soft limiting)
  for (size_t i = 0; i < frames * 2; ++i) {
    output[i] = std::clamp(output[i], -1.0f, 1.0f);
  }
}

void MultiTrackMixer::SetMasterVolume(float volume) {
  master_volume_.store(std::clamp(volume, 0.0f, 2.0f), std::memory_order_release);
}

float MultiTrackMixer::GetMasterVolume() const {
  return master_volume_.load(std::memory_order_acquire);
}

}  // namespace playback
}  // namespace sezo
