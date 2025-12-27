#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sezo {
namespace audio {

/**
 * Audio format information.
 */
struct AudioFormat {
  int32_t sample_rate;
  int32_t channels;
  int64_t total_frames;
};

/**
 * Base class for audio decoders.
 * Supports streaming decode of audio files.
 */
class AudioDecoder {
 public:
  virtual ~AudioDecoder() = default;

  /**
   * Open an audio file for decoding.
   * @param file_path Path to the audio file
   * @return true if successful
   */
  virtual bool Open(const std::string& file_path) = 0;

  /**
   * Close the decoder and release resources.
   */
  virtual void Close() = 0;

  /**
   * Read decoded PCM samples.
   * @param buffer Output buffer (interleaved float samples)
   * @param frames Number of frames to read
   * @return Number of frames actually read
   */
  virtual size_t Read(float* buffer, size_t frames) = 0;

  /**
   * Seek to a specific frame position.
   * @param frame Frame position to seek to
   * @return true if successful
   */
  virtual bool Seek(int64_t frame) = 0;

  /**
   * Get the audio format information.
   * @return Format information
   */
  virtual const AudioFormat& GetFormat() const = 0;

  /**
   * Check if the decoder is open and ready.
   * @return true if open
   */
  virtual bool IsOpen() const = 0;

 protected:
  AudioFormat format_{0, 0, 0};
};

}  // namespace audio
}  // namespace sezo
