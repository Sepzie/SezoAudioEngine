# Product Requirements Document: Multi-Track Audio Engine Module

## 1. Project Overview

**Project Name:** Sezo Audio Engine

**Description:** A high-performance, open-source Expo Module providing synchronized multi-track audio playback with real-time pitch shifting, speed adjustment, and simultaneous recording capabilities. Designed for music practice apps, karaoke applications, podcast editors, and any mobile application requiring advanced audio manipulation.

**Primary Goal:** Deliver a production-ready, cross-platform audio engine that matches commercial solutions (like Superpowered SDK) in functionality while remaining free and open source. The module should be generic, reusable, and independent of any specific application business logic.

**Target Platforms:** Android (primary focus), iOS (secondary)

**License:** MIT or Apache 2.0

## 2. In-Scope vs. Out-of-Scope

### In-Scope

**Core Audio Features:**
- Multi-track audio playback with sample-accurate synchronization
- Real-time pitch shifting (-12 to +12 semitones)
- Real-time speed adjustment (0.5x to 2.0x)
- Independent tempo and pitch control
- Simultaneous audio recording during playback
- Offline track extraction (single track or all tracks with effects applied)
- Per-track volume control, mute, and solo
- Low-latency audio I/O (target: <20ms round-trip)
- Background playback support with media controls
- System audio session management
- Progress tracking and seeking

**File Format Support:**
- Input: MP3, WAV, M4A
- Output:
  - MP3 (via LAME encoder or Android MediaCodec)
  - AAC/M4A (via Android MediaCodec) - Recommended
  - WAV (via dr_wav) - Optional, for lossless quality

**API Design:**
- Clean TypeScript interface
- Promise-based async operations
- Event emitters for real-time updates
- Proper error handling and recovery

**Platform-Specific Implementation:**
- Android: Oboe + Signalsmith Stretch + custom mixing
- iOS: AVAudioEngine with native effects units

**Developer Experience:**
- Comprehensive documentation
- Example application demonstrating all features
- Unit tests for core functionality
- Clear error messages and debugging support

### Out-of-Scope (V1)

- Audio effects beyond pitch/speed (reverb, delay, EQ) - can be added in V2
- Stem separation (users provide pre-separated files)
- Lyrics transcription (handled by separate services)
- Key detection or music analysis
- MIDI support
- Video synchronization
- Cloud storage integration
- Waveform visualization (data can be exposed for apps to render)
- DJ-specific features (scratching, looping beyond basic repeat)
- Advanced audio editing (cut, copy, paste, trim)

## 3. Core Features

### 3.1 Multi-Track Playback

**Description:** Load and play multiple audio files simultaneously with perfect synchronization.

**Requirements:**
- Support 2-16 simultaneous tracks (configurable limit)
- Sample-accurate synchronization across all tracks
- Graceful handling of different sample rates (resample to engine rate)
- Efficient memory management (streaming from disk, not full load)
- Thread-safe track loading and playback

**Technical Considerations:**
- All tracks must share a single master clock
- Buffer sizes must be consistent across tracks
- Handle file I/O on background threads
- Support hot-swapping tracks without stopping playback

### 3.2 Real-Time Effects

**Pitch Shifting:**
- Range: -12 to +12 semitones
- Granularity: 0.01 semitone steps
- Quality: High (minimize artifacts)
- Latency: Acceptable for real-time use (<100ms added latency)

**Speed Adjustment:**
- Range: 0.5x to 2.0x (50% to 200% speed)
- Granularity: 0.01x steps
- Independent of pitch (time-stretching)
- Maintain audio quality across range

**Combined Control:**
- Allow setting tempo and pitch independently
- Allow setting tempo and pitch together (transposition)
- Smooth transitions between values (no clicks/pops)

### 3.3 Audio Recording

**Requirements:**
- Record microphone input while tracks are playing
- Synchronized recording (recording timestamps match playback)
- Configurable sample rate and bit depth
- Input level metering for UI feedback
- Support for stereo or mono input
- Basic audio processing: noise gate, volume normalization

**Output:**
- Primary: AAC/M4A format (compressed, high quality, via Android MediaCodec)
- Alternative: MP3 format (via LAME encoder)
- Optional: WAV format (uncompressed, for maximum quality)
- Metadata: sample rate, bit depth, duration, bitrate
- Configurable quality/bitrate (64-320 kbps)

