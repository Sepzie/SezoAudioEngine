#include <gtest/gtest.h>

#include "audio/MP3Decoder.h"
#include "audio/WAVDecoder.h"

namespace sezo {
namespace audio {

TEST(DecoderTest, InvalidPathFails) {
  GTEST_SKIP() << "TODO: open invalid path and expect Open() == false.";
}

TEST(DecoderTest, ReadsExpectedFrameCount) {
  GTEST_SKIP() << "TODO: use fixtures to validate duration and frame counts.";
}

TEST(DecoderTest, PartialReadsReturnZeroOnEOF) {
  GTEST_SKIP() << "TODO: read past EOF and verify safe behavior.";
}

}  // namespace audio
}  // namespace sezo
