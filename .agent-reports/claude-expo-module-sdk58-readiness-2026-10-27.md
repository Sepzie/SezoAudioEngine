# claude/expo-module-sdk58-readiness — 2026-10-27

Goal: make `packages/expo-module` build on Expo SDK 58 (AGP 9.2 / Kotlin 2.2 / Gradle 9.4) while staying
compatible with SDK 55–57. Version 0.0.20, not published, not tagged.

## What I did
- **Android `build.gradle`.** Reference: a scratch `npx create-expo-module@latest` (create-expo-module 58.0.3,
  expo 58.0.20, expo-modules-core 58.0.27). Its template is just `com.android.library` +
  `expo-module-gradle-plugin`, `group`/`version`, `namespace`, `defaultConfig.versionName`. Applied the same
  plugin; removed `org.jetbrains.kotlin.android`, `kotlin-stdlib:1.9.0` and `implementation project(":expo-modules-core")`
  (the plugin adds stdlib for the app's `kotlinVersion` and `compileOnly` expo-modules-core); `kotlinOptions` →
  `kotlin { compilerOptions { jvmTarget = JvmTarget.JVM_17 } }`; dropped fixed `compileSdk 35`/`minSdk 24`/`targetSdk 35`
  so the app's `compileSdkVersion`/`minSdkVersion` apply (the plugin sets them). `group`/`version`/`versionName` come
  from `package.json`: SDK 58's plugin requires `project.version`, SDK 55's requires `defaultConfig.versionName`.
  Kept namespace, `ndkVersion`, JitPack `android-engine-v0.1.9` and the `sezoAudioEngineVersion` override.
- **iOS.** Podspec `:ios => '16.4'`. Nothing else assumed 15.x (only `#available(iOS 13.0, *)` checks, now always true).
  Explicit `self.engine` for the three callback assignments in `OnCreate` (Xcode 27 `ImplicitStrongCapture`).
- **TypeScript 6**: devDependency `~6.0.3`; TS 6 requires `rootDir`
  with `outDir` (TS5011) and rejects `moduleResolution: node10` (TS5107), fixed in `tsconfig.json` / `test/tsconfig.json`.
  `dist/` is byte-identical.
- **Example → SDK 56** (Refs #7, which asked for SDK 54 + New Architecture; SDK 56 has New Architecture always on).
  `expo install expo@^56` then `expo install --fix`, after removing `"sdkVersion": "52.0.0"` from `app.json` (with it,
  `--fix` keeps resolving SDK 52 versions). Result: expo ~56.0.24, RN 0.85.3, React 19.2.3, slider 5.2.0, expo-asset /
  expo-file-system / expo-intent-launcher / expo-build-properties 56.x, `@types/react` ~19.2.14, typescript ~6.0.3.
  Code fixes: `StyleSheet.absoluteFillObject` → `absoluteFill` (gone in RN 0.85), `expo-file-system` → `expo-file-system/legacy`
  (`getInfoAsync`/`getContentUriAsync` on the main entry are throwing stubs since SDK 54). `expo-build-properties` keeps
  `ios.deploymentTarget: "16.4"`. CI's `example-android` job is blocking again (its `continue-on-error` was only until this move).

## Verification
Harness: scratch `create-expo-app` projects (blank-typescript template), module installed from `npm pack`,
`expo prebuild -p android`, `./gradlew :sezo-audio-engine:assembleDebug :sezo-audio-engine:assembleRelease`, JDK 17.
| Toolchain | Expo | RN | AGP | Kotlin | Gradle | Flags | Result |
|---|---|---|---|---|---|---|---|
| SDK 55 | 55.0.32 | 0.83.10 | 8.12.0 | 2.1.20 | 9.0.0 | (none) | ✅ |
| SDK 58 template defaults | 58.0.20 | 0.88.0 | 9.2.1 | 2.2.0 | 9.4.1 | `builtInKotlin=false`, `newDsl=false` | ✅ |
| SDK 58 built-in Kotlin | same | | | | | `builtInKotlin=true`, `newDsl=false` | ✅ (old build.gradle: ❌ "Cannot add extension with name 'kotlin'") |
| SDK 58 new DSL | same | | | | | `builtInKotlin=true`, `newDsl=true` | n/a: Expo's own `expo-constants` fails (`libraryVariants`) before our module |

Example (SDK 56: AGP 8.12.0 / Kotlin 2.1.20 / Gradle 9.3.1): `expo prebuild -p android` + `./gradlew :app:assembleDebug
-PreactNativeArchitectures=x86_64` → `app-debug.apk`. Example typecheck under TS 6 passes.

iOS: `pod ipc spec` → platforms `{ios: 16.4}`. Example `expo prebuild -p ios` → `Podfile.properties.json` has
`ios.deploymentTarget: 16.4`. No xcodebuild or pod install run locally. TS: build, Jest (76 tests, 100%), example typecheck,
`yarn install --frozen-lockfile` all pass.

## Gotchas
- SDK 58's bare template still ships `android.builtInKotlin=false` / `android.newDsl=false`, and its `app/build.gradle`
  applies `kotlin-android` itself, so apps keep those flags until Expo's own modules support the new DSL.
- `expo-module-gradle-plugin` first shipped in expo-modules-core 2.5.0 (SDK 53). This module now needs SDK 53+ on
  Android and an iOS 16.4 app target; `peerDependencies.expo` still says `>=51`, which is a release decision for the owner.
- `example/ios` and `example/android` are git-ignored but not cleaned by anything; remove them after a local prebuild.
- `expo install --fix` honours a stale `expo.sdkVersion` in `app.json` over the installed `expo`.
- SDK 56's iOS dependencies need Swift tools 6.2 (Xcode 26); `macos-15` defaults to Xcode 16.4, so `example-ios` now runs on `macos-26`.
- `AGENTS.md` gained a Public repository rule; downstream consumers are described generically.

## Follow-ups
- Narrow `peerDependencies.expo` (suggest `>=55.0.0 <59.0.0`) when tagging.
- Close #7 if SDK 56 satisfies it. The example app was not run on a device or simulator, only built.
- The Swift capture change is unverified against Xcode 27; `example-ios` CI compiles it.