### 3.4 Track Extraction

**Description:** Export a single track or all tracks with current effects applied.

**Requirements:**
- Extract one track or all tracks to new audio files
- Apply current pitch/speed and per-track settings during extraction
- Support the same output formats as recording
- Provide progress updates and completion callbacks
- No circular buffer required for extraction (offline processing)

### 3.5 Recording Encoding Strategies

**File Size Comparison (3-minute recording, 44.1kHz):**

| Format | Bitrate | File Size | Quality | Use Case |
|--------|---------|-----------|---------|----------|
| WAV | Lossless | ~15.8 MB | Perfect | Archive/Pro use |
| AAC | 128 kbps | ~2.8 MB | Excellent | Recommended default |
| AAC | 64 kbps | ~1.4 MB | Good | Low storage devices |
| MP3 | 128 kbps | ~2.8 MB | Very Good | Maximum compatibility |
| MP3 | 64 kbps | ~1.4 MB | Acceptable | Voice-only recordings |

**Encoding Options:**

**Option 1: Android MediaCodec (AAC) - RECOMMENDED**
- **Pros:**
  - Built into Android (no extra libraries)
  - Hardware-accelerated on most devices
  - Excellent quality/size ratio
  - Fast encoding (real-time or faster)
  - Zero licensing concerns
- **Cons:**
  - Slightly less compatibility than MP3 (but AAC is widely supported)
  - Quality varies by device (but consistently good)
- **Implementation:**
  - Use MediaCodec with audio/mp4a-latm MIME type
  - Configure for AAC-LC profile
  - Typical bitrates: 64-128 kbps for voice, 128-192 kbps for singing

**Option 2: LAME MP3 Encoder - ALTERNATIVE**
- **Pros:**
  - Industry-standard quality
  - Maximum compatibility (MP3 plays everywhere)
  - Fine-grained quality control
  - Predictable output across devices
- **Cons:**
  - LGPL license (requires dynamic linking)
  - Additional 500KB to app size
  - Slightly slower than hardware AAC
  - CPU encoding only
- **Implementation:**
  - Bundle LAME as shared library
  - Link dynamically (satisfies LGPL)
  - VBR or CBR encoding options

**Option 3: Hybrid Approach**
- Record to WAV buffer in real-time (dr_wav)
- Encode to AAC/MP3 in background thread after recording stops
- Show progress indicator during encoding
- **Benefits:**
  - No recording glitches (WAV is trivial to write)
  - Can offer format choice after recording
  - Can re-encode later if needed

**Recommended Implementation:**

```typescript
// Primary: AAC via MediaCodec
startRecording({ format: 'aac', bitrate: 128 })

// Alternative: MP3 via LAME (if bundled)
startRecording({ format: 'mp3', bitrate: 128 })

// Archive quality: WAV (for users who need lossless)
startRecording({ format: 'wav' })
```

**Quality Presets:**

```typescript
quality: 'low'    // 64 kbps AAC  (~1.4 MB / 3 min)
quality: 'medium' // 128 kbps AAC (~2.8 MB / 3 min) [Default]
quality: 'high'   // 192 kbps AAC (~4.3 MB / 3 min)
```

### 3.6 Track Management

**Per-Track Controls:**
- Volume: 0.0 (silent) to 1.0 (full) with overdrive support to 2.0
- Mute: Toggle track on/off without changing volume setting
- Solo: Mute all other tracks except soloed track(s)
- Pan: Left (-1.0) to Right (1.0) stereo positioning

**Master Controls:**
- Master volume affecting all tracks
- Master mute (pause without stopping transport)
- Output level metering

### 3.7 Transport Controls

**Playback States:**
- Stopped: Ready to play, position at 0 or last seek position
- Playing: Audio actively rendering
- Paused: Playback stopped, position maintained
- Recording: Playing + recording microphone input

**Operations:**
- Play/Pause/Stop
- Seek to arbitrary position (in milliseconds or samples)
- Get current position and total duration
- Set loop points (optional for V1, useful for practice apps)

### 3.8 Background Playback and Media Controls

**Android:**
- Foreground service with notification
- MediaSession integration for lock screen controls
- Handle audio focus changes (phone calls, other apps)
- Headphone disconnect handling

