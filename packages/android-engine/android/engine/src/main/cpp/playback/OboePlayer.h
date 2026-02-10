#pragma once

#include "MultiTrackMixer.h"
#include "core/MasterClock.h"
#include "core/TransportController.h"

#include <oboe/Oboe.h>

#include <atomic>
#include <functional>
#include <memory>

namespace sezo {
namespace playback {

/**
 * Oboe-based audio player with callback implementation.
 * Handles stream error recovery and automatic reconnection.
 */
class OboePlayer : public oboe::AudioStreamDataCallback,
                   public oboe::AudioStreamErrorCallback {
 public:
  using StreamErrorCallback = std::function<void(const std::string& message)>;

  /**
   * Constructor.
   * @param mixer Multi-track mixer
   * @param clock Master clock
   * @param transport Transport controller
   */
  OboePlayer(std::shared_ptr<MultiTrackMixer> mixer,
             std::shared_ptr<core::MasterClock> clock,
             std::shared_ptr<core::TransportController> transport);
  ~OboePlayer() override;

  /**
   * Initialize the audio stream.
   * @param sample_rate Desired sample rate
   * @return true if successful
   */
  bool Initialize(int32_t sample_rate);

  /**
   * Start the audio stream.
   * @return true if successful
   */
  bool Start();

  /**
   * Stop the audio stream.
   * @return true if successful
   */
  bool Stop();

  /**
   * Close the audio stream and release resources.
   */
  void Close();

  /**
   * Check if the stream is running.
   * @return true if running
   */
  bool IsRunning() const;

  /**
   * Check if the stream is in a healthy/usable state.
   * Unlike IsRunning(), this returns false for disconnected or errored streams.
   * @return true if the stream is healthy
   */
  bool IsHealthy() const;

  /**
   * Attempt to restart the audio stream after a disconnect or error.
   * Closes the old stream and reopens with the same parameters.
   * @return true if restart was successful
   */
  bool RestartStream();

  /**
   * Set callback for unrecoverable stream errors.
   */
  void SetStreamErrorCallback(StreamErrorCallback callback);

  /**
   * Oboe audio data callback.
   */
  oboe::DataCallbackResult onAudioReady(
      oboe::AudioStream* audio_stream,
      void* audio_data,
      int32_t num_frames) override;

  /**
   * Called when a stream error occurs, before the stream is closed.
   */
  void onErrorBeforeClose(oboe::AudioStream* audio_stream,
                          oboe::Result error) override;

  /**
   * Called when a stream error occurs, after the stream is closed.
   * Attempts automatic stream recovery.
   */
  void onErrorAfterClose(oboe::AudioStream* audio_stream,
                         oboe::Result error) override;

 private:
  bool OpenStream(oboe::SharingMode sharing_mode);

  std::shared_ptr<MultiTrackMixer> mixer_;
  std::shared_ptr<core::MasterClock> clock_;
  std::shared_ptr<core::TransportController> transport_;
  std::shared_ptr<oboe::AudioStream> stream_;

  int32_t sample_rate_ = 0;
  std::atomic<bool> stream_recovering_{false};
  std::atomic<bool> was_playing_before_error_{false};

  StreamErrorCallback error_callback_;
};

}  // namespace playback
}  // namespace sezo
