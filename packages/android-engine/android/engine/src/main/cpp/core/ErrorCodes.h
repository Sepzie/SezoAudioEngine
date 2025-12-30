#pragma once

namespace sezo {
namespace core {

enum class ErrorCode {
  kOk = 0,
  kNotInitialized,
  kInvalidArgument,
  kInvalidState,
  kTrackNotFound,
  kTrackLimitReached,
  kUnsupportedFormat,
  kDecoderOpenFailed,
  kSeekFailed,
  kStreamError,
  kRecordingFailed,
  kExtractionFailed
};

}  // namespace core
}  // namespace sezo
