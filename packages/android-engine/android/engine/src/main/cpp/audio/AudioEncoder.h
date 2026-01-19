#pragma once

#include <cstdint>
#include <string>

namespace sezo {
namespace audio {

/**
 * Supported audio encoding formats
 */
enum class EncoderFormat {
  kWAV,  // Uncompressed PCM WAV
  kAAC,  // Raw AAC (ADTS)
  kM4A,  // AAC wrapped in an MPEG-4 container
  kMP3   // MP3 (LAME - optional)
};

/**
 * Audio encoder configuration
 */
struct EncoderConfig {
  EncoderFormat format = EncoderFormat::kWAV;
  int32_t sample_rate = 44100;
  int32_t channels = 2;
  int32_t bitrate = 128000;  // For compressed formats (bits per second)
  int32_t bits_per_sample = 16;  // For WAV (16 or 24)
};

/**
 * Base class for audio encoders.
 * Encoders write audio data to files in various formats.
 */
class AudioEncoder {
 public:
  virtual ~AudioEncoder() = default;

  /**
   * Open encoder for writing.
   * @param output_path Path to output file
   * @param config Encoder configuration
   * @return true if successful
   */
  virtual bool Open(const std::string& output_path, const EncoderConfig& config) = 0;

  /**
   * Write audio samples to the encoder.
   * @param samples Interleaved float samples (-1.0 to 1.0)
   * @param frame_count Number of frames (samples / channels)
   * @return true if successful
   */
  virtual bool Write(const float* samples, size_t frame_count) = 0;

  /**
   * Finalize encoding and close the file.
   * Must be called to ensure file is valid.
   * @return true if successful
   */
  virtual bool Close() = 0;

  /**
   * Check if encoder is currently open.
   */
  virtual bool IsOpen() const = 0;

  /**
   * Get the total number of frames written.
   */
  virtual int64_t GetFramesWritten() const = 0;

  /**
   * Get the output file size in bytes.
   * Only valid after Close() is called.
   */
  virtual int64_t GetFileSize() const = 0;
};

}  // namespace audio
}  // namespace sezo
