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
  kStreamError,
  kExtractionFailed
};

}  // namespace core
}  // namespace sezo
