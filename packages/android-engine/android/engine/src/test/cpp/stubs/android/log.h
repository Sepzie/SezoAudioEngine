#pragma once

// Host-only stub for Android logging APIs used in engine sources.
// This keeps host builds independent of NDK headers.

#ifdef __cplusplus
extern "C" {
#endif

enum {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,
  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,
  ANDROID_LOG_SILENT,
};

static inline int __android_log_print(int /*prio*/, const char* /*tag*/, const char* /*fmt*/, ...) {
  return 0;
}

#ifdef __cplusplus
}
#endif
