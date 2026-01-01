# Sezo Audio Engine C++ Tests

This directory hosts GoogleTest-based C++ unit tests for the audio engine.

## Host build (recommended for fast iteration)

```bash
cmake -S packages/android-engine/android/engine/src/test/cpp -B build/sezo-tests-host
cmake --build build/sezo-tests-host
ctest --test-dir build/sezo-tests-host --output-on-failure
```

## Android build

Use the Android test runner script to build with the NDK toolchain and run via adb:

```bash
bash packages/android-engine/android/engine/src/test/cpp/scripts/run_android_tests.sh
```

Options and env vars:

```bash
# Example: run on a specific device and ABI
SEZO_ANDROID_ABI=arm64-v8a SEZO_ANDROID_SERIAL=DEVICE_SERIAL \\
  bash packages/android-engine/android/engine/src/test/cpp/scripts/run_android_tests.sh
```

If exactly one device is connected, the script will auto-select it. With multiple
devices, set `SEZO_ANDROID_SERIAL` to choose.

## Notes

- GoogleTest v1.14.0 is vendored at `packages/android-engine/android/engine/src/main/cpp/third_party/googletest`.
- Host builds use a stub `android/log.h` from `stubs/` to avoid NDK headers.
- Only a subset of engine sources is compiled into the test binary by default;
  add more in `CMakeLists.txt` as coverage expands.
