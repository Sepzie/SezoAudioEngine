#include "audio/AACEncoder.h"

#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#define LOG_TAG "AACEncoder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace audio {
namespace {

constexpr int kAdtsHeaderSize = 7;
constexpr int64_t kCodecTimeoutUs = 10000;

int GetAdtsSampleRateIndex(int sample_rate) {
  switch (sample_rate) {
    case 96000: return 0;
    case 88200: return 1;
    case 64000: return 2;
    case 48000: return 3;
    case 44100: return 4;
    case 32000: return 5;
    case 24000: return 6;
    case 22050: return 7;
    case 16000: return 8;
    case 12000: return 9;
    case 11025: return 10;
    case 8000: return 11;
    case 7350: return 12;
    default: return 4;
  }
}

void BuildAdtsHeader(uint8_t* header, int packet_length, int sample_rate, int channels) {
  int profile = 2;  // AAC LC
  int freq_idx = GetAdtsSampleRateIndex(sample_rate);
  int chan_cfg = channels;

  header[0] = 0xFF;
  header[1] = 0xF1;
  header[2] = static_cast<uint8_t>(((profile - 1) << 6) | (freq_idx << 2) | (chan_cfg >> 2));
  header[3] = static_cast<uint8_t>(((chan_cfg & 3) << 6) | (packet_length >> 11));
  header[4] = static_cast<uint8_t>((packet_length >> 3) & 0xFF);
  header[5] = static_cast<uint8_t>(((packet_length & 7) << 5) | 0x1F);
  header[6] = 0xFC;
}

int16_t FloatToPcm16(float sample) {
  float clamped = std::max(-1.0f, std::min(1.0f, sample));
  return static_cast<int16_t>(std::lrint(clamped * 32767.0f));
}

}  // namespace

AACEncoder::AACEncoder() = default;

AACEncoder::~AACEncoder() {
  if (is_open_) {
    Close();
  }
}

bool AACEncoder::Open(const std::string& output_path, const EncoderConfig& config) {
  if (is_open_) {
    LOGE("Encoder already open");
    return false;
  }

  if (config.format != EncoderFormat::kAAC) {
    LOGE("Invalid format for AACEncoder");
    return false;
  }

  if (config.channels <= 0 || config.sample_rate <= 0) {
    LOGE("Invalid AAC config: channels=%d sample_rate=%d",
         config.channels, config.sample_rate);
    return false;
  }

  output_path_ = output_path;
  sample_rate_ = config.sample_rate;
  channels_ = config.channels;
  bitrate_ = config.bitrate;
  frames_written_ = 0;
  file_size_ = 0;
  total_frames_queued_ = 0;

  file_ = std::fopen(output_path.c_str(), "wb");
  if (!file_) {
    LOGE("Failed to open AAC file for writing: %s", output_path.c_str());
    Reset();
    return false;
  }

  auto* format = AMediaFormat_new();
  if (!format) {
    LOGE("Failed to create AAC media format");
    Reset();
    return false;
  }
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, sample_rate_);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, channels_);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitrate_);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_AAC_PROFILE, 2);  // AAC LC

  auto* codec = AMediaCodec_createEncoderByType("audio/mp4a-latm");
  if (!codec) {
    LOGE("Failed to create AAC encoder");
    AMediaFormat_delete(format);
    Reset();
    return false;
  }

  media_status_t status = AMediaCodec_configure(
      codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  if (status != AMEDIA_OK) {
    LOGE("Failed to configure AAC encoder: %d", status);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(format);
    Reset();
    return false;
  }

  status = AMediaCodec_start(codec);
  if (status != AMEDIA_OK) {
    LOGE("Failed to start AAC encoder: %d", status);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(format);
    Reset();
    return false;
  }

  codec_ = codec;
  format_ = format;
  is_open_ = true;

  LOGD("Opened AAC encoder: %s, %d Hz, %d ch, %d bps",
       output_path.c_str(), sample_rate_, channels_, bitrate_);
  return true;
}

bool AACEncoder::Write(const float* samples, size_t frame_count) {
  if (!is_open_) {
    LOGE("Encoder not open");
    return false;
  }

  if (frame_count == 0 || !samples) {
    return true;
  }

  const size_t sample_count = frame_count * static_cast<size_t>(channels_);
  std::vector<int16_t> pcm(sample_count);
  for (size_t i = 0; i < sample_count; ++i) {
    pcm[i] = FloatToPcm16(samples[i]);
  }

  if (!QueueInput(pcm.data(), frame_count)) {
    return false;
  }

  return DrainOutput(false);
}

