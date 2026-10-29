# feat/15-recording-foreground-service — 2026-10-28

Part of #15 (Android lifecycle and background robustness): keep the process and
the audio session alive while a recording is in progress.

## What I did
- **Android:** new `RecordingForegroundService` (type `microphone`, plus
  `mediaPlayback` when playback was running at `startRecording()`), declared in
  the module manifest with `foregroundServiceType="mediaPlayback|microphone"` and
  `FOREGROUND_SERVICE_MICROPHONE`. `startRecording()` checks `RECORD_AUDIO`
  (rejects with `RECORDING_PERMISSION_DENIED`), starts the engine recording, then
  starts the service with `ContextCompat.startForegroundService`; the service
  promotes itself with `ServiceCompat.startForeground(service, id, notification, type)`.
  `stopRecording()`, `release()` and module destroy stop it. While recording,
  `OnActivityEntersBackground` no longer pauses playback.
- **Plugin:** `{ android: { recordingForegroundService: false } }` writes
  `tools:node="remove"` entries for the service and `FOREGROUND_SERVICE_MICROPHONE`
  into the app manifest. The default export was split so this option works
  independently of `engineFromSource`.
- **iOS:** `startRecording()` already configured `.playAndRecord` and called
  `setActive(true)`. The real gap was that `pause`/`stop`/playback completion
  called `engine.stop()`, which stops the input tap mid-take (and lets iOS
  suspend a backgrounded app). Those paths now skip stopping the engine while
  recording; `stopRecording()` stops it when nothing is playing. An interruption
  during a take finalises the file and emits `recordingStopped`
  (`reason: 'interrupted'`) and `engineStateChanged` `recordingInterrupted`.
- **TS:** optional `reason?: RecordingStopReason` on the `recordingStopped`
  payload; `dist/AudioEngineModule.types.d.ts` regenerated (no `.js` change).
- Docs: `docs/expo/install.md` (background recording, Play declaration, plugin
  option, resource overrides, iOS session behaviour), `docs/expo/events.md`,
  `docs/expo/api.md` (two new codes), package README, CHANGELOG Unreleased.

## Decisions
- **Separate service instead of reusing `MediaPlaybackService`.** That service
  follows the background-playback toggle and play/pause (demotes on pause, stops
  on stop/disable); merging would need one state machine for two triggers and a
  type recomputation from the background, which Android 14 forbids for
  `microphone`. A separate service is also trivially removable via the plugin.
  Cost: when recording with background playback enabled there are two
  notifications.
- **Types fixed at promotion.** `mediaPlayback` is added only if playback runs at
  `startRecording()`; the service is not re-promoted later because re-calling
  `startForeground` with `microphone` from the background throws on Android 14+.
  Audio output needs no FGS type, so this is cosmetic.
- **Crash safety.** A service started with `startForegroundService` must call
  `startForeground`. If the `microphone` promotion throws, the service falls back
  to `mediaPlayback` (process stays alive) and reports
  `RECORDING_FOREGROUND_SERVICE_FAILED`. The module stops the service with a STOP
  intent (ordered after a pending START) and `stopSelf(startId)`, falling back to
  `stopService`.
- **Preconditions checked in the module** before starting the service: service
  declared (plugin may have removed it), app in the foreground
  (`ActivityManager.getMyMemoryState` importance == FOREGROUND). Failures emit a
  warning and the recording continues unprotected.
- FGS type values are spelled out as constants (2, 0x80) to avoid `InlinedApi`
  lint on minSdk 24; `ServiceCompat` masks them on API 29–33.
- Notification texts and icon are overridable via app resources
  (`sezo_audio_engine_recording_*`) rather than new API.

## Verification
- `yarn build:expo-module`: OK.
- `yarn test:expo-module`: 76/76 pass (added a type assertion for `reason`).
- iOS: `xcrun --sdk iphonesimulator swiftc -typecheck` over `ios/AudioEngine/*.swift`
  (with a stub for the ExpoModulesCore-dependent `AudioEngineError`): clean.
  `ExpoAudioEngineModule.swift` was not type-checked (needs ExpoModulesCore).
- Plugin: node script ran `removeRecordingServiceFromManifest` through
  `AndroidConfig.Manifest` read/write (output has the two `tools:node="remove"`
  entries, idempotent) and checked which mods the default export registers.
- **Not run:** Gradle/Kotlin compile (no Gradle on this machine by policy; CI
  `android-engine` and `example-android` are the check), `yarn test:cpp:host`
  (cmake is not installed here; no C++ changed), any device run.

## Follow-ups
- Extraction (`ExtractionController` stops the engine and enters manual
  rendering) and `loadTracks` (stops the engine) during a take still cut the
  recording on iOS; reject or defer them while recording.
- iOS `mediaServicesWereResetNotification` is not handled.
- Android: no recording-interrupted path (e.g. input stream disconnect); the
  service also outlives a task swipe until `stopRecording`.
- Consider merging the two notifications when recording with background playback.
