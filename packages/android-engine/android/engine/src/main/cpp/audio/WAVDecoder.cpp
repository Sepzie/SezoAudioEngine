#include "WAVDecoder.h"
#include <cstring>

namespace sezo {
namespace audio {

WAVDecoder::WAVDecoder() {
  std::memset(&decoder_, 0, sizeof(decoder_));
}

WAVDecoder::~WAVDecoder() {
  Close();
}

bool WAVDecoder::Open(const std::string& file_path) {
  if (is_open_) {
    Close();
  }

  if (!drwav_init_file(&decoder_, file_path.c_str(), nullptr)) {
    return false;
  }

  format_.sample_rate = decoder_.sampleRate;
  format_.channels = decoder_.channels;
  format_.total_frames = static_cast<int64_t>(decoder_.totalPCMFrameCount);

  is_open_ = true;
  return true;
}

void WAVDecoder::Close() {
  if (is_open_) {
    drwav_uninit(&decoder_);
    is_open_ = false;
  }
}

size_t WAVDecoder::Read(float* buffer, size_t frames) {
  if (!is_open_) {
    return 0;
  }

  drwav_uint64 frames_read = drwav_read_pcm_frames_f32(&decoder_, frames, buffer);
  return static_cast<size_t>(frames_read);
}

bool WAVDecoder::Seek(int64_t frame) {
  if (!is_open_) {
    return false;
  }

  return drwav_seek_to_pcm_frame(&decoder_, static_cast<drwav_uint64>(frame));
}

}  // namespace audio
}  // namespace sezo
