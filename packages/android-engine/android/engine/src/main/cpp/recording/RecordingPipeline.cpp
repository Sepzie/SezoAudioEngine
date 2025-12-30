#include "RecordingPipeline.h"
#include <android/log.h>
#include <algorithm>
#include <chrono>

#define LOG_TAG "RecordingPipeline"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace recording {

RecordingPipeline::RecordingPipeline() = default;

RecordingPipeline::~RecordingPipeline() {
  if (is_recording_.load()) {
    StopRecording();
  }
  StopWorker();
}

bool RecordingPipeline::StartRecording(
    const std::string& output_path,
    const RecordingConfig& config,
    RecordingCallback callback) {

  if (is_recording_.load()) {
    LOGE("Already recording");
    return false;
  }

  output_path_ = output_path;
  config_ = config;
  callback_ = callback;

  // Create microphone capture
  microphone_ = std::make_unique<MicrophoneCapture>(config.sample_rate, config.channels);
  if (!microphone_->Initialize()) {
    LOGE("Failed to initialize microphone capture");
    return false;
  }

  // Create encoder based on format
  if (config.format == "aac") {
    encoder_ = std::make_unique<audio::AACEncoder>();
  } else if (config.format == "mp3") {
    encoder_ = std::make_unique<audio::MP3Encoder>();
  } else if (config.format == "wav") {
    encoder_ = std::make_unique<audio::WAVEncoder>();
  } else {
    LOGE("Unsupported format: %s", config.format.c_str());
    return false;
  }

  // Initialize encoder
  if (!encoder_->Initialize(output_path, config.sample_rate, config.channels,
                            config.bitrate, config.bits_per_sample)) {
    LOGE("Failed to initialize encoder");
    return false;
  }

  // Clear recording buffer
  recording_buffer_.clear();
  recorded_samples_.store(0);

  // Start microphone
  if (!microphone_->Start()) {
    LOGE("Failed to start microphone");
    encoder_->Finalize();
    encoder_.reset();
    return false;
  }

  // Start worker thread
  worker_shutdown_.store(false);
  worker_running_.store(true);
  worker_thread_ = std::thread(&RecordingPipeline::RecordingWorkerLoop, this);

  is_recording_.store(true);
  LOGD("Recording started: %s, %d Hz, %d channels, format=%s",
       output_path.c_str(), config.sample_rate, config.channels, config.format.c_str());

  return true;
}

RecordingResult RecordingPipeline::StopRecording() {
  if (!is_recording_.load()) {
    LOGD("Not recording");
    return RecordingResult{false, "", 0, 0, "Not recording"};
  }

  is_recording_.store(false);

  // Stop microphone capture
  if (microphone_) {
    microphone_->Stop();
  }

  // Stop worker thread
  StopWorker();

  // Encode final recording
  RecordingResult result = EncodeRecording();

  // Cleanup
  if (microphone_) {
    microphone_->Close();
    microphone_.reset();
  }

  if (encoder_) {
    encoder_.reset();
  }

  recording_buffer_.clear();

  // Call completion callback
  if (callback_) {
    callback_(result);
  }

  LOGD("Recording stopped: %lld samples, %lld bytes",
       (long long)result.duration_samples, (long long)result.file_size);

  return result;
}

bool RecordingPipeline::IsRecording() const {
  return is_recording_.load();
}

float RecordingPipeline::GetInputLevel() const {
  if (microphone_) {
    return microphone_->GetInputLevel();
  }
  return 0.0f;
}

void RecordingPipeline::SetVolume(float volume) {
  if (microphone_) {
    microphone_->SetVolume(volume);
  }
}

int64_t RecordingPipeline::GetRecordedSamples() const {
  return recorded_samples_.load();
}

void RecordingPipeline::RecordingWorkerLoop() {
  LOGD("Recording worker started");

  const size_t kFramesPerRead = 4096;
  std::vector<float> read_buffer(kFramesPerRead * config_.channels);

  while (!worker_shutdown_.load() && is_recording_.load()) {
    // Read from microphone
    size_t frames_available = microphone_->GetAvailableFrames();

    if (frames_available > 0) {
      size_t frames_to_read = std::min(frames_available, kFramesPerRead);
      size_t frames_read = microphone_->ReadData(read_buffer.data(), frames_to_read);

      if (frames_read > 0) {
        size_t samples = frames_read * config_.channels;

        // Append to recording buffer
        recording_buffer_.insert(recording_buffer_.end(),
                                read_buffer.begin(),
                                read_buffer.begin() + samples);

        recorded_samples_.fetch_add(frames_read);
      }
    } else {
      // No data available, sleep briefly
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  LOGD("Recording worker stopped");
  worker_running_.store(false);
}

void RecordingPipeline::StopWorker() {
  if (worker_running_.load()) {
    worker_shutdown_.store(true);

    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
  }
}

RecordingResult RecordingPipeline::EncodeRecording() {
  RecordingResult result;
  result.output_path = output_path_;
  result.duration_samples = recorded_samples_.load();

  if (recording_buffer_.empty()) {
    result.success = false;
    result.error_message = "No audio data recorded";
    LOGE("No audio data to encode");
    return result;
  }

  LOGD("Encoding %zu samples to %s", recording_buffer_.size(), output_path_.c_str());

  // Write audio data to encoder
  if (!encoder_->Write(recording_buffer_.data(), recording_buffer_.size())) {
    result.success = false;
    result.error_message = "Failed to write audio data";
    LOGE("Failed to write audio data to encoder");
    encoder_->Finalize();
    return result;
  }

  // Finalize encoding
  if (!encoder_->Finalize()) {
    result.success = false;
    result.error_message = "Failed to finalize encoding";
    LOGE("Failed to finalize encoder");
    return result;
  }

  // Get file size
  FILE* file = fopen(output_path_.c_str(), "rb");
  if (file) {
    fseek(file, 0, SEEK_END);
    result.file_size = ftell(file);
    fclose(file);
  }

  result.success = true;
  LOGD("Encoding complete: %lld bytes", (long long)result.file_size);

  return result;
}

}  // namespace recording
}  // namespace sezo
