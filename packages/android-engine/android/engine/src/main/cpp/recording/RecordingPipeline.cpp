#include "RecordingPipeline.h"

#include "audio/MP3Encoder.h"
#include "audio/WAVEncoder.h"

#if defined(__ANDROID__)
#include "MicrophoneCapture.h"
#include "audio/AACEncoder.h"
#include "audio/M4AEncoder.h"
#endif

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <vector>

#define LOG_TAG "RecordingPipeline"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace recording {

namespace {

// Frames read from the source per iteration. Small enough that the encoder
// sees data every few tens of milliseconds, large enough to keep the encoder
// call overhead low (4096 frames at 48 kHz is ~85 ms of audio).
constexpr size_t kFramesPerRead = 4096;

// Idle sleep while the source has nothing buffered.
constexpr auto kIdleSleep = std::chrono::milliseconds(5);

}  // namespace

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
#if defined(__ANDROID__)
  if (is_recording_.load()) {
    LOGE("Already recording");
    return false;
  }

  auto microphone = std::make_unique<MicrophoneCapture>(config.sample_rate, config.channels);
  if (!microphone->Initialize()) {
    LOGE("Failed to initialize microphone capture");
    return false;
  }

  return StartRecording(std::move(microphone), output_path, config, std::move(callback));
#else
  (void)output_path;
  (void)config;
  (void)callback;
  LOGE("Microphone capture is only available on Android");
  return false;
#endif
}

bool RecordingPipeline::StartRecording(
    std::unique_ptr<AudioSource> source,
    const std::string& output_path,
    const RecordingConfig& config,
    RecordingCallback callback) {
  if (is_recording_.load()) {
    LOGE("Already recording");
    return false;
  }

  if (!source) {
    LOGE("No audio source");
    return false;
  }

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  const int32_t actual_sample_rate = source->GetSampleRate();
  const int32_t actual_channels = source->GetChannelCount();
  if (actual_sample_rate <= 0 || actual_channels <= 0) {
    LOGE("Invalid source config: %d Hz, %d ch", actual_sample_rate, actual_channels);
    return false;
  }

  output_path_ = output_path;
  config_ = config;
  config_.sample_rate = actual_sample_rate;
  config_.channels = actual_channels;
  callback_ = std::move(callback);
  source_ = std::move(source);

  if (!OpenEncoder(output_path)) {
    source_.reset();
    return false;
  }

  recorded_samples_.store(0);
  encode_failed_.store(false);

  if (!source_->Start()) {
    LOGE("Failed to start audio source");
    encoder_->Close();
    encoder_.reset();
    source_.reset();
    return false;
  }

  worker_shutdown_.store(false);
  is_recording_.store(true);
  worker_thread_ = std::thread(&RecordingPipeline::RecordingWorkerLoop, this);
  LOGD("Recording started: %s, %d Hz, %d channels, format=%s",
       output_path.c_str(),
       config_.sample_rate,
       config_.channels,
       config_.format.c_str());

  return true;
}

bool RecordingPipeline::OpenEncoder(const std::string& output_path) {
  audio::EncoderConfig encoder_config;
  encoder_config.sample_rate = config_.sample_rate;
  encoder_config.channels = config_.channels;
  encoder_config.bitrate = config_.bitrate;
  encoder_config.bits_per_sample = config_.bits_per_sample;

  if (config_.format == "aac") {
#if defined(__ANDROID__)
    encoder_ = std::make_unique<audio::AACEncoder>();
    encoder_config.format = audio::EncoderFormat::kAAC;
#else
    LOGE("AAC encoding requires Android MediaCodec");
    return false;
#endif
  } else if (config_.format == "m4a") {
#if defined(__ANDROID__)
    encoder_ = std::make_unique<audio::M4AEncoder>();
    encoder_config.format = audio::EncoderFormat::kM4A;
#else
    LOGE("M4A encoding requires Android MediaCodec");
    return false;
#endif
  } else if (config_.format == "mp3") {
    encoder_ = std::make_unique<audio::MP3Encoder>();
    encoder_config.format = audio::EncoderFormat::kMP3;
  } else if (config_.format == "wav") {
    encoder_ = std::make_unique<audio::WAVEncoder>();
    encoder_config.format = audio::EncoderFormat::kWAV;
  } else {
    LOGE("Unsupported format: %s", config_.format.c_str());
    return false;
  }

  if (!encoder_->Open(output_path, encoder_config)) {
    LOGE("Failed to open encoder");
    encoder_.reset();
    return false;
  }

  return true;
}

