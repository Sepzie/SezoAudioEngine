# Changelog

## Unreleased
- Android: recordings are encoded while they are captured. The recording pipeline used to buffer the whole take as float PCM in memory and run it through the encoder inside `stopRecording()`, so stop latency and peak memory grew with the take (a minute or more for long takes on some devices). The worker thread now writes each chunk from the input ring buffer to the encoder as it arrives; `stopRecording()` drains the ring, finalises the container and returns a complete file in bounded time.
- Android: `stopRecording()` reports the sample rate and channel count the input stream actually delivered, and derives `duration` from them, instead of assuming 44.1 kHz mono.
- iOS: `stopRecording()` releases the recording file before reading `fileSize`, so the reported size is the finalised one.
- Engine: `RecordingPipeline` reads from an `AudioSource` interface (implemented by `MicrophoneCapture`) and builds on the host; host tests cover streaming, no-loss and bounded-stop behaviour.

## 0.0.20
- Android: build with `expo-module-gradle-plugin` (current Expo module template) instead of applying `org.jetbrains.kotlin.android` directly, so the module builds on AGP 9 with built-in Kotlin (Expo SDK 58) as well as AGP 8 + Kotlin 2.1 (SDK 55).
- Android: drop the hard-coded `kotlin-stdlib:1.9.0` (the plugin adds the stdlib matching the app's `kotlinVersion`) and the explicit `expo-modules-core` dependency (the plugin adds it); `kotlinOptions` → `kotlin { compilerOptions { jvmTarget } }`.
- Android: `compileSdk`/`minSdk` now come from the app (Expo's `compileSdkVersion`/`minSdkVersion`) instead of a fixed 35/24; the library no longer sets `targetSdk`. The JitPack engine (`android-engine-v0.1.9`) and `sezoAudioEngineVersion` override are unchanged.
- iOS: podspec deployment target 15.1 → 16.4 (Expo SDK 56+ floor). Apps must target iOS 16.4 or later.
- iOS: capture `self` explicitly where the module wires engine callbacks in `OnCreate` (Xcode 27 `ImplicitStrongCapture` warnings).
- Dev: `typescript` devDependency `^5.4.5` → `~6.0.3` (Expo SDK 56+ ships TypeScript 6; a TS 5 devDependency made pnpm resolve a second `expo` in consumer workspaces). `tsconfig.json` sets `rootDir` (TS 6 requires it with `outDir`), the Jest tsconfig moves off the deprecated `node10` resolution; `dist/` output is unchanged.
- Example app: moved from Expo SDK 52 to SDK 56 (React Native 0.85, New Architecture); `expo-build-properties` sets the iOS deployment target to 16.4; file access uses `expo-file-system/legacy`. The `example-android` CI job is blocking again.
- Requires Expo SDK 53+ on Android (first SDK that ships `expo-module-gradle-plugin`) and an iOS 16.4 deployment target; `peerDependencies.expo` is unchanged.

## 0.0.19
- Widen `peerDependencies.expo` from `>=51.0.0 <54.0.0` to `>=51.0.0 <59.0.0` (Expo SDK 51–58).
- No native or JS changes; sources are identical to the published 0.0.18.

## 0.0.18
- iOS: use a raw-value Bluetooth HFP category option (`1 << 2`) so the audio session builds on both older and newer iOS SDKs.
- Android: compileSdk/targetSdk 35, NDK 27, default `android-engine-v0.1.9` (16 KB page-size compatible).

## 0.0.1
- Scaffolded module structure.
