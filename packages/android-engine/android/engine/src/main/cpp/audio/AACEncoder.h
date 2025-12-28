#pragma once

#include "audio/AudioEncoder.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace sezo {
namespace audio {

/**
 * AAC encoder using Android MediaCodec.
 * Writes ADTS AAC frames to a .aac file.
 */
class AACEncoder : public AudioEncoder {
 public:
  AACEncoder();
  ~AACEncoder() override;

  bool Open(const std::string& output_path, const EncoderConfig& config) override;
  bool Write(const float* samples, size_t frame_count) override;
  bool Close() override;
  bool IsOpen() const override;
  int64_t GetFramesWritten() const override;
  int64_t GetFileSize() const override;

 private:
  bool QueueInput(const int16_t* samples, size_t frame_count);
  bool DrainOutput(bool end_of_stream);
  void Reset();

  void* codec_ = nullptr;   // AMediaCodec*
  void* format_ = nullptr;  // AMediaFormat*
  std::FILE* file_ = nullptr;
  std::string output_path_;
  int32_t sample_rate_ = 0;
  int32_t channels_ = 0;
  int32_t bitrate_ = 0;
  int64_t frames_written_ = 0;
  int64_t file_size_ = 0;
  int64_t total_frames_queued_ = 0;
  bool is_open_ = false;
};

}  // namespace audio
}  // namespace sezo
