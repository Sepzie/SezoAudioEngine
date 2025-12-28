#pragma once

#include "AudioDecoder.h"
#include "dr_wav.h"

#include <memory>

namespace sezo {
namespace audio {

/**
 * WAV decoder using dr_wav.
 */
class WAVDecoder : public AudioDecoder {
 public:
  WAVDecoder();
  ~WAVDecoder() override;

  bool Open(const std::string& file_path) override;
  void Close() override;
  size_t Read(float* buffer, size_t frames) override;
  bool Seek(int64_t frame) override;
  const AudioFormat& GetFormat() const override { return format_; }
  bool IsOpen() const override { return is_open_; }

 private:
  drwav decoder_;
  bool is_open_ = false;
};

}  // namespace audio
}  // namespace sezo
