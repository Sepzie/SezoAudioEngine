#include "MicrophoneCapture.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

#define LOG_TAG "MicrophoneCapture"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace recording {

MicrophoneCapture::MicrophoneCapture(int32_t sample_rate, int32_t channel_count)
    : sample_rate_(sample_rate), channel_count_(channel_count) {}

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

  const int32_t actual_sample_rate = stream_->getSampleRate();
  const int32_t actual_channels = stream_->getChannelCount();
  if (actual_sample_rate <= 0 || actual_channels <= 0) {
    LOGE("Invalid input stream config: sample rate=%d, channels=%d",
         actual_sample_rate, actual_channels);
    stream_.reset();
    return false;
  }

  sample_rate_ = actual_sample_rate;
  channel_count_ = actual_channels;

  // Allocate circular buffer for ~2 seconds of audio using actual stream config.
  const size_t buffer_size =
      static_cast<size_t>(sample_rate_) * static_cast<size_t>(channel_count_) * 2;
  buffer_ = std::make_unique<core::CircularBuffer>(buffer_size);
  monitor_buffer_ = std::make_unique<core::CircularBuffer>(buffer_size);

  LOGD("Input stream opened: sample rate=%d, channels=%d, buffer size=%d",
       stream_->getSampleRate(),
       stream_->getChannelCount(),
       stream_->getBufferSizeInFrames());

  return true;
}

bool MicrophoneCapture::Start() {
  if (!StartStream()) {
    return false;
  }
  SetRecordingSinkEnabled(true);
  return true;
}

bool MicrophoneCapture::Stop() {
  SetRecordingSinkEnabled(false);
  return StopStream();
}

bool MicrophoneCapture::StartStream() {
  if (!stream_) {
    LOGE("Cannot start: stream not initialized");
    return false;
  }
  if (!buffer_ || !monitor_buffer_) {
    LOGE("Cannot start: buffer not initialized");
    return false;
  }

  if (is_capturing_.load()) {
    LOGD("Already capturing");
    return true;
  }

  oboe::Result result = stream_->start();
  if (result != oboe::Result::OK) {
    LOGE("Failed to start input stream: %s", oboe::convertToText(result));
    return false;
  }

  is_capturing_.store(true);
  LOGD("Microphone capture started");
  return true;
}

