#include "audio/M4ADecoder.h"

#include <android/log.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>

#define LOG_TAG "M4ADecoder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace audio {
namespace {

constexpr int64_t kCodecTimeoutUs = 10000;
constexpr int kMaxDecodeTries = 8;
constexpr int32_t kPcm16Encoding = 2;
#ifdef AMEDIAFORMAT_KEY_PCM_ENCODING
constexpr int32_t kPcmFloatEncoding = 4;
#endif

bool HasAudioMime(const char* mime) {
  return mime && std::strncmp(mime, "audio/", 6) == 0;
}

}  // namespace

M4ADecoder::M4ADecoder() = default;

M4ADecoder::~M4ADecoder() {
  Close();
}

bool M4ADecoder::Open(const std::string& file_path) {
  if (is_open_) {
    Close();
  }

  extractor_ = AMediaExtractor_new();
  if (!extractor_) {
    LOGE("Failed to create MediaExtractor");
    return false;
  }

  media_status_t status = AMediaExtractor_setDataSource(extractor_, file_path.c_str());
  if (status != AMEDIA_OK) {
    LOGE("Failed to set data source: %s", file_path.c_str());
    Close();
    return false;
  }

  const size_t track_count = AMediaExtractor_getTrackCount(extractor_);
  AMediaFormat* selected_format = nullptr;
  int selected_track = -1;
  for (size_t i = 0; i < track_count; ++i) {
    AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor_, i);
    const char* mime = nullptr;
    if (format && AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
      if (HasAudioMime(mime)) {
        selected_format = format;
        selected_track = static_cast<int>(i);
        break;
      }
    }
    if (format) {
      AMediaFormat_delete(format);
    }
  }

  if (!selected_format || selected_track < 0) {
    LOGE("No audio track found");
    Close();
    return false;
  }

  const char* mime = nullptr;
  AMediaFormat_getString(selected_format, AMEDIAFORMAT_KEY_MIME, &mime);
  if (!mime) {
    LOGE("Missing mime type");
    AMediaFormat_delete(selected_format);
    Close();
    return false;
  }

  int32_t sample_rate = 0;
  int32_t channels = 0;
  AMediaFormat_getInt32(selected_format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sample_rate);
  AMediaFormat_getInt32(selected_format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
  if (sample_rate <= 0 || channels <= 0) {
    LOGE("Invalid audio format: %d Hz, %d ch", sample_rate, channels);
    AMediaFormat_delete(selected_format);
    Close();
    return false;
  }

  int64_t duration_us = 0;
  if (!AMediaFormat_getInt64(selected_format, AMEDIAFORMAT_KEY_DURATION, &duration_us)) {
    duration_us = 0;
  }

  if (AMediaExtractor_selectTrack(extractor_, selected_track) != AMEDIA_OK) {
    LOGE("Failed to select audio track");
    AMediaFormat_delete(selected_format);
    Close();
    return false;
  }

  codec_ = AMediaCodec_createDecoderByType(mime);
  if (!codec_) {
    LOGE("Failed to create decoder for %s", mime);
    AMediaFormat_delete(selected_format);
    Close();
    return false;
  }

  status = AMediaCodec_configure(codec_, selected_format, nullptr, nullptr, 0);
  if (status != AMEDIA_OK) {
    LOGE("Failed to configure decoder: %d", status);
    AMediaFormat_delete(selected_format);
    Close();
    return false;
  }

  status = AMediaCodec_start(codec_);
  if (status != AMEDIA_OK) {
    LOGE("Failed to start decoder: %d", status);
    AMediaFormat_delete(selected_format);
    Close();
    return false;
  }

  track_format_ = selected_format;
  audio_track_index_ = selected_track;
  format_.sample_rate = sample_rate;
  format_.channels = channels;
  if (duration_us > 0) {
    format_.total_frames =
        static_cast<int64_t>((duration_us * sample_rate) / 1000000LL);
  } else {
    format_.total_frames = 0;
  }

  ResetState();
  is_open_ = true;
  LOGD("Opened M4A decoder: %s (%d Hz, %d ch)", file_path.c_str(), sample_rate, channels);
  return true;
}

void M4ADecoder::Close() {
  if (codec_) {
    AMediaCodec_stop(codec_);
    AMediaCodec_delete(codec_);
    codec_ = nullptr;
  }
  if (track_format_) {
    AMediaFormat_delete(track_format_);
    track_format_ = nullptr;
  }
  if (extractor_) {
    AMediaExtractor_delete(extractor_);
    extractor_ = nullptr;
  }
  pending_samples_.clear();
  pending_offset_ = 0;
  is_open_ = false;
}

size_t M4ADecoder::Read(float* buffer, size_t frames) {
  if (!is_open_ || !buffer || frames == 0) {
    return 0;
  }

  const size_t channels = static_cast<size_t>(format_.channels);
  const size_t samples_needed = frames * channels;
  size_t samples_written = 0;

  while (samples_written < samples_needed) {
    if (pending_offset_ < pending_samples_.size()) {
      const size_t available = pending_samples_.size() - pending_offset_;
      const size_t to_copy = std::min(available, samples_needed - samples_written);
      std::memcpy(buffer + samples_written,
                  pending_samples_.data() + pending_offset_,
                  to_copy * sizeof(float));
      pending_offset_ += to_copy;
      samples_written += to_copy;
      if (pending_offset_ >= pending_samples_.size()) {
        pending_samples_.clear();
        pending_offset_ = 0;
      }
      continue;
    }

    if (!DecodeMore()) {
      break;
    }
  }

  return samples_written / channels;
}