RecordingResult RecordingPipeline::StopRecording() {
  if (!is_recording_.load()) {
    LOGD("Not recording");
    RecordingResult result;
    result.success = false;
    result.error_message = "Not recording";
    return result;
  }

  const auto stop_begin = std::chrono::steady_clock::now();
  is_recording_.store(false);

  // Order matters: stop the source first so nothing new arrives, then let the
  // worker drain what is still buffered before it exits.
  if (source_) {
    source_->Stop();
  }
  StopWorker();

  RecordingResult result;
  result.output_path = output_path_;
  result.duration_samples = recorded_samples_.load();
  result.sample_rate = config_.sample_rate;
  result.channels = config_.channels;

  if (!encoder_) {
    result.success = false;
    result.error_message = "Encoder unavailable";
  } else if (encode_failed_.load()) {
    result.success = false;
    result.error_message = "Failed to write audio data";
    encoder_->Close();
  } else if (result.duration_samples == 0) {
    result.success = false;
    result.error_message = "No audio data recorded";
    encoder_->Close();
  } else if (!encoder_->Close()) {
    result.success = false;
    result.error_message = "Failed to close encoder";
  } else {
    result.success = true;
    result.file_size = encoder_->GetFileSize();
  }

  encoder_.reset();
  source_.reset();

  if (callback_) {
    callback_(result);
  }

  const auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - stop_begin)
                           .count();
  LOGD("Recording stopped in %lld ms: %lld frames, %lld bytes",
       static_cast<long long>(stop_ms),
       static_cast<long long>(result.duration_samples),
       static_cast<long long>(result.file_size));

  return result;
}

bool RecordingPipeline::IsRecording() const {
  return is_recording_.load();
}

float RecordingPipeline::GetInputLevel() const {
  if (source_) {
    return source_->GetInputLevel();
  }
  return 0.0f;
}

void RecordingPipeline::SetVolume(float volume) {
  if (source_) {
    source_->SetVolume(volume);
  }
}

int64_t RecordingPipeline::GetRecordedSamples() const {
  return recorded_samples_.load();
}

void RecordingPipeline::RecordingWorkerLoop() {
  LOGD("Recording worker started");

  const size_t channels = static_cast<size_t>(config_.channels);
  std::vector<float> read_buffer(kFramesPerRead * channels);

  while (true) {
    // Read the shutdown flag before checking the source: once shutdown is
    // requested the source has already been stopped, so an empty source seen
    // afterwards means it is fully drained.
    const bool shutdown = worker_shutdown_.load();
    const size_t frames_available = source_->GetAvailableFrames();

    if (frames_available == 0) {
      if (shutdown) {
        break;
      }
      std::this_thread::sleep_for(kIdleSleep);
      continue;
    }

    const size_t frames_to_read = std::min(frames_available, kFramesPerRead);
    const size_t frames_read = source_->ReadData(read_buffer.data(), frames_to_read);
    if (frames_read == 0) {
      if (shutdown) {
        break;
      }
      continue;
    }

    if (encode_failed_.load()) {
      // Keep draining so the source does not overrun, but stop writing.
      continue;
    }

    if (!encoder_->Write(read_buffer.data(), frames_read)) {
      LOGE("Failed to write audio data to encoder");
      encode_failed_.store(true);
      continue;
    }

    recorded_samples_.fetch_add(static_cast<int64_t>(frames_read));
  }

  LOGD("Recording worker stopped");
}

void RecordingPipeline::StopWorker() {
  worker_shutdown_.store(true);

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

}  // namespace recording
}  // namespace sezo
