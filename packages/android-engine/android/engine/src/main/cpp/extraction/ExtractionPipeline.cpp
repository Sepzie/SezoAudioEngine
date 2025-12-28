#include "extraction/ExtractionPipeline.h"
#include "audio/WAVEncoder.h"

#include <android/log.h>
#include <algorithm>
#include <cstring>

#define LOG_TAG "ExtractionPipeline"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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
      LOGE("AAC encoder not yet implemented");
      return nullptr;
    case audio::EncoderFormat::kMP3:
      LOGE("MP3 encoder not yet implemented");
      return nullptr;
    default:
      LOGE("Unknown encoder format");
      return nullptr;
  }
}

ExtractionResult ExtractionPipeline::ExtractTrack(
    std::shared_ptr<playback::Track> track,
    const std::string& output_path,
    const ExtractionConfig& config,
    ProgressCallback progress_callback) {

  ExtractionResult result;
  result.track_id = track->GetId();
  result.output_path = output_path;
  result.format = config.format;

  if (!track || !track->IsLoaded()) {
    result.error_message = "Track not loaded";
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
  encoder_config.channels = track->GetChannels();
  encoder_config.bitrate = config.bitrate;
  encoder_config.bits_per_sample = config.bits_per_sample;

  // Open encoder
  if (!encoder->Open(output_path, encoder_config)) {
    result.error_message = "Failed to open encoder";
    LOGE("%s: %s", result.error_message.c_str(), output_path.c_str());
    return result;
  }

  LOGD("Extracting track '%s' to '%s'", track->GetId().c_str(), output_path.c_str());

  // Seek track to beginning
  track->Seek(0);

  // Get track duration
  int64_t total_frames = track->GetDuration();
  int64_t frames_processed = 0;

  // Render buffer
  std::vector<float> buffer(kRenderBufferFrames * track->GetChannels());

  bool success = true;
  while (frames_processed < total_frames) {
    // Calculate frames to render this iteration
    size_t frames_to_render = static_cast<size_t>(
        std::min(static_cast<int64_t>(kRenderBufferFrames),
                 total_frames - frames_processed));

    // Render audio from track
    size_t frames_rendered = RenderTrack(
        track, buffer.data(), frames_to_render, config.include_effects);

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

    frames_processed += static_cast<int64_t>(frames_rendered);

    // Report progress
    if (progress_callback && total_frames > 0) {
      float progress = static_cast<float>(frames_processed) /
                       static_cast<float>(total_frames);
      progress_callback(progress);
    }

    // If we rendered fewer frames than requested, we're at the end
    if (frames_rendered < frames_to_render) {
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
  }

  return result;
}

ExtractionResult ExtractionPipeline::ExtractMixedTracks(
    const std::vector<std::shared_ptr<playback::Track>>& tracks,
    const std::string& output_path,
    const ExtractionConfig& config,
    ProgressCallback progress_callback) {

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

  // Create encoder
  auto encoder = CreateEncoder(config.format);
  if (!encoder) {
    result.error_message = "Failed to create encoder";
    LOGE("%s", result.error_message.c_str());
    return result;
  }

  // Get output channels (use first track as reference)
  int32_t output_channels = tracks[0]->GetChannels();

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

  // Seek all tracks to beginning
  for (const auto& track : tracks) {
    track->Seek(0);
  }

  // Find longest track duration
  int64_t total_frames = 0;
  for (const auto& track : tracks) {
    total_frames = std::max(total_frames, track->GetDuration());
  }

  int64_t frames_processed = 0;

  // Render buffer
  std::vector<float> buffer(kRenderBufferFrames * output_channels);

  bool success = true;
  while (frames_processed < total_frames) {
    // Calculate frames to render this iteration
    size_t frames_to_render = static_cast<size_t>(
        std::min(static_cast<int64_t>(kRenderBufferFrames),
                 total_frames - frames_processed));

    // Render mixed audio
    size_t frames_rendered = RenderMixedTracks(
        tracks, buffer.data(), frames_to_render);

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

    frames_processed += static_cast<int64_t>(frames_rendered);

    // Report progress
    if (progress_callback && total_frames > 0) {
      float progress = static_cast<float>(frames_processed) /
                       static_cast<float>(total_frames);
      progress_callback(progress);
    }

    // If we rendered fewer frames than requested, we're at the end
    if (frames_rendered < frames_to_render) {
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
  }

  return result;
}

size_t ExtractionPipeline::RenderTrack(
    std::shared_ptr<playback::Track> track,
    float* buffer,
    size_t frames,
    bool include_effects [[maybe_unused]]) {

  if (!track || !track->IsLoaded()) {
    return 0;
  }

  // Read samples from track
  // The Track::ReadSamples already applies effects if they are set
  // (pitch/speed/volume/pan/mute/solo)
  // Note: include_effects parameter is reserved for future use when we may want to
  // optionally bypass effects during extraction
  size_t frames_read = track->ReadSamples(buffer, frames);

  return frames_read;
}

size_t ExtractionPipeline::RenderMixedTracks(
    const std::vector<std::shared_ptr<playback::Track>>& tracks,
    float* buffer,
    size_t frames) {

  if (tracks.empty() || frames == 0) {
    return 0;
  }

  // Get channels from first track
  int32_t channels = tracks[0]->GetChannels();
  size_t total_samples = frames * channels;

  // Clear output buffer
  std::memset(buffer, 0, total_samples * sizeof(float));

  // Temporary buffer for each track
  std::vector<float> track_buffer(total_samples);

  size_t min_frames_read = frames;
  bool any_track_active = false;

  // Mix all tracks
  for (const auto& track : tracks) {
    if (!track || !track->IsLoaded()) {
      continue;
    }

    // Skip muted tracks (unless solo is active)
    if (track->IsMuted() && !track->IsSolo()) {
      continue;
    }

    // Read from this track
    std::memset(track_buffer.data(), 0, total_samples * sizeof(float));
    size_t frames_read = track->ReadSamples(track_buffer.data(), frames);

    if (frames_read > 0) {
      any_track_active = true;
      min_frames_read = std::min(min_frames_read, frames_read);

      // Mix into output buffer
      for (size_t i = 0; i < frames_read * channels; ++i) {
        buffer[i] += track_buffer[i];
      }
    }
  }

  if (!any_track_active) {
    return 0;
  }

  // Clamp mixed output to prevent clipping
  for (size_t i = 0; i < min_frames_read * channels; ++i) {
    buffer[i] = std::max(-1.0f, std::min(1.0f, buffer[i]));
  }

  return min_frames_read;
}

}  // namespace extraction
}  // namespace sezo
