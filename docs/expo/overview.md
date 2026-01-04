# Expo Module Overview

The Expo module exposes the audio engine to JavaScript via Expo Modules. The Android implementation is functional today. The iOS implementation is planned and will mirror the same API surface.

## When to Use It

- You want a React Native/Expo API for audio playback and recording.
- You prefer a single, cross-platform API for Android now and iOS later.

## Status

- Android: implemented and tested
- iOS: stubbed, API compatibility planned

## Limitations

- Background playback helpers are currently no-ops.
- `content://` URIs are not supported on Android yet (copy to a file path first).
