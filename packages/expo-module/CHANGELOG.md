# Changelog

## 0.0.19
- Widen `peerDependencies.expo` from `>=51.0.0 <54.0.0` to `>=51.0.0 <59.0.0` (Expo SDK 51–58).
- No native or JS changes; sources are identical to the published 0.0.18.

## 0.0.18
- iOS: use a raw-value Bluetooth HFP category option (`1 << 2`) so the audio session builds on both older and newer iOS SDKs.
- Android: compileSdk/targetSdk 35, NDK 27, default `android-engine-v0.1.9` (16 KB page-size compatible).

## 0.0.1
- Scaffolded module structure.
