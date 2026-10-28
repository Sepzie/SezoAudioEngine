# fix/11-streaming-recording-encode — 2026-10-28

Issue: #11 (recording stop latency on Android). Branch `fix/11-streaming-recording-encode`; the streaming
pipeline itself landed in 6734980. This session added on-device proof with MediaCodec and fixed what the
device exposed.

## What I did
- **On-device test** `src/test/cpp/tests/test_recording_pipeline_streaming_android.cpp`: drives
  `RecordingPipeline::StartRecording(std::unique_ptr<AudioSource>, ...)` with a generated 440 Hz tone
  (48 kHz mono, amplitude 0.5) through the M4A and the ADTS AAC encoders, 10-second and 5-minute takes.
  The producer offers 1 s of audio every 10 ms through a 1 s ring and blocks while it is full, so the
  encoder is the bottleneck and the ring is full when stop is called. Asserts: stop < 500 ms, success,
  `duration_samples` == frames pushed, sample rate / channels echo the source (config deliberately asks for
  44.1 kHz stereo), file size within 0.5x..2x of 128 kbps and equal to the size on disk. M4A: container
  duration and decoded frame count within ±8192 of frames pushed (`audio::M4ADecoder`), decoded RMS 0.354 ± 0.02,
  pitch 440 ± 1 Hz from zero crossings. AAC: every ADTS header parsed, frame lengths add up to the file size,
  48 kHz / mono in every header, packets x 1024 within ±8192, and the same decode checks.
- `ToneSource`, `ProducerStats`, `ToneSample`, `WaitFor` moved to `tests/tone_source.h`, shared with the host test.
- **Test build fix**: the Android test binary did not link (`M4AEncoder`/`M4ADecoder` missing from the test
  `CMakeLists.txt`, referenced by `RecordingPipeline`, `Track` and `ExtractionPipeline`). Added both.
- **Test harness**: `tests/binder_thread_pool_android.cpp` starts a binder thread pool (looked up with `dlsym` in
  `libbinder_ndk.so`; the functions are not in the public NDK headers).
- **Engine fixes** (see Defects).
- `gtest_discover_tests(... DISCOVERY_TIMEOUT 60)`: the first host run timed out listing tests on a loaded machine.
- CHANGELOG "Unreleased" updated.

## Measured (Pixel 8, Android 16, arm64-v8a)
| Take | Format | Stop latency | Backlog in ring at stop | File size | Capture speed |
|---|---|---|---|---|---|
| 10 s | M4A | 137 ms | 869 ms of audio | 164,000 B | 9.1x real time |
| 5 min | M4A | 123 ms | 1077 ms | 4,861,018 B | 8.0x |
| 10 s | AAC (ADTS) | 115 ms | 954 ms | 164,065 B | 7.3x |
| 5 min | AAC (ADTS) | 182 ms | 1077 ms | 4,899,308 B | 8.1x |

Stop latency here includes encoding a full ~1 s backlog; with a real microphone the worker keeps up and the
backlog at stop is a few tens of ms. Nominal size at 128 kbps: 160,000 B (10 s), 4,800,000 B (5 min).
Before the encoder fixes (same test, binder pool in place): 10 s M4A stopped in 416 ms at 3.1x real time,
AAC 338 ms at 3.4x. Pre-#11 stop latency (whole take encoded in `StopRecording()`) was not re-measured.

Suites: host `yarn test:cpp:host` 40 tests, 33 passed, 7 skipped (Android-only), 0 failed. Device
`yarn test:cpp:android` 52 tests, 52 passed, 0 skipped, 0 failed (microphone tests ran under the shell user).

## Defects found on the device
1. **Encoders waited 10 ms per write.** `DrainOutput(false)` blocked in `dequeueOutputBuffer` for the full
   timeout every time no output was ready, and the codec's default input buffer (2048 B = one 1024-frame mono
   packet) meant one codec round trip per packet. Throughput was ~3x real time, so a full ring took ~400 ms
   to drain at stop and a slower device would have little headroom. Now the capture-time drain is
   non-blocking (back-pressure comes from `dequeueInputBuffer`), and `KEY_MAX_INPUT_SIZE` is 8192 frames.
   ~8x real time. Covered by the stop budget and duration assertions in the new test.
2. **EOS could be dropped.** `Close()` tried once (10 ms) to get an input buffer for the end-of-stream flag;
   if the codec still held them all, EOS was never queued, the tail of the take was lost and the drain spun
   for 500 ms. It now retries up to 50 times, draining output between attempts. Same change in both encoders.
3. **M4ADecoder could not open files outside an app process**: `AMediaExtractor_setDataSource(path)` needs the
   Java HTTP service ("can't create http service"). Local paths (and `file://`) now open via
   `AMediaExtractor_setDataSourceFd`; the path call remains the fallback.

## Gotchas
- A bare native binary run from `adb shell` has no binder thread pool. Codec2 delivers output over binder, so
  MediaCodec accepts four input buffers and then stalls forever (worker spins on `TRY_AGAIN_LATER`, test hangs
  in the pipeline destructor). App processes always have a pool, so this is harness-only, but any future
  MediaCodec test in this binary depends on `binder_thread_pool_android.cpp`.
- `debuggerd -b` needs root on a user build; `/proc/<pid>/task/*/wchan` was enough to see where threads waited.
- The run script's `mapfile` needs bash 4; on macOS set `SEZO_ANDROID_SERIAL` so that path is skipped.
- The 5-minute decode is slow (~50 s per take) because `M4ADecoder` polls with 10 ms timeouts; the full device
  suite takes ~3.5 min.

## Not verified
- No Gradle build (another Gradle build was running on the shared machine). The changed engine files were
  compiled with the NDK clang and the release flags (`-Wall -Wextra -Werror -O3`); CI's `android-engine` job
  builds the AAR.
- Not run in the example app or on other devices; no real-microphone long take.

## Status
Done; PR open against `main`, not merged.

## Follow-ups
- `M4ADecoder::DecodeMore` also blocks 10 ms per empty poll; same treatment would speed up M4A loading.
- Run the device suite in CI on an emulator (it needs the binder pool helper, already in place).
