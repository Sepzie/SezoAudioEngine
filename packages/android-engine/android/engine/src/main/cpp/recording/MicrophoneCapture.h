#pragma once

#include "core/CircularBuffer.h"

#include <oboe/Oboe.h>

#include <atomic>
#include <memory>

namespace sezo {
namespace recording {

/**
 * Captures audio from microphone using Oboe.
 * Implements a duplex stream callback for synchronized recording.
 */
class MicrophoneCapture : public oboe::AudioStreamDataCallback {
 public:
  /**
   * Constructor.
   * @param sample_rate Recording sample rate
   * @param channel_count Number of channels (1 = mono, 2 = stereo)
   */
  MicrophoneCapture(int32_t sample_rate, int32_t channel_count);
  ~MicrophoneCapture() override;

  /**
   * Initialize the microphone capture stream.
   * @return true if successful
   */
  bool Initialize();

  /**
   * Start capturing audio.
   * @return true if successful
   */
  bool Start();

  /**
   * Stop capturing audio.
   * @return true if successful
   */
  bool Stop();

  /**
   * Close the stream and release resources.
   */
  void Close();

  /**
   * Check if capture is active.
   * @return true if capturing
   */
  bool IsCapturing() const;

  /**
   * Read captured audio data from the internal buffer.
   * @param data Destination buffer
   * @param frame_count Number of frames to read
   * @return Number of frames actually read
   */
  size_t ReadData(float* data, size_t frame_count);

  /**
   * Get the number of available frames in the buffer.
   * @return Available frame count
   */
  size_t GetAvailableFrames() const;

  /**
   * Get current input level (peak amplitude).
   * @return Peak level (0.0 to 1.0)
   */
  float GetInputLevel() const;

  /**
   * Get the actual sample rate of the input stream.
   */
  int32_t GetSampleRate() const;

  /**
   * Get the actual channel count of the input stream.
   */
  int32_t GetChannelCount() const;

  /**
   * Set recording volume/gain.
   * @param volume Volume multiplier (0.0 to 2.0)
   */
  void SetVolume(float volume);

  /**
   * Oboe audio callback for input stream.
   */
  oboe::DataCallbackResult onAudioReady(
      oboe::AudioStream* audio_stream,
      void* audio_data,
      int32_t num_frames) override;

 private:
  int32_t sample_rate_;
  int32_t channel_count_;
  std::shared_ptr<oboe::AudioStream> stream_;

  // Circular buffer for captured audio (holds ~2 seconds of audio)
  std::unique_ptr<core::CircularBuffer> buffer_;

  std::atomic<float> volume_{1.0f};
  std::atomic<float> input_level_{0.0f};
  std::atomic<bool> is_capturing_{false};
};

}  // namespace recording
}  // namespace sezo
