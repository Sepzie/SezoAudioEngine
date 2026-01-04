# Android Engine Installation

## JitPack

Add JitPack to your repositories:

```gradle
repositories {
  maven { url "https://jitpack.io" }
}
```

Add the dependency:

```gradle
implementation("com.github.Sepzie:SezoAudioEngine:VERSION")
```

Replace `VERSION` with a tag like `v0.1.2`.

JitPack page: https://jitpack.io/#Sepzie/SezoAudioEngine

## Local Module

If you want to build from source:

1. Add the module in `settings.gradle`:
   ```gradle
   include(":engine")
   project(":engine").projectDir = file("path/to/SezoAudioEngine/packages/android-engine/android/engine")
   ```
2. Add the dependency:
   ```gradle
   implementation project(":engine")
   ```

## Permissions

If you use recording, add:

```xml
<uses-permission android:name="android.permission.RECORD_AUDIO" />
```
