#include <gtest/gtest.h>

#include "core/CircularBuffer.h"

namespace sezo {
namespace core {

TEST(CircularBufferTest, WrapAroundPreservesOrder) {
  CircularBuffer buffer(5);
  float first[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  float second[3] = {5.0f, 6.0f, 7.0f};
  float output_first[3] = {};
  float output_second[4] = {};

  EXPECT_EQ(buffer.Write(first, 4), 4u);
  EXPECT_EQ(buffer.Read(output_first, 3), 3u);
  for (int i = 0; i < 3; ++i) {
    EXPECT_FLOAT_EQ(output_first[i], first[i]);
  }

  EXPECT_EQ(buffer.Write(second, 3), 3u);
  EXPECT_EQ(buffer.Read(output_second, 4), 4u);
  const float expected[4] = {4.0f, 5.0f, 6.0f, 7.0f};
  for (int i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(output_second[i], expected[i]);
  }
}

TEST(CircularBufferTest, AvailableAndFreeSpaceInvariants) {
  const size_t capacity = 8;
  CircularBuffer buffer(capacity);
  const size_t expected_total = capacity - 1;

  auto expect_invariant = [&]() {
    EXPECT_EQ(buffer.Available() + buffer.FreeSpace(), expected_total);
  };

  expect_invariant();

  float data[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  EXPECT_EQ(buffer.Write(data, 3), 3u);
  EXPECT_EQ(buffer.Available(), 3u);
  EXPECT_EQ(buffer.FreeSpace(), expected_total - 3);
  expect_invariant();

  float output[2] = {};
  EXPECT_EQ(buffer.Read(output, 2), 2u);
  EXPECT_EQ(buffer.Available(), 1u);
  EXPECT_EQ(buffer.FreeSpace(), expected_total - 1);
  expect_invariant();

  EXPECT_EQ(buffer.Write(data, 4), 4u);
  EXPECT_EQ(buffer.Available(), 5u);
  EXPECT_EQ(buffer.FreeSpace(), expected_total - 5);
  expect_invariant();
}

TEST(CircularBufferTest, ResetClearsReadWritePositions) {
  CircularBuffer buffer(6);
  float input[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  float output[4] = {};

  EXPECT_EQ(buffer.Write(input, 4), 4u);
  EXPECT_EQ(buffer.Read(output, 2), 2u);

  buffer.Reset();
  EXPECT_EQ(buffer.Available(), 0u);
  EXPECT_EQ(buffer.FreeSpace(), 5u);
  EXPECT_EQ(buffer.Read(output, 4), 0u);

  EXPECT_EQ(buffer.Write(input, 2), 2u);
  EXPECT_EQ(buffer.Read(output, 2), 2u);
  EXPECT_FLOAT_EQ(output[0], input[0]);
  EXPECT_FLOAT_EQ(output[1], input[1]);
}

}  // namespace core
}  // namespace sezo
