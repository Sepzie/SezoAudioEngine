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
sezoAudioEngineVersion=android-engine-v0.1.8
```

If you want to build from source instead, include the local engine module in
`android/settings.gradle` and it will be picked up automatically.

iOS recording or background playback:

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