**iOS:**
- AVAudioSession configuration for background
- MPNowPlayingInfoCenter for lock screen metadata
- MPRemoteCommandCenter for media controls
- Handle interruptions (phone calls, Siri, etc.)

**Metadata:**
- Track title
- Artist name (optional)
- Artwork (optional)
- Current playback position
- Total duration

## 4. Technical Architecture (Android Focus)

### 4.1 Core Components

**Audio Engine (C++):**

```
AudioEngine
|-- PlaybackPipeline
|  |-- MultiTrackMixer
|  |-- AudioDecoder (dr_libs: dr_mp3, dr_wav)
|  `-- TrackSynchronizer
|-- RecordingPipeline
|  |-- MicrophoneCapture
|  |-- RecordingBuffer
|  `-- FileEncoder
|     |-- MediaCodecEncoder (AAC/MP3) - Primary
|     |-- LAMEEncoder (MP3) - Optional
|     `-- WAVWriter (dr_wav) - Optional
|-- ProcessingPipeline
|  |-- SignalsmithStretchProcessor (pitch/speed)
|  |-- VolumeControl
|  `-- BasicDSP (noise gate, normalization)
|-- ExtractionPipeline
|  |-- TrackExporter
|  |-- EffectsRenderer (offline)
|  `-- FileEncoder
`-- SyncEngine
   |-- MasterClock
   |-- TransportController
   `-- TimingManager
```

**Native Bridge (Kotlin):**

```
AudioEngineModule (Expo Module API)
|-- JNI Bridge to C++
|-- Lifecycle Management
|-- Error Handling
`-- Event Emission
```

**Service Layer (Kotlin):**

```
AudioEngineService (Foreground Service)
|-- MediaSession Management
|-- Notification Controller
`-- Audio Focus Handling
```

### 4.2 Technology Stack

**Android:**
- **Oboe 1.8+** (Apache 2.0): Low-latency audio I/O
- **Signalsmith Stretch**: Pitch and tempo processing
- **dr_libs** (Public Domain/Unlicense): Audio decoding
  - dr_mp3.h - MP3 decoding
  - dr_wav.h - WAV encoding/decoding (optional, for lossless)
- **Android MediaCodec**: Built-in AAC/MP3 encoding (Recommended)
  - Zero additional dependencies
  - Hardware-accelerated on most devices
  - AAC encoder for recordings
- **LAME MP3 Encoder** (LGPL 2.1, Optional): Alternative MP3 encoding
  - Better quality control than MediaCodec
  - Requires dynamic linking for LGPL compliance
- **NDK r25+**: Native C++ compilation
- **CMake 3.18+**: Build system
- **Kotlin**: Native module bridge
- **Expo Modules API**: React Native integration

**iOS (Brief):**
- **AVAudioEngine**: Core audio processing
- **AVAudioUnitTimePitch**: Pitch and tempo effects
- **AVAudioPlayerNode**: Multi-track playback
- **Swift**: Native module bridge
- **Expo Modules API**: React Native integration

### 4.3 Cross-Repository Development and Compatibility

**Single Repository Structure:** This module is designed as a single repository containing two packages: the Android engine (C++/Kotlin) and the Expo module (Android wrapper + iOS implementation). This approach provides:
- Clean separation of concerns
- Independent versioning and release cycles
- Easier open-source contribution
- Reusability across multiple projects
- Proper module encapsulation

**Internal Package Boundaries:** The Android engine package is shared by the Expo module through internal dependencies rather than a published Maven artifact.

**Recommended Repository Structure:**

```
sezo-audio-engine/                    # Single repository
|-- packages/
|  |-- android-engine/                 # Android engine (C++/Kotlin)
|  |  |-- engine/
|  |  |-- build.gradle
|  |  `-- README.md
|  `-- expo-module/                    # Expo module (Android wrapper + iOS)
|     |-- android/
|     |  |-- src/
|     |  |  |-- main/
|     |  |  |  `-- java/expo/modules/audioengine/
|     |  |  |     |-- ExpoAudioEngineModule.kt
|     |  |  |     |-- AudioEngineService.kt
|     |  |  |     `-- MediaSessionController.kt
|     |  `-- cpp/
|     |     |-- AudioEngine.cpp
|     |     |-- AudioEngine.h
|     |     |-- dr_mp3.h               # Single header include
|     |     |-- dr_wav.h               # Single header include
|     |     |-- OboePlayer.cpp
|     |     |-- SignalsmithStretchProcessor.cpp
|     |     `-- CMakeLists.txt
|     |-- ios/                          # iOS native implementation
|     |-- src/
|     |  |-- index.ts                   # Main TypeScript exports
|     |  |-- AudioEngineModule.ts       # Native module wrapper
|     |  `-- AudioEngineModule.types.ts # TypeScript types
|     |-- example/                      # Example Expo app
|     |  |-- app.json
|     |  |-- package.json
|     |  `-- App.tsx
|     |-- docs/
|     |  |-- API.md
|     |  |-- SETUP.md
|     |  `-- TROUBLESHOOTING.md
|     |-- __tests__/
|     |-- expo-module.config.json       # Expo module configuration
|     |-- package.json
|     |-- tsconfig.json
|     |-- README.md
|     `-- CHANGELOG.md
|-- LICENSE
`-- README.md
```

