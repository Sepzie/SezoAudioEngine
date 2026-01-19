#pragma once

#include "AudioDecoder.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <string>
#include <vector>

namespace sezo {
namespace audio {

/**
 * M4A/AAC decoder using MediaExtractor + MediaCodec.
 */
class M4ADecoder : public AudioDecoder {
 public:
  M4ADecoder();
  ~M4ADecoder() override;

  bool Open(const std::string& file_path) override;
  void Close() override;
  size_t Read(float* buffer, size_t frames) override;
  bool Seek(int64_t frame) override;
  const AudioFormat& GetFormat() const override { return format_; }
  bool IsOpen() const override { return is_open_; }

 private:
  bool DecodeMore();
  bool QueueInput();
  bool DrainOutput();
  void ResetState();
  void AppendPcm(const uint8_t* data, size_t bytes);

  AMediaExtractor* extractor_ = nullptr;
  AMediaCodec* codec_ = nullptr;
  AMediaFormat* track_format_ = nullptr;
  int audio_track_index_ = -1;

  bool is_open_ = false;
  bool input_eos_ = false;
  bool output_eos_ = false;
  bool output_is_float_ = false;
  bool output_format_set_ = false;

  std::vector<float> pending_samples_;
  size_t pending_offset_ = 0;
};

}  // namespace audio
}  // namespace sezo
