# AAR Packaging Plan (Expo Module)

Goal: Make the Expo module self-contained by shipping the Android engine AAR inside the npm package, so consuming apps do not need JitPack or local Gradle projects.

## Steps

1) Build the engine AAR
- Project: `packages/android-engine/android/engine`
- Task: `./gradlew assembleRelease`
- Output: `build/outputs/aar/engine-release.aar`

2) Copy the AAR into the Expo module
- Target folder: `packages/expo-module/android/libs/`
- Example: `packages/expo-module/android/libs/engine-release.aar`

3) Update the Expo module Gradle config
- Add a flatDir repo for the local AAR
- Replace `implementation project(':android-engine')` with:
  - `implementation(name: "engine-release", ext: "aar")`

4) Ensure the AAR is published to npm
- Add `android/libs/*.aar` to `packages/expo-module/package.json` `files`
- Verify `.npmignore` does not exclude the AAR

5) Optional automation
- Add a script to build and copy the AAR before publishing
- Add a CI check to ensure the AAR exists in the package

## Notes
- Package size increases due to embedded native binaries.
- Rebuild and recopy the AAR when the engine changes.
- If ABI-specific .so files are not inside the AAR, include them under `android/src/main/jniLibs/`.