**Compatibility Requirements for Cross-Repo Usage:**

**1. Expo SDK Version Compatibility:**

```json
// sezo-audio-engine/package.json
{
  "peerDependencies": {
    "expo": ">=51.0.0 <54.0.0",
    "react-native": ">=0.74.0"
  }
}
```

**2. Minimum Version Requirements:**

| Component | Minimum Version | Recommended | Notes |
|-----------|------------------|-------------|-------|
| Expo SDK | 51.0.0 | 52.0.0+ | New Architecture support |
| React Native | 0.74.0 | 0.76.0+ | Better JSI performance |
| Android Min SDK | 24 (Android 7.0) | 24+ | Oboe requirement |
| Android Target SDK | 33+ | 34 | Latest features |
| NDK | r25+ | r26 | C++17 support |
| Gradle | 8.0+ | 8.4+ | AGP 8.x compatibility |
| Kotlin | 1.8+ | 1.9+ | Coroutines support |

**3. Expo Module Configuration:**

```json
// expo-module.config.json
{
  "platforms": ["android", "ios"],
  "android": {
    "modules": ["expo.modules.audioengine.ExpoAudioEngineModule"]
  },
  "ios": {
    "modules": ["ExpoAudioEngineModule"]
  }
}
```

**4. Android Gradle Configuration Requirements:** Consumer apps must have these settings in their `android/build.gradle`:

```gradle
// Required NDK version
android {
    ndkVersion = "26.1.10909125"  // Or higher

    defaultConfig {
        minSdkVersion 24
        targetSdkVersion 34

        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a', 'x86', 'x86_64'
        }
    }

    // Enable C++17 for dr_libs and modern C++
    externalNativeBuild {
        cmake {
            version "3.22.1"
            arguments "-DANDROID_STL=c++_shared"
        }
    }
}
```

**5. Required Permissions (for consumer apps):** Consumer apps must declare these permissions in their `app.config.js`:

```typescript
android: {
  permissions: [
    "RECORD_AUDIO",                        // Required for recording
    "FOREGROUND_SERVICE",                  // Required for background playback
    "FOREGROUND_SERVICE_MEDIA_PLAYBACK",   // Android 14+ requirement
    "WAKE_LOCK",                           // Keep CPU awake during recording
    "MODIFY_AUDIO_SETTINGS"                // Optional: advanced audio routing
  ]
}
```

**6. Linking Methods:**

**Local Development (File Link):**

```json
// consuming-app/package.json
{
  "dependencies": {
    "sezo-audio-engine": "file:../sezo-audio-engine"
  }
}
```

**Git Repository:**

```json
{
  "dependencies": {
    "sezo-audio-engine": "github:yourusername/sezo-audio-engine#v1.0.0"
  }
}
```

**NPM Registry:**

```json
{
  "dependencies": {
    "sezo-audio-engine": "^1.0.0"
  }
}
```

**7. Installation Steps for Consumer Apps:**

```bash
# Install the module
npm install sezo-audio-engine

# iOS only: Install pods
npx pod-install

# If using managed workflow with EAS Build
# No prebuild needed - EAS handles it

# If using bare workflow
npx expo prebuild --clean
```

**8. Config Plugin for Consumer Apps:** The module should provide a config plugin that consumer apps can use:

