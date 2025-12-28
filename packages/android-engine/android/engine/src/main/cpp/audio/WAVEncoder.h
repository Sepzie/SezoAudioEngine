#pragma once

#include "audio/AudioEncoder.h"

#include <cstdio>
#include <string>

namespace sezo {
namespace audio {

/**
 * WAV file encoder using dr_wav.
 * Writes uncompressed PCM WAV files.
 */
class WAVEncoder : public AudioEncoder {
 public:
  WAVEncoder();
  ~WAVEncoder() override;

  bool Open(const std::string& output_path, const EncoderConfig& config) override;
  bool Write(const float* samples, size_t frame_count) override;
  bool Close() override;
  bool IsOpen() const override;
  int64_t GetFramesWritten() const override;
  int64_t GetFileSize() const override;

 private:
  void* wav_handle_ = nullptr;  // drwav* handle
  std::string output_path_;
  int32_t sample_rate_ = 0;
  int32_t channels_ = 0;
  int32_t bits_per_sample_ = 0;
  int64_t frames_written_ = 0;
  bool is_open_ = false;
};

}  // namespace audio
}  // namespace sezo
