#include "extraction/ExtractionPipeline.h"
#include "audio/AACEncoder.h"
#include "audio/MP3Decoder.h"
#include "audio/MP3Encoder.h"
#include "audio/WAVDecoder.h"
#include "audio/WAVEncoder.h"
#include "playback/TimeStretch.h"

#include <android/log.h>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

#define LOG_TAG "ExtractionPipeline"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kProgressStep = 0.01f;

bool HasExtension(const std::string& path, const char* extension) {
  const size_t path_len = path.size();
  const size_t ext_len = std::strlen(extension);
  if (path_len < ext_len) {
    return false;
  }

  const size_t start = path_len - ext_len;
  for (size_t i = 0; i < ext_len; ++i) {
    const char path_char = static_cast<char>(
        std::tolower(static_cast<unsigned char>(path[start + i])));
    if (path_char != extension[i]) {
      return false;
    }
  }
  return true;
}

std::unique_ptr<sezo::audio::AudioDecoder> CreateDecoderForPath(
    const std::string& path) {
  if (HasExtension(path, ".mp3")) {
    return std::make_unique<sezo::audio::MP3Decoder>();
  }
  if (HasExtension(path, ".wav")) {
    return std::make_unique<sezo::audio::WAVDecoder>();
  }
  return nullptr;
}

struct OfflineTrackState {
  std::shared_ptr<sezo::playback::Track> track;
  std::unique_ptr<sezo::audio::AudioDecoder> decoder;
  std::unique_ptr<sezo::playback::TimeStretch> time_stretcher;
  std::vector<float> stretch_input_buffer;
  double stretch_input_fraction = 0.0;
  int32_t channels = 0;
  float volume = 1.0f;
  float pan = 0.0f;
  bool muted = false;
  bool solo = false;
  int64_t total_frames = 0;
  int64_t input_frames_processed = 0;
};

bool InitOfflineState(OfflineTrackState& state, bool include_effects, std::string* error) {
  if (!state.track) {
    if (error) {
      *error = "Track is null";
    }
    return false;
  }

  state.decoder = CreateDecoderForPath(state.track->GetFilePath());
  if (!state.decoder) {
    if (error) {
      *error = "Unsupported audio format: " + state.track->GetFilePath();
    }
    return false;
  }

  if (!state.decoder->Open(state.track->GetFilePath())) {
    if (error) {
      *error = "Failed to open decoder: " + state.track->GetFilePath();
    }
    return false;
  }

  state.channels = state.decoder->GetFormat().channels;
  state.total_frames = state.decoder->GetFormat().total_frames;
  state.volume = state.track->GetVolume();
  state.pan = state.track->GetPan();
  state.muted = state.track->IsMuted();
  state.solo = state.track->IsSolo();

  if (include_effects && state.channels > 0 && state.channels <= 2) {
    state.time_stretcher = std::make_unique<sezo::playback::TimeStretch>(
        state.decoder->GetFormat().sample_rate,
        state.channels);
    state.time_stretcher->SetPitchSemitones(state.track->GetPitchSemitones());
    state.time_stretcher->SetStretchFactor(state.track->GetStretchFactor());
    if (!state.time_stretcher->IsActive()) {
      state.time_stretcher.reset();
    }
  }

  return true;
}

void ApplyVolumePan(float* buffer, size_t frames, int32_t channels, float volume, float pan) {
  if (channels == 2) {
    const float left_gain = volume * std::cos((pan + 1.0f) * 0.25f * kPi);
    const float right_gain = volume * std::sin((pan + 1.0f) * 0.25f * kPi);
    for (size_t i = 0; i < frames * 2; i += 2) {
      buffer[i] *= left_gain;
      buffer[i + 1] *= right_gain;
    }
  } else if (channels == 1 && volume != 1.0f) {
    for (size_t i = 0; i < frames; ++i) {
      buffer[i] *= volume;
    }
  }
}

double GetStretchFactor(const OfflineTrackState& state, bool include_effects) {
  if (!include_effects || !state.time_stretcher) {
    return 1.0;
  }
  return static_cast<double>(state.time_stretcher->GetStretchFactor());
}