bool MicrophoneCapture::StopStream() {
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

void MicrophoneCapture::WaitForCallbackOutsideSinks() const {
  // Pairs with the sequentially consistent store/load in onAudioReady: after a
  // sink flag is cleared, either the callback saw it cleared, or it had already
  // raised callback_in_sinks_ and we wait for it to finish that write.
  while (callback_in_sinks_.load(std::memory_order_seq_cst)) {
    std::this_thread::yield();
  }
}

void MicrophoneCapture::SetRecordingSinkEnabled(bool enabled) {
  if (!buffer_) {
    return;
  }
  if (enabled) {
    if (recording_sink_enabled_.load(std::memory_order_seq_cst)) {
      return;
    }
    // The callback does not touch a disabled sink and its reader has not
    // started yet, so the ring can be cleared here.
    buffer_->Reset();
    recording_sink_enabled_.store(true, std::memory_order_seq_cst);
  } else {
    recording_sink_enabled_.store(false, std::memory_order_seq_cst);
    WaitForCallbackOutsideSinks();
  }
}

void MicrophoneCapture::SetMonitorSinkEnabled(bool enabled) {
  if (!monitor_buffer_) {
    return;
  }
  if (enabled) {
    if (monitor_sink_enabled_.load(std::memory_order_seq_cst)) {
      return;
    }
    monitor_buffer_->Reset();
    monitor_sink_enabled_.store(true, std::memory_order_seq_cst);
  } else {
    monitor_sink_enabled_.store(false, std::memory_order_seq_cst);
    WaitForCallbackOutsideSinks();
  }
}

bool MicrophoneCapture::IsRecordingSinkEnabled() const {
  return recording_sink_enabled_.load();
}

bool MicrophoneCapture::IsMonitorSinkEnabled() const {
  return monitor_sink_enabled_.load();
}

size_t MicrophoneCapture::ReadMonitorData(float* data, size_t frame_count) {
  if (!monitor_buffer_) {
    return 0;
  }
  const size_t channels = static_cast<size_t>(channel_count_);
  return monitor_buffer_->Read(data, frame_count * channels) / channels;
}

size_t MicrophoneCapture::GetMonitorAvailableFrames() const {
  if (!monitor_buffer_) {
    return 0;
  }
  return monitor_buffer_->Available() / static_cast<size_t>(channel_count_);
}

void MicrophoneCapture::Close() {
  if (!stream_) {
    return;
  }

  recording_sink_enabled_.store(false, std::memory_order_seq_cst);
  monitor_sink_enabled_.store(false, std::memory_order_seq_cst);
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
  if (!buffer_) {
    return 0;
  }
  size_t sample_count = frame_count * channel_count_;
  return buffer_->Read(data, sample_count) / channel_count_;
}

size_t MicrophoneCapture::GetAvailableFrames() const {
  if (!buffer_) {
    return 0;
  }
  return buffer_->Available() / channel_count_;
}

float MicrophoneCapture::GetInputLevel() const {
  return input_level_.load();
}

int32_t MicrophoneCapture::GetSampleRate() const {
  return sample_rate_;
}

int32_t MicrophoneCapture::GetChannelCount() const {
  return channel_count_;
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
  const size_t sample_count = static_cast<size_t>(num_frames) * static_cast<size_t>(channel_count_);

  callback_in_sinks_.store(true, std::memory_order_seq_cst);

  // Monitor sink: raw input, before the recording gain.
  if (monitor_sink_enabled_.load(std::memory_order_seq_cst)) {
    const size_t written = monitor_buffer_->Write(input_buffer, sample_count);
    if (written < sample_count) {
      LOGD("Monitor buffer overrun: wrote %zu / %zu samples", written, sample_count);
    }
  }

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

  // Recording sink
  if (recording_sink_enabled_.load(std::memory_order_seq_cst)) {
    size_t written = buffer_->Write(input_buffer, sample_count);
    if (written < sample_count) {
      LOGD("Buffer overrun: wrote %zu / %zu samples", written, sample_count);
    }
  }

  callback_in_sinks_.store(false, std::memory_order_seq_cst);

  return oboe::DataCallbackResult::Continue;
}

CaptureRecordingSource::CaptureRecordingSource(std::shared_ptr<MicrophoneCapture> capture)
    : capture_(std::move(capture)) {}

CaptureRecordingSource::~CaptureRecordingSource() {
  if (capture_) {
    capture_->SetRecordingSinkEnabled(false);
  }
}

bool CaptureRecordingSource::Start() {
  if (!capture_ || !capture_->IsCapturing()) {
    LOGE("Cannot start recording sink: capture stream not running");
    return false;
  }
  capture_->SetRecordingSinkEnabled(true);
  return true;
}

bool CaptureRecordingSource::Stop() {
  if (!capture_) {
    return false;
  }
  capture_->SetRecordingSinkEnabled(false);
  return true;
}

int32_t CaptureRecordingSource::GetSampleRate() const {
  return capture_ ? capture_->GetSampleRate() : 0;
}

int32_t CaptureRecordingSource::GetChannelCount() const {
  return capture_ ? capture_->GetChannelCount() : 0;
}

size_t CaptureRecordingSource::GetAvailableFrames() const {
  return capture_ ? capture_->GetAvailableFrames() : 0;
}

size_t CaptureRecordingSource::ReadData(float* data, size_t frame_count) {
  return capture_ ? capture_->ReadData(data, frame_count) : 0;
}

float CaptureRecordingSource::GetInputLevel() const {
  return capture_ ? capture_->GetInputLevel() : 0.0f;
}

void CaptureRecordingSource::SetVolume(float volume) {
  if (capture_) {
    capture_->SetVolume(volume);
  }
}

}  // namespace recording
}  // namespace sezo