```typescript
// consumer-app/app.config.js
export default {
  plugins: [
    [
      'sezo-audio-engine',
      {
        enableBackgroundAudio: true,
        enableLowLatency: true,
        androidMinSdk: 24
      }
    ]
  ]
}
```

**9. Version Testing Matrix:** The module should be tested against:

| Expo SDK | React Native | Android SDK | NDK | Status |
|----------|--------------|-------------|-----|--------|
| 51.x | 0.74.x | 24-34 | r25 | Supported |
| 52.x | 0.76.x | 24-34 | r26 | Primary Target |
| 53.x (future) | 0.77.x+ | 24-35 | r26+ | To Test |

**10. Breaking Change Prevention:**
- Use semantic versioning strictly
- Deprecate before removing APIs
- Maintain changelog
- Document migration guides
- Test against multiple Expo SDK versions in CI

**11. CI/CD for Compatibility:**

```yaml
# .github/workflows/test.yml
strategy:
  matrix:
    expo-sdk: ['51.0.0', '52.0.0']
    node: ['18', '20']
```

**12. Known Incompatibilities:**
- **Expo SDK <51**: Does not support New Architecture properly
- **React Native <0.74**: Missing JSI improvements for real-time audio
- **Android SDK <24**: Oboe requires API 24+
- **NDK <r25**: Missing C++17 features needed by dr_libs

### 4.4 Threading Model

**Audio Thread (Real-time priority):**
- Oboe callback execution
- Sample mixing and processing
- Critical: No allocations, no locks, no blocking

**File I/O Thread:**
- Loading audio files
- Decoding compressed audio
- Writing recordings to disk

**Main Thread:**
- JavaScript/React Native communication
- UI state updates
- Non-time-critical operations

**Background Thread:**
- Long-running operations (encoding, analysis)
- Resource-intensive processing

### 4.5 Memory Management

**Streaming Strategy:**
- Don't load entire files into memory
- Use circular buffers for file I/O
- Typical buffer: 4096-8192 samples per track
- Pre-buffer 2-3 buffers ahead of playback position
- Offline extraction does not require circular buffers

**Memory Limits:**
- Target: <50MB RAM for 4 tracks at 44.1kHz
- Release buffers when tracks unloaded
- Clear recording buffers after save

## 5. Module Interface (TypeScript API)

### 5.1 Core API

```typescript
interface AudioEngine {
  // Initialization
  initialize(config: AudioEngineConfig): Promise<void>;
  release(): Promise<void>;

  // Track Management
  loadTracks(tracks: AudioTrack[]): Promise<void>;
  unloadTrack(trackId: string): Promise<void>;
  unloadAllTracks(): Promise<void>;
  getLoadedTracks(): AudioTrack[];

  // Playback Control
  play(): void;
  pause(): void;
  stop(): void;
  seek(positionMs: number): void;
  isPlaying(): boolean;
  getCurrentPosition(): number;
  getDuration(): number;

  // Track Controls
  setTrackVolume(trackId: string, volume: number): void;
  setTrackMuted(trackId: string, muted: boolean): void;
  setTrackSolo(trackId: string, solo: boolean): void;
  setTrackPan(trackId: string, pan: number): void;

  // Master Controls
  setMasterVolume(volume: number): void;
  getMasterVolume(): number;

  // Real-time Effects
  setPitch(semitones: number): void;
  getPitch(): number;
  setSpeed(rate: number): void;
  getSpeed(): number;
  setTempoAndPitch(tempo: number, pitch: number): void;

  // Recording
  startRecording(config?: RecordingConfig): Promise<void>;
  stopRecording(): Promise<RecordingResult>;
  isRecording(): boolean;
  setRecordingVolume(volume: number): void;
  // Extraction
  extractTrack(trackId: string, config?: ExtractionConfig): Promise<ExtractionResult>;
  extractAllTracks(config?: ExtractionConfig): Promise<ExtractionResult[]>;

  // Metering
  getInputLevel(): number;
  getOutputLevel(): number;
  getTrackLevel(trackId: string): number;

  // Background Playback
  enableBackgroundPlayback(metadata: MediaMetadata): Promise<void>;
  updateNowPlayingInfo(metadata: Partial<MediaMetadata>): void;
  disableBackgroundPlayback(): Promise<void>;

  // Events
  addListener(event: AudioEngineEvent, callback: Function): Subscription;
}
```

