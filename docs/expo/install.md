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
sezoAudioEngineVersion=android-engine-v0.1.6
```

If you want to build from source instead, include the local engine module in
`android/settings.gradle` and it will be picked up automatically.

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
