# Implementation Checklist

Rule: After each feature is added, update this checklist to reflect progress.

## Repository Setup & Infrastructure

[ ] Define repo metadata (CODEOWNERS, issue templates, PR template)
[ ] Add root `package.json` scripts for lint/build/test/release
[ ] Add root tooling config (ESLint/Prettier/EditorConfig)
[ ] Add CI workflow skeleton (lint/test/build)  
[ ] Add release workflow (npm publish + tag)
[ ] Add changelog automation or guidelines
[ ] Add license headers policy (if desired)
[ ] Define versioning/release strategy between packages
[ ] Add `docs/ARCHITECTURE.md`
[ ] Add `docs/THREADING.md`
[ ] Add `docs/MEMORY.md`
[ ] Add `docs/ROADMAP.md`

## Phase 0: Foundation & Build System ✅ COMPLETED

[x] Create monorepo structure (packages/android-engine, packages/expo-module)
[x] Create TypeScript type definitions
[x] Create TypeScript module wrapper
[x] Create Expo module Kotlin skeleton with all API methods stubbed
[x] Create Android engine C++ skeleton (AudioEngine class)
[x] Create Android engine Kotlin wrapper
[x] Create basic Gradle build files
[x] Create basic CMake files
[x] Download and integrate Oboe library (git submodule)
[x] Download and integrate dr_mp3.h single-header library
[x] Download and integrate dr_wav.h single-header library
[x] Link android-engine as dependency in expo-module Gradle
[x] Configure CMake to build native libraries with proper includes
[x] Configure NDK version and ABI filters
[x] Implemented complete C++ skeleton with:
  - CircularBuffer (lock-free ring buffer)
  - MasterClock (sample-accurate timing)
  - TransportController (playback states)
  - TimingManager (sample/time conversion)
  - AudioDecoder, MP3Decoder, WAVDecoder
  - Track (per-track management and controls)
  - MultiTrackMixer (mixing algorithm)
  - OboePlayer (Oboe callback implementation)
  - Full AudioEngine implementation
[x] Implemented JNI bridge:
  - AudioEngineJNI.h/cpp with all native methods
  - Kotlin AudioEngine wrapper with native method calls
[ ] Test that project builds successfully on Android (READY TO TEST)
[ ] Create example app basic structure

## Phase 1: Core Playback Foundation (Week 1-2)

### Android Engine - Native Layer (C++)

[ ] Oboe integration:
  [ ] Initialize Oboe AudioStreamBuilder
  [ ] Configure stream parameters (sample rate, buffer size, channels)
  [ ] Implement AudioStreamCallback interface
  [ ] Handle stream lifecycle (start/stop/close)
  [ ] Set thread affinity for real-time priority

[ ] Audio decoding:
  [ ] Integrate dr_mp3 for MP3 decoding
  [ ] Create AudioDecoder base class
  [ ] Implement MP3Decoder using dr_mp3
  [ ] Implement WAVDecoder using dr_wav
  [ ] Test decoding various audio files

[ ] Buffering & streaming:
  [ ] Implement CircularBuffer class (lock-free ring buffer)
  [ ] Implement StreamingBuffer for file I/O
  [ ] Create background file reader thread
  [ ] Implement pre-buffering strategy
  [ ] Handle buffer underruns gracefully

[ ] Synchronization engine:
  [ ] Implement MasterClock (sample-accurate timing)
  [ ] Implement TransportController (play/pause/stop/seek)
  [ ] Implement TimingManager (position tracking)
  [ ] Add playback state machine

[ ] Multi-track mixer:
  [ ] Create Track class (holds decoded samples, state, controls)
  [ ] Implement MultiTrackMixer (sum N tracks)
  [ ] Add sample-accurate track synchronization
  [ ] Implement basic mixing algorithm (sum + clip prevention)

[ ] Track management:
  [ ] Implement track loading (async file open + decode)
  [ ] Implement track unloading (cleanup resources)
  [ ] Track ID management and lookup
  [ ] Thread-safe track collection

[ ] Basic controls:
  [ ] Implement per-track volume (0.0 - 2.0)
  [ ] Implement master volume
  [ ] Implement play/pause/stop
  [ ] Implement seek (sample-accurate)
  [ ] Implement getCurrentPosition
  [ ] Implement getDuration

[ ] JNI bridge API:
  [ ] Create JNI wrapper for AudioEngine
  [ ] Implement initialize() native method
  [ ] Implement loadTracks() native method
  [ ] Implement playback control methods (play/pause/stop/seek)
  [ ] Implement volume control methods
  [ ] Handle Java/C++ string conversions
  [ ] Handle error propagation to Java

[ ] Error handling:
  [ ] Define error codes enum
  [ ] Implement error callback mechanism
  [ ] Add validation for parameters
  [ ] Add logging infrastructure

[ ] Resource management:
  [ ] Implement proper destructor cleanup
  [ ] Add resource leak detection (debug mode)
  [ ] Test memory usage under load

### Android Engine - Kotlin Layer

[ ] Wire Gradle module publishing (AAR for internal use)
[ ] Implement JNI bridge API surface (C++ <-> Kotlin):
  [ ] Load native library
  [ ] Define external native methods
  [ ] Implement native method wrappers
  [ ] Handle exceptions from native layer
  [ ] Convert between Kotlin and C++ types

[ ] State management:
  [ ] Track loaded tracks in Kotlin
  [ ] Manage playback state
  [ ] Thread-safe access to engine

### Expo Module - Android Integration

