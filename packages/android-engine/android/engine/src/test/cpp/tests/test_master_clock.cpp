#include <gtest/gtest.h>

#include "core/MasterClock.h"

namespace sezo {
namespace core {

TEST(MasterClockTest, AdvanceAndGetPosition) {
  MasterClock clock;
  EXPECT_EQ(clock.GetPosition(), 0);

  clock.Advance(256);
  clock.Advance(128);
  EXPECT_EQ(clock.GetPosition(), 384);
}

TEST(MasterClockTest, ResetClearsPosition) {
  MasterClock clock;
  clock.Advance(512);
  EXPECT_EQ(clock.GetPosition(), 512);

  clock.Reset();
  EXPECT_EQ(clock.GetPosition(), 0);

  clock.Advance(7);
  EXPECT_EQ(clock.GetPosition(), 7);
}

TEST(MasterClockTest, SetPositionOverrides) {
  MasterClock clock;
  clock.Advance(10);
  clock.SetPosition(42);
  EXPECT_EQ(clock.GetPosition(), 42);

  clock.Advance(8);
  EXPECT_EQ(clock.GetPosition(), 50);
}

}  // namespace core
}  // namespace sezo
