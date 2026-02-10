# Development Guide

This project uses **Yarn workspaces** for monorepo management. Changes to source files are automatically reflected via symlinks.

## Setup

```bash
# Install dependencies (from root)
npx yarn install
```

## Development Workflow

### 1. Build TypeScript Changes
When you modify TypeScript files in `packages/expo-module/src/`:

```bash
npx yarn build:expo-module
```

### 2. Build Android Native Changes
When you modify Kotlin/C++ files in `packages/android-engine/` or `packages/expo-module/android/`:

```bash
npx yarn build:android-engine
```

### 3. Build Example App
To rebuild the example Android app:

```bash
npx yarn build:example:android
```

### 4. Run Example App
To run the example app on Android:

```bash
npx yarn run:example
```

### Quick Commands

```bash
# 🚀 YOUR GO-TO COMMAND: Build everything and install on device
npx yarn example:android

# Build modules only (TypeScript + Android native)
npx yarn dev

# Build, install, AND launch the app
npx yarn dev:example

# Clean all build artifacts
npx yarn clean

# Full clean rebuild from scratch
npx yarn rebuild
```

## How Symlinks Work

- **Yarn workspaces** creates a symlink: `node_modules/sezo-audio-engine` → `packages/expo-module`
- Changes to `packages/expo-module/` are immediately visible in `node_modules/`
- **No more manual copying!**

## Important Notes

### For TypeScript Changes:
- Edit files in `packages/expo-module/src/`
- Run `npx yarn build:expo-module` to compile to `dist/`
- Metro bundler picks up changes automatically (with hot reload)

### For Android Native Changes:
- Edit Kotlin files in `packages/expo-module/android/src/`
- Changes are visible immediately via symlink
- Must rebuild Android app: `npx yarn build:example:android`
- Install new APK: `npx yarn install:example:android:apk`

### For C++ Engine Changes:
- Edit C++ files in `packages/android-engine/cpp/`
- Run `npx yarn build:android-engine`
- Rebuild example app: `npx yarn build:example:android`

## Troubleshooting

### "Module not found" errors
- Ensure you ran `npx yarn install` from the root
- Check that `node_modules/sezo-audio-engine` is a symlink (use `ls -la`)

### Android build failures
- Clean gradle cache: `npx yarn clean`
- Full rebuild: `npx yarn rebuild`

### Metro bundler issues
- Clear Metro cache: `cd packages/expo-module/example && rm -rf .expo`
- Restart Metro: Press `r` in the terminal

## Test Lab Report Persistence

The example app Test Lab now persists each run report to app documents under:

- `sezo-testlab-reports/testlab-report-<timestamp>.json`

You can load, share, and delete these reports from the Test Lab UI.

### Mirror Reports to Your Computer During Dev

Run the local receiver on your machine:

```bash
npx yarn testlab:receiver
```

This command also attempts:

- `adb reverse tcp:<receiver-port> tcp:<receiver-port>`

so Android devices/emulators connected over adb can reach your computer receiver automatically.

When the app is connected to the dev server (`__DEV__`), each saved report is also POSTed to:

- `http://<dev-server-host>:8099/testlab/report` (default)

Receiver output directory:

- `artifacts/testlab-reports/`

Optional environment variables:

- `EXPO_PUBLIC_TESTLAB_REPORT_UPLOAD_URL` (full URL override in app)
- `EXPO_PUBLIC_TESTLAB_REPORT_UPLOAD_PORT` (default `8099`)
- `EXPO_PUBLIC_TESTLAB_REPORT_UPLOAD_PATH` (default `/testlab/report`)
- `SEZO_TESTLAB_REPORT_PORT` (receiver port)
- `SEZO_TESTLAB_REPORT_PATH` (receiver route)
- `SEZO_TESTLAB_REPORT_DIR` (receiver output dir)
