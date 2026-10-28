#!/usr/bin/env bash
# Release helpers for the Android C++ core (packages/android-engine), used by
# .github/workflows/android-engine-release.yml and the pairing check in ci.yml.
#
#   android-engine-release.sh version            print the core version (packages/android-engine/VERSION)
#   android-engine-release.sh module-default     print the module's default sezoAudioEngineVersion
#   android-engine-release.sh check-pairing BASE fail if the core changed since BASE without a version
#                                                bump, or if VERSION and the module default disagree
#   android-engine-release.sh wait-jitpack TAG   request the JitPack build of TAG and wait for its result
#
# Run from the repository root.
set -euo pipefail

VERSION_FILE="packages/android-engine/VERSION"
MODULE_GRADLE="packages/expo-module/android/build.gradle"
ENGINE_DIR="packages/android-engine/"
JITPACK_GROUP="com.github.Sepzie"
JITPACK_GROUP_PATH="com/github/Sepzie"
JITPACK_ARTIFACT="SezoAudioEngine"

die() {
  echo "error: $*" >&2
  exit 1
}

# read_version [REF]: VERSION at REF (working tree if omitted); empty if absent.
read_version() {
  local ref="${1:-}"
  if [[ -n "$ref" ]]; then
    git show "${ref}:${VERSION_FILE}" 2>/dev/null | tr -d '[:space:]' || true
  elif [[ -f "$VERSION_FILE" ]]; then
    tr -d '[:space:]' < "$VERSION_FILE"
  fi
}

# read_module_default [REF]: the `?: "android-engine-v..."` fallback in the module's build.gradle.
read_module_default() {
  local ref="${1:-}" contents
  if [[ -n "$ref" ]]; then
    contents="$(git show "${ref}:${MODULE_GRADLE}" 2>/dev/null || true)"
  else
    contents="$(cat "$MODULE_GRADLE")"
  fi
  printf '%s\n' "$contents" |
    sed -n 's/.*findProperty("sezoAudioEngineVersion")[^"]*"\(android-engine-v[^"]*\)".*/\1/p' |
    head -n 1
}

cmd_version() {
  local version
  version="$(read_version)"
  [[ -n "$version" ]] || die "$VERSION_FILE is missing or empty"
  [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "$VERSION_FILE must be X.Y.Z, got '$version'"
  echo "$version"
}

cmd_module_default() {
  local value
  value="$(read_module_default)"
  [[ -n "$value" ]] || die "no default sezoAudioEngineVersion found in $MODULE_GRADLE"
  echo "$value"
}

cmd_check_pairing() {
  local base="${1:-}"
  [[ -n "$base" ]] || die "usage: check-pairing BASE_REF"

  local version module_default
  version="$(cmd_version)"
  module_default="$(cmd_module_default)"
  echo "core VERSION: $version; module default: $module_default"
  if [[ "$module_default" != "android-engine-v${version}" ]]; then
    die "$MODULE_GRADLE defaults to $module_default but $VERSION_FILE is $version; they must match (android-engine-v${version})"
  fi

  # Files that ship in the core artifact. Tests and Markdown do not.
  local changed
  changed="$(git diff --name-only "${base}...HEAD" -- "$ENGINE_DIR" |
    grep -v -E '^packages/android-engine/android/engine/src/test/|\.md$' |
    grep -v -x -F "$VERSION_FILE" || true)"
  if [[ -z "$changed" ]]; then
    echo "core sources unchanged since $base"
    return 0
  fi
  echo "core files changed since $base:"
  printf '  %s\n' $changed

  local base_version base_default
  base_version="$(read_version "$base")"
  base_default="$(read_module_default "$base")"
  if [[ "$version" == "$base_version" && "$module_default" == "$base_default" ]]; then
    die "the core changed but $VERSION_FILE ($version) and the module default ($module_default) did not; bump both in this PR"
  fi
  echo "core version bumped: ${base_version:-<none>} -> $version"
}

jitpack_status() {
  curl -fsS --max-time 30 "https://jitpack.io/api/builds/${JITPACK_GROUP}/${JITPACK_ARTIFACT}/$1" 2>/dev/null |
    sed -n 's/^ *"status" *: *"\([^"]*\)".*/\1/p' | head -n 1 || true
}

cmd_wait_jitpack() {
  local tag="${1:-}"
  [[ -n "$tag" ]] || die "usage: wait-jitpack TAG"
  local timeout_s="${JITPACK_TIMEOUT_S:-2400}" interval_s="${JITPACK_POLL_S:-30}"
  local pom="https://jitpack.io/${JITPACK_GROUP_PATH}/${JITPACK_ARTIFACT}/${tag}/${JITPACK_ARTIFACT}-${tag}.pom"
  local log="https://jitpack.io/${JITPACK_GROUP_PATH}/${JITPACK_ARTIFACT}/${tag}/build.log"
  local deadline=$(( $(date +%s) + timeout_s )) status=""

  while true; do
    # Requesting an artifact is what starts a JitPack build; repeat it while
    # JitPack has not seen the tag yet (its tag cache lags a fresh push).
    curl -fsS -o /dev/null --max-time 60 "$pom" 2>/dev/null || true
    status="$(jitpack_status "$tag")"
    echo "$(date -u +%H:%M:%S) JitPack $tag: ${status:-unknown}"
    case "$status" in
      ok)
        echo "JitPack build of $tag succeeded: $pom"
        return 0
        ;;
      error)
        echo "JitPack build of $tag failed. Log: $log" >&2
        curl -fsS --max-time 60 "$log" 2>/dev/null | tail -n 80 >&2 || true
        return 1
        ;;
    esac
    if (( $(date +%s) >= deadline )); then
      die "JitPack did not finish $tag within ${timeout_s}s (last status: ${status:-unknown}); see $log"
    fi
    sleep "$interval_s"
  done
}

case "${1:-}" in
  version) cmd_version ;;
  module-default) cmd_module_default ;;
  check-pairing) shift; cmd_check_pairing "$@" ;;
  wait-jitpack) shift; cmd_wait_jitpack "$@" ;;
  *)
    sed -n '2,10p' "$0" >&2
    exit 2
    ;;
esac