[ ] Link to android-engine package in Gradle
[ ] Implement ExpoAudioEngineModule methods:
  [ ] initialize() - call native engine
  [ ] release() - cleanup
  [ ] loadTracks() - convert JS tracks to native
  [ ] play/pause/stop - delegate to engine
  [ ] Volume controls - delegate to engine
  [ ] Position/duration getters
[ ] Implement event emission for playback state changes
[ ] Handle file URI conversion (content:// to file path)
[ ] Add permission checks (RECORD_AUDIO when needed)

### Testing & Validation

[ ] Create test audio files (MP3, WAV, different sample rates)
[ ] Test single track playback
[ ] Test 2-track synchronized playback
[ ] Test 4-track synchronized playback
[ ] Test seek accuracy
[ ] Test volume controls
[ ] Test memory usage (< 50MB for 4 tracks)
[ ] Test on multiple Android devices (API 24+)
[ ] Measure audio latency
[ ] Test error handling (missing files, corrupted files)

## Phase 2: Real-Time Effects (Week 3-4)

[ ] Android engine: integrate Signalsmith Stretch
[ ] Android engine: implement pitch shifting
[ ] Android engine: implement speed adjustment
[ ] Android engine: implement smooth parameter transitions
[ ] Test pitch shifting across range (-12 to +12 semitones)
[ ] Test speed adjustment across range (0.5x to 2.0x)
[ ] Optimize for real-time performance
[ ] Add to Expo module API

## Phase 3: Recording (Week 5-6)

[ ] Android engine: implement microphone capture (Oboe input)
[ ] Android engine: implement recording buffer
[ ] Android engine: implement WAV writer (dr_wav)
[ ] Android engine: implement MediaCodec AAC encoder
[ ] Android engine: implement bitrate/quality presets
[ ] Android engine: implement optional LAME MP3 encoder
[ ] Add synchronized recording (timestamps match playback)
[ ] Implement background encoding thread
[ ] Add basic DSP (noise gate, normalization)
[ ] Add to Expo module API
[ ] Test recording quality and synchronization

## Phase 4: Track Management & Polish (Week 7)

[ ] Android engine: implement per-track mute
[ ] Android engine: implement per-track solo
[ ] Android engine: implement per-track pan
[ ] Android engine: implement output level metering
[ ] Android engine: implement input level metering
[ ] Android engine: implement track level metering
[ ] Android engine: implement loop points (optional)
[ ] Android engine: implement hot-swap track loading
[ ] Android engine: improve resource cleanup + lifecycle
[ ] Android engine: add logging + debug flags
[ ] Android engine: add performance counters (CPU/memory/latency)
[ ] Optimize memory usage
[ ] Add to Expo module API

## Phase 5: Background Playback (Week 8)

[ ] Android Expo module: add background playback service
[ ] Android Expo module: add MediaSession integration
[ ] Android Expo module: add notification controls
[ ] Android Expo module: handle audio focus changes
[ ] Android Expo module: handle headset disconnect
[ ] Test background playback across Android versions
[ ] Test media control integration

## Phase 6: Offline Extraction

[ ] Android engine: implement offline extraction pipeline
[ ] Android engine: implement effects rendering for extraction
[ ] Add extractTrack() method
[ ] Add extractAllTracks() method
[ ] Test extraction quality and performance

## Phase 7: iOS Implementation (Future)

[ ] iOS module: implement AVAudioSession config
[ ] iOS module: implement AVAudioEngine graph
[ ] iOS module: implement multi-track player nodes
[ ] iOS module: implement pitch/tempo via AVAudioUnitTimePitch
[ ] iOS module: implement per-track controls (volume/mute/solo/pan)
[ ] iOS module: implement transport + seek
[ ] iOS module: implement recording pipeline
[ ] iOS module: implement encoding (AAC/M4A)
[ ] iOS module: implement offline extraction
[ ] iOS module: implement background playback
[ ] iOS module: implement MPNowPlayingInfoCenter updates
[ ] iOS module: implement MPRemoteCommandCenter controls
[ ] iOS module: map errors to JS
[ ] Test API parity with Android

## TypeScript & Documentation

[ ] TypeScript: add JSDoc to all public APIs
[ ] TypeScript: validate parameter ranges client-side
[ ] TypeScript: define error types + constants
[ ] TypeScript: add event subscription helpers
[ ] TypeScript: add config plugin (Expo)
[ ] TypeScript: add config plugin options validation
[ ] TypeScript: add example usage snippets

## Example App

[ ] Example app: add track loader UI
[ ] Example app: add playback controls
[ ] Example app: add pitch/speed sliders
[ ] Example app: add per-track controls UI
[ ] Example app: add recording controls UI
[ ] Example app: add extraction UI + progress
[ ] Example app: add background playback demo
[ ] Example app: add error handling UI

## Documentation

[ ] Docs: API reference
[ ] Docs: setup guide
[ ] Docs: troubleshooting guide
[ ] Docs: architecture overview
[ ] Docs: threading model
[ ] Docs: memory management
[ ] Docs: performance targets
[ ] Docs: permissions and privacy
[ ] Docs: config plugin usage
[ ] Docs: example app walkthrough

## Testing

[ ] Tests: C++ unit tests for mixer/sync/decoder
[ ] Tests: C++ unit tests for pitch/speed
[ ] Tests: Kotlin unit tests for module state
[ ] Tests: TypeScript tests for API surface
[ ] Tests: integration tests for load/play/record/extract
[ ] Tests: performance benchmarks (latency/CPU/memory)
[ ] Tests: device matrix runs (API 24-34)

## CI/CD

[ ] CI: matrix for Expo SDK 51/52
[ ] CI: Android build with NDK r25/r26
[ ] CI: iOS build (Xcode latest)
[ ] CI: lint/test gating
