#include <gtest/gtest.h>

#include <vector>

#include "audio/MP3Decoder.h"
#include "audio/WAVDecoder.h"
#include "test_helpers.h"

namespace sezo {
namespace audio {

TEST(DecoderTest, InvalidPathFails) {
  WAVDecoder wav;
  EXPECT_FALSE(wav.Open("fixtures/does_not_exist.wav"));
  EXPECT_FALSE(wav.IsOpen());

  MP3Decoder mp3;
  EXPECT_FALSE(mp3.Open("fixtures/does_not_exist.mp3"));
  EXPECT_FALSE(mp3.IsOpen());
}

TEST(DecoderTest, ReadsExpectedFrameCount) {
  const int32_t kSampleRate = 48000;
  const std::string wav_path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(wav_path)) {
    GTEST_SKIP() << "Missing fixture: " << wav_path;
  }

  WAVDecoder wav;
  ASSERT_TRUE(wav.Open(wav_path));
  const auto& wav_format = wav.GetFormat();
  EXPECT_EQ(wav_format.sample_rate, kSampleRate);
  EXPECT_EQ(wav_format.channels, 1);
  EXPECT_EQ(wav_format.total_frames, kSampleRate);

  std::vector<float> buffer(1024);
  size_t total_read = 0;
  while (true) {
    const size_t frames_read = wav.Read(buffer.data(), 1024);
    if (frames_read == 0) {
      break;
    }
    total_read += frames_read;
  }
  EXPECT_EQ(total_read, static_cast<size_t>(wav_format.total_frames));

  const std::string mp3_path = test::FixturePath("short.mp3");
  if (!test::FileExists(mp3_path)) {
    GTEST_SKIP() << "Missing MP3 fixture: " << mp3_path;
  }

  MP3Decoder mp3;
  ASSERT_TRUE(mp3.Open(mp3_path));
  const auto& mp3_format = mp3.GetFormat();
  EXPECT_EQ(mp3_format.sample_rate, kSampleRate);
  EXPECT_EQ(mp3_format.channels, 1);
  EXPECT_GT(mp3_format.total_frames, 0);

  buffer.assign(1024 * mp3_format.channels, 0.0f);
  total_read = 0;
  while (true) {
    const size_t frames_read = mp3.Read(buffer.data(), 1024);
    if (frames_read == 0) {
      break;
    }
    total_read += frames_read;
  }
  EXPECT_EQ(total_read, static_cast<size_t>(mp3_format.total_frames));
}

TEST(DecoderTest, PartialReadsReturnZeroOnEOF) {
  const std::string wav_path = test::FixturePath("mono_1khz_1s.wav");
  if (!test::FileExists(wav_path)) {
    GTEST_SKIP() << "Missing fixture: " << wav_path;
  }

  WAVDecoder wav;
  ASSERT_TRUE(wav.Open(wav_path));
  const auto& format = wav.GetFormat();

  std::vector<float> buffer(static_cast<size_t>(format.total_frames) + 64);
  const size_t frames_read = wav.Read(buffer.data(),
                                      static_cast<size_t>(format.total_frames) + 64);
  EXPECT_EQ(frames_read, static_cast<size_t>(format.total_frames));
  EXPECT_EQ(wav.Read(buffer.data(), 16), 0u);
}

}  // namespace audio
}  // namespace sezo
