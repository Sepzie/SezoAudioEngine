#include <gtest/gtest.h>

#include "core/TransportController.h"

namespace sezo {
namespace core {

TEST(TransportControllerTest, PlayPauseStopTransitions) {
  GTEST_SKIP() << "TODO: verify transitions and IsPlaying for each state.";
}

TEST(TransportControllerTest, PauseFromStoppedDoesNotPlay) {
  GTEST_SKIP() << "TODO: pause when stopped should remain stopped.";
}

}  // namespace core
}  // namespace sezo
