#include "audio/M4AEncoder.h"

#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define LOG_TAG "M4AEncoder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace sezo {
namespace audio {
namespace {

constexpr int64_t kCodecTimeoutUs = 10000;

int16_t FloatToPcm16(float sample) {
  float clamped = std::max(-1.0f, std::min(1.0f, sample));
  return static_cast<int16_t>(std::lrint(clamped * 32767.0f));
}

}  // namespace

M4AEncoder::M4AEncoder() = default;

M4AEncoder::~M4AEncoder() {
  if (is_open_) {
    Close();
  }
}

bool M4AEncoder::Open(const std::string& output_path, const EncoderConfig& config) {
  if (is_open_) {
    LOGE("Encoder already open");
    return false;
  }

  if (config.format != EncoderFormat::kM4A) {
    LOGE("Invalid format for M4AEncoder");
    return false;
  }

  if (config.channels <= 0 || config.sample_rate <= 0) {
    LOGE("Invalid M4A config: channels=%d sample_rate=%d",
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
  muxer_track_index_ = -1;
  muxer_started_ = false;

  fd_ = ::open(output_path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
  if (fd_ < 0) {
    LOGE("Failed to open M4A output: %s", output_path.c_str());
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

  auto* muxer = AMediaMuxer_new(fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
  if (!muxer) {
    LOGE("Failed to create M4A muxer");
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(format);
    Reset();
    return false;
  }

  codec_ = codec;
  format_ = format;
  muxer_ = muxer;
  is_open_ = true;

  LOGD("Opened M4A encoder: %s, %d Hz, %d ch, %d bps",
       output_path.c_str(), sample_rate_, channels_, bitrate_);
  return true;
}

bool M4AEncoder::Write(const float* samples, size_t frame_count) {
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

bool M4AEncoder::Close() {
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

  if (muxer_) {
    if (muxer_started_) {
      AMediaMuxer_stop(static_cast<AMediaMuxer*>(muxer_));
    }
    AMediaMuxer_delete(static_cast<AMediaMuxer*>(muxer_));
    muxer_ = nullptr;
    muxer_started_ = false;
    muxer_track_index_ = -1;
  }

  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }

  UpdateFileSize();
  is_open_ = false;

  LOGD("Closed M4A encoder: %lld frames written to %s",
       static_cast<long long>(frames_written_), output_path_.c_str());
  return true;
}

bool M4AEncoder::IsOpen() const {
  return is_open_;
}

int64_t M4AEncoder::GetFramesWritten() const {
  return frames_written_;
}

int64_t M4AEncoder::GetFileSize() const {
  return file_size_;
}

bool M4AEncoder::QueueInput(const int16_t* samples, size_t frame_count) {
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

bool M4AEncoder::DrainOutput(bool end_of_stream) {
  auto* codec = static_cast<AMediaCodec*>(codec_);
  auto* muxer = static_cast<AMediaMuxer*>(muxer_);
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
        if (!muxer_started_) {
          if (!StartMuxer()) {
            AMediaCodec_releaseOutputBuffer(codec, output_index, false);
            return false;
          }
        }

        media_status_t status =
            AMediaMuxer_writeSampleData(muxer, muxer_track_index_, buffer, &info);
        if (status != AMEDIA_OK) {
          LOGE("Failed to write M4A sample: %d", status);
          AMediaCodec_releaseOutputBuffer(codec, output_index, false);
          return false;
        }
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
    } else if (output_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      if (!StartMuxer()) {
        return false;
      }
    } else if (output_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      continue;
    } else {
      LOGE("Unexpected AAC output status: %zd", output_index);
      return false;
    }
  }

  return true;
}

bool M4AEncoder::StartMuxer() {
  if (muxer_started_) {
    return true;
  }

  auto* codec = static_cast<AMediaCodec*>(codec_);
  auto* muxer = static_cast<AMediaMuxer*>(muxer_);
  if (!codec || !muxer) {
    LOGE("Muxer unavailable");
    return false;
  }

  AMediaFormat* output_format = AMediaCodec_getOutputFormat(codec);
  if (!output_format) {
    LOGE("Failed to get AAC output format");
    return false;
  }

  int track_index = AMediaMuxer_addTrack(muxer, output_format);
  AMediaFormat_delete(output_format);
  if (track_index < 0) {
    LOGE("Failed to add M4A track");
    return false;
  }

  media_status_t status = AMediaMuxer_start(muxer);
  if (status != AMEDIA_OK) {
    LOGE("Failed to start M4A muxer: %d", status);
    return false;
  }

  muxer_track_index_ = track_index;
  muxer_started_ = true;
  return true;
}

void M4AEncoder::UpdateFileSize() {
  struct stat st {};
  if (output_path_.empty()) {
    return;
  }
  if (::stat(output_path_.c_str(), &st) == 0) {
    file_size_ = static_cast<int64_t>(st.st_size);
  }
}

void M4AEncoder::Reset() {
  if (codec_) {
    AMediaCodec_delete(static_cast<AMediaCodec*>(codec_));
  }
  if (format_) {
    AMediaFormat_delete(static_cast<AMediaFormat*>(format_));
  }
  if (muxer_) {
    AMediaMuxer_delete(static_cast<AMediaMuxer*>(muxer_));
  }
  if (fd_ >= 0) {
    ::close(fd_);
  }
  codec_ = nullptr;
  format_ = nullptr;
  muxer_ = nullptr;
  muxer_track_index_ = -1;
  muxer_started_ = false;
  fd_ = -1;
  is_open_ = false;
}

}  // namespace audio
}  // namespace sezo
