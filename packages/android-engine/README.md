# Android Engine

Native Android audio engine package (C++/Kotlin) for multi-track playback, pitch/speed control, recording, and offline extraction. Android-only; background playback and media controls are expected to be implemented by the host app layer.

## Highlights

- Multi-track playback with sample-accurate sync
- Per-track volume, pan, mute, solo
- Real-time pitch shifting and time-stretch
- Microphone recording (AAC/MP3/WAV) and offline extraction
- Low-latency I/O via Oboe

## Requirements

- minSdk 24, targetSdk 34
- NDK 26.1.10909125, CMake 3.22.1+
- Kotlin 1.9+
- ABIs: armeabi-v7a, arm64-v8a, x86, x86_64

## Build

```bash
cd packages/android-engine/android
./gradlew :engine:assembleRelease
```

Output AAR:
`packages/android-engine/android/engine/build/outputs/aar/engine-release.aar`

## JitPack (Preferred Early Release)

Add JitPack to your repositories:

```gradle
repositories {
  maven { url "https://jitpack.io" }
}
```

Add the dependency (use a git tag like `v0.1.0`):

```gradle
implementation("com.github.Sepzie:SezoAudioEngine:VERSION")
```

JitPack page: https://jitpack.io/#Sepzie/SezoAudioEngine

## Versioning and Tags

JitPack uses git tags as versions. Recommended format: `vMAJOR.MINOR.PATCH`.

Example release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Then depend on:
```gradle
implementation("com.github.Sepzie:SezoAudioEngine:v0.1.0")
```

## Local Integration (without publishing)

1. Include the module:
   - `settings.gradle`:
     ```gradle
     include(":engine")
     project(":engine").projectDir = file("path/to/SezoAudioEngine/packages/android-engine/android/engine")
     ```
2. Add the dependency:
   - `build.gradle`:
     ```gradle
     implementation project(":engine")
     ```

## Tests

Host tests:
```bash
yarn test:cpp:host
```

Android tests (device/emulator):
```bash
yarn test:cpp:android
```

Fixtures live under `packages/android-engine/android/engine/src/test/cpp/fixtures`.
Set `SEZO_TEST_FIXTURES_DIR` to override fixture lookup if needed.

## Publishing

JitPack publishing is configured; Maven Central can be added later if needed.
