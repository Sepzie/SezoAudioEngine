#pragma once

#include "audio/AudioEncoder.h"
#include "playback/Track.h"
#include "playback/MultiTrackMixer.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sezo {
namespace extraction {

/**
 * Progress callback for extraction operations.
 * @param progress Progress percentage (0.0 to 1.0)
 */
using ProgressCallback = std::function<void(float progress)>;

/**
 * Extraction configuration
 */
struct ExtractionConfig {
  audio::EncoderFormat format = audio::EncoderFormat::kWAV;
  int32_t sample_rate = 44100;
  int32_t bitrate = 128000;  // For compressed formats
  int32_t bits_per_sample = 16;  // For WAV
  bool include_effects = true;  // Apply pitch/speed effects during extraction
  std::string output_dir;  // Optional output directory
};

/**
 * Extraction result information
 */
struct ExtractionResult {
  std::string track_id;  // Empty for mixed output
  std::string output_path;
  int64_t duration_samples = 0;
  int64_t file_size = 0;
  audio::EncoderFormat format = audio::EncoderFormat::kWAV;
  int32_t bitrate = 0;
  bool success = false;
  std::string error_message;
};

/**
 * Offline extraction pipeline for rendering tracks to audio files.
 * This pipeline runs outside the real-time audio callback and can
 * render tracks with effects applied to various output formats.
 */
class ExtractionPipeline {
 public:
  ExtractionPipeline();
  ~ExtractionPipeline();

  /**
   * Extract a single track to an audio file.
   * @param track Track to extract
   * @param output_path Output file path
   * @param config Extraction configuration
   * @param progress_callback Optional progress callback
   * @return Extraction result
   */
  ExtractionResult ExtractTrack(
      std::shared_ptr<playback::Track> track,
      const std::string& output_path,
      const ExtractionConfig& config,
      ProgressCallback progress_callback = nullptr);

  /**
   * Extract multiple tracks mixed together to an audio file.
   * @param tracks Tracks to mix and extract
   * @param output_path Output file path
   * @param config Extraction configuration
   * @param progress_callback Optional progress callback
   * @return Extraction result
   */
  ExtractionResult ExtractMixedTracks(
      const std::vector<std::shared_ptr<playback::Track>>& tracks,
      const std::string& output_path,
      const ExtractionConfig& config,
      ProgressCallback progress_callback = nullptr);

 private:
  /**
   * Create an encoder for the specified format.
   */
  std::unique_ptr<audio::AudioEncoder> CreateEncoder(audio::EncoderFormat format);

  static constexpr size_t kRenderBufferFrames = 4096;
};

}  // namespace extraction
}  // namespace sezo
