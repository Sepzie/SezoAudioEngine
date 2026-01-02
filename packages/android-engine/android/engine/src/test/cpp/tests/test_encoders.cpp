#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "audio/MP3Encoder.h"
#include "audio/WAVEncoder.h"
#include "audio/WAVDecoder.h"
#include "test_helpers.h"

#if defined(__ANDROID__)
#include "audio/AACEncoder.h"
#endif

namespace sezo {
namespace audio {

TEST(EncoderTest, WavRejectsInvalidBitDepth) {
  WAVEncoder encoder;
  EncoderConfig config;
  config.format = EncoderFormat::kWAV;
  config.sample_rate = 48000;
  config.channels = 2;
  config.bits_per_sample = 12;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_wav_invalid_", ".wav"));
  EXPECT_FALSE(encoder.Open(temp_file.path(), config));
  EXPECT_FALSE(encoder.IsOpen());
}

TEST(EncoderTest, WavWritesExpectedFrames) {
  const int32_t kSampleRate = 48000;
  const int32_t kChannels = 2;
  const size_t kFrames = 480;

  std::vector<float> samples(kFrames * kChannels);
  const double kPi = 3.14159265358979323846;
  for (size_t i = 0; i < kFrames; ++i) {
    const float phase = static_cast<float>(2.0 * kPi * 1000.0 * static_cast<double>(i) /
                                           static_cast<double>(kSampleRate));
    const float value = 0.5f * std::sin(phase);
    samples[i * 2] = value;
    samples[i * 2 + 1] = value;
  }

  WAVEncoder encoder;
  EncoderConfig config;
  config.format = EncoderFormat::kWAV;
  config.sample_rate = kSampleRate;
  config.channels = kChannels;
  config.bits_per_sample = 16;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_wav_", ".wav"));
  ASSERT_TRUE(encoder.Open(temp_file.path(), config));
  ASSERT_TRUE(encoder.Write(samples.data(), kFrames));
  EXPECT_EQ(encoder.GetFramesWritten(), static_cast<int64_t>(kFrames));
  ASSERT_TRUE(encoder.Close());

  const int64_t expected_size =
      44 + static_cast<int64_t>(kFrames) * kChannels * (config.bits_per_sample / 8);
  EXPECT_EQ(encoder.GetFileSize(), expected_size);

  WAVDecoder decoder;
  ASSERT_TRUE(decoder.Open(temp_file.path()));
  const auto& format = decoder.GetFormat();
  EXPECT_EQ(format.sample_rate, kSampleRate);
  EXPECT_EQ(format.channels, kChannels);
  EXPECT_EQ(format.total_frames, static_cast<int64_t>(kFrames));
}

TEST(EncoderTest, Mp3BehaviorDependsOnLame) {
  MP3Encoder encoder;
  EncoderConfig config;
  config.format = EncoderFormat::kMP3;
  config.sample_rate = 48000;
  config.channels = 1;
  config.bitrate = 128000;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_mp3_", ".mp3"));

#ifdef SEZO_ENABLE_LAME
  ASSERT_TRUE(encoder.Open(temp_file.path(), config));
  std::vector<float> samples(480);
  const double kPi = 3.14159265358979323846;
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = 0.25f * std::sin(static_cast<float>(2.0 * kPi * 440.0 * i / 48000.0));
  }
  EXPECT_TRUE(encoder.Write(samples.data(), samples.size()));
  EXPECT_TRUE(encoder.Close());
  EXPECT_GT(encoder.GetFileSize(), 0);
#else
  EXPECT_FALSE(encoder.Open(temp_file.path(), config));
  EXPECT_FALSE(encoder.IsOpen());
#endif
}

#if defined(__ANDROID__)
TEST(EncoderTest, AacEncodesWithMediaCodec) {
  AACEncoder encoder;
  EncoderConfig config;
  config.format = EncoderFormat::kAAC;
  config.sample_rate = 48000;
  config.channels = 1;
  config.bitrate = 96000;

  test::ScopedTempFile temp_file(test::MakeTempPath("sezo_aac_", ".aac"));
  ASSERT_TRUE(encoder.Open(temp_file.path(), config));

  const size_t frames = 1024;
  std::vector<float> samples(frames);
  const double kPi = 3.14159265358979323846;
  for (size_t i = 0; i < frames; ++i) {
    samples[i] = 0.25f * std::sin(static_cast<float>(2.0 * kPi * 440.0 * i / 48000.0));
  }

  EXPECT_TRUE(encoder.Write(samples.data(), frames));
  EXPECT_TRUE(encoder.Close());
  EXPECT_GT(encoder.GetFileSize(), 0);
}
#else
TEST(EncoderTest, AacSkippedOnHost) {
  GTEST_SKIP() << "Android-only AAC encoder test.";
}
#endif

}  // namespace audio
}  // namespace sezo
