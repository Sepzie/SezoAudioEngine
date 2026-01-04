# Android Engine Overview

The Android Engine is the native audio core (C++ + Kotlin) packaged as an AAR. It provides synchronized multi-track playback, time-stretching, pitch shifting, recording, and offline extraction.

## When to Use It

- You are building a native Android app.
- You need low-latency audio with precise synchronization.
- You want full control over lifecycle and background playback in your app layer.

## Requirements

- minSdk 24, targetSdk 34
- NDK 26.1.10909125
- CMake 3.22.1+
- Kotlin 1.9+

## Limitations

- Background playback and MediaSession integration are handled by the host app, not the engine.
- The engine expects file paths (not content URIs). Copy content URIs to a temp file first.
