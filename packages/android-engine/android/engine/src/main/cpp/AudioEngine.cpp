#include "AudioEngine.h"
#include <android/log.h>
#include <utility>

#define LOG_TAG "AudioEngine"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
  Release();
}

void AudioEngine::SetErrorCallback(ErrorCallback callback) {
  std::lock_guard<std::mutex> lock(error_mutex_);
  error_callback_ = std::move(callback);
}

core::ErrorCode AudioEngine::GetLastErrorCode() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

std::string AudioEngine::GetLastErrorMessage() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_message_;
}

bool AudioEngine::Initialize(int32_t sample_rate, int32_t max_tracks) {
  if (initialized_) {
    LOGD("AudioEngine already initialized");
    return true;
  }

  if (sample_rate <= 0) {
    ReportError(core::ErrorCode::kInvalidArgument, "Invalid sample rate");
    return false;
  }
  if (max_tracks <= 0) {
    ReportError(core::ErrorCode::kInvalidArgument, "Invalid max tracks");
    return false;
  }

  sample_rate_ = sample_rate;
  max_tracks_ = max_tracks;

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
    ReportError(core::ErrorCode::kStreamError, "Failed to initialize audio stream");
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
  if (player_) {
    player_->Close();
  }

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
    ReportError(core::ErrorCode::kNotInitialized, "AudioEngine not initialized");
    return false;
  }
  if (track_id.empty()) {
    ReportError(core::ErrorCode::kInvalidArgument, "Track id is empty");
    return false;
  }
  if (file_path.empty()) {
    ReportError(core::ErrorCode::kInvalidArgument, "File path is empty");
    return false;
  }
  if (tracks_.size() >= static_cast<size_t>(max_tracks_)) {
    ReportError(core::ErrorCode::kTrackLimitReached, "Max track limit reached");
    return false;
  }

  const bool is_mp3 = file_path.find(".mp3") != std::string::npos;
  const bool is_wav = file_path.find(".wav") != std::string::npos;
  if (!is_mp3 && !is_wav) {
    ReportError(core::ErrorCode::kUnsupportedFormat, "Unsupported audio format: " + file_path);
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
    ReportError(core::ErrorCode::kDecoderOpenFailed, "Failed to load track: " + file_path);
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
    ReportError(core::ErrorCode::kTrackNotFound, "Track not found: " + track_id);
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
  if (!player_->IsRunning()) {
    if (!player_->Start()) {
      transport_->Stop();
      ReportError(core::ErrorCode::kStreamError, "Failed to start audio stream");
      return;
    }
  }
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
  if (!player_->Stop()) {
    ReportError(core::ErrorCode::kStreamError, "Failed to stop audio stream");
  }
  Seek(0.0);
  LOGD("Playback stopped");
}

void AudioEngine::Seek(double position_ms) {
  if (!initialized_) {
    return;
  }

  const double duration_ms = timing_->GetDurationMs();
  double clamped_ms = position_ms;
  if (clamped_ms < 0.0) {
    clamped_ms = 0.0;
  } else if (duration_ms > 0.0 && clamped_ms > duration_ms) {
    clamped_ms = duration_ms;
  }
  if (clamped_ms != position_ms) {
    ReportError(core::ErrorCode::kInvalidArgument, "Seek position out of range");
  }

  const int64_t frame = timing_->MsToSamples(clamped_ms);
  clock_->SetPosition(frame);

  // Seek all tracks
  bool seek_ok = true;
  for (auto& pair : tracks_) {
    if (!pair.second->Seek(frame)) {
      seek_ok = false;
    }
  }
  if (!seek_ok) {
    ReportError(core::ErrorCode::kSeekFailed, "One or more tracks failed to seek");
  }

  LOGD("Seeked to %.2f ms (%lld frames)", clamped_ms, static_cast<long long>(frame));
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
  } else {
    ReportError(core::ErrorCode::kTrackNotFound, "Track not found: " + track_id);
  }
}

void AudioEngine::SetTrackMuted(const std::string& track_id, bool muted) {
  auto track = mixer_->GetTrack(track_id);
  if (track) {
    track->SetMuted(muted);
  } else {
    ReportError(core::ErrorCode::kTrackNotFound, "Track not found: " + track_id);
  }
}

void AudioEngine::SetTrackSolo(const std::string& track_id, bool solo) {
  auto track = mixer_->GetTrack(track_id);
  if (track) {
    track->SetSolo(solo);
  } else {
    ReportError(core::ErrorCode::kTrackNotFound, "Track not found: " + track_id);
  }
}

void AudioEngine::SetTrackPan(const std::string& track_id, float pan) {
  auto track = mixer_->GetTrack(track_id);
  if (track) {
    track->SetPan(pan);
  } else {
    ReportError(core::ErrorCode::kTrackNotFound, "Track not found: " + track_id);
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

void AudioEngine::ReportError(core::ErrorCode code, const std::string& message) {
  ErrorCallback callback;
  {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = code;
    last_error_message_ = message;
    callback = error_callback_;
  }

  if (callback) {
    callback(code, message);
  }
  LOGE("%s", message.c_str());
}

}  // namespace sezo
