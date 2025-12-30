#include "MicrophoneCapture.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>

#define LOG_TAG "MicrophoneCapture"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace recording {

MicrophoneCapture::MicrophoneCapture(int32_t sample_rate, int32_t channel_count)
    : sample_rate_(sample_rate), channel_count_(channel_count) {
  // Allocate circular buffer for ~2 seconds of audio
  size_t buffer_size = sample_rate * channel_count * 2;
  buffer_ = std::make_unique<core::CircularBuffer>(buffer_size);
}

MicrophoneCapture::~MicrophoneCapture() {
  Close();
}

bool MicrophoneCapture::Initialize() {
  oboe::AudioStreamBuilder builder;

  builder.setDirection(oboe::Direction::Input)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setSharingMode(oboe::SharingMode::Exclusive)
      ->setFormat(oboe::AudioFormat::Float)
      ->setChannelCount(channel_count_)
      ->setSampleRate(sample_rate_)
      ->setDataCallback(this);

  oboe::Result result = builder.openStream(stream_);
  if (result != oboe::Result::OK) {
    LOGE("Failed to create input stream: %s", oboe::convertToText(result));
    return false;
  }

  LOGD("Input stream opened: sample rate=%d, channels=%d, buffer size=%d",
       stream_->getSampleRate(),
       stream_->getChannelCount(),
       stream_->getBufferSizeInFrames());

  return true;
}

bool MicrophoneCapture::Start() {
  if (!stream_) {
    LOGE("Cannot start: stream not initialized");
    return false;
  }

  if (is_capturing_.load()) {
    LOGD("Already capturing");
    return true;
  }

  buffer_->Reset();

  oboe::Result result = stream_->start();
  if (result != oboe::Result::OK) {
    LOGE("Failed to start input stream: %s", oboe::convertToText(result));
    return false;
  }

  is_capturing_.store(true);
  LOGD("Microphone capture started");
  return true;
}

bool MicrophoneCapture::Stop() {
  if (!stream_) {
    return false;
  }

  is_capturing_.store(false);

  oboe::Result result = stream_->stop();
  if (result != oboe::Result::OK && result != oboe::Result::ErrorInvalidState) {
    LOGE("Failed to stop input stream: %s", oboe::convertToText(result));
    return false;
  }

  LOGD("Microphone capture stopped");
  return true;
}

void MicrophoneCapture::Close() {
  if (!stream_) {
    return;
  }

  is_capturing_.store(false);

  oboe::Result stop_result = stream_->stop();
  if (stop_result != oboe::Result::OK && stop_result != oboe::Result::ErrorInvalidState) {
    LOGE("Failed to stop stream during close: %s", oboe::convertToText(stop_result));
  }

  oboe::Result close_result = stream_->close();
  if (close_result != oboe::Result::OK) {
    LOGE("Failed to close input stream: %s", oboe::convertToText(close_result));
  }

  stream_.reset();
  LOGD("Microphone capture closed");
}

bool MicrophoneCapture::IsCapturing() const {
  return is_capturing_.load();
}

size_t MicrophoneCapture::ReadData(float* data, size_t frame_count) {
  size_t sample_count = frame_count * channel_count_;
  return buffer_->Read(data, sample_count) / channel_count_;
}

size_t MicrophoneCapture::GetAvailableFrames() const {
  return buffer_->Available() / channel_count_;
}

float MicrophoneCapture::GetInputLevel() const {
  return input_level_.load();
}

void MicrophoneCapture::SetVolume(float volume) {
  volume_.store(std::clamp(volume, 0.0f, 2.0f));
}

oboe::DataCallbackResult MicrophoneCapture::onAudioReady(
    oboe::AudioStream* audio_stream,
    void* audio_data,
    int32_t num_frames) {
  (void)audio_stream;

  if (!is_capturing_.load()) {
    return oboe::DataCallbackResult::Continue;
  }

  auto* input_buffer = static_cast<float*>(audio_data);
  size_t sample_count = num_frames * channel_count_;

  // Apply volume gain and calculate input level
  float volume = volume_.load();
  float peak = 0.0f;

  if (volume != 1.0f) {
    for (size_t i = 0; i < sample_count; ++i) {
      input_buffer[i] *= volume;
      peak = std::max(peak, std::abs(input_buffer[i]));
    }
  } else {
    for (size_t i = 0; i < sample_count; ++i) {
      peak = std::max(peak, std::abs(input_buffer[i]));
    }
  }

  input_level_.store(peak);

  // Write to circular buffer
  size_t written = buffer_->Write(input_buffer, sample_count);
  if (written < sample_count) {
    LOGD("Buffer overrun: wrote %zu / %zu samples", written, sample_count);
  }

  return oboe::DataCallbackResult::Continue;
}

}  // namespace recording
}  // namespace sezo
