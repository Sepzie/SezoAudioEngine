#include "AudioEngine.h"
#include "extraction/ExtractionPipeline.h"
#include <android/log.h>
#include <algorithm>
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

  StartExtractionWorker();

  initialized_ = true;
  LOGD("AudioEngine initialized: sample_rate=%d, max_tracks=%d", sample_rate, max_tracks);
  return true;
}

void AudioEngine::Release() {
  if (!initialized_) {
    return;
  }

  CancelAllExtractions();
  StopExtractionWorker();

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

bool AudioEngine::LoadTrack(const std::string& track_id,
                            const std::string& file_path,
                            double start_time_ms) {
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

  const int64_t start_time_samples = std::max<int64_t>(0, timing_->MsToSamples(start_time_ms));

  // Create and load track
  auto track = std::make_shared<playback::Track>(track_id, file_path);
  if (!track->Load()) {
    LOGE("Failed to load track: %s", file_path.c_str());
    ReportError(core::ErrorCode::kDecoderOpenFailed, "Failed to load track: " + file_path);
    return false;
  }
  track->SetStartTimeSamples(start_time_samples);
  const int64_t current_frame = clock_->GetPosition();
  if (current_frame > start_time_samples) {
    const int64_t track_frame = current_frame - start_time_samples;
    track->Seek(track_frame);
  }

  // Add to mixer
  mixer_->AddTrack(track);
  tracks_[track_id] = track;

  RecalculateDuration();

  const int64_t track_duration = track->GetDuration();
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
  RecalculateDuration();

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
    const int64_t track_start = pair.second->GetStartTimeSamples();
    const int64_t track_frame = frame - track_start;
    if (!pair.second->Seek(std::max<int64_t>(0, track_frame))) {
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

// Phase 2: Per-track effects
void AudioEngine::SetTrackPitch(const std::string& track_id, float semitones) {
  auto it = tracks_.find(track_id);
  if (it != tracks_.end()) {
    it->second->SetPitchSemitones(semitones);
  } else {
    ReportError(core::ErrorCode::kTrackNotFound, "Track not found: " + track_id);
  }
}

float AudioEngine::GetTrackPitch(const std::string& track_id) const {
  auto it = tracks_.find(track_id);
  if (it != tracks_.end()) {
    return it->second->GetPitchSemitones();
  }
  return 0.0f;
}

void AudioEngine::SetTrackSpeed(const std::string& track_id, float rate) {
  auto it = tracks_.find(track_id);
  if (it != tracks_.end()) {
    it->second->SetStretchFactor(rate);
  } else {
    ReportError(core::ErrorCode::kTrackNotFound, "Track not found: " + track_id);
  }
}

float AudioEngine::GetTrackSpeed(const std::string& track_id) const {
  auto it = tracks_.find(track_id);
  if (it != tracks_.end()) {
    return it->second->GetStretchFactor();
  }
  return 1.0f;
}

// Phase 2: Master effects (apply to all tracks)
void AudioEngine::SetPitch(float semitones) {
  pitch_ = semitones;
  // Apply to all loaded tracks
  for (auto& pair : tracks_) {
    pair.second->SetPitchSemitones(semitones);
  }
}

float AudioEngine::GetPitch() const {
  return pitch_;
}

void AudioEngine::SetSpeed(float rate) {
  speed_ = rate;
  // Apply to all loaded tracks
  for (auto& pair : tracks_) {
    pair.second->SetStretchFactor(rate);
  }
}

float AudioEngine::GetSpeed() const {
  return speed_;
}

// Phase 3: Recording
bool AudioEngine::StartRecording(
    const std::string& output_path,
    const recording::RecordingConfig& config,
    RecordingCompletionCallback callback) {

  if (!initialized_) {
    ReportError(core::ErrorCode::kNotInitialized, "AudioEngine not initialized");
    return false;
  }

  if (IsRecording()) {
    ReportError(core::ErrorCode::kInvalidState, "Already recording");
    return false;
  }

  // Create recording pipeline if not exists
  if (!recording_pipeline_) {
    recording_pipeline_ = std::make_unique<recording::RecordingPipeline>();
  }

  const auto state = transport_->GetState();
  const int64_t start_samples =
      (state == core::PlaybackState::kStopped) ? 0 : clock_->GetPosition();
  recording_start_samples_.store(start_samples, std::memory_order_release);

  recording::RecordingPipeline::RecordingCallback wrapped_callback = nullptr;
  if (callback) {
    wrapped_callback = [this, callback](const recording::RecordingResult& result) {
      recording::RecordingResult enriched = result;
      const int64_t start = recording_start_samples_.load(std::memory_order_acquire);
      enriched.start_time_samples = start;
      enriched.start_time_ms = timing_ ? timing_->SamplesToMs(start) : 0.0;
      callback(enriched);
    };
  }

  // Start recording
  bool success = recording_pipeline_->StartRecording(output_path, config, wrapped_callback);
  if (!success) {
    ReportError(core::ErrorCode::kRecordingFailed, "Failed to start recording");
    recording_start_samples_.store(0, std::memory_order_release);
  } else {
    LOGD("Recording started: %s", output_path.c_str());
  }

  return success;
}

recording::RecordingResult AudioEngine::StopRecording() {
  if (!recording_pipeline_) {
    recording::RecordingResult result;
    result.success = false;
    result.error_message = "Not recording";
    return result;
  }

  auto result = recording_pipeline_->StopRecording();
  const int64_t start_samples = recording_start_samples_.load(std::memory_order_acquire);
  result.start_time_samples = start_samples;
  result.start_time_ms = timing_ ? timing_->SamplesToMs(start_samples) : 0.0;
  LOGD("Recording stopped: %s, %lld samples",
       result.output_path.c_str(),
       static_cast<long long>(result.duration_samples));

  return result;
}

bool AudioEngine::IsRecording() const {
  return recording_pipeline_ && recording_pipeline_->IsRecording();
}

float AudioEngine::GetInputLevel() const {
  if (recording_pipeline_) {
    return recording_pipeline_->GetInputLevel();
  }
  return 0.0f;
}

void AudioEngine::SetRecordingVolume(float volume) {
  if (recording_pipeline_) {
    recording_pipeline_->SetVolume(volume);
  }
}

// Phase 6: Extraction
AudioEngine::ExtractionResult AudioEngine::ExtractTrack(
    const std::string& track_id,
    const std::string& output_path,
    const ExtractionOptions& options,
    ExtractionProgressCallback progress_callback,
    std::atomic<bool>* cancel_flag) {

  ExtractionResult result;
  result.track_id = track_id;
  result.output_path = output_path;

  if (!initialized_) {
    result.error_message = "AudioEngine not initialized";
    ReportError(core::ErrorCode::kNotInitialized, result.error_message);
    return result;
  }

  auto it = tracks_.find(track_id);
  if (it == tracks_.end()) {
    result.error_message = "Track not found: " + track_id;
    ReportError(core::ErrorCode::kTrackNotFound, result.error_message);
    return result;
  }

  auto track = it->second;
  if (!track->IsLoaded()) {
    result.error_message = "Track not loaded: " + track_id;
    ReportError(core::ErrorCode::kTrackNotFound, result.error_message);
    return result;
  }

  // Create extraction pipeline
  extraction::ExtractionPipeline pipeline;

  // Convert options to extraction config
  extraction::ExtractionConfig config;
  config.sample_rate = sample_rate_;
  config.bitrate = options.bitrate;
  config.bits_per_sample = options.bits_per_sample;
  config.include_effects = options.include_effects;

  // Map format string to enum
  if (options.format == "wav") {
    config.format = audio::EncoderFormat::kWAV;
  } else if (options.format == "aac") {
    config.format = audio::EncoderFormat::kAAC;
  } else if (options.format == "m4a") {
    config.format = audio::EncoderFormat::kM4A;
  } else if (options.format == "mp3") {
    config.format = audio::EncoderFormat::kMP3;
  } else {
    result.error_message = "Unsupported format: " + options.format;
    ReportError(core::ErrorCode::kInvalidArgument, result.error_message);
    return result;
  }

  // Perform extraction
  auto extraction_result = pipeline.ExtractTrack(
      track, output_path, config, progress_callback, cancel_flag);

  // Convert extraction result
  result.success = extraction_result.success;
  result.duration_samples = extraction_result.duration_samples;
  result.file_size = extraction_result.file_size;
  result.error_message = extraction_result.error_message;

  if (!result.success) {
    ReportError(core::ErrorCode::kExtractionFailed, result.error_message);
  }

  return result;
}

AudioEngine::ExtractionResult AudioEngine::ExtractAllTracks(
    const std::string& output_path,
    const ExtractionOptions& options,
    ExtractionProgressCallback progress_callback,
    std::atomic<bool>* cancel_flag) {

  ExtractionResult result;
  result.output_path = output_path;

  if (!initialized_) {
    result.error_message = "AudioEngine not initialized";
    ReportError(core::ErrorCode::kNotInitialized, result.error_message);
    return result;
  }

  if (tracks_.empty()) {
    result.error_message = "No tracks loaded";
    ReportError(core::ErrorCode::kTrackNotFound, result.error_message);
    return result;
  }

  // Collect all loaded tracks
  std::vector<std::shared_ptr<playback::Track>> track_list;
  for (const auto& pair : tracks_) {
    if (pair.second->IsLoaded()) {
      track_list.push_back(pair.second);
    }
  }

  if (track_list.empty()) {
    result.error_message = "No loaded tracks to extract";
    ReportError(core::ErrorCode::kTrackNotFound, result.error_message);
    return result;
  }

  // Create extraction pipeline
  extraction::ExtractionPipeline pipeline;

  // Convert options to extraction config
  extraction::ExtractionConfig config;
  config.sample_rate = sample_rate_;
  config.bitrate = options.bitrate;
  config.bits_per_sample = options.bits_per_sample;
  config.include_effects = options.include_effects;

  // Map format string to enum
  if (options.format == "wav") {
    config.format = audio::EncoderFormat::kWAV;
  } else if (options.format == "aac") {
    config.format = audio::EncoderFormat::kAAC;
  } else if (options.format == "m4a") {
    config.format = audio::EncoderFormat::kM4A;
  } else if (options.format == "mp3") {
    config.format = audio::EncoderFormat::kMP3;
  } else {
    result.error_message = "Unsupported format: " + options.format;
    ReportError(core::ErrorCode::kInvalidArgument, result.error_message);
    return result;
  }

  // Perform extraction
  auto extraction_result = pipeline.ExtractMixedTracks(
      track_list, output_path, config, progress_callback, cancel_flag);

  // Convert extraction result
  result.success = extraction_result.success;
  result.duration_samples = extraction_result.duration_samples;
  result.file_size = extraction_result.file_size;
  result.error_message = extraction_result.error_message;

  if (!result.success) {
    ReportError(core::ErrorCode::kExtractionFailed, result.error_message);
  }

  return result;
}

int64_t AudioEngine::StartExtractTrack(
    const std::string& track_id,
    const std::string& output_path,
    const ExtractionOptions& options,
    ExtractionProgressCallback progress_callback,
    ExtractionCompletionCallback completion_callback) {

  if (!initialized_) {
    ReportError(core::ErrorCode::kNotInitialized, "AudioEngine not initialized");
    return 0;
  }

  if (track_id.empty() || output_path.empty()) {
    ReportError(core::ErrorCode::kInvalidArgument, "Invalid extraction arguments");
    return 0;
  }

  StartExtractionWorker();

  ExtractionTask task;
  task.job_id = NextExtractionJobId();
  task.is_mix = false;
  task.track_id = track_id;
  task.output_path = output_path;
  task.options = options;
  task.progress_callback = std::move(progress_callback);
  task.completion_callback = std::move(completion_callback);
  task.cancel_flag = std::make_shared<std::atomic<bool>>(false);

  {
    std::lock_guard<std::mutex> lock(extraction_mutex_);
    extraction_queue_.push_back(task);
    extraction_cancel_flags_[task.job_id] = task.cancel_flag;
  }
  extraction_cv_.notify_one();
  return task.job_id;
}

int64_t AudioEngine::StartExtractAllTracks(
    const std::string& output_path,
    const ExtractionOptions& options,
    ExtractionProgressCallback progress_callback,
    ExtractionCompletionCallback completion_callback) {

  if (!initialized_) {
    ReportError(core::ErrorCode::kNotInitialized, "AudioEngine not initialized");
    return 0;
  }

  if (output_path.empty()) {
    ReportError(core::ErrorCode::kInvalidArgument, "Output path is empty");
    return 0;
  }

  StartExtractionWorker();

  ExtractionTask task;
  task.job_id = NextExtractionJobId();
  task.is_mix = true;
  task.output_path = output_path;
  task.options = options;
  task.progress_callback = std::move(progress_callback);
  task.completion_callback = std::move(completion_callback);
  task.cancel_flag = std::make_shared<std::atomic<bool>>(false);

  {
    std::lock_guard<std::mutex> lock(extraction_mutex_);
    extraction_queue_.push_back(task);
    extraction_cancel_flags_[task.job_id] = task.cancel_flag;
  }
  extraction_cv_.notify_one();
  return task.job_id;
}

bool AudioEngine::CancelExtraction(int64_t job_id) {
  std::lock_guard<std::mutex> lock(extraction_mutex_);
  auto it = extraction_cancel_flags_.find(job_id);
  if (it == extraction_cancel_flags_.end()) {
    return false;
  }
  it->second->store(true, std::memory_order_release);
  extraction_cv_.notify_one();
  return true;
}

void AudioEngine::CancelAllExtractions() {
  std::lock_guard<std::mutex> lock(extraction_mutex_);
  for (auto& entry : extraction_cancel_flags_) {
    entry.second->store(true, std::memory_order_release);
  }
  extraction_cv_.notify_one();
}

bool AudioEngine::IsExtractionRunning() const {
  std::lock_guard<std::mutex> lock(extraction_mutex_);
  return current_extraction_job_id_ != 0;
}

void AudioEngine::StartExtractionWorker() {
  if (extraction_worker_running_.load(std::memory_order_acquire)) {
    return;
  }

  extraction_shutdown_.store(false, std::memory_order_release);
  extraction_worker_running_.store(true, std::memory_order_release);
  extraction_thread_ = std::thread(&AudioEngine::ExtractionWorkerLoop, this);
}

void AudioEngine::StopExtractionWorker() {
  if (!extraction_worker_running_.load(std::memory_order_acquire)) {
    return;
  }

  extraction_shutdown_.store(true, std::memory_order_release);
  CancelAllExtractions();
  extraction_cv_.notify_one();

  if (extraction_thread_.joinable()) {
    extraction_thread_.join();
  }
  extraction_worker_running_.store(false, std::memory_order_release);
}

void AudioEngine::ExtractionWorkerLoop() {
  while (true) {
    ExtractionTask task;
    {
      std::unique_lock<std::mutex> lock(extraction_mutex_);
      extraction_cv_.wait(lock, [this] {
        return extraction_shutdown_.load(std::memory_order_acquire) ||
               !extraction_queue_.empty();
      });

      if (extraction_queue_.empty()) {
        if (extraction_shutdown_.load(std::memory_order_acquire)) {
          break;
        }
        continue;
      }

      task = std::move(extraction_queue_.front());
      extraction_queue_.pop_front();
      current_extraction_job_id_ = task.job_id;
    }

    ExtractionResult result;
    result.track_id = task.track_id;
    result.output_path = task.output_path;

    const auto cancel_flag = task.cancel_flag;
    if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
      result.success = false;
      result.error_message = "Extraction cancelled";
    } else {
      auto progress_wrapper = [cancel_flag, &task](float progress) {
        if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
          return;
        }
        if (task.progress_callback) {
          task.progress_callback(progress);
        }
      };

      if (task.is_mix) {
        result = ExtractAllTracks(
            task.output_path, task.options, progress_wrapper,
            cancel_flag ? cancel_flag.get() : nullptr);
      } else {
        result = ExtractTrack(
            task.track_id, task.output_path, task.options, progress_wrapper,
            cancel_flag ? cancel_flag.get() : nullptr);
      }

      if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
        result.success = false;
        if (result.error_message.empty()) {
          result.error_message = "Extraction cancelled";
        }
      }
    }

    if (task.completion_callback) {
      task.completion_callback(task.job_id, result);
    }

    {
      std::lock_guard<std::mutex> lock(extraction_mutex_);
      extraction_cancel_flags_.erase(task.job_id);
      current_extraction_job_id_ = 0;
    }
  }
}

int64_t AudioEngine::NextExtractionJobId() {
  std::lock_guard<std::mutex> lock(extraction_mutex_);
  return next_extraction_job_id_++;
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

void AudioEngine::RecalculateDuration() {
  if (!timing_) {
    return;
  }

  int64_t max_end = 0;
  for (const auto& pair : tracks_) {
    if (!pair.second || !pair.second->IsLoaded()) {
      continue;
    }
    const int64_t start = pair.second->GetStartTimeSamples();
    const int64_t duration = pair.second->GetDuration();
    const int64_t end = start + std::max<int64_t>(0, duration);
    if (end > max_end) {
      max_end = end;
    }
  }

  timing_->SetDuration(max_end);
}

}  // namespace sezo