bool AACEncoder::Close() {
  if (!is_open_) {
    return false;
  }

  auto* codec = static_cast<AMediaCodec*>(codec_);

  ssize_t input_index = AMediaCodec_dequeueInputBuffer(codec, kCodecTimeoutUs);
  if (input_index >= 0) {
    int64_t pts_us = (total_frames_queued_ * 1000000LL) / sample_rate_;
    AMediaCodec_queueInputBuffer(
        codec, input_index, 0, 0, pts_us, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
  } else {
    LOGE("Failed to dequeue input buffer for EOS: %zd", input_index);
  }

  DrainOutput(true);

  AMediaCodec_stop(codec);
  AMediaCodec_delete(codec);
  AMediaFormat_delete(static_cast<AMediaFormat*>(format_));

  codec_ = nullptr;
  format_ = nullptr;

  if (file_) {
    std::fclose(file_);
    file_ = nullptr;
  }

  is_open_ = false;

  LOGD("Closed AAC encoder: %lld frames written to %s",
       static_cast<long long>(frames_written_), output_path_.c_str());
  return true;
}

bool AACEncoder::IsOpen() const {
  return is_open_;
}

int64_t AACEncoder::GetFramesWritten() const {
  return frames_written_;
}

int64_t AACEncoder::GetFileSize() const {
  return file_size_;
}

bool AACEncoder::QueueInput(const int16_t* samples, size_t frame_count) {
  auto* codec = static_cast<AMediaCodec*>(codec_);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(samples);
  size_t bytes_remaining = frame_count * static_cast<size_t>(channels_) * sizeof(int16_t);
  const size_t bytes_per_frame = static_cast<size_t>(channels_) * sizeof(int16_t);

  while (bytes_remaining > 0) {
    ssize_t input_index = AMediaCodec_dequeueInputBuffer(codec, kCodecTimeoutUs);
    if (input_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      if (!DrainOutput(false)) {
        return false;
      }
      continue;
    }
    if (input_index < 0) {
      LOGE("Failed to dequeue input buffer: %zd", input_index);
      return false;
    }

    size_t buffer_size = 0;
    uint8_t* buffer = AMediaCodec_getInputBuffer(codec, input_index, &buffer_size);
    if (!buffer || buffer_size == 0) {
      LOGE("Invalid AAC input buffer");
      return false;
    }

    size_t bytes_to_copy = std::min(buffer_size, bytes_remaining);
    std::memcpy(buffer, data, bytes_to_copy);

    int64_t frames_in_chunk = static_cast<int64_t>(bytes_to_copy / bytes_per_frame);
    int64_t pts_us = (total_frames_queued_ * 1000000LL) / sample_rate_;

    media_status_t status = AMediaCodec_queueInputBuffer(
        codec, input_index, 0, bytes_to_copy, pts_us, 0);
    if (status != AMEDIA_OK) {
      LOGE("Failed to queue AAC input buffer: %d", status);
      return false;
    }

    total_frames_queued_ += frames_in_chunk;
    frames_written_ += frames_in_chunk;
    data += bytes_to_copy;
    bytes_remaining -= bytes_to_copy;
  }

  return true;
}

bool AACEncoder::DrainOutput(bool end_of_stream) {
  auto* codec = static_cast<AMediaCodec*>(codec_);
  AMediaCodecBufferInfo info;
  int empty_tries = 0;

  while (true) {
    ssize_t output_index = AMediaCodec_dequeueOutputBuffer(codec, &info, kCodecTimeoutUs);
    if (output_index >= 0) {
      if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
        AMediaCodec_releaseOutputBuffer(codec, output_index, false);
        continue;
      }

      size_t buffer_size = 0;
      uint8_t* buffer = AMediaCodec_getOutputBuffer(codec, output_index, &buffer_size);
      if (buffer && info.size > 0) {
        uint8_t adts_header[kAdtsHeaderSize];
        BuildAdtsHeader(adts_header, info.size + kAdtsHeaderSize, sample_rate_, channels_);

        if (std::fwrite(adts_header, 1, kAdtsHeaderSize, file_) != kAdtsHeaderSize) {
          LOGE("Failed to write AAC ADTS header");
          AMediaCodec_releaseOutputBuffer(codec, output_index, false);
          return false;
        }

        if (std::fwrite(buffer + info.offset, 1, info.size, file_) !=
            static_cast<size_t>(info.size)) {
          LOGE("Failed to write AAC payload");
          AMediaCodec_releaseOutputBuffer(codec, output_index, false);
          return false;
        }

        file_size_ += kAdtsHeaderSize + info.size;
      }

      bool saw_eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
      AMediaCodec_releaseOutputBuffer(codec, output_index, false);

      if (saw_eos) {
        break;
      }
    } else if (output_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      if (!end_of_stream) {
        break;
      }
      if (++empty_tries > 50) {
        break;
      }
    } else if (output_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
               output_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      continue;
    } else {
      LOGE("Unexpected AAC output status: %zd", output_index);
      return false;
    }
  }

  return true;
}

void AACEncoder::Reset() {
  if (codec_) {
    AMediaCodec_delete(static_cast<AMediaCodec*>(codec_));
  }
  if (format_) {
    AMediaFormat_delete(static_cast<AMediaFormat*>(format_));
  }
  if (file_) {
    std::fclose(file_);
  }
  codec_ = nullptr;
  format_ = nullptr;
  file_ = nullptr;
  is_open_ = false;
}

}  // namespace audio
}  // namespace sezo
