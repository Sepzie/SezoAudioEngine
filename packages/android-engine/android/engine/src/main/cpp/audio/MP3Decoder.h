#pragma once

#include "AudioDecoder.h"
#include "dr_mp3.h"

#include <memory>

namespace sezo {
namespace audio {

/**
 * MP3 decoder using dr_mp3.
 */
class MP3Decoder : public AudioDecoder {
 public:
  MP3Decoder();
  ~MP3Decoder() override;

  bool Open(const std::string& file_path) override;
  void Close() override;
  size_t Read(float* buffer, size_t frames) override;
  bool Seek(int64_t frame) override;
  const AudioFormat& GetFormat() const override { return format_; }
  bool IsOpen() const override { return is_open_; }

 private:
  drmp3 decoder_;
  bool is_open_ = false;
};

}  // namespace audio
}  // namespace sezo
