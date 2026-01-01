#include <gtest/gtest.h>

#include "core/TransportController.h"

namespace sezo {
namespace core {

TEST(TransportControllerTest, PlayPauseStopTransitions) {
  TransportController transport;
  EXPECT_EQ(transport.GetState(), PlaybackState::kStopped);
  EXPECT_FALSE(transport.IsPlaying());

  transport.Play();
  EXPECT_EQ(transport.GetState(), PlaybackState::kPlaying);
  EXPECT_TRUE(transport.IsPlaying());

  transport.Pause();
  EXPECT_EQ(transport.GetState(), PlaybackState::kPaused);
  EXPECT_FALSE(transport.IsPlaying());

  transport.Play();
  EXPECT_EQ(transport.GetState(), PlaybackState::kPlaying);
  EXPECT_TRUE(transport.IsPlaying());

  transport.Stop();
  EXPECT_EQ(transport.GetState(), PlaybackState::kStopped);
  EXPECT_FALSE(transport.IsPlaying());
}

TEST(TransportControllerTest, PauseFromStoppedDoesNotPlay) {
  TransportController transport;
  transport.Pause();
  EXPECT_EQ(transport.GetState(), PlaybackState::kStopped);
  EXPECT_FALSE(transport.IsPlaying());
}

}  // namespace core
}  // namespace sezo
