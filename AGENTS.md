# Sezo Audio Engine — Agent Guide

Canonical instructions for AI coding agents working in this repo. `CLAUDE.md` points here. This repo is self-contained: everything an agent needs to work on the engine is here or in this repo's GitHub issues.

## What this is
Open-source (MIT) audio engine for Expo/React Native and Android: synchronized multi-track playback, real-time pitch and speed control, simultaneous recording, offline extraction. Published as `sezo-audio-engine` on npm (Expo module) and as `android-engine` on JitPack. Docs: https://sepzie.github.io/SezoAudioEngine/ (built from `docs/` by `.github/workflows/docs.yml`).

The main consumer is SingWithMe (github.com/Sepzie/SingWithMeV3), which vendors this repo as a git submodule and pins production to `expo-module-v*` tags. Keep the public API documented and semver-clean; app-specific logic does not belong here.

## Layout
- `packages/expo-module/` — the npm package: `src/` (TS API), `ios/` (Swift engine + Expo module), `android/` (Kotlin module wrapping the Android engine), `example/` (Expo app used as the manual test bed), `CHANGELOG.md`
- `packages/android-engine/` — Android engine (C++ with Oboe, Kotlin API); Gradle project, JitPack-published; C++ tests under `android/engine/src/test/cpp`
- `docs/` + `mkdocs.yml` — docs site
- `TODOs/` — older audits (Android lifecycle/background playback, AAR packaging); tracked as GitHub issues now

## Commands
```bash
yarn install                      # root workspaces (yarn classic)
yarn build:expo-module            # tsc → packages/expo-module/dist
yarn test:cpp:host                # C++ engine tests (needs cmake)
yarn build:android-engine         # Gradle build of the Android engine (needs NDK)
yarn dev:example:ios              # build module + run the example app on iOS
yarn dev:example:android
cd packages/expo-module && npm pack   # verify what a release ships
```
Requirements: Node LTS, Yarn 1, Xcode 26+, Android SDK + NDK 27.1, cmake for C++ tests.

## Conventions
- Conventional commits; branches `feat/…`, `fix/…`, `chore/…`, `agent/<issue>-…`
- Version lives only in `packages/expo-module/package.json`; the podspec reads it. Every release gets a `CHANGELOG.md` entry and a tag `expo-module-vX.Y.Z`. Publishing (`npm publish` in `packages/expo-module`) is done by the owner.
- Supported Expo SDK range is the `peerDependencies.expo` field; changing it is a release decision.
- Native changes need a build on the affected platform before review; say in the PR what was built and run.
- Don't rewrite `dist/`; it is generated.
- Heavy builds: this engine is developed on a Mac mini that also hosts SingWithMe's self-hosted iOS CI runner. Run one Xcode/Gradle build at a time locally and prefer GitHub-hosted runners for CI.

## Roadmap
GitHub milestones: **0.1.0 – Solid foundation** (CI, real tests, release workflow, contributor docs) → **Expo SDK 55–58 support** → **Robustness** (Android lifecycle, recording stop latency, error model) → **Adoption**. Pick issues labelled `agent-ready`.

## Session report
At the end of a task write `.agent-reports/<branch>-<YYYY-MM-DD>.md`: what you did, what worked, blockers, decisions, gotchas, status, follow-ups. No report = incomplete task.
