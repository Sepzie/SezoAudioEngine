# Expo Module Installation

## Package Install

Install from npm:

```bash
npm install sezo-audio-engine
```

NPM package: https://www.npmjs.com/package/sezo-audio-engine

## Android Permissions

If you use recording, add:

```xml
<uses-permission android:name="android.permission.RECORD_AUDIO" />
```

Request it at runtime before calling `startRecording()`; without it
`startRecording()` rejects with `RECORDING_PERMISSION_DENIED`.

### Recording in the background

While a recording is in progress the module runs a foreground service with the
`microphone` type (plus `mediaPlayback` when playback was running at
`startRecording()`), so the process and microphone access survive the app going
to the background. It starts in `startRecording()` (Android 14+ only allows a
microphone foreground service to start while the app is in the foreground and
`RECORD_AUDIO` is granted) and stops in `stopRecording()` or `release()`. While
it runs, the module does not pause playback when the app is backgrounded, even
if background playback is not enabled. The service shows an ongoing
"Recording" notification; override its texts and icon with app resources named
`sezo_audio_engine_recording_notification_title`,
`sezo_audio_engine_recording_notification_text`,
`sezo_audio_engine_recording_channel_name` (strings) and
`sezo_audio_engine_recording_icon` (drawable).

The module's manifest declares the service and the
`FOREGROUND_SERVICE_MICROPHONE` permission. **Google Play requires a
foreground service declaration in Play Console for the `microphone` type**
(App content → Foreground service permissions) for apps targeting Android 14+.
Apps that never record can remove both from their manifest with the config
plugin:

```json
{
  "expo": {
    "plugins": [["sezo-audio-engine", { "android": { "recordingForegroundService": false } }]]
  }
}
```

Without the service, recordings still work in the foreground but are not
protected in the background.

## Android Engine Dependency

The Expo module links to the Android engine from JitPack by default. Add the JitPack repository to your app:

```gradle
// android/build.gradle
allprojects {
  repositories {
    maven { url "https://www.jitpack.io" }
  }
}
```

Optionally pin the engine version in `android/gradle.properties`:

```properties
sezoAudioEngineVersion=android-engine-v0.1.10
```

### Core versions

The module's default `sezoAudioEngineVersion` always names the core release it
was built against. Core changes bump `packages/android-engine/VERSION` and that
default in the same change; the `android-engine-v<VERSION>` tag and its JitPack
build are created automatically when the change lands on `main`. Override the
default only to test another core build. Apps that build the core from source
(below) do not use the tag at all.

### Building the core from source

When the package comes from a checkout of this repository (a git clone, or the
repo vendored as a git submodule) instead of npm, the Android C++ core can be
built from source so native changes need no JitPack release. Add the config
plugin to `app.json` and run `npx expo prebuild`:

```json
{ "expo": { "plugins": ["sezo-audio-engine"] } }
```

If `packages/android-engine/android/engine` exists next to the package (with
its submodules: `git submodule update --init --recursive`), the plugin includes
it in `android/settings.gradle` as `:android-engine` and the module links that
project; the app then needs the NDK (27.1) and CMake 3.22.1. Installed from
npm, the directory is absent, the plugin does nothing and JitPack is used.
Opt out with `["sezo-audio-engine", { "android": { "engineFromSource": false } }]`
or `SEZO_AUDIO_ENGINE_FROM_SOURCE=0`.

## iOS Info.plist

If you use recording or background playback, add the keys below.
`NSMicrophoneUsageDescription` is required to record at all, and
`UIBackgroundModes: ["audio"]` is what keeps a recording (or playback) running
while the app is in the background:

```json
{
  "expo": {
    "ios": {
      "infoPlist": {
        "NSMicrophoneUsageDescription": "Allow access to the microphone for recording audio.",
        "UIBackgroundModes": ["audio"]
      }
    }
  }
}
```

### Recording and the audio session on iOS

`startRecording()` configures the session as `.playAndRecord` and activates it;
the session and the audio engine stay active until `stopRecording()` (pausing
or stopping playback during a take does not stop the engine). If the system
interrupts the session (a phone call, Siri, another app taking the session),
the engine finalises the take and emits `recordingStopped` with
`reason: 'interrupted'` and the file's `uri`, plus `engineStateChanged` with
`reason: 'recordingInterrupted'`; a later `stopRecording()` call returns an
empty result. Listen for the event to keep the take.

## Expo Config

The module is built with Expo Modules and is auto-registered by `expo-module.config.json`.
