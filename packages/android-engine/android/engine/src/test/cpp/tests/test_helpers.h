#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <chrono>
#include <system_error>

namespace sezo {
namespace test {

inline std::string FixturesDir() {
  const char* env_dir = std::getenv("SEZO_TEST_FIXTURES_DIR");
  if (env_dir && *env_dir) {
    return std::string(env_dir);
  }
  std::filesystem::path path = std::filesystem::path(__FILE__).parent_path().parent_path();
  path /= "fixtures";
  return path.string();
}

inline std::string FixturePath(const std::string& name) {
  std::filesystem::path path = FixturesDir();
  path /= name;
  return path.string();
}

inline bool FileExists(const std::string& path) {
  return std::filesystem::exists(path);
}

inline std::string MakeTempPath(const std::string& prefix, const std::string& suffix) {
#if defined(__ANDROID__)
  std::filesystem::path base("/data/local/tmp");
#else
  const char* env_tmp = std::getenv("TMPDIR");
  std::filesystem::path base = env_tmp ? env_tmp : "/tmp";
#endif
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::string filename = prefix + std::to_string(now) + suffix;
  return (base / filename).string();
}

class ScopedTempFile {
 public:
  explicit ScopedTempFile(std::string path) : path_(std::move(path)) {}
  ~ScopedTempFile() {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

inline float Rms(const float* data, size_t count) {
  if (!data || count == 0) {
    return 0.0f;
  }
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const double value = static_cast<double>(data[i]);
    sum += value * value;
  }
  return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

inline float MaxAbs(const float* data, size_t count) {
  float max_value = 0.0f;
  if (!data) {
    return max_value;
  }
  for (size_t i = 0; i < count; ++i) {
    const float value = std::abs(data[i]);
    if (value > max_value) {
      max_value = value;
    }
  }
  return max_value;
}

inline bool AllFinite(const float* data, size_t count) {
  if (!data) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!std::isfinite(data[i])) {
      return false;
    }
  }
  return true;
}

// Frequency of a tone from its rising zero crossings (first channel only).
inline float EstimateFrequencyHz(const float* data,
                                 size_t frames,
                                 int32_t channels,
                                 int32_t sample_rate) {
  if (!data || frames < 2 || channels <= 0 || sample_rate <= 0) {
    return 0.0f;
  }
  size_t first = 0;
  size_t last = 0;
  size_t crossings = 0;
  for (size_t i = 1; i < frames; ++i) {
    const float prev = data[(i - 1) * static_cast<size_t>(channels)];
    const float curr = data[i * static_cast<size_t>(channels)];
    if (prev < 0.0f && curr >= 0.0f) {
      if (crossings == 0) {
        first = i;
      }
      last = i;
      ++crossings;
    }
  }
  if (crossings < 2) {
    return 0.0f;
  }
  return static_cast<float>(crossings - 1) * static_cast<float>(sample_rate) /
      static_cast<float>(last - first);
}

inline float SemitonesToRatio(float semitones) {
  return std::pow(2.0f, semitones / 12.0f);
}

}  // namespace test
}  // namespace sezo
