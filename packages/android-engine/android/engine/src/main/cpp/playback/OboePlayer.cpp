#include "OboePlayer.h"
#include <android/log.h>

#define LOG_TAG "OboePlayer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace playback {

OboePlayer::OboePlayer(std::shared_ptr<MultiTrackMixer> mixer,
                       std::shared_ptr<core::MasterClock> clock,
                       std::shared_ptr<core::TransportController> transport)
    : mixer_(mixer), clock_(clock), transport_(transport) {}

OboePlayer::~OboePlayer() {
  Close();
}

bool OboePlayer::OpenStream(oboe::SharingMode sharing_mode) {
  oboe::AudioStreamBuilder builder;

  builder.setDirection(oboe::Direction::Output)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setSharingMode(sharing_mode)
      ->setFormat(oboe::AudioFormat::Float)
      ->setChannelCount(oboe::ChannelCount::Stereo)
      ->setSampleRate(sample_rate_)
      ->setDataCallback(this)
      ->setErrorCallback(this);

  oboe::Result result = builder.openStream(stream_);
  if (result != oboe::Result::OK) {
    LOGE("Failed to create stream (sharing=%s): %s",
         sharing_mode == oboe::SharingMode::Exclusive ? "Exclusive" : "Shared",
         oboe::convertToText(result));
    return false;
  }

  LOGD("Stream opened: sample rate=%d, buffer size=%d, frames per burst=%d, sharing=%s",
       stream_->getSampleRate(),
       stream_->getBufferSizeInFrames(),
       stream_->getFramesPerBurst(),
       stream_->getSharingMode() == oboe::SharingMode::Exclusive ? "Exclusive" : "Shared");

  return true;
}

bool OboePlayer::Initialize(int32_t sample_rate) {
  sample_rate_ = sample_rate;

  // Try Exclusive mode first for lowest latency, fall back to Shared
  if (!OpenStream(oboe::SharingMode::Exclusive)) {
    LOGW("Exclusive mode failed, falling back to Shared mode");
    if (!OpenStream(oboe::SharingMode::Shared)) {
      return false;
    }
  }

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

bool OboePlayer::IsHealthy() const {
  if (stream_recovering_.load(std::memory_order_acquire)) {
    return false;
  }
  if (!stream_) {
    return false;
  }
  auto state = stream_->getState();
  // Healthy means the stream exists and is in a usable state
  return state == oboe::StreamState::Started ||
         state == oboe::StreamState::Open ||
         state == oboe::StreamState::Stopped ||
         state == oboe::StreamState::Paused;
}

bool OboePlayer::RestartStream() {
  // Prevent concurrent recovery attempts
  bool expected = false;
  if (!stream_recovering_.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
    LOGW("Stream recovery already in progress");
    return false;
  }

  LOGD("Restarting audio stream...");

  bool was_playing = transport_->IsPlaying();

  // Close existing stream
  if (stream_) {
    stream_->stop();
    stream_->close();
    stream_.reset();
  }

  // Try Exclusive first, fall back to Shared
  bool opened = OpenStream(oboe::SharingMode::Exclusive);
  if (!opened) {
    LOGW("Exclusive mode failed during restart, trying Shared mode");
    opened = OpenStream(oboe::SharingMode::Shared);
  }

  if (!opened) {
    LOGE("Failed to restart audio stream");
    stream_recovering_.store(false, std::memory_order_release);
    return false;
  }

  // Restart playback if it was playing before
  if (was_playing) {
    if (!Start()) {
      LOGE("Failed to start stream after restart");
      stream_recovering_.store(false, std::memory_order_release);
      return false;
    }
  }

  stream_recovering_.store(false, std::memory_order_release);
  LOGD("Audio stream restarted successfully");
  return true;
}

void OboePlayer::SetStreamErrorCallback(StreamErrorCallback callback) {
  error_callback_ = std::move(callback);
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

  const int64_t timeline_start = clock_->GetPosition();

  // Mix all tracks
  mixer_->Mix(output_buffer, num_frames, timeline_start);

  // Advance master clock
  clock_->Advance(num_frames);

  return oboe::DataCallbackResult::Continue;
}

void OboePlayer::onErrorBeforeClose(oboe::AudioStream* audio_stream,
                                     oboe::Result error) {
  (void)audio_stream;
  LOGE("Stream error before close: %s", oboe::convertToText(error));
  was_playing_before_error_.store(transport_->IsPlaying(),
                                   std::memory_order_release);
}

void OboePlayer::onErrorAfterClose(oboe::AudioStream* audio_stream,
                                    oboe::Result error) {
  (void)audio_stream;
  LOGE("Stream error after close: %s, attempting recovery...",
       oboe::convertToText(error));

  // The stream has been closed by Oboe. Reset our pointer so Close()/RestartStream()
  // don't try to close it again.
  stream_.reset();

  // Prevent concurrent recovery
  bool expected = false;
  if (!stream_recovering_.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
    LOGW("Recovery already in progress from another path");
    return;
  }

  bool was_playing = was_playing_before_error_.load(std::memory_order_acquire);

  // Try Exclusive first, fall back to Shared
  bool opened = OpenStream(oboe::SharingMode::Exclusive);
  if (!opened) {
    LOGW("Exclusive mode failed during error recovery, trying Shared mode");
    opened = OpenStream(oboe::SharingMode::Shared);
  }

  if (!opened) {
    LOGE("Stream recovery failed - could not reopen stream");
    stream_recovering_.store(false, std::memory_order_release);
    if (error_callback_) {
      error_callback_("Audio stream disconnected and recovery failed");
    }
    return;
  }

  // Restart playback if it was active before the error
  if (was_playing) {
    oboe::Result start_result = stream_->start();
    if (start_result != oboe::Result::OK) {
      LOGE("Stream recovery: failed to restart playback: %s",
           oboe::convertToText(start_result));
      stream_recovering_.store(false, std::memory_order_release);
      if (error_callback_) {
        error_callback_("Audio stream recovered but failed to restart playback");
      }
      return;
    }
    LOGD("Stream recovery: playback restarted successfully");
  }

  stream_recovering_.store(false, std::memory_order_release);
  LOGD("Stream recovery completed successfully");
}

}  // namespace playback
}  // namespace sezo
