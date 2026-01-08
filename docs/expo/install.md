# Expo Module Installation

## Package Install

When published to npm:

```bash
npm install sezo-audio-engine
```

Until then, install from a git URL or a local workspace path.

## Android Permissions

If you use recording, add:

```xml
<uses-permission android:name="android.permission.RECORD_AUDIO" />
```

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
