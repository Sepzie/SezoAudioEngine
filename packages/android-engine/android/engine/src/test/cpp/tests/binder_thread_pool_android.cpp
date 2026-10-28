// Starts a binder thread pool in the Android test binary.
//
// Codec2 components (MediaCodec) run in a media service process and deliver
// finished work back over binder. An app process always has a binder thread
// pool; a bare native executable launched from adb shell does not, so encoder
// output never arrives and MediaCodec stalls once its input buffers are full.
// The pool functions are not in the public NDK headers, so look them up at
// runtime from libbinder_ndk.

#if defined(__ANDROID__)
#include <dlfcn.h>

#include <cstdint>
#include <cstdio>

namespace {

struct BinderThreadPool {
  BinderThreadPool() {
    void* lib = dlopen("libbinder_ndk.so", RTLD_NOW);
    if (!lib) {
      std::fprintf(stderr, "binder thread pool: libbinder_ndk.so unavailable\n");
      return;
    }
    using SetMaxFn = bool (*)(uint32_t);
    using StartFn = void (*)();
    auto set_max = reinterpret_cast<SetMaxFn>(
        dlsym(lib, "ABinderProcess_setThreadPoolMaxThreadCount"));
    auto start = reinterpret_cast<StartFn>(dlsym(lib, "ABinderProcess_startThreadPool"));
    if (!start) {
      std::fprintf(stderr, "binder thread pool: ABinderProcess_startThreadPool unavailable\n");
      return;
    }
    if (set_max) {
      set_max(4);
    }
    start();
  }
};

const BinderThreadPool kBinderThreadPool;

}  // namespace
#endif