bool M4ADecoder::Seek(int64_t frame) {
  if (!is_open_) {
    return false;
  }

  if (frame < 0) {
    frame = 0;
  }
  int64_t time_us = (frame * 1000000LL) / format_.sample_rate;
  if (AMediaExtractor_seekTo(extractor_, time_us,
                             AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC) != AMEDIA_OK) {
    LOGE("Failed to seek to %" PRId64, frame);
    return false;
  }

  if (AMediaCodec_flush(codec_) != AMEDIA_OK) {
    LOGE("Failed to flush decoder");
    return false;
  }

  ResetState();
  return true;
}

bool M4ADecoder::DecodeMore() {
  for (int i = 0; i < kMaxDecodeTries; ++i) {
    if (!input_eos_) {
      QueueInput();
    }
    if (DrainOutput()) {
      if (!pending_samples_.empty()) {
        return true;
      }
    }
    if (output_eos_) {
      return false;
    }
  }
  return !pending_samples_.empty();
}

bool M4ADecoder::QueueInput() {
  ssize_t input_index = AMediaCodec_dequeueInputBuffer(codec_, kCodecTimeoutUs);
  if (input_index < 0) {
    return false;
  }

  size_t buffer_size = 0;
  uint8_t* buffer = AMediaCodec_getInputBuffer(codec_, input_index, &buffer_size);
  if (!buffer || buffer_size == 0) {
    return false;
  }

  ssize_t sample_size = AMediaExtractor_readSampleData(extractor_, buffer, buffer_size);
  if (sample_size < 0) {
    AMediaCodec_queueInputBuffer(codec_, input_index, 0, 0, 0,
                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    input_eos_ = true;
    return true;
  }

  int64_t pts_us = AMediaExtractor_getSampleTime(extractor_);
  AMediaCodec_queueInputBuffer(codec_, input_index, 0, sample_size, pts_us, 0);
  AMediaExtractor_advance(extractor_);
  return true;
}

bool M4ADecoder::DrainOutput() {
  AMediaCodecBufferInfo info;
  ssize_t output_index = AMediaCodec_dequeueOutputBuffer(codec_, &info, kCodecTimeoutUs);
  if (output_index >= 0) {
    size_t buffer_size = 0;
    uint8_t* buffer = AMediaCodec_getOutputBuffer(codec_, output_index, &buffer_size);
    if (buffer && info.size > 0) {
      AppendPcm(buffer + info.offset, static_cast<size_t>(info.size));
    }

    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
      output_eos_ = true;
    }

    AMediaCodec_releaseOutputBuffer(codec_, output_index, false);
    return true;
  }

  if (output_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
    AMediaFormat* format = AMediaCodec_getOutputFormat(codec_);
    if (format) {
      int32_t pcm_encoding = kPcm16Encoding;
#ifdef AMEDIAFORMAT_KEY_PCM_ENCODING
      if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_PCM_ENCODING, &pcm_encoding)) {
        output_is_float_ = (pcm_encoding == kPcmFloatEncoding);
      }
#else
      (void)pcm_encoding;
#endif

      int32_t out_sample_rate = 0;
      int32_t out_channels = 0;
      AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &out_sample_rate);
      AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &out_channels);
      if (!output_format_set_) {
        output_format_set_ = true;
        if (out_sample_rate > 0 && out_sample_rate != format_.sample_rate) {
          LOGD("Output sample rate changed: %d -> %d", format_.sample_rate, out_sample_rate);
          format_.sample_rate = out_sample_rate;
        }
        if (out_channels > 0 && out_channels != format_.channels) {
          LOGD("Output channels changed: %d -> %d", format_.channels, out_channels);
          format_.channels = out_channels;
        }
      }
      AMediaFormat_delete(format);
    }
    return false;
  }

  if (output_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER ||
      output_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
    return false;
  }

  LOGE("Unexpected decoder output status: %zd", output_index);
  return false;
}

void M4ADecoder::ResetState() {
  input_eos_ = false;
  output_eos_ = false;
  output_is_float_ = false;
  output_format_set_ = false;
  pending_samples_.clear();
  pending_offset_ = 0;
}

void M4ADecoder::AppendPcm(const uint8_t* data, size_t bytes) {
  if (!data || bytes == 0) {
    return;
  }

  if (output_is_float_) {
    const size_t sample_count = bytes / sizeof(float);
    const float* samples = reinterpret_cast<const float*>(data);
    pending_samples_.insert(pending_samples_.end(), samples, samples + sample_count);
  } else {
    const size_t sample_count = bytes / sizeof(int16_t);
    const int16_t* samples = reinterpret_cast<const int16_t*>(data);
    pending_samples_.reserve(pending_samples_.size() + sample_count);
    for (size_t i = 0; i < sample_count; ++i) {
      pending_samples_.push_back(static_cast<float>(samples[i]) / 32768.0f);
    }
  }
}

}  // namespace audio
}  // namespace sezo
