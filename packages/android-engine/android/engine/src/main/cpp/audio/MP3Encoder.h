#pragma once

#include "audio/AudioEncoder.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace sezo {
namespace audio {

/**
 * MP3 encoder using LAME (optional).
 */
class MP3Encoder : public AudioEncoder {
 public:
  MP3Encoder();
  ~MP3Encoder() override;

  bool Open(const std::string& output_path, const EncoderConfig& config) override;
  bool Write(const float* samples, size_t frame_count) override;
  bool Close() override;
  bool IsOpen() const override;
  int64_t GetFramesWritten() const override;
  int64_t GetFileSize() const override;

 private:
  void Reset();

#ifdef SEZO_ENABLE_LAME
  void* lame_ = nullptr;  // lame_t
#endif
  std::FILE* file_ = nullptr;
  std::string output_path_;
  int32_t sample_rate_ = 0;
  int32_t channels_ = 0;
  int32_t bitrate_ = 0;
  int64_t frames_written_ = 0;
  int64_t file_size_ = 0;
  bool is_open_ = false;
};

}  // namespace audio
}  // namespace sezo
