# chore/android-engine-release-workflow — 2026-10-28

Refs #4 (release workflow), #10. Branch from main 595cc7a (PR #26 merged).

## What I did
- **Module paired with the new core**: default `sezoAudioEngineVersion` in
  `packages/expo-module/android/build.gradle` `android-engine-v0.1.9` → `android-engine-v0.1.10`; install docs and
  module README examples updated.
- **Single source of truth** `packages/android-engine/VERSION` (`0.1.10`). `engine/build.gradle` uses it as
  `version` when the `VERSION` env var is unset (JitPack sets it to the tag; that override is kept).
- **`scripts/android-engine-release.sh`**: `version`, `module-default`, `check-pairing BASE`, `wait-jitpack TAG`.
- **`.github/workflows/android-engine-release.yml`**: on push to `main` touching `packages/android-engine/**`,
  `jitpack.yml` (or the script/workflow), and on dispatch: checks VERSION against the module default, creates an
  annotated tag `android-engine-v<VERSION>` at the pushed commit if it does not exist (never moves one), then
  requests the POM and polls `https://jitpack.io/api/builds/...` every 30 s (40 min cap) until `ok` or `error`;
  on `error` prints the tail of the JitPack build log and fails.
- **CI pairing job** `android-engine version pairing` (PRs): fails when VERSION and the module default disagree, and
  when shipped core files changed since the PR base while neither changed. Exempt from the bump: `src/test/` and
  `*.md` under `packages/android-engine/` (they do not ship in the AAR).
- Docs: AGENTS.md "Releases", `docs/expo/install.md` "Core versions"; CHANGELOG Unreleased.

## JitPack v0.1.10
The tag `android-engine-v0.1.10` is **not on GitHub** (`git ls-remote --tags origin` lists v0.1.7–v0.1.9 only), so
JitPack reports `tagNotFound` ("Tag or commit not found") and no build ran. I did not create it. With this PR,
merging creates it automatically (VERSION is 0.1.10 and the tag is missing), at the merge commit instead of
595cc7a; the core sources are identical. Alternatively push the tag at 595cc7a before merging; the workflow then
leaves it alone and only waits for the build.

## Verification
- Script, locally with bash 3.2 in a scratch worktree: bumped branch with core change → pass; core change without
  bump → fail with message; test-only change → pass; VERSION/default mismatch → fail; malformed VERSION → fail.
- `wait-jitpack android-engine-v0.1.9` → ok; `wait-jitpack android-engine-v0.1.10` with a 5 s timeout →
  `tagNotFound`, then `none`, then timeout failure.
- Workflow YAML parses (js-yaml). No Gradle locally; `engine/build.gradle` change is checked by CI's
  `android-engine` and `example-android` jobs.

## Not verified
- The release workflow itself only runs on `main`; tag push with `GITHUB_TOKEN` assumes no tag protection rule
  blocks `android-engine-v*`. `publishToMavenLocal` (what JitPack runs) is not run in CI; CI runs `assembleRelease`.

## Gotchas
- JitPack's tag cache lags a fresh push ("Rechecking"); the script keeps re-requesting the POM until it builds.
- Tags pushed with `GITHUB_TOKEN` do not trigger other workflows (none listen to `android-engine-v*`).

## Status
PR open, CI pending at time of writing.
