# Sezo Audio Engine (Expo module)

The Expo module is the cross-platform package for Sezo Audio Engine. It includes a full iOS implementation and adds background playback support.

## Install

```bash
npm install sezo-audio-engine
```

Or:

```bash
yarn add sezo-audio-engine
```

## Configure permissions

Android recording:

```xml
<uses-permission android:name="android.permission.RECORD_AUDIO" />
```

While recording, the module runs a `microphone` foreground service so the take
survives the app going to the background. Its manifest declares the service and
`FOREGROUND_SERVICE_MICROPHONE`; Google Play requires a foreground service
declaration for the `microphone` type in Play Console. Apps that never record
can drop both with the config plugin option
`["sezo-audio-engine", { "android": { "recordingForegroundService": false } }]`.
See https://sepzie.github.io/SezoAudioEngine/expo/install/#recording-in-the-background.

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

**Building the core from source.**

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

iOS recording or background playback (`UIBackgroundModes: audio` keeps a
recording running in the background; the session stays active until
`stopRecording()`):

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

## Usage

```ts
import { AudioEngineModule } from 'sezo-audio-engine';
```

See the docs for full API details and examples.

## Docs

- Overview: https://sepzie.github.io/SezoAudioEngine/expo/overview/
- Install: https://sepzie.github.io/SezoAudioEngine/expo/install/
- API: https://sepzie.github.io/SezoAudioEngine/expo/api/
