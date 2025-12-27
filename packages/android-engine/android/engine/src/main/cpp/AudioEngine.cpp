#include "AudioEngine.h"
#include <android/log.h>

#define LOG_TAG "AudioEngine"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
  Release();
}

bool AudioEngine::Initialize(int32_t sample_rate, int32_t max_tracks) {
  if (initialized_) {
    LOGD("AudioEngine already initialized");
    return true;
  }

  sample_rate_ = sample_rate;

  // Create core components
  clock_ = std::make_shared<core::MasterClock>();
  timing_ = std::make_shared<core::TimingManager>(sample_rate);
  transport_ = std::make_shared<core::TransportController>();

  // Create playback components
  mixer_ = std::make_shared<playback::MultiTrackMixer>();
  player_ = std::make_shared<playback::OboePlayer>(mixer_, clock_, transport_);

  // Initialize Oboe player
  if (!player_->Initialize(sample_rate)) {
    LOGE("Failed to initialize Oboe player");
    Release();
    return false;
  }

  initialized_ = true;
  LOGD("AudioEngine initialized: sample_rate=%d, max_tracks=%d", sample_rate, max_tracks);
  return true;
}

void AudioEngine::Release() {
  if (!initialized_) {
    return;
  }

  Stop();
  UnloadAllTracks();

  player_.reset();
  mixer_.reset();
  transport_.reset();
  timing_.reset();
  clock_.reset();

  initialized_ = false;
  LOGD("AudioEngine released");
}

bool AudioEngine::LoadTrack(const std::string& track_id, const std::string& file_path) {
  if (!initialized_) {
    LOGE("AudioEngine not initialized");
    return false;
  }

  // Check if track already loaded
  if (tracks_.find(track_id) != tracks_.end()) {
    LOGD("Track %s already loaded", track_id.c_str());
    return true;
  }

  // Create and load track
  auto track = std::make_shared<playback::Track>(track_id, file_path);
  if (!track->Load()) {
    LOGE("Failed to load track: %s", file_path.c_str());
    return false;
  }

  // Add to mixer
  mixer_->AddTrack(track);
  tracks_[track_id] = track;

  // Update duration (use longest track)
  const int64_t track_duration = track->GetDuration();
  if (track_duration > timing_->GetDurationSamples()) {
    timing_->SetDuration(track_duration);
  }

  LOGD("Track loaded: id=%s, path=%s, duration=%lld frames",
       track_id.c_str(), file_path.c_str(), static_cast<long long>(track_duration));
  return true;
}

bool AudioEngine::UnloadTrack(const std::string& track_id) {
  auto it = tracks_.find(track_id);
  if (it == tracks_.end()) {
    return false;
  }

  mixer_->RemoveTrack(track_id);
  tracks_.erase(it);

  LOGD("Track unloaded: %s", track_id.c_str());
  return true;
}

void AudioEngine::UnloadAllTracks() {
  mixer_->ClearTracks();
  tracks_.clear();
  timing_->SetDuration(0);
  LOGD("All tracks unloaded");
}

std::vector<std::string> AudioEngine::GetLoadedTrackIds() const {
  std::vector<std::string> ids;
  ids.reserve(tracks_.size());
  for (const auto& pair : tracks_) {
    ids.push_back(pair.first);
  }
  return ids;
}

void AudioEngine::Play() {
  if (!initialized_) {
    return;
  }

  transport_->Play();
  player_->Start();
  LOGD("Playback started");
}

void AudioEngine::Pause() {
  if (!initialized_) {
    return;
  }

  transport_->Pause();
  LOGD("Playback paused");
}

void AudioEngine::Stop() {
  if (!initialized_) {
    return;
  }

  transport_->Stop();
  player_->Stop();
  clock_->Reset();
  LOGD("Playback stopped");
}

void AudioEngine::Seek(double position_ms) {
  if (!initialized_) {
    return;
  }

  const int64_t frame = timing_->MsToSamples(position_ms);
  clock_->SetPosition(frame);

  // Seek all tracks
  for (auto& pair : tracks_) {
    pair.second->Seek(frame);
  }

  LOGD("Seeked to %.2f ms (%lld frames)", position_ms, static_cast<long long>(frame));
}

bool AudioEngine::IsPlaying() const {
  return initialized_ && transport_->IsPlaying();
}

double AudioEngine::GetCurrentPosition() const {
  if (!initialized_) {
    return 0.0;
  }
  return timing_->SamplesToMs(clock_->GetPosition());
}

double AudioEngine::GetDuration() const {
  if (!initialized_) {
    return 0.0;
  }
  return timing_->GetDurationMs();
}

void AudioEngine::SetTrackVolume(const std::string& track_id, float volume) {
  auto track = mixer_->GetTrack(track_id);
  if (track) {
    track->SetVolume(volume);
  }
}

void AudioEngine::SetTrackMuted(const std::string& track_id, bool muted) {
  auto track = mixer_->GetTrack(track_id);
  if (track) {
    track->SetMuted(muted);
  }
}

void AudioEngine::SetTrackSolo(const std::string& track_id, bool solo) {
  auto track = mixer_->GetTrack(track_id);
  if (track) {
    track->SetSolo(solo);
  }
}

void AudioEngine::SetTrackPan(const std::string& track_id, float pan) {
  auto track = mixer_->GetTrack(track_id);
  if (track) {
    track->SetPan(pan);
  }
}

void AudioEngine::SetMasterVolume(float volume) {
  if (mixer_) {
    mixer_->SetMasterVolume(volume);
  }
}

float AudioEngine::GetMasterVolume() const {
  return mixer_ ? mixer_->GetMasterVolume() : 1.0f;
}

void AudioEngine::SetPitch(float semitones) {
  pitch_ = semitones;
  // TODO: Apply to processing pipeline (Phase 2)
}

float AudioEngine::GetPitch() const {
  return pitch_;
}

void AudioEngine::SetSpeed(float rate) {
  speed_ = rate;
  // TODO: Apply to processing pipeline (Phase 2)
}

float AudioEngine::GetSpeed() const {
  return speed_;
}

}  // namespace sezo
