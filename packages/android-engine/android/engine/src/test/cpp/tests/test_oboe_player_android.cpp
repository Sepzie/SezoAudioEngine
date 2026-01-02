#include <gtest/gtest.h>

#if defined(__ANDROID__)
#include "playback/OboePlayer.h"
#include "test_helpers.h"

#include <algorithm>
#include <vector>
#endif

namespace sezo {
namespace playback {

#if defined(__ANDROID__)
TEST(OboePlayerTest, InitializeStartStopLifecycle) {
  auto clock = std::make_shared<core::MasterClock>();
  auto transport = std::make_shared<core::TransportController>();
  auto mixer = std::make_shared<MultiTrackMixer>();

  OboePlayer player(mixer, clock, transport);
  ASSERT_TRUE(player.Initialize(48000));
  EXPECT_FALSE(player.IsRunning());

  transport->Play();
  EXPECT_TRUE(player.Start());
  EXPECT_TRUE(player.IsRunning());

  EXPECT_TRUE(player.Stop());
  EXPECT_FALSE(player.IsRunning());
  player.Close();
}

TEST(OboePlayerTest, CallbackAdvancesClock) {
  auto clock = std::make_shared<core::MasterClock>();
  auto transport = std::make_shared<core::TransportController>();
  auto mixer = std::make_shared<MultiTrackMixer>();

  OboePlayer player(mixer, clock, transport);
  const int32_t frames = 128;
  std::vector<float> output(static_cast<size_t>(frames) * 2, 1.0f);

  player.onAudioReady(nullptr, output.data(), frames);
  EXPECT_EQ(clock->GetPosition(), 0);
  EXPECT_LT(test::MaxAbs(output.data(), output.size()), 1e-6f);

  transport->Play();
  std::fill(output.begin(), output.end(), 0.0f);
  player.onAudioReady(nullptr, output.data(), frames);
  EXPECT_EQ(clock->GetPosition(), frames);
  EXPECT_TRUE(test::AllFinite(output.data(), output.size()));
}
#else
TEST(OboePlayerTest, SkippedOnHost) {
  GTEST_SKIP() << "Android-only OboePlayer tests.";
}
#endif

}  // namespace playback
}  // namespace sezo