### 5.2 Type Definitions

```typescript
interface AudioEngineConfig {
  sampleRate?: number;           // Default: 44100
  bufferSize?: number;           // Default: Auto-detected for low latency
  maxTracks?: number;            // Default: 8
  enableProcessing?: boolean;    // Enable pitch/speed, default: true
}

interface AudioTrack {
  id: string;                    // Unique identifier
  uri: string;                   // File path or URL
  type?: 'local' | 'remote';     // Default: 'local'
  volume?: number;               // 0.0 - 1.0, default: 1.0
  pan?: number;                  // -1.0 to 1.0, default: 0.0
  muted?: boolean;               // Default: false
}

interface RecordingConfig {
  sampleRate?: number;           // Default: 44100
  channels?: number;             // 1 (mono) or 2 (stereo), default: 1
  format?: 'aac' | 'mp3' | 'wav'; // Default: 'aac'
  bitrate?: number;              // For compressed formats: 64-320 kbps, default: 128
  quality?: 'low' | 'medium' | 'high'; // Preset quality levels
  enableNoiseGate?: boolean;     // Default: false
  enableNormalization?: boolean; // Default: false
}

interface RecordingResult {
  uri: string;                   // Path to saved file
  duration: number;              // Duration in milliseconds
  sampleRate: number;
  channels: number;
  format: 'aac' | 'mp3' | 'wav';
  bitrate?: number;              // For compressed formats
  fileSize: number;              // File size in bytes
}

interface ExtractionConfig {
  format?: 'aac' | 'mp3' | 'wav'; // Default: 'aac'
  bitrate?: number;              // For compressed formats: 64-320 kbps, default: 128
  includeEffects?: boolean;      // Default: true
  outputDir?: string;            // Optional output directory
}

interface ExtractionResult {
  trackId?: string;              // Present for single-track extraction
  uri: string;                   // Path to saved file
  duration: number;              // Duration in milliseconds
  format: 'aac' | 'mp3' | 'wav';
  bitrate?: number;              // For compressed formats
  fileSize: number;              // File size in bytes
}

interface MediaMetadata {
  title: string;
  artist?: string;
  album?: string;
  artwork?: string;              // URI to image
}

type AudioEngineEvent =
  | 'playbackStateChange'        // (state: PlaybackState) => void
  | 'positionUpdate'             // (position: number) => void
  | 'playbackComplete'           // () => void
  | 'trackLoaded'                // (trackId: string) => void
  | 'trackUnloaded'              // (trackId: string) => void
  | 'recordingStarted'           // () => void
  | 'recordingStopped'           // (result: RecordingResult) => void
  | 'extractionProgress'         // (trackId: string | null, progress: number) => void
  | 'extractionComplete'         // (results: ExtractionResult[]) => void
  | 'error';                     // (error: AudioEngineError) => void

type PlaybackState = 'stopped' | 'playing' | 'paused' | 'recording';

interface AudioEngineError {
  code: string;
  message: string;
  details?: any;
}
```

## 6. Implementation Phases

### Phase 1: Core Foundation (2 weeks)

**Week 1:**
- Set up Expo Module structure
- Configure Android NDK and CMake
- Integrate Oboe library
- Implement basic single-track playback
- Create JNI bridge skeleton
- Implement TypeScript API stubs

**Week 2:**
- Implement multi-track mixer (2-4 tracks)
- Add synchronization engine
- Integrate dr_libs (dr_mp3, dr_wav) for audio decoding
- Test multi-track playback
- Basic error handling

**Deliverable:** Can load and play 2-4 MP3/WAV files in sync

### Phase 2: Real-Time Effects (1.5 weeks)

**Tasks:**
- Integrate Signalsmith Stretch library
- Implement pitch shifting (-12 to +12 semitones)
- Implement speed adjustment (0.5x to 2.0x)
- Add smooth parameter transitions
- Optimize for real-time performance
- Test across different audio files

**Deliverable:** Pitch and speed controls working in real-time

### Phase 3: Recording (2 weeks)

**Week 1:**
- Implement microphone capture with Oboe
- Create recording buffer management
- Implement WAV file writer (dr_wav) for buffer
- Add synchronized recording (timestamps match playback)
- Test recording quality and sync

