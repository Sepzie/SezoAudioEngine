#pragma once

namespace sezo {
namespace core {

enum class ErrorCode {
  kOk = 0,
  kNotInitialized,
  kInvalidArgument,
  kTrackNotFound,
  kTrackLimitReached,
  kUnsupportedFormat,
  kDecoderOpenFailed,
  kSeekFailed,
  kStreamError
};

}  // namespace core
}  // namespace sezo
