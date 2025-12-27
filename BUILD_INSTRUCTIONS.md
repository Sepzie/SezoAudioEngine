# Build Instructions

## Prerequisites

- Android Studio Arctic Fox or later
- Android NDK r25 or later (r26 recommended)
- CMake 3.22.1 or later
- Kotlin 1.9+
- Git (for submodules)

## Initial Setup

### 1. Initialize Submodules

The project uses Oboe as a git submodule. Initialize it:

```bash
cd /path/to/SezoAudioEngine
git submodule update --init --recursive
```

### 2. Verify Dependencies

Check that the following libraries are present:

- **Oboe**: `packages/android-engine/android/engine/src/main/cpp/third_party/oboe/`
- **dr_mp3.h**: `packages/android-engine/android/engine/src/main/cpp/third_party/dr_libs/dr_mp3.h`
- **dr_wav.h**: `packages/android-engine/android/engine/src/main/cpp/third_party/dr_libs/dr_wav.h`

## Building the Android Engine

### Option 1: Build with Gradle (Recommended)

```bash
cd packages/android-engine/android
./gradlew :engine:build
```

This will:
- Compile C++ code using CMake
- Build Kotlin sources
- Create AAR library
- Run unit tests (when available)

### Option 2: Build from Android Studio

1. Open `packages/android-engine/android/` in Android Studio
2. Sync Gradle
3. Build > Make Module 'engine'

## Building the Expo Module

The expo-module depends on android-engine, so ensure the android-engine is built first.

```bash
cd packages/expo-module
npm install
cd android
./gradlew build
```

## Build Outputs

- **Android Engine AAR**: `packages/android-engine/android/engine/build/outputs/aar/`
- **Native Libraries**: `packages/android-engine/android/engine/build/intermediates/cmake/`
- **Expo Module**: `packages/expo-module/android/build/`

## Troubleshooting

### CMake not found
Ensure CMake is installed via Android Studio SDK Manager.

### NDK version mismatch
Check `packages/android-engine/android/engine/build.gradle` and ensure `ndkVersion` matches your installed NDK.

### Oboe not found
Run `git submodule update --init --recursive` to fetch Oboe.

### dr_libs headers not found
Verify the files exist in `packages/android-engine/android/engine/src/main/cpp/third_party/dr_libs/`.

### Linking errors
Ensure the expo-module's `settings.gradle` correctly points to the android-engine module.

## Clean Build

To perform a clean build:

```bash
cd packages/android-engine/android
./gradlew clean
./gradlew :engine:build
```

## Running Tests

Unit tests will be added in Phase 1. To run them:

```bash
cd packages/android-engine/android
./gradlew :engine:test
```

## Next Steps

After successful compilation, you can:
1. Test the example app (when created)
2. Integrate into your own Expo/React Native project
3. Run performance benchmarks
4. Add additional features

## Development Workflow

When making C++ changes:
1. Edit C++ files
2. Rebuild: `./gradlew :engine:build`
3. Test changes in example app

When making Kotlin changes:
1. Edit Kotlin files
2. Rebuild: `./gradlew :engine:build`
3. Test changes

## Notes

- The project uses C++17 standard
- Oboe is built with low-latency settings
- All builds are optimized for real-time audio processing
- Native libs are built for all ABIs: armeabi-v7a, arm64-v8a, x86, x86_64
