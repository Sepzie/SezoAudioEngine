# Sezo Audio Engine

Sezo Audio Engine ships two packages in one repo:

- **Android Engine**: a native Android AAR (C++/Kotlin) for low-latency, multi-track audio.
- **Expo Module**: a JavaScript/TypeScript API that wraps the native engine for Expo/React Native.

Use the Android Engine directly for native Android apps. Use the Expo Module if you want a cross-platform JS API.

## Status

- Android Engine: production-ready for Android.
- Expo Module: Android implementation is functional; iOS is planned and will share the same API surface.

## Quick Links

- Android Engine docs: `android/overview.md`
- Expo Module docs: `expo/overview.md`
- Concepts: `common/concepts.md`
- JitPack: https://jitpack.io/#Sepzie/SezoAudioEngine

## Repo Layout

- `packages/android-engine`: native engine
- `packages/expo-module`: Expo module wrapper and example app

## Support

Open a GitHub issue for bugs or feature requests.