**Week 2:**
- Integrate Android MediaCodec for AAC encoding
- Implement background encoding thread
- Add quality/bitrate configuration
- Optional: Integrate LAME for MP3 encoding
- Add basic DSP (noise gate, normalization)
- Test compressed output quality

**Deliverable:** Can record microphone while playing tracks, save as AAC/MP3

### Phase 4: Track Management and Controls (1 week)

**Note:** Adjusted from 1.5 weeks to 1 week as recording phase expanded

**Tasks:**
- Implement per-track volume/mute/solo
- Add master volume control
- Implement audio level metering
- Add seek functionality
- Optimize memory usage
- Implement proper resource cleanup

**Deliverable:** Full control over individual tracks and master output

### Phase 5: Background Playback (1 week)

**Android:**
- Implement foreground service
- Add MediaSession integration
- Create notification with controls
- Handle audio focus changes
- Handle headphone disconnection

**iOS (Brief):**
- Configure AVAudioSession
- Implement MPNowPlayingInfoCenter
- Add MPRemoteCommandCenter support
- Handle interruptions

**Deliverable:** Works in background with lock screen controls

### Phase 6: Polish and Testing (1 week)

**Tasks:**
- Comprehensive error handling
- Memory leak testing
- Performance optimization
- Cross-device testing (different Android versions/devices)
- Documentation
- Example app completion
- API refinement based on testing

**Deliverable:** Production-ready V1.0.0

### Phase 7: iOS Implementation (2-3 weeks, parallel or sequential)

**Tasks:**
- Implement iOS native module
- Use AVAudioEngine for all features
- Match Android API exactly
- Test on iOS devices
- Ensure feature parity

**Deliverable:** iOS support with same API as Android

## 7. Testing Strategy

### Unit Tests (C++)
- Test individual components (mixer, decoder, sync engine)
- Mock Oboe callbacks for deterministic testing
- Test edge cases (buffer underruns, file errors, etc.)

### Integration Tests (Kotlin/TypeScript)
- Test full workflows (load + play + record + extract + save)
- Test state transitions
- Test error recovery
- Test memory management

### Performance Tests
- Measure latency (target: <20ms round-trip)
- Measure CPU usage (target: <30% on mid-range device)
- Measure memory usage (target: <50MB for 4 tracks)
- Test on low-end devices (Android API 24+)

### Device Testing
- Test on multiple Android versions (API 24-34)
- Test on different manufacturers (Samsung, Google, Xiaomi, etc.)
- Test with different audio hardware (USB audio, Bluetooth, etc.)
- Test background behavior across devices

### Example Application
- Demonstrate all API features
- Serve as integration test
- Provide usage examples for documentation

## 8. Documentation Requirements

### API Documentation
- JSDoc comments for all public methods
- Code examples for common use cases
- TypeScript type definitions
- Error codes and handling

### Architecture Documentation
- High-level system design
- Threading model explanation
- Memory management strategy
- Performance considerations

### Setup Guide
- Installation instructions
- Required dependencies
- Build configuration (CMake, Gradle)
- Troubleshooting common issues

### Usage Examples
- Basic playback example
- Multi-track mixing example
- Recording while playing example
- Background playback setup
- Error handling patterns

### Contributing Guide
- Code style guidelines
- Pull request process
- Testing requirements
- Native development setup

## 9. Performance Requirements

**Latency:**
- Audio output latency: <10ms (target), <20ms (acceptable)
- Input to output latency: <15ms (target), <30ms (acceptable)
- Effect parameter changes: <50ms to apply

**CPU Usage:**
- Idle (no playback): <1% CPU
- 2-track playback: <15% CPU (mid-range device)
- 4-track playback + recording + effects: <30% CPU
- Should not cause thermal throttling during normal use

**Memory:**
- Base module overhead: <10MB
- Per track in memory: <10MB (streaming)
- Recording buffer: <20MB (1 minute mono at 44.1kHz)
- Total for typical use: <50MB

**Battery:**
- Background playback should not drain >5%/hour more than music apps
- Properly release resources when app backgrounded

**Startup Time:**
- Module initialization: <500ms
- Load single track: <1 second
- Load 4 tracks: <3 seconds

## 10. Error Handling

### Error Categories

**Initialization Errors:**
- AUDIO_ENGINE_INIT_FAILED: Cannot initialize audio subsystem
- UNSUPPORTED_DEVICE: Device doesn't meet minimum requirements
- PERMISSION_DENIED: Microphone permission not granted

