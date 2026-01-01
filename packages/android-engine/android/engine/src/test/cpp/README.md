# Sezo Audio Engine C++ Tests

This directory hosts GoogleTest-based C++ unit tests for the audio engine.

## Host build (recommended for fast iteration)

```bash
cmake -S packages/android-engine/android/engine/src/test/cpp -B build/sezo-tests-host
cmake --build build/sezo-tests-host
ctest --test-dir build/sezo-tests-host --output-on-failure
```

## Android build

Enable tests in the Android CMake build by setting `SEZO_ENABLE_TESTS=ON`.
This adds a `sezo_engine_tests` binary to the native build output, which you can
push to a device and run via `adb`.

```
# Example: configure externalNativeBuild with -DSEZO_ENABLE_TESTS=ON
```

## Notes

- GoogleTest v1.14.0 is vendored at `packages/android-engine/android/engine/src/main/cpp/third_party/googletest`.
- Host builds use a stub `android/log.h` from `stubs/` to avoid NDK headers.
- Only a subset of engine sources is compiled into the test binary by default;
  add more in `CMakeLists.txt` as coverage expands.