size_t RenderOfflineTrack(
    OfflineTrackState& state,
    float* output,
    size_t frames,
    bool include_effects,
    size_t* input_frames_read) {

  if (!state.decoder || frames == 0 || state.channels <= 0) {
    if (input_frames_read) {
      *input_frames_read = 0;
    }
    return 0;
  }

  if (state.muted) {
    std::fill_n(output, frames * static_cast<size_t>(state.channels), 0.0f);
    if (input_frames_read) {
      *input_frames_read = frames;
    }
    return frames;
  }

  const bool use_time_stretch = include_effects && state.time_stretcher &&
      state.time_stretcher->IsActive() && (state.channels == 1 || state.channels == 2);

  if (use_time_stretch) {
    const float stretch = state.time_stretcher->GetStretchFactor();
    const double requested_input =
        static_cast<double>(frames) * stretch + state.stretch_input_fraction;
    size_t input_frames = static_cast<size_t>(requested_input);
    state.stretch_input_fraction = requested_input - static_cast<double>(input_frames);
    if (input_frames < 1) {
      input_frames = 1;
    }

    const size_t input_samples = input_frames * static_cast<size_t>(state.channels);
    if (state.stretch_input_buffer.size() < input_samples) {
      state.stretch_input_buffer.resize(input_samples);
    }

    const size_t frames_read = state.decoder->Read(
        state.stretch_input_buffer.data(), input_frames);
    if (frames_read == 0) {
      if (input_frames_read) {
        *input_frames_read = 0;
      }
      return 0;
    }

    if (frames_read < input_frames) {
      std::fill_n(
          state.stretch_input_buffer.data() + frames_read * state.channels,
          (input_frames - frames_read) * state.channels,
          0.0f);
    }

    state.time_stretcher->Process(
        state.stretch_input_buffer.data(), input_frames, output, frames);
    ApplyVolumePan(output, frames, state.channels, state.volume, state.pan);

    if (input_frames_read) {
      *input_frames_read = frames_read;
    }
    return frames;
  }

  const size_t frames_read = state.decoder->Read(output, frames);
  if (frames_read == 0) {
    if (input_frames_read) {
      *input_frames_read = 0;
    }
    return 0;
  }

  ApplyVolumePan(output, frames_read, state.channels, state.volume, state.pan);
  if (frames_read < frames) {
    std::fill_n(output + frames_read * state.channels,
                (frames - frames_read) * state.channels,
                0.0f);
  }

  if (input_frames_read) {
    *input_frames_read = frames_read;
  }
  return frames_read;
}

}  // namespace

namespace sezo {
namespace extraction {

ExtractionPipeline::ExtractionPipeline() = default;

ExtractionPipeline::~ExtractionPipeline() = default;

std::unique_ptr<audio::AudioEncoder> ExtractionPipeline::CreateEncoder(
    audio::EncoderFormat format) {
  switch (format) {
    case audio::EncoderFormat::kWAV:
      return std::make_unique<audio::WAVEncoder>();
    case audio::EncoderFormat::kAAC:
      return std::make_unique<audio::AACEncoder>();
    case audio::EncoderFormat::kMP3:
      return std::make_unique<audio::MP3Encoder>();
    default:
      LOGE("Unknown encoder format");
      return nullptr;
  }
}

ExtractionResult ExtractionPipeline::ExtractTrack(
    std::shared_ptr<playback::Track> track,
    const std::string& output_path,
    const ExtractionConfig& config,
    ProgressCallback progress_callback,
    std::atomic<bool>* cancel_flag) {

  ExtractionResult result;
  result.track_id = track->GetId();
  result.output_path = output_path;
  result.format = config.format;

  if (!track || !track->IsLoaded()) {
    result.error_message = "Track not loaded";
    LOGE("%s", result.error_message.c_str());
    return result;
  }

  OfflineTrackState state;
  state.track = track;
  if (!InitOfflineState(state, config.include_effects, &result.error_message)) {
    LOGE("%s", result.error_message.c_str());
    return result;
  }
  if (state.channels <= 0) {
    result.error_message = "Invalid track channels";
    LOGE("%s", result.error_message.c_str());
    return result;
  }

  // Create encoder
  auto encoder = CreateEncoder(config.format);
  if (!encoder) {
    result.error_message = "Failed to create encoder";
    LOGE("%s", result.error_message.c_str());
    return result;
  }

  // Setup encoder config
  audio::EncoderConfig encoder_config;
  encoder_config.format = config.format;
  encoder_config.sample_rate = config.sample_rate;
  encoder_config.channels = state.channels;
  encoder_config.bitrate = config.bitrate;
  encoder_config.bits_per_sample = config.bits_per_sample;

  // Open encoder
  if (!encoder->Open(output_path, encoder_config)) {
    result.error_message = "Failed to open encoder";
    LOGE("%s: %s", result.error_message.c_str(), output_path.c_str());
    return result;
  }

  LOGD("Extracting track '%s' to '%s'", track->GetId().c_str(), output_path.c_str());

  // Get track duration
  const int64_t total_frames = state.total_frames;
  int64_t input_frames_processed = 0;
  float last_progress = -1.0f;

  // Render buffer
  std::vector<float> buffer(kRenderBufferFrames * state.channels);

  bool success = true;
  while (total_frames <= 0 || input_frames_processed < total_frames) {
    if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
      result.error_message = "Extraction cancelled";
      success = false;
      break;
    }
    // Calculate frames to render this iteration
    size_t frames_to_render = kRenderBufferFrames;
    if (total_frames > 0) {
      const double stretch = GetStretchFactor(state, config.include_effects);
      const double remaining_output =
          static_cast<double>(total_frames - input_frames_processed) / stretch;
      if (remaining_output <= 0.0) {
        break;
      }
      frames_to_render = static_cast<size_t>(
          std::min<double>(remaining_output, kRenderBufferFrames));
      if (frames_to_render == 0) {
        frames_to_render = 1;
      }
    }

    // Render audio from track
    size_t input_frames_read = 0;
    size_t frames_rendered = RenderOfflineTrack(
        state, buffer.data(), frames_to_render, config.include_effects, &input_frames_read);

    if (frames_rendered == 0) {
      // End of track
      break;
    }

    // Write to encoder
    if (!encoder->Write(buffer.data(), frames_rendered)) {
      result.error_message = "Failed to write to encoder";
      LOGE("%s", result.error_message.c_str());
      success = false;
      break;
    }

    input_frames_processed += static_cast<int64_t>(
        input_frames_read > 0 ? input_frames_read : frames_rendered);

    // Report progress
    if (progress_callback && total_frames > 0 &&
        !(cancel_flag && cancel_flag->load(std::memory_order_acquire))) {
      float progress = static_cast<float>(input_frames_processed) /
                       static_cast<float>(total_frames);
      progress = std::min(1.0f, std::max(0.0f, progress));
      if (progress >= 1.0f || progress - last_progress >= kProgressStep) {
        last_progress = progress;
        progress_callback(progress);
      }
    }

    // If we rendered fewer frames than requested, we're at the end
    if (frames_rendered < frames_to_render && input_frames_read == 0) {
      break;
    }
  }

