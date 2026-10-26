# agent/2-ci-tests-release — 2026-10-26

Issues: #2 (CI on every PR), #3 (real `npm test`, example TS errors), #4 (release workflow + Dependabot).
Branch `agent/2-ci-tests-release`, stacked on `docs/agents-md` (itself on `chore/expo-peer-range-0.0.19`).
PR: https://github.com/Sepzie/SezoAudioEngine/pull/17 (base `docs/agents-md`).

## What I did

- **Example TS errors (#3).** `packages/expo-module/example/App.tsx` had 5 errors. Two
  use-before-declare: the recording-listener effect and the position-poll effect referenced
  `handleRecordingResult` / `handleStop` in their dependency arrays before the `const` was
  declared. Moved those two effects (verbatim) below the callbacks. Three setState mismatches:
  `onChange={setRecordingFormat}` etc. made `TogglePillGroup`'s generic widen to
  `string | number`; wrapped them as `(value) => setX(value)`. The diff is only moves plus those
  three lines. Added a `typecheck` script to the example.
- **Jest (#3).** `packages/expo-module` now runs Jest via ts-jest (`jest.config.cjs`,
  `test/tsconfig.json`). `test/fakeNativeModule.ts` fakes `expo-modules-core`'s
  `requireNativeModule` with an EventEmitter-like object and loads `src/` in an isolated module
  registry per test. 76 tests: native module resolved once by name, no native calls at import,
  import-time failure surfaces, the exact public method list, table-driven arg/return forwarding
  for every method, optional args forwarded as `undefined`, objects passed by reference, sync
  throw and async rejection propagation, and `addListener`/`remove` wiring (delivery,
  isolation per event, remove, double remove, multiple listeners). `test/types.test.ts` holds
  compile-time checks (ts-jest type-checks it, so an unused `@ts-expect-error` fails). 100%
  coverage of `src/`, enforced by a threshold.
- **CI (#2).** `.github/workflows/ci.yml` on `pull_request`, push to `main`, and manual dispatch.
  Jobs: `changes` (git diff based filter), `expo-module` (install with frozen lockfile, tsc build,
  committed `dist/` freshness, Jest with coverage, example `tsc --noEmit`, CHANGELOG entry for the
  current version, `npm pack --dry-run`), `android-engine` (Temurin 17, NDK 27.1 + CMake 3.22.1
  via the preinstalled sdkmanager, `:engine:assembleRelease`, AAR uploaded as an artifact),
  `cpp-tests` (`yarn test:cpp:host`), `example-ios` (macos-15, prebuild, pod install,
  simulator `xcodebuild`), `example-android` (prebuild, JitPack repo appended, `assembleDebug`
  for x86_64). Concurrency per ref, cancel-in-progress on PRs only. Timeouts on every job.
- **Host C++ test fix.** `yarn test:cpp:host` was broken on non-Android hosts since M4A decoding
  landed: `Track.cpp` included `M4ADecoder.h` (NDK MediaCodec) unconditionally. Guarded the
  include and the `M4ADecoder` construction with `#if defined(__ANDROID__)`; on host `.m4a`
  returns "unsupported". Android builds unchanged (verified by the `android-engine` job).
- **Release (#4).** `.github/workflows/release.yml` on `expo-module-v*` tags: fails first with a
  clear `NPM_TOKEN missing` error if the secret is absent; checks tag == `package.json` version
  and that the version is not already on npm; extracts the CHANGELOG section
  (`scripts/changelog-section.mjs`); install, build, `dist/` freshness, tests; `npm pack` into a
  temp dir and asserts key files are in the tarball; `npm publish <tarball> --provenance --access
  public` (token written to a step-scoped npmrc, never exposed to install scripts);
  `gh release create` with the notes and tarball attached. On PRs touching the release tooling,
  and on dispatch, the same steps run as a dry run (`npm publish --dry-run`, no release).
- **Dependabot.** npm at `/` weekly, minor+patch grouped, Expo/React/RN majors (and RN minors)
  ignored; GitHub Actions weekly, grouped.
- **Docs.** `CONTRIBUTING.md`: PR rules, a table of CI jobs with their path triggers and local
  commands, the release process, the NPM_TOKEN setup, recovery steps. Root scripts
  `test:expo-module` and `typecheck:example`. `.gitignore` gains `coverage/`.

## CI status (GitHub-hosted runners)

Run 40404283855 (identical code except the example-android `continue-on-error`):

| Job | Result | Time |
| --- | --- | --- |
| expo-module | green | ~40 s |
| android-engine | green (4 ABIs, AAR artifact) | ~2.5 min |
| cpp-tests | green, 34/34 tests | ~50 s |
| example-ios | green on macos-15 / Xcode 16.4 | ~18 min |
| example-android | red, now `continue-on-error` | ~3 min |
| release (dry run) | green; warns NPM_TOKEN missing | ~30 s |

`example-android` fails in `:expo-modules-core:compileDebugKotlin`: the Compose compiler bundled
with SDK 52's expo-modules-core needs Kotlin 1.9.25, but the example pins `react-native@0.76.0`,
whose Gradle plugin brings Kotlin 1.9.24. That is a property of the example's dependency pins,
not of this module; #7 (SDK 54 bump) should fix it, then drop `continue-on-error`.

## Decisions

- Local work was limited to `yarn install`, `tsc` and Jest; every native build ran on GitHub runners.
- `src/` is a thin pass-through with no JS-side validation or state machine, so the tests pin
  the forwarding contract (including "JS does not clamp or validate") rather than invent
  validation. Adding JS validation would be a behaviour change for the error-model milestone.
- Jest 29 + ts-jest (CJS transform via a test-only tsconfig), not jest-expo: no React Native
  runtime is needed, and it keeps the package's devDependencies small.
- Gradle caching via `actions/setup-java` `cache: gradle` instead of `gradle/actions/setup-gradle`:
  v6 of the latter moved caching to a closed-source component with separate terms of use.
- Release publishes the exact tarball that was inspected, not a fresh pack.
- The CI `dist/` freshness check is deliberate: consumers using this repo as a submodule use the
  committed `dist/`.
- Job-level path filtering via a `changes` job (skipped jobs count as passing for required checks),
  not workflow-level `paths:` (which leaves required checks pending).

## Gotchas

- `packages/expo-module` is `"type": "module"`, so the Jest config must be `.cjs`.
- A stale root `package-lock.json` is tracked next to `yarn.lock`; Yarn warns about it on every
  install and it may confuse Dependabot's package-manager detection.
- `ubuntu-latest` is migrating to Ubuntu 26 (runner annotation); nothing here pins 24.04.

## Follow-ups for the owner

- Add the `NPM_TOKEN` repository secret (see CONTRIBUTING "Releasing"), or switch the publish
  step to npm trusted publishing.
- After merging to `main`, make these required checks: `expo-module (build, test, typecheck, pack)`,
  `android-engine (assembleRelease)`, `cpp-tests (host)`, `example-ios (simulator build)`.
  Do not require `example-android` until #7 lands.
- Consider deleting the root `package-lock.json` (the repo uses Yarn 1).
- Consider adding a linter (ESLint) and a lint step in the `expo-module` job; none is configured.
- Optional: a Gradle Dependabot entry for `packages/android-engine/android`.

## Status

All deliverables done and pushed. CI green except the known non-blocking `example-android`.
