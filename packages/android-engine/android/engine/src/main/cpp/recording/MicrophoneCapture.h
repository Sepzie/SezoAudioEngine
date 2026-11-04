#pragma once

#include "AudioSource.h"
#include "core/CircularBuffer.h"

#include <oboe/Oboe.h>

#include <atomic>
#include <memory>

namespace sezo {
namespace recording {

/**
 * Captures audio from microphone using Oboe.
 *
 * One input stream feeds two independent sinks, each a single-producer /
 * single-consumer ring buffer written on the Oboe callback:
 * - the recording sink (gain applied), read through the AudioSource interface
 *   or through CaptureRecordingSource;
 * - the monitor sink (raw input), read by the input pitch monitor.
 * The stream runs while either consumer needs it; each sink is switched on and
 * off on its own, so a recording can start and stop while the monitor keeps
 * running and the other way round. The callback only copies into the enabled
 * rings and measures the peak level.
 */
class MicrophoneCapture : public AudioSource, public oboe::AudioStreamDataCallback {
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
   * AudioSource: start the stream and the recording sink.
   * @return true if successful
   */
  bool Start() override;

  /**
   * AudioSource: stop the recording sink and the stream.
   * @return true if successful
   */
  bool Stop() override;

  /** Start the input stream without enabling any sink. */
  bool StartStream();

  /** Stop the input stream. Sinks keep their state. */
  bool StopStream();

  /**
   * Switch the recording sink. Enabling clears its ring; disabling returns once
   * the callback can no longer write to it, so the ring then only drains.
   */
  void SetRecordingSinkEnabled(bool enabled);

  /** Same as SetRecordingSinkEnabled, for the monitor sink. */
  void SetMonitorSinkEnabled(bool enabled);

  bool IsRecordingSinkEnabled() const;
  bool IsMonitorSinkEnabled() const;

  /** Read raw interleaved frames from the monitor sink. */
  size_t ReadMonitorData(float* data, size_t frame_count);

  /** Frames buffered in the monitor sink. */
  size_t GetMonitorAvailableFrames() const;

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
  size_t ReadData(float* data, size_t frame_count) override;

  /**
   * Get the number of available frames in the buffer.
   * @return Available frame count
   */
  size_t GetAvailableFrames() const override;

  /**
   * Get current input level (peak amplitude).
   * @return Peak level (0.0 to 1.0)
   */
  float GetInputLevel() const override;

  /**
   * Get the actual sample rate of the input stream.
   */
  int32_t GetSampleRate() const override;

  /**
   * Get the actual channel count of the input stream.
   */
  int32_t GetChannelCount() const override;

  /**
   * Set recording volume/gain.
   * @param volume Volume multiplier (0.0 to 2.0)
   */
  void SetVolume(float volume) override;

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

  // Waits until the callback is outside its sink writes.
  void WaitForCallbackOutsideSinks() const;

  // Recording sink: ~2 seconds of audio, gain applied.
  std::unique_ptr<core::CircularBuffer> buffer_;
  // Monitor sink: ~2 seconds of raw audio.
  std::unique_ptr<core::CircularBuffer> monitor_buffer_;

  std::atomic<float> volume_{1.0f};
  std::atomic<float> input_level_{0.0f};
  std::atomic<bool> is_capturing_{false};
  std::atomic<bool> recording_sink_enabled_{false};
  std::atomic<bool> monitor_sink_enabled_{false};
  // Set by the callback around its sink writes (see SetRecordingSinkEnabled).
  std::atomic<bool> callback_in_sinks_{false};
};

/**
 * AudioSource view of a shared MicrophoneCapture's recording sink, for
 * RecordingPipeline. Start()/Stop() switch the sink only; the owner of the
 * capture starts and stops the stream, so a running input monitor is not
 * interrupted by a recording ending.
 */
class CaptureRecordingSource : public AudioSource {
 public:
  explicit CaptureRecordingSource(std::shared_ptr<MicrophoneCapture> capture);
  ~CaptureRecordingSource() override;

  bool Start() override;
  bool Stop() override;
  int32_t GetSampleRate() const override;
  int32_t GetChannelCount() const override;
  size_t GetAvailableFrames() const override;
  size_t ReadData(float* data, size_t frame_count) override;
  float GetInputLevel() const override;
  void SetVolume(float volume) override;

 private:
  std::shared_ptr<MicrophoneCapture> capture_;
};

}  // namespace recording
}  // namespace sezo
