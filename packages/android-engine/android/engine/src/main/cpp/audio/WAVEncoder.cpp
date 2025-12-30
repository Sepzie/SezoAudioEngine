#include "audio/WAVEncoder.h"

#include "dr_wav.h"

#include <android/log.h>
#include <sys/stat.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define LOG_TAG "WAVEncoder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace audio {
namespace {

int16_t FloatToPcm16(float sample) {
  float clamped = std::max(-1.0f, std::min(1.0f, sample));
  return static_cast<int16_t>(std::lrint(clamped * 32767.0f));
}

int32_t FloatToPcm32(float sample) {
  float clamped = std::max(-1.0f, std::min(1.0f, sample));
  return static_cast<int32_t>(std::lrint(clamped * 2147483647.0f));
}

}  // namespace

WAVEncoder::WAVEncoder() = default;

WAVEncoder::~WAVEncoder() {
  if (is_open_) {
    Close();
  }
}

bool WAVEncoder::Open(const std::string& output_path, const EncoderConfig& config) {
  if (is_open_) {
    LOGE("Encoder already open");
    return false;
  }

  if (config.format != EncoderFormat::kWAV) {
    LOGE("Invalid format for WAVEncoder");
    return false;
  }

  if (config.bits_per_sample != 16 && config.bits_per_sample != 24 &&
      config.bits_per_sample != 32) {
    LOGE("Unsupported bits per sample: %d (must be 16, 24, or 32)",
         config.bits_per_sample);
    return false;
  }

  output_path_ = output_path;
  sample_rate_ = config.sample_rate;
  channels_ = config.channels;
  bits_per_sample_ = config.bits_per_sample;
  frames_written_ = 0;

  // Setup WAV format
  drwav_data_format format;
  format.container = drwav_container_riff;
  format.format = DR_WAVE_FORMAT_PCM;
  format.channels = static_cast<drwav_uint32>(channels_);
  format.sampleRate = static_cast<drwav_uint32>(sample_rate_);
  format.bitsPerSample = static_cast<drwav_uint32>(bits_per_sample_);

  // Open WAV file for writing
  auto* wav = new drwav();
  if (!drwav_init_file_write(wav, output_path.c_str(), &format, nullptr)) {
    LOGE("Failed to open WAV file for writing: %s", output_path.c_str());
    delete wav;
    return false;
  }

  wav_handle_ = wav;
  is_open_ = true;

  LOGD("Opened WAV encoder: %s, %d Hz, %d ch, %d bits",
       output_path.c_str(), sample_rate_, channels_, bits_per_sample_);

  return true;
}

bool WAVEncoder::Write(const float* samples, size_t frame_count) {
  if (!is_open_) {
    LOGE("Encoder not open");
    return false;
  }

  if (frame_count == 0 || samples == nullptr) {
    return true;  // Nothing to write
  }

  auto* wav = static_cast<drwav*>(wav_handle_);

  drwav_uint64 frames_written = 0;
  const size_t sample_count = frame_count * static_cast<size_t>(channels_);

  if (bits_per_sample_ == 16) {
    std::vector<int16_t> pcm(sample_count);
    for (size_t i = 0; i < sample_count; ++i) {
      pcm[i] = FloatToPcm16(samples[i]);
    }
    frames_written = drwav_write_pcm_frames(wav, frame_count, pcm.data());
  } else if (bits_per_sample_ == 24) {
    std::vector<uint8_t> pcm(sample_count * 3);
    for (size_t i = 0; i < sample_count; ++i) {
      int32_t value = FloatToPcm32(samples[i]);
      uint32_t packed = static_cast<uint32_t>(value);
      const size_t offset = i * 3;
      pcm[offset] = static_cast<uint8_t>(packed & 0xFF);
      pcm[offset + 1] = static_cast<uint8_t>((packed >> 8) & 0xFF);
      pcm[offset + 2] = static_cast<uint8_t>((packed >> 16) & 0xFF);
    }
    frames_written = drwav_write_pcm_frames(wav, frame_count, pcm.data());
  } else if (bits_per_sample_ == 32) {
    std::vector<int32_t> pcm(sample_count);
    for (size_t i = 0; i < sample_count; ++i) {
      pcm[i] = FloatToPcm32(samples[i]);
    }
    frames_written = drwav_write_pcm_frames(wav, frame_count, pcm.data());
  } else {
    LOGE("Unsupported bits per sample: %d", bits_per_sample_);
    return false;
  }

  if (frames_written != frame_count) {
    LOGE("Failed to write all frames: wrote %llu of %zu",
         static_cast<unsigned long long>(frames_written), frame_count);
    return false;
  }

  frames_written_ += static_cast<int64_t>(frames_written);
  return true;
}

bool WAVEncoder::Close() {
  if (!is_open_) {
    return false;
  }

  auto* wav = static_cast<drwav*>(wav_handle_);
  drwav_uninit(wav);
  delete wav;

  wav_handle_ = nullptr;
  is_open_ = false;

  LOGD("Closed WAV encoder: %lld frames written to %s",
       static_cast<long long>(frames_written_), output_path_.c_str());

  return true;
}

bool WAVEncoder::IsOpen() const {
  return is_open_;
}

int64_t WAVEncoder::GetFramesWritten() const {
  return frames_written_;
}

int64_t WAVEncoder::GetFileSize() const {
  if (is_open_) {
    LOGE("Cannot get file size while encoder is still open");
    return 0;
  }

  struct stat st;
  if (stat(output_path_.c_str(), &st) == 0) {
    return static_cast<int64_t>(st.st_size);
  }

  LOGE("Failed to get file size for: %s", output_path_.c_str());
  return 0;
}

}  // namespace audio
}  // namespace sezo
