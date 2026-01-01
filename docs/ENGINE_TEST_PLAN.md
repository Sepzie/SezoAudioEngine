# Engine Test Plan

**Goal:** Focus on functional, business-logic coverage for the C++ audio engine.
Avoid boilerplate tests; prioritize behavior that can regress (sync, timing,
state transitions, buffer correctness, and file I/O semantics).

## Scope Overview

- **Host tests (fast, deterministic):** core logic, DSP behavior, buffer math,
  decoders/encoders that do not require Android APIs.
- **Android tests (device/emulator):** Oboe/MediaCodec, microphone, background
  threading, end-to-end recording/extraction, and engine lifecycle.

## Test Targets (by module)

### Core
- `core/CircularBuffer.*`
  - Wraparound order preservation
  - Free/available invariants across writes/reads
  - Reset behavior for future reads
- `core/MasterClock.*`
  - Advance/reset/set position correctness
- `core/TransportController.*`
  - State transitions (play/pause/stop/record)
- `core/TimingManager.*`
  - ms <-> samples conversions are consistent
  - duration updates reflected in getters

### Playback
- `playback/TimeStretch.*`
  - Pass-through when inactive
  - Pitch changes do not corrupt timing (when stretch=1.0)
  - Stretch factor yields expected input/output ratio
  - No NaNs/Inf in output for silence or small buffers
- `playback/Track.*`
  - Mute outputs silence
  - Seek resets internal state and timing
  - Volume/pan applied consistently
  - Time-stretch input underrun handling
- `playback/MultiTrackMixer.*`
  - Solo overrides mute
  - Track offsets respected
  - Master volume applied and soft-clipping limits
- `playback/OboePlayer.*` (Android)
  - Initialize/start/stop lifecycle
  - Audio callback advances clock and respects transport

### Audio I/O
- `audio/MP3Decoder.*`, `audio/WAVDecoder.*`
  - Invalid file errors are handled
  - Frame counts match headers
  - Partial reads behave safely
- `audio/WAVEncoder.*` (Host)
  - Invalid config rejected
  - File size/frames match expected writes
- `audio/MP3Encoder.*` (Host + optional LAME)
  - Fails gracefully when LAME disabled
  - Encodes when LAME is enabled
- `audio/AACEncoder.*` (Android)
  - MediaCodec initialization
  - Encoded output length > 0 for known input

### Recording
- `recording/MicrophoneCapture.*` (Android)
  - Start/stop lifecycle and error paths
  - Input level reporting sanity
- `recording/RecordingPipeline.*` (Android)
  - Start/stop produces valid file
  - No initial buffer noise
  - Recorded duration ~ requested duration

### Extraction
- `extraction/ExtractionPipeline.*` (Android)
  - Single-track export applies effects
  - Multi-track export respects solo/mute/volume
  - Progress monotonic and completion callback fired

### Engine Integration
- `AudioEngine.*` (Android)
  - Initialize/release idempotence
  - Load/unload tracks updates duration
  - Playback/seek state transitions
  - Per-track vs master effects behavior
  - Error codes for invalid inputs

## Fixtures Needed

Create deterministic test assets under a fixtures directory (to be added):

- `fixtures/mono_1khz_1s.wav` (known duration)
- `fixtures/stereo_1khz_1s.wav`
- `fixtures/silence_1s.wav`
- `fixtures/short.mp3` (known duration)

## Placeholder Test Files

The following test stubs exist in:
`packages/android-engine/android/engine/src/test/cpp/tests/`

- `test_circular_buffer.cpp`
- `test_master_clock.cpp`
- `test_transport_controller.cpp`
- `test_timing_manager.cpp`
- `test_time_stretch.cpp`
- `test_track.cpp`
- `test_multi_track_mixer.cpp`
- `test_decoders.cpp`
- `test_encoders.cpp`
- `test_microphone_capture_android.cpp`
- `test_recording_pipeline_android.cpp`
- `test_extraction_pipeline_android.cpp`
- `test_audio_engine_android.cpp`
- `test_oboe_player_android.cpp`

Each test is a `GTEST_SKIP()` placeholder with targeted TODOs.

## Notes

- Add new tests by replacing `GTEST_SKIP()` with real assertions.
- Prefer golden files for DSP verification and duration checks.
- Keep Android-only tests guarded with `#if defined(__ANDROID__)`.