**Track Loading Errors:**
- FILE_NOT_FOUND: Audio file doesn't exist
- UNSUPPORTED_FORMAT: File format not supported
- DECODE_ERROR: Cannot decode audio file
- INVALID_AUDIO_DATA: File is corrupted

**Playback Errors:**
- BUFFER_UNDERRUN: Audio glitch due to system load
- DEVICE_DISCONNECTED: Audio device removed during playback
- AUDIO_FOCUS_LOST: Another app took audio focus

**Recording Errors:**
- MICROPHONE_UNAVAILABLE: Cannot access microphone
- STORAGE_FULL: Not enough space to save recording
- RECORDING_FAILED: Generic recording failure

**Processing Errors:**
- EFFECT_FAILED: Pitch/speed processing error
- INVALID_PARAMETER: Parameter out of valid range

### Recovery Strategies

- Automatic retry for transient errors (up to 3 attempts)
- Graceful degradation (disable effects if processing fails)
- Clear error messages with actionable guidance
- Emit errors via event system for app handling
- Log detailed error info for debugging

## 11. Security and Privacy Considerations

**Permissions:**
- Request RECORD_AUDIO only when recording needed
- Request FOREGROUND_SERVICE for background playback
- Request WRITE_EXTERNAL_STORAGE only if saving to public storage

**Data Handling:**
- No telemetry or analytics in the module itself
- No network requests (purely local processing)
- Recordings saved to app-private storage by default
- Clear documentation on data storage locations

**Audio Data:**
- Audio buffers cleared after use
- Recordings encrypted if device encrypted
- No persistent caching without explicit opt-in

## 12. Open Source Considerations

**Repository Structure:**

```
sezo-audio-engine/
|-- packages/
|  |-- android-engine/    # Android engine (C++/Kotlin)
|  `-- expo-module/       # Expo module (Android wrapper + iOS)
|-- LICENSE               # MIT or Apache 2.0
|-- README.md             # Getting started
|-- CONTRIBUTING.md       # Contribution guidelines
`-- package.json          # npm package config
```

**Versioning:**
- Follow Semantic Versioning (semver)
- Clear changelog for each release
- Deprecation warnings before breaking changes

**Community:**
- Issue templates for bugs and features
- Pull request template
- Code of conduct
- Active maintenance commitment

**Dependencies:**
- Minimize external dependencies
- Use well-maintained, open-source libraries
- Document all third-party licenses
- Ensure LGPL compliance (dynamic linking)

## 13. Success Metrics

**Adoption:**
- 100+ GitHub stars in first 3 months
- 10+ apps using the module in production
- Featured in Expo/React Native community showcases

**Quality:**
- <10 critical bugs reported in first 6 months
- 90%+ API stability (no breaking changes)
- Positive developer feedback on ease of use

**Performance:**
- Meets all latency/CPU/memory targets
- Works on 95%+ of Android devices (API 24+)
- No memory leaks detected in long-running tests

**Community:**
- 5+ external contributors
- Active issue responses (<48 hours)
- Regular updates (monthly at minimum)

## 14. Future Roadmap (Post-V1)

**V1.1:**
- Additional audio effects (reverb, delay, EQ)
- MP3 encoding for recordings (LAME integration)
- Loop points and repeat functionality

**V1.2:**
- Waveform data extraction
- Offline track extraction improvements
- BPM detection
- Audio analysis utilities

**V2.0:**
- Web Audio API implementation (web support)
- Advanced mixing features
- Plugin architecture for custom effects

**V2.5:**
- MIDI support
- Multi-channel audio (>2 channels)
- Professional DAW features

## 15. Conclusion

This module aims to fill a significant gap in the React Native/Expo ecosystem by providing professional-grade audio processing capabilities that are currently only available through expensive commercial SDKs. By focusing on a clean, generic API and robust implementation, sezo-audio-engine can become the de facto standard for advanced audio applications on mobile. The modular architecture ensures maintainability, the comprehensive testing strategy ensures reliability, and the open-source approach ensures accessibility for developers worldwide. With Android as the primary implementation target and iOS following the same API contract, developers can build sophisticated audio applications with confidence.

## 16. Open Questions

- How should internal versioning and releases be coordinated between the Android engine and Expo module packages?
