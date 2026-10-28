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

If you use recording or background playback, add:

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

## Expo Config

The module is built with Expo Modules and is auto-registered by `expo-module.config.json`.
