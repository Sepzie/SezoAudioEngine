#include "audio/WAVEncoder.h"

#include "dr_wav.h"

#include <android/log.h>
#include <sys/stat.h>
#include <cstring>

#define LOG_TAG "WAVEncoder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace audio {

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

  // dr_wav expects samples in interleaved format (already the case)
  // Write float samples directly - dr_wav will convert to PCM
  drwav_uint64 frames_written = drwav_write_pcm_frames(
      wav, frame_count, samples);

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
