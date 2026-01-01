#include <gtest/gtest.h>

#include "core/CircularBuffer.h"

namespace sezo {
namespace core {

TEST(CircularBufferTest, WrapAroundPreservesOrder) {
  GTEST_SKIP() << "TODO: write > capacity and ensure read order is preserved across wrap.";
}

TEST(CircularBufferTest, AvailableAndFreeSpaceInvariants) {
  GTEST_SKIP() << "TODO: validate Available() + FreeSpace() == capacity - 1.";
}

TEST(CircularBufferTest, ResetClearsReadWritePositions) {
  GTEST_SKIP() << "TODO: after Reset(), reads return 0 and positions restart at 0.";
}

}  // namespace core
}  // namespace sezo
