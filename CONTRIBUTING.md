# Contributing

Thanks for your interest in contributing. This repo is a Yarn 1 workspace containing the
`sezo-audio-engine` Expo module (`packages/expo-module`), its example app
(`packages/expo-module/example`) and the Android engine (`packages/android-engine`).
See [DEVELOPMENT.md](DEVELOPMENT.md) and [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) for
day-to-day setup, and [AGENTS.md](AGENTS.md) for conventions.

## Pull requests

- Branch from `main` (`feat/…`, `fix/…`, `chore/…`) and use conventional commits.
- Every PR runs [`ci.yml`](.github/workflows/ci.yml). Keep it green.
- If you change `packages/expo-module/src`, run `yarn build:expo-module` and commit the
  regenerated `packages/expo-module/dist` (CI fails if it is stale; consumers that vendor this
  repo as a git submodule use the committed `dist/`).
- Native changes: say in the PR what you built and ran, and on which platform.

## CI jobs and how to run them locally

Requirements: Node LTS, Yarn 1 (`npm i -g yarn`), and for native work Xcode, JDK 17, the
Android SDK with NDK `27.1.12297006` and CMake `3.22.1`, and host `cmake`.

| CI job | When it runs on a PR | Run it locally |
| --- | --- | --- |
| `expo-module` | always | see below |
| `android-engine` | `packages/android-engine/**`, `.gitmodules`, root `package.json`/`yarn.lock`, `ci.yml` | `git submodule update --init --recursive && cd packages/android-engine/android && ./gradlew :engine:assembleRelease` |
| `cpp-tests` | same as `android-engine` | `git submodule update --init --recursive && yarn test:cpp:host` |
| `example-ios` | `packages/expo-module/{ios,src,example}/**`, its `package.json`, root `package.json`/`yarn.lock`, `ci.yml` | see below |
| `example-android` | `packages/expo-module/{android,src,example}/**`, its `package.json`, root `package.json`/`yarn.lock`, `ci.yml` | see below |
| `release` (dry run) | `.github/workflows/release.yml`, `scripts/changelog-section.mjs` | see [Releasing](#releasing) |

Pushes to `main` run every job.

**`expo-module`**

```bash
yarn install --frozen-lockfile
yarn build:expo-module                 # tsc -> packages/expo-module/dist (commit it)
yarn test:expo-module --coverage       # Jest; the native module is faked, 100% coverage of src/ is enforced
yarn typecheck:example                 # tsc --noEmit for the example app
node scripts/changelog-section.mjs "$(node -p "require('./packages/expo-module/package.json').version")"
(cd packages/expo-module && npm pack --dry-run)   # what would ship
```

There is no linter configured yet.

**`example-ios`** (macOS, Xcode)

```bash
yarn install --frozen-lockfile && yarn build:expo-module
cd packages/expo-module/example
npx expo prebuild --platform ios --no-install
(cd ios && pod install)
xcodebuild -workspace ios/SezoAudioEngineExample.xcworkspace -scheme SezoAudioEngineExample \
  -configuration Debug -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' \
  CODE_SIGNING_ALLOWED=NO build
```

**`example-android`** (currently non-blocking: `continue-on-error` until the example moves off
Expo SDK 52 in #7; its pinned `react-native@0.76.0` uses Kotlin 1.9.24 while SDK 52's
`expo-modules-core` needs 1.9.25, so `:expo-modules-core:compileDebugKotlin` fails)

```bash
yarn install --frozen-lockfile && yarn build:expo-module
cd packages/expo-module/example
npx expo prebuild --platform android --no-install
# The module pulls the engine AAR from JitPack: add maven { url "https://www.jitpack.io" }
# to allprojects.repositories in android/build.gradle (CI appends it automatically).
(cd android && ./gradlew :app:assembleDebug -PreactNativeArchitectures=x86_64)
```

Native builds are heavy. On a shared machine run one at a time, or just push and let the
GitHub-hosted runners do it.

## Releasing

Releases of the npm package are cut from `main` by pushing a tag; the
[`release`](.github/workflows/release.yml) workflow does the rest.

### One-time setup (repository owner)

Add an **`NPM_TOKEN`** repository secret (Settings → Secrets and variables → Actions):
an npm granular access token with read and write access to the `sezo-audio-engine` package
(it must be allowed to publish without an interactive 2FA prompt). Without it the release
workflow stops at its first step with an `NPM_TOKEN missing` error; nothing is published.
npm limits the lifetime of write tokens, so expect to rotate it. Moving to npm trusted
publishing (OIDC, no token) later only needs the publish step changed.

### Steps

1. On a branch, bump `version` in `packages/expo-module/package.json` (the podspec reads it).
2. Add a `## X.Y.Z` section at the top of `packages/expo-module/CHANGELOG.md`. Its body becomes
   the GitHub release notes; the workflow fails if it is missing or empty.
3. `yarn build:expo-module` and commit `dist/` if it changed. Open a PR, get CI green, merge.
4. Tag the merge commit on `main` and push the tag:

   ```bash
   git checkout main && git pull
   git tag expo-module-vX.Y.Z
   git push origin expo-module-vX.Y.Z
   ```

5. The workflow then:
   - checks `NPM_TOKEN` is set,
   - checks the tag matches `package.json` and that `X.Y.Z` is not already on npm,
   - builds, checks `dist/` is up to date, runs the Jest suite,
   - `npm pack`s and checks the tarball contains `dist/`, the podspec, the Android sources and
     `expo-module.config.json`,
   - `npm publish --provenance --access public`,
   - creates the GitHub release `expo-module-vX.Y.Z` with the CHANGELOG section as notes and the
     tarball attached.

If a step fails before `npm publish`, fix the cause, delete and re-push the tag
(`git push --delete origin expo-module-vX.Y.Z`). If publish succeeded but the GitHub release
step failed, create the release by hand (`gh release create expo-module-vX.Y.Z --notes-file …`)
rather than re-running, since npm will refuse to publish the same version twice.

The Android engine is published separately through JitPack from `android-engine-v*` tags
(see `jitpack.yml`).

## Dependencies

Dependabot opens weekly grouped PRs for npm (minor and patch) and GitHub Actions. Expo,
React and React Native majors are ignored; they move with the supported Expo SDK range and
are bumped deliberately.
