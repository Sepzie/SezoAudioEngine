#include <gtest/gtest.h>

#include "core/CircularBuffer.h"

namespace sezo {
namespace core {

TEST(SmokeTest, CircularBufferReadWrite) {
  CircularBuffer buffer(8);
  float input[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  float output[4] = {};

  EXPECT_EQ(buffer.Write(input, 4), 4u);
  EXPECT_EQ(buffer.Read(output, 4), 4u);

  for (int i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(output[i], input[i]);
  }
}

}  // namespace core
}  // namespace sezo