  // Close encoder
  if (!encoder->Close()) {
    result.error_message = "Failed to close encoder";
    LOGE("%s", result.error_message.c_str());
    success = false;
  }

  // Fill in result
  result.duration_samples = encoder->GetFramesWritten();
  result.file_size = encoder->GetFileSize();
  result.bitrate = config.bitrate;
  result.success = success;

  if (success) {
    LOGD("Successfully extracted track '%s': %lld frames, %lld bytes",
         track->GetId().c_str(),
         static_cast<long long>(result.duration_samples),
         static_cast<long long>(result.file_size));
  } else if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
    std::remove(output_path.c_str());
  }

  return result;
}

ExtractionResult ExtractionPipeline::ExtractMixedTracks(
    const std::vector<std::shared_ptr<playback::Track>>& tracks,
    const std::string& output_path,
    const ExtractionConfig& config,
    ProgressCallback progress_callback,
    std::atomic<bool>* cancel_flag) {

  ExtractionResult result;
  result.output_path = output_path;
  result.format = config.format;

  if (tracks.empty()) {
    result.error_message = "No tracks provided";
    LOGE("%s", result.error_message.c_str());
    return result;
  }

  // Verify all tracks are loaded
  for (const auto& track : tracks) {
    if (!track || !track->IsLoaded()) {
      result.error_message = "One or more tracks not loaded";
      LOGE("%s", result.error_message.c_str());
      return result;
    }
  }

  std::vector<OfflineTrackState> states;
  states.reserve(tracks.size());
  for (const auto& track : tracks) {
    OfflineTrackState state;
    state.track = track;
    std::string error;
    if (!InitOfflineState(state, config.include_effects, &error)) {
      result.error_message = error;
      LOGE("%s", result.error_message.c_str());
      return result;
    }
    if (state.channels <= 0) {
      result.error_message = "Invalid track channels";
      LOGE("%s", result.error_message.c_str());
      return result;
    }
    states.push_back(std::move(state));
  }

  // Create encoder
  auto encoder = CreateEncoder(config.format);
  if (!encoder) {
    result.error_message = "Failed to create encoder";
    LOGE("%s", result.error_message.c_str());
    return result;
  }

  // Get output channels (use first track as reference)
  int32_t output_channels = states[0].channels;

  // Setup encoder config
  audio::EncoderConfig encoder_config;
  encoder_config.format = config.format;
  encoder_config.sample_rate = config.sample_rate;
  encoder_config.channels = output_channels;
  encoder_config.bitrate = config.bitrate;
  encoder_config.bits_per_sample = config.bits_per_sample;

  // Open encoder
  if (!encoder->Open(output_path, encoder_config)) {
    result.error_message = "Failed to open encoder";
    LOGE("%s: %s", result.error_message.c_str(), output_path.c_str());
    return result;
  }

  LOGD("Extracting %zu mixed tracks to '%s'", tracks.size(), output_path.c_str());

  bool has_solo = false;
  for (const auto& state : states) {
    if (state.solo) {
      has_solo = true;
      break;
    }
  }

  // Find longest track duration (input frames)
  int64_t total_frames = 0;
  for (const auto& state : states) {
    if (state.total_frames > total_frames) {
      total_frames = state.total_frames;
    }
  }

  float last_progress = -1.0f;

  // Render buffer
  std::vector<float> buffer(kRenderBufferFrames * output_channels);

  bool success = true;
  while (true) {
    if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
      result.error_message = "Extraction cancelled";
      success = false;
      break;
    }
    // Calculate frames to render this iteration
    double max_remaining_output = 0.0;
    for (const auto& state : states) {
      if (!state.decoder) {
        continue;
      }
      if (has_solo && !state.solo) {
        continue;
      }
      if (state.muted) {
        continue;
      }
      if (state.total_frames <= 0) {
        max_remaining_output = static_cast<double>(kRenderBufferFrames);
        break;
      }
      const double stretch = GetStretchFactor(state, config.include_effects);
      const double remaining_input =
          static_cast<double>(state.total_frames - state.input_frames_processed);
      if (remaining_input <= 0.0) {
        continue;
      }
      const double remaining_output = remaining_input / stretch;
      if (remaining_output > max_remaining_output) {
        max_remaining_output = remaining_output;
      }
    }

    if (max_remaining_output <= 0.0) {
      break;
    }

    size_t frames_to_render = static_cast<size_t>(
        std::min<double>(max_remaining_output, kRenderBufferFrames));
    if (frames_to_render == 0) {
      frames_to_render = 1;
    }

    // Render mixed audio
    std::memset(buffer.data(), 0, frames_to_render * output_channels * sizeof(float));

    std::vector<float> track_buffer(frames_to_render * output_channels);
    size_t min_frames_read = frames_to_render;
    bool any_track_active = false;

    for (auto& state : states) {
      if (!state.decoder) {
        continue;
      }
      if (has_solo && !state.solo) {
        continue;
      }
      if (state.muted) {
        continue;
      }

      std::memset(track_buffer.data(), 0, track_buffer.size() * sizeof(float));
      size_t input_frames_read = 0;
      const size_t frames_read = RenderOfflineTrack(
          state, track_buffer.data(), frames_to_render, config.include_effects, &input_frames_read);

      if (frames_read > 0) {
        any_track_active = true;
        min_frames_read = std::min(min_frames_read, frames_read);
        state.input_frames_processed += static_cast<int64_t>(
            input_frames_read > 0 ? input_frames_read : frames_read);

        for (size_t i = 0; i < frames_read * output_channels; ++i) {
          buffer[i] += track_buffer[i];
        }
      }
    }

    if (!any_track_active) {
      break;
    }

    // Clamp mixed output to prevent clipping
    for (size_t i = 0; i < min_frames_read * output_channels; ++i) {
      buffer[i] = std::max(-1.0f, std::min(1.0f, buffer[i]));
    }

    size_t frames_rendered = min_frames_read;

    if (frames_rendered == 0) {
      // End of all tracks
      break;
    }

    // Write to encoder
    if (!encoder->Write(buffer.data(), frames_rendered)) {
      result.error_message = "Failed to write to encoder";
      LOGE("%s", result.error_message.c_str());
      success = false;
      break;
    }

    int64_t max_input_processed = 0;
    for (const auto& state : states) {
      if (state.input_frames_processed > max_input_processed) {
        max_input_processed = state.input_frames_processed;
      }
    }

    // Report progress
    if (progress_callback && total_frames > 0 &&
        !(cancel_flag && cancel_flag->load(std::memory_order_acquire))) {
      float progress = static_cast<float>(max_input_processed) /
                       static_cast<float>(total_frames);
      progress = std::min(1.0f, std::max(0.0f, progress));
      if (progress >= 1.0f || progress - last_progress >= kProgressStep) {
        last_progress = progress;
        progress_callback(progress);
      }
    }

    // If we rendered fewer frames than requested, we're at the end
    if (frames_rendered < frames_to_render) {
      break;
    }

    if (total_frames > 0 && max_input_processed >= total_frames) {
      break;
    }
  }

  // Close encoder
  if (!encoder->Close()) {
    result.error_message = "Failed to close encoder";
    LOGE("%s", result.error_message.c_str());
    success = false;
  }

  // Fill in result
  result.duration_samples = encoder->GetFramesWritten();
  result.file_size = encoder->GetFileSize();
  result.bitrate = config.bitrate;
  result.success = success;

  if (success) {
    LOGD("Successfully extracted %zu mixed tracks: %lld frames, %lld bytes",
         tracks.size(),
         static_cast<long long>(result.duration_samples),
         static_cast<long long>(result.file_size));
  } else if (cancel_flag && cancel_flag->load(std::memory_order_acquire)) {
    std::remove(output_path.c_str());
  }

  return result;
}

}  // namespace extraction
}  // namespace sezo
