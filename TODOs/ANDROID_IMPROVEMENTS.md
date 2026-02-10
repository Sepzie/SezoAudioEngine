# SezoAudioEngine Android - Robustness & Background Playback Audit

## Table of Contents

1. [The Core Problem: Why the Engine Dies After Backgrounding](#1-the-core-problem)
2. [Architecture Overview](#2-architecture-overview)
3. [Architecture Review: What's Good & What's Not](#3-architecture-review)
4. [Engine Robustness Issues](#4-engine-robustness-issues)
5. [Background Playback: Current State & What's Needed](#5-background-playback)
6. [Recommended Improvements](#6-recommended-improvements)
7. [Implementation Priority & Roadmap](#7-implementation-priority)

---

## 1. The Core Problem: Why the Engine Dies After Backgrounding {#1-the-core-problem}

The reported issue - "engine doesn't work after coming back to the app" - is caused by a combination of factors:

### Root Cause Analysis

1. **No lifecycle awareness at any layer.** The Expo module (`ExpoAudioEngineModule.kt`) has zero lifecycle callbacks. It doesn't implement `OnActivityPauseListener`, `OnActivityResumeListener`, or any Expo lifecycle interface. When the app goes to background:
   - The Oboe audio stream keeps running (wasting battery, may get killed by OS)
   - Android's audio system may reclaim the exclusive audio stream
   - When returning, the stream may be in a dead/disconnected state

2. **No Oboe stream error recovery.** `OboePlayer` implements `AudioStreamDataCallback` but does NOT implement `AudioStreamErrorCallback`. When the OS disconnects the stream (common after backgrounding), the engine has no way to know or recover. The stream silently dies.

3. **No audio focus management.** The engine never requests audio focus, so Android can freely take it away. When another app plays audio while we're backgrounded, our stream can be invalidated without notification.

4. **`initialized_` flag stays `true` on dead streams.** Even when the Oboe stream is disconnected by the OS, `initialized_` remains true, so all methods think the engine is healthy. Calls to `play()` try to start a dead stream, and `OboePlayer::Start()` just logs an error and returns false - but `AudioEngine::Play()` doesn't propagate this clearly enough to JS.

5. **No state validation on resume.** There's no mechanism to check if the engine is actually functional before resuming operations. The consumer app would need to manually `release()` + `initialize()` + re-load tracks every time.

---

## 2. Architecture Overview {#2-architecture-overview}

```
┌─────────────────────────────────────────────────┐
│  JavaScript (Consumer App)                       │
│  AudioEngineModule.ts - thin passthrough         │
├─────────────────────────────────────────────────┤
│  Expo Module Layer (Kotlin)                      │
│  ExpoAudioEngineModule.kt                        │
│  - No lifecycle hooks                            │
│  - No background service                         │
│  - Stubs for enableBackgroundPlayback/etc         │
├─────────────────────────────────────────────────┤
│  Kotlin Wrapper                                  │
│  AudioEngine.kt                                  │
│  - JNI bridge to C++                             │
│  - No lifecycle awareness                        │
│  - Holds raw native pointer (nativeHandle)       │
├─────────────────────────────────────────────────┤
│  C++ Native Engine                               │
│  AudioEngine.cpp + components                    │
│  ├── TransportController (atomic state machine)  │
│  ├── OboePlayer (audio output stream)            │
│  ├── MultiTrackMixer (real-time mixing)          │
│  ├── MasterClock (timeline position)             │
│  ├── RecordingPipeline (mic capture)             │
│  └── ExtractionWorker (background thread)        │
└─────────────────────────────────────────────────┘
```

### Key files

| File | Role |
|------|------|
| `packages/expo-module/android/src/main/java/expo/modules/audioengine/ExpoAudioEngineModule.kt` | Expo module - JS bridge, event emission |
| `packages/android-engine/android/engine/src/main/java/com/sezo/audioengine/AudioEngine.kt` | Kotlin JNI wrapper |
| `packages/android-engine/android/engine/src/main/cpp/AudioEngine.cpp` | Core engine orchestrator |
| `packages/android-engine/android/engine/src/main/cpp/playback/OboePlayer.cpp` | Oboe audio stream |
| `packages/android-engine/android/engine/src/main/cpp/core/TransportController.cpp` | Playback state machine |
| `packages/android-engine/android/engine/src/main/cpp/playback/MultiTrackMixer.cpp` | Track mixer |

---

## 3. Architecture Review: What's Good & What's Not {#3-architecture-review}

### What's Done Well

**The C++ core engine is solidly architected.** The separation between `TransportController`, `MasterClock`, `TimingManager`, `OboePlayer`, and `MultiTrackMixer` is clean. Each component has a single responsibility and they compose well through shared pointers. This is exactly how you'd want to structure a real-time audio engine.

**The transport state machine is correct.** Using `std::atomic<PlaybackState>` with proper memory ordering in `TransportController` is the right call for a flag that's read from the audio thread. No mutex contention on the hot path.

**The audio callback is lean.** `OboePlayer::onAudioReady` does exactly what it should - checks transport state, calls mixer, advances clock, returns. No allocations, no locks, no syscalls. This is textbook real-time audio code.

**The extraction worker pattern is well-done.** Dedicated thread with a condition variable, cancellation via atomic flags, proper queue management. The async extraction with progress callbacks through JNI back to JS is a nice design.

**The layering makes sense.** C++ engine → JNI → Kotlin wrapper → Expo module → TypeScript. Each layer adds the right amount of abstraction. The Kotlin `AudioEngine.kt` is a thin JNI binding (which it should be), and the Expo module handles the framework-specific concerns.

### What's Not Great

**The Expo module is too thin.** `ExpoAudioEngineModule.kt` is basically a 1:1 passthrough to the Kotlin wrapper. It's the right place to handle Android platform concerns (lifecycle, services, audio focus, permissions) but it does none of that. It should be the "smart" layer that makes the raw engine behave well on Android, but right now it's just plumbing.

**No defensive boundary between JS and native.** The module trusts that JS will call things in the right order. If JS calls `play()` before `initialize()`, or calls `loadTracks()` after `release()`, the Kotlin layer throws a generic `"Engine not initialized"` exception. But there's no protection against more subtle misuse - like calling `play()` from two React components simultaneously, or calling `release()` while an extraction is being awaited. The module should be the guardian layer that serializes and validates, but it doesn't.

**Mixed synchronicity is problematic.** Some functions are `Function` (synchronous) and some are `AsyncFunction`, but the split feels arbitrary. `play()`, `pause()`, `stop()` are synchronous Functions that can fail silently (stream start failure gets swallowed). Meanwhile `loadTracks` is async even though the native `LoadTrack` is synchronous. There's no clear principle governing which is which, and the synchronous ones can't propagate errors to JS.

**State lives in too many places.** The Expo module keeps its own `loadedTrackIds` set, the C++ engine has its own `tracks_` map, and the mixer has its own track list. These can get out of sync. For example, if a native `loadTrack` call succeeds at the mixer level but something goes wrong after, the Expo module's `loadedTrackIds` won't match reality. There should be one source of truth.

**The Kotlin wrapper adds no value.** `AudioEngine.kt` is a pure JNI passthrough - every method is just `nativeFoo(nativeHandle, args)`. It doesn't add error handling, threading, or state management. It holds a raw `nativeHandle: Long` that could become a dangling pointer if `destroy()` is called while other methods are in-flight. Either this layer should do something useful (like null-check the handle, synchronize access) or the JNI could be called directly from the Expo module.

**No observability into engine health.** The engine has error codes and callbacks, but there's no way for JS to ask "is the engine actually healthy right now?" You can call `isPlaying()` and get `false`, but you can't distinguish between "paused" and "stream is dead." A health/status API would help the consumer app handle edge cases.

**The `tracks_` map has no concurrency protection** while being accessed from multiple threads (JS thread for load/unload, extraction worker thread for reading tracks, indirectly from audio thread via mixer). The shared_ptrs help prevent use-after-free on individual tracks, but the map iteration itself is unsafe.

### Summary

The foundation is strong - the real-time audio core is well-written and the architecture layers are correct. The weakness is in the "glue" layers: the Expo module and Kotlin wrapper don't pull their weight. They should be handling all the messy Android platform concerns (lifecycle, audio focus, error recovery, state validation) so the C++ engine can stay focused on audio processing. Right now those responsibilities just aren't handled anywhere.

---

## 4. Engine Robustness Issues {#4-engine-robustness-issues}

### 4.1 Critical: No Oboe Stream Error/Disconnect Handling

**File:** `OboePlayer.h` / `OboePlayer.cpp`

The player only implements `AudioStreamDataCallback` (the audio render callback). It does NOT implement `AudioStreamErrorCallback`. This means:

- When Android disconnects the stream (device change, policy change, background kill), the engine has zero awareness
- The `onAudioReady` callback simply stops being called, but `IsRunning()` may still return stale state
- No automatic stream restart or reconnection

**What Oboe recommends:** Implement `onErrorBeforeClose` and `onErrorAfterClose` to detect disconnects and restart the stream. This is the #1 pattern in Oboe's documentation for robust audio apps.

### 4.2 Critical: `initialized_` Is Not Atomic

**File:** `AudioEngine.h:220`

```cpp
bool initialized_ = false;  // NOT atomic, NOT mutex-protected
```

This flag is read by every public method and written by `Initialize()` and `Release()`. Since the Oboe audio callback runs on a high-priority thread and JS calls come from another thread, this is a data race. In practice it rarely causes issues because the usage pattern is sequential, but it's technically undefined behavior.

### 4.3 High: No Thread Safety on `tracks_` Map

**File:** `AudioEngine.cpp`

The `tracks_` map (`std::map<string, shared_ptr<Track>>`) is accessed from:
- The JS thread (via `LoadTrack`, `UnloadTrack`, `SetTrackVolume`, etc.)
- The extraction worker thread (via `ExtractTrack`, `ExtractAllTracks`)
- Indirectly from the audio thread via the mixer

There's no mutex protecting `tracks_`. If JS calls `UnloadTrack` while an extraction is reading from the same track, this could crash.

### 4.4 High: Oboe Stream Uses Exclusive Mode Without Fallback

**File:** `OboePlayer.cpp:25`

```cpp
->setSharingMode(oboe::SharingMode::Exclusive)
```

Exclusive mode gives lowest latency but means Android will not share the audio device. If another app claims audio focus or if the stream gets disconnected, there's no fallback to `Shared` mode. Oboe can automatically fall back if you set Exclusive as a *preference* rather than requiring it, but the current code doesn't handle the case where Exclusive fails and retries with Shared.

### 4.5 Medium: `Play()` Silently Fails if Stream Is Dead

**File:** `AudioEngine.cpp:199-221`

```cpp
void AudioEngine::Play() {
  // ...
  transport_->Play();  // State set to Playing BEFORE stream is confirmed
  if (!player_->IsRunning()) {
    if (!player_->Start()) {
      transport_->Stop();  // Rolled back, but...
      ReportError(...);
      return;  // JS gets no indication this failed
    }
  }
}
```

When `Play()` is called as a synchronous `Function` (not `AsyncFunction`) in the Expo module, the error is logged but not thrown to JS. The consumer app has no way to know that playback failed to start.

### 4.6 Medium: No Playback Completion Detection

The `onAudioReady` callback in `OboePlayer.cpp` always returns `Continue`, even when all tracks have finished playing. The engine never fires a "playback complete" event. This means:
- The Oboe stream runs forever, even when there's nothing to play (just rendering silence)
- The consumer app has no native "finished playing" notification

### 4.7 Low: Extraction Worker Thread Join Could Block Indefinitely

**File:** `AudioEngine.cpp:758-771`

`StopExtractionWorker()` calls `extraction_thread_.join()` which blocks until the worker loop exits. If an extraction operation is stuck (e.g., encoding a large file), this blocks the calling thread indefinitely. There's no timeout.

---

## 5. Background Playback: Current State & What's Needed {#5-background-playback}

### Current State: Stub Functions Only

```kotlin
// ExpoAudioEngineModule.kt:407-409
AsyncFunction("enableBackgroundPlayback") { _metadata: Map<String, Any?> -> }
Function("updateNowPlayingInfo") { _metadata: Map<String, Any?> -> }
AsyncFunction("disableBackgroundPlayback") { }
```

These are empty no-ops. The TS API surface already exposes them, so consumers can call them, but nothing happens.

### What's Required for Android Background Audio

#### 5.1 Foreground Service

Android requires a **Foreground Service** to play audio when the app is not in the foreground. Without it, the OS will kill the process after ~1 minute in background.

**What we need:**
- A `MediaPlaybackService` extending `Service` with `startForeground()`
- A persistent notification showing playback state (required by Android for foreground services)
- Proper `FOREGROUND_SERVICE` and `FOREGROUND_SERVICE_MEDIA_PLAYBACK` permissions in the manifest
- Service start/stop tied to background playback enable/disable

#### 5.2 MediaSession Integration

`MediaSessionCompat` provides:
- Lock screen controls (play/pause/seek)
- Bluetooth/headphone button handling
- Integration with system media notification
- Other apps (Google Maps, etc.) can see what's playing

**What we need:**
- Create a `MediaSession` when background playback is enabled
- Update session metadata (title, artist, duration) via `updateNowPlayingInfo`
- Handle `MediaSession.Callback` events (play, pause, stop, seekTo) and route them to the engine
- Update playback state on the session when engine state changes

#### 5.3 Audio Focus

Android's audio focus system mediates between apps that want to use the audio output.

**What we need:**
- Request `AudioManager.AUDIOFOCUS_GAIN` when starting playback
- Handle focus loss gracefully:
  - `AUDIOFOCUS_LOSS_TRANSIENT` → pause, resume when regained
  - `AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK` → lower volume
  - `AUDIOFOCUS_LOSS` → pause/stop
- Release focus when stopping playback

#### 5.4 Manifest Changes

Currently the engine library manifest only declares:
```xml
<uses-sdk android:minSdkVersion="24" />
```

**What we need added (expo module level):**
```xml
<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_MEDIA_PLAYBACK" />

<service
    android:name=".MediaPlaybackService"
    android:foregroundServiceType="mediaPlayback"
    android:exported="false" />
```

#### 5.5 Lifecycle Integration

The Expo module needs to respond to app lifecycle events to manage the transition between foreground and background states.

**What we need in `ExpoAudioEngineModule`:**
- Implement `OnActivityPauseListener`: When pausing without background playback → pause engine
- Implement `OnActivityResumeListener`: When resuming → check/restart stream health, resume if was playing
- Implement `OnDestroyListener`: Clean up engine, stop service

---

## 6. Recommended Improvements {#6-recommended-improvements}

### Phase A: Engine Robustness (Fixes the "doesn't work after coming back" issue)

These changes directly address the reported problem and should be done first:

#### A1. Add Oboe `AudioStreamErrorCallback` to OboePlayer

**Files to change:** `OboePlayer.h`, `OboePlayer.cpp`

- Implement `onErrorBeforeClose()` and `onErrorAfterClose()`
- In `onErrorAfterClose`, attempt to reopen the stream with the same parameters
- If stream was playing before disconnect, automatically restart playback
- Report error to engine if recovery fails
- This is the single most impactful fix for the reported issue

#### A2. Add Lifecycle Callbacks to ExpoAudioEngineModule

**Files to change:** `ExpoAudioEngineModule.kt`

- Implement Expo's `OnActivityPauseListener`, `OnActivityResumeListener`, `OnDestroyListener`
- On pause (without background mode): save playback state, pause engine
- On resume: validate stream health, restore playback state if was playing
- On destroy: call `release()` to clean up native resources
- Emit a JS event when engine state changes due to lifecycle

#### A3. Add Stream Health Check / Auto-Recovery

**Files to change:** `OboePlayer.cpp`, `AudioEngine.cpp`

- Add an `IsHealthy()` method to `OboePlayer` that checks actual stream state
- On `Play()`, if stream is unhealthy, attempt to close and reopen it before starting
- Add a recovery path: `Close() → Initialize() → Start()` with the same parameters
- Keep track data intact during recovery (only the audio stream needs restart)

#### A4. Make `initialized_` Atomic

**Files to change:** `AudioEngine.h`

Simple change: `bool initialized_` → `std::atomic<bool> initialized_`

#### A5. Fix `Play()` Error Propagation to JS

**Files to change:** `ExpoAudioEngineModule.kt`

Change `play` from `Function` (synchronous, no error return) to `AsyncFunction` that can throw if the stream fails to start. Or add a return value to indicate success/failure.

### Phase B: Background Playback

#### B1. Create MediaPlaybackService

**New file:** `packages/expo-module/android/src/main/java/expo/modules/audioengine/MediaPlaybackService.kt`

- Foreground service with notification
- Holds the audio focus request
- Creates and manages `MediaSession`
- Routes media button events to engine

#### B2. Implement Audio Focus Management

**Files to change:** `ExpoAudioEngineModule.kt` or new `AudioFocusManager.kt`

- Request/release audio focus on play/stop
- Handle transient loss (auto-pause, auto-resume)
- Handle duck (lower volume, restore)
- Handle permanent loss (stop)

#### B3. Implement MediaSession

**Files to change:** `MediaPlaybackService.kt`, `ExpoAudioEngineModule.kt`

- Create session on `enableBackgroundPlayback()`
- Update metadata on `updateNowPlayingInfo()`
- Handle callbacks (play, pause, stop, seekTo, skipForward, skipBackward)
- Sync playback state from engine to session

#### B4. Wire Up enableBackgroundPlayback / disableBackgroundPlayback

**Files to change:** `ExpoAudioEngineModule.kt`

- `enableBackgroundPlayback()`: start foreground service, create media session, request audio focus
- `disableBackgroundPlayback()`: stop foreground service, release media session, abandon audio focus
- `updateNowPlayingInfo()`: update media session metadata + notification

#### B5. Update AndroidManifest

**Files to change:** Add `AndroidManifest.xml` to expo module's `src/main/`

- Add foreground service permissions
- Declare `MediaPlaybackService`

### Phase C: Additional Hardening

#### C1. Add Mutex to `tracks_` Map Access

Protect concurrent access to `tracks_` from JS thread and extraction thread.

#### C2. Add Playback Completion Detection

In `onAudioReady`, detect when all tracks have been played to completion. Emit a `playbackComplete` event and optionally stop the stream.

#### C3. Oboe Shared Mode Fallback

If Exclusive mode fails or is unavailable, fall back to Shared mode.

#### C4. Add Timeout to Extraction Thread Join

Use `std::future` with timeout instead of bare `thread::join()`.

---

## 7. Implementation Priority & Roadmap {#7-implementation-priority}

### Immediate Priority (Fixes the reported bug)

| # | Task | Impact | Effort | Files |
|---|------|--------|--------|-------|
| A1 | Oboe stream error/disconnect recovery | Fixes main issue | Medium | OboePlayer.h/.cpp |
| A2 | Lifecycle callbacks in Expo module | Fixes main issue | Low | ExpoAudioEngineModule.kt |
| A3 | Stream health check + auto-recovery | Fixes main issue | Medium | OboePlayer.cpp, AudioEngine.cpp |
| A5 | Play() error propagation | Better UX | Low | ExpoAudioEngineModule.kt |

**These 4 items together should fix the "engine doesn't work after coming back" problem.** The stream will either survive backgrounding (via recovery) or will be properly restarted on resume (via lifecycle callbacks + health check).

### Second Priority (Background playback)

| # | Task | Impact | Effort | Files |
|---|------|--------|--------|-------|
| B5 | AndroidManifest permissions | Prerequisite | Low | New AndroidManifest.xml |
| B2 | Audio focus management | Required | Medium | New AudioFocusManager.kt |
| B1 | Foreground service | Required | High | New MediaPlaybackService.kt |
| B3 | MediaSession integration | Required | High | MediaPlaybackService.kt |
| B4 | Wire up JS API stubs | Required | Low | ExpoAudioEngineModule.kt |

### Third Priority (Hardening)

| # | Task | Impact | Effort | Files |
|---|------|--------|--------|-------|
| A4 | Make initialized_ atomic | Safety | Trivial | AudioEngine.h |
| C1 | Mutex for tracks_ map | Safety | Low | AudioEngine.cpp |
| C2 | Playback completion detection | Feature | Medium | OboePlayer.cpp, AudioEngine.cpp |
| C3 | Shared mode fallback | Robustness | Low | OboePlayer.cpp |
| C4 | Extraction thread timeout | Safety | Low | AudioEngine.cpp |

---

## Notes

- The iOS side likely has similar lifecycle issues but was not part of this audit
- The TS API surface already has the right shape for background playback (enable/disable/updateNowPlaying) - we just need to implement the native side
- Oboe's `AudioStreamErrorCallback` is the most battle-tested approach for handling Android audio disconnects. Google's own apps use this pattern
- Consider testing with `adb shell am kill` to simulate aggressive process management while audio is playing
