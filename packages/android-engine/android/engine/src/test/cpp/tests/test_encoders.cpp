#include <gtest/gtest.h>

#include "audio/MP3Encoder.h"
#include "audio/WAVEncoder.h"

#if defined(__ANDROID__)
#include "audio/AACEncoder.h"
#endif

namespace sezo {
namespace audio {

TEST(EncoderTest, WavRejectsInvalidBitDepth) {
  GTEST_SKIP() << "TODO: bits_per_sample outside 16/24/32 should fail.";
}

TEST(EncoderTest, WavWritesExpectedFrames) {
  GTEST_SKIP() << "TODO: write known samples and validate file size/frames.";
}

TEST(EncoderTest, Mp3BehaviorDependsOnLame) {
  GTEST_SKIP() << "TODO: without LAME, Open() fails; with LAME, encodes.";
}

#if defined(__ANDROID__)
TEST(EncoderTest, AacEncodesWithMediaCodec) {
  GTEST_SKIP() << "TODO: encode short buffer and verify output file size > 0.";
}
#else
TEST(EncoderTest, AacSkippedOnHost) {
  GTEST_SKIP() << "Android-only AAC encoder test.";
}
#endif

}  // namespace audio
}  // namespace sezo
