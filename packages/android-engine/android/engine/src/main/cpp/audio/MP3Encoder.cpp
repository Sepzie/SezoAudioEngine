#include "audio/MP3Encoder.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef SEZO_ENABLE_LAME
#include <lame/lame.h>
#endif

#define LOG_TAG "MP3Encoder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace audio {
namespace {

#ifdef SEZO_ENABLE_LAME
int16_t FloatToPcm16(float sample) {
  float clamped = std::max(-1.0f, std::min(1.0f, sample));
  return static_cast<int16_t>(std::lrint(clamped * 32767.0f));
}
#endif

}  // namespace

MP3Encoder::MP3Encoder() = default;

MP3Encoder::~MP3Encoder() {
  if (is_open_) {
    Close();
  }
}

bool MP3Encoder::Open(const std::string& output_path, const EncoderConfig& config) {
  if (is_open_) {
    LOGE("Encoder already open");
    return false;
  }

  if (config.format != EncoderFormat::kMP3) {
    LOGE("Invalid format for MP3Encoder");
    return false;
  }

  output_path_ = output_path;
  sample_rate_ = config.sample_rate;
  channels_ = config.channels;
  bitrate_ = config.bitrate;
  frames_written_ = 0;
  file_size_ = 0;

  if (channels_ <= 0 || channels_ > 2) {
    LOGE("Unsupported MP3 channel count: %d", channels_);
    return false;
  }

  file_ = std::fopen(output_path.c_str(), "wb");
  if (!file_) {
    LOGE("Failed to open MP3 file for writing: %s", output_path.c_str());
    Reset();
    return false;
  }

#ifdef SEZO_ENABLE_LAME
  lame_t lame = lame_init();
  if (!lame) {
    LOGE("Failed to initialize LAME");
    Reset();
    return false;
  }

  lame_set_in_samplerate(lame, sample_rate_);
  lame_set_num_channels(lame, channels_);
  lame_set_brate(lame, bitrate_ / 1000);
  lame_set_quality(lame, 2);

  if (lame_init_params(lame) < 0) {
    LOGE("Failed to init LAME params");
    lame_close(lame);
    Reset();
    return false;
  }

  lame_ = lame;
  is_open_ = true;

  LOGD("Opened MP3 encoder: %s, %d Hz, %d ch, %d bps",
       output_path.c_str(), sample_rate_, channels_, bitrate_);
  return true;
#else
  LOGE("LAME not enabled - MP3 encoding unavailable");
  Reset();
  return false;
#endif
}

bool MP3Encoder::Write(const float* samples, size_t frame_count) {
  if (!is_open_) {
    LOGE("Encoder not open");
    return false;
  }

  if (frame_count == 0 || !samples) {
    return true;
  }

#ifdef SEZO_ENABLE_LAME
  auto* lame = static_cast<lame_t>(lame_);
  const size_t sample_count = frame_count * static_cast<size_t>(channels_);
  std::vector<int16_t> pcm(sample_count);
  for (size_t i = 0; i < sample_count; ++i) {
    pcm[i] = FloatToPcm16(samples[i]);
  }

  int mp3_buffer_size = static_cast<int>(1.25 * frame_count + 7200);
  std::vector<unsigned char> mp3_buffer(static_cast<size_t>(mp3_buffer_size));

  int bytes_encoded = 0;
  if (channels_ == 1) {
    bytes_encoded = lame_encode_buffer(
        lame, pcm.data(), pcm.data(), static_cast<int>(frame_count),
        mp3_buffer.data(), mp3_buffer_size);
  } else {
    bytes_encoded = lame_encode_buffer_interleaved(
        lame, pcm.data(), static_cast<int>(frame_count),
        mp3_buffer.data(), mp3_buffer_size);
  }

  if (bytes_encoded < 0) {
    LOGE("LAME encode error: %d", bytes_encoded);
    return false;
  }

  if (bytes_encoded > 0) {
    if (std::fwrite(mp3_buffer.data(), 1, bytes_encoded, file_) !=
        static_cast<size_t>(bytes_encoded)) {
      LOGE("Failed to write MP3 data");
      return false;
    }
    file_size_ += bytes_encoded;
  }

  frames_written_ += static_cast<int64_t>(frame_count);
  return true;
#else
  (void)samples;
  (void)frame_count;
  LOGE("LAME not enabled - MP3 encoding unavailable");
  return false;
#endif
}

bool MP3Encoder::Close() {
  if (!is_open_) {
    return false;
  }

#ifdef SEZO_ENABLE_LAME
  auto* lame = static_cast<lame_t>(lame_);
  std::vector<unsigned char> mp3_buffer(7200);
  int bytes_flushed = lame_encode_flush(lame, mp3_buffer.data(), mp3_buffer.size());
  if (bytes_flushed > 0) {
    if (std::fwrite(mp3_buffer.data(), 1, bytes_flushed, file_) !=
        static_cast<size_t>(bytes_flushed)) {
      LOGE("Failed to write MP3 flush data");
      return false;
    }
    file_size_ += bytes_flushed;
  }

  lame_close(lame);
  lame_ = nullptr;
#endif

  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }

  is_open_ = false;
  LOGD("Closed MP3 encoder: %lld frames written to %s",
       static_cast<long long>(frames_written_), output_path_.c_str());
  return true;
}

bool MP3Encoder::IsOpen() const {
  return is_open_;
}

int64_t MP3Encoder::GetFramesWritten() const {
  return frames_written_;
}

int64_t MP3Encoder::GetFileSize() const {
  return file_size_;
}

void MP3Encoder::Reset() {
#ifdef SEZO_ENABLE_LAME
  if (lame_) {
    lame_close(static_cast<lame_t>(lame_));
  }
  lame_ = nullptr;
#endif
  if (file_) {
    std::fclose(file_);
  }
  file_ = nullptr;
  is_open_ = false;
}

}  // namespace audio
}  // namespace sezo
