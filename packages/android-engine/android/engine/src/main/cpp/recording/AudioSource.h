#pragma once

#include <cstddef>
#include <cstdint>

namespace sezo {
namespace recording {

/**
 * A source of interleaved float PCM that the RecordingPipeline drains on its
 * worker thread. The microphone implements it on Android; tests implement it
 * with generated audio so the pipeline can run on a host without Oboe.
 *
 * Contract: after Stop() returns, no new frames become available, so a reader
 * that sees GetAvailableFrames() == 0 after Stop() has drained the source.
 */
class AudioSource {
 public:
  virtual ~AudioSource() = default;

  /** Begin producing frames. */
  virtual bool Start() = 0;

  /** Stop producing frames. Frames already buffered stay readable. */
  virtual bool Stop() = 0;

  virtual int32_t GetSampleRate() const = 0;
  virtual int32_t GetChannelCount() const = 0;

  /** Frames currently buffered and readable. */
  virtual size_t GetAvailableFrames() const = 0;

  /**
   * Read up to frame_count interleaved frames into data.
   * @return Number of frames actually read.
   */
  virtual size_t ReadData(float* data, size_t frame_count) = 0;

  /** Peak input level (0.0 to 1.0) of the most recent block, if the source measures it. */
  virtual float GetInputLevel() const { return 0.0f; }

  /** Capture gain (0.0 to 2.0), if the source supports it. */
  virtual void SetVolume(float volume) { (void)volume; }
};

}  // namespace recording
}  // namespace sezo
