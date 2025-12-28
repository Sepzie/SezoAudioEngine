#include "OboePlayer.h"
#include <android/log.h>

#define LOG_TAG "OboePlayer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace playback {

OboePlayer::OboePlayer(std::shared_ptr<MultiTrackMixer> mixer,
                       std::shared_ptr<core::MasterClock> clock,
                       std::shared_ptr<core::TransportController> transport)
    : mixer_(mixer), clock_(clock), transport_(transport) {}

OboePlayer::~OboePlayer() {
  Close();
}

bool OboePlayer::Initialize(int32_t sample_rate) {
  oboe::AudioStreamBuilder builder;

  builder.setDirection(oboe::Direction::Output)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setSharingMode(oboe::SharingMode::Exclusive)
      ->setFormat(oboe::AudioFormat::Float)
      ->setChannelCount(oboe::ChannelCount::Stereo)
      ->setSampleRate(sample_rate)
      ->setDataCallback(this);

  oboe::Result result = builder.openStream(stream_);
  if (result != oboe::Result::OK) {
    LOGE("Failed to create stream: %s", oboe::convertToText(result));
    return false;
  }

  LOGD("Stream opened: sample rate=%d, buffer size=%d, frames per burst=%d",
       stream_->getSampleRate(),
       stream_->getBufferSizeInFrames(),
       stream_->getFramesPerBurst());

  return true;
}

bool OboePlayer::Start() {
  if (!stream_) {
    return false;
  }

  if (stream_->getState() == oboe::StreamState::Started) {
    LOGD("Stream already started");
    return true;
  }

  oboe::Result result = stream_->start();
  if (result != oboe::Result::OK) {
    LOGE("Failed to start stream: %s", oboe::convertToText(result));
    return false;
  }

  LOGD("Stream started");
  return true;
}

bool OboePlayer::Stop() {
  if (!stream_) {
    return false;
  }

  oboe::Result result = stream_->stop();
  if (result != oboe::Result::OK && result != oboe::Result::ErrorInvalidState) {
    LOGE("Failed to stop stream: %s", oboe::convertToText(result));
    return false;
  }

  LOGD("Stream stopped");
  return true;
}

void OboePlayer::Close() {
  if (!stream_) {
    return;
  }

  oboe::Result stop_result = stream_->stop();
  if (stop_result != oboe::Result::OK && stop_result != oboe::Result::ErrorInvalidState) {
    LOGE("Failed to stop stream during close: %s", oboe::convertToText(stop_result));
  }

  oboe::Result close_result = stream_->close();
  if (close_result != oboe::Result::OK) {
    LOGE("Failed to close stream: %s", oboe::convertToText(close_result));
  }

  stream_.reset();
  LOGD("Stream closed");
}

bool OboePlayer::IsRunning() const {
  if (!stream_) {
    return false;
  }
  return stream_->getState() == oboe::StreamState::Started;
}

oboe::DataCallbackResult OboePlayer::onAudioReady(
    oboe::AudioStream* audio_stream,
    void* audio_data,
    int32_t num_frames) {
  (void)audio_stream;
  auto* output_buffer = static_cast<float*>(audio_data);

  // Check if we should be playing
  if (!transport_->IsPlaying()) {
    // Fill with silence
    std::fill_n(output_buffer, num_frames * 2, 0.0f);
    return oboe::DataCallbackResult::Continue;
  }

  // Mix all tracks
  mixer_->Mix(output_buffer, num_frames);

  // Advance master clock
  clock_->Advance(num_frames);

  return oboe::DataCallbackResult::Continue;
}

}  // namespace playback
}  // namespace sezo
