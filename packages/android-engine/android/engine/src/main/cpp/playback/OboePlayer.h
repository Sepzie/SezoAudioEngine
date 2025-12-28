#pragma once

#include "MultiTrackMixer.h"
#include "core/MasterClock.h"
#include "core/TransportController.h"

#include <oboe/Oboe.h>

#include <memory>

namespace sezo {
namespace playback {

/**
 * Oboe-based audio player with callback implementation.
 */
class OboePlayer : public oboe::AudioStreamDataCallback {
 public:
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
   * Oboe audio callback.
   */
  oboe::DataCallbackResult onAudioReady(
      oboe::AudioStream* audio_stream,
      void* audio_data,
      int32_t num_frames) override;

 private:
  std::shared_ptr<MultiTrackMixer> mixer_;
  std::shared_ptr<core::MasterClock> clock_;
  std::shared_ptr<core::TransportController> transport_;
  std::shared_ptr<oboe::AudioStream> stream_;
};

}  // namespace playback
}  // namespace sezo
