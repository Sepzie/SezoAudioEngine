#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "extraction/ExtractionPipeline.h"
#endif

namespace sezo {
namespace extraction {

#if defined(__ANDROID__)
TEST(ExtractionPipelineTest, ExtractSingleTrackAppliesEffects) {
  GTEST_SKIP() << "TODO: export track with pitch/speed and verify duration.";
}

TEST(ExtractionPipelineTest, ExportRespectsSoloMute) {
  GTEST_SKIP() << "TODO: ensure soloed tracks only are exported.";
}

TEST(ExtractionPipelineTest, ProgressIsMonotonic) {
  GTEST_SKIP() << "TODO: validate progress callback increments to 1.0.";
}
#else
TEST(ExtractionPipelineTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only extraction pipeline tests.";
}
#endif

}  // namespace extraction
}  // namespace sezo
