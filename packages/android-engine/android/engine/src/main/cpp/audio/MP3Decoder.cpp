#include "MP3Decoder.h"
#include <cstring>

namespace sezo {
namespace audio {

MP3Decoder::MP3Decoder() {
  std::memset(&decoder_, 0, sizeof(decoder_));
}

MP3Decoder::~MP3Decoder() {
  Close();
}

bool MP3Decoder::Open(const std::string& file_path) {
  if (is_open_) {
    Close();
  }

  if (!drmp3_init_file(&decoder_, file_path.c_str(), nullptr)) {
    return false;
  }

  format_.sample_rate = decoder_.sampleRate;
  format_.channels = decoder_.channels;

  // Get total frame count by seeking to end
  drmp3_uint64 total_pcm_frames;
  total_pcm_frames = drmp3_get_pcm_frame_count(&decoder_);
  format_.total_frames = static_cast<int64_t>(total_pcm_frames);

  is_open_ = true;
  return true;
}

void MP3Decoder::Close() {
  if (is_open_) {
    drmp3_uninit(&decoder_);
    is_open_ = false;
  }
}

size_t MP3Decoder::Read(float* buffer, size_t frames) {
  if (!is_open_) {
    return 0;
  }

  drmp3_uint64 frames_read = drmp3_read_pcm_frames_f32(&decoder_, frames, buffer);
  return static_cast<size_t>(frames_read);
}

bool MP3Decoder::Seek(int64_t frame) {
  if (!is_open_) {
    return false;
  }

  return drmp3_seek_to_pcm_frame(&decoder_, static_cast<drmp3_uint64>(frame));
}

}  // namespace audio
}  // namespace sezo
