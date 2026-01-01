#!/usr/bin/env bash
set -euo pipefail

ABI="${SEZO_ANDROID_ABI:-arm64-v8a}"
API_LEVEL="${SEZO_ANDROID_API_LEVEL:-24}"
BUILD_DIR="${SEZO_ANDROID_BUILD_DIR:-build/sezo-tests-android/${ABI}}"
SERIAL="${SEZO_ANDROID_SERIAL:-}"
ADB_BIN="${ADB:-adb}"
MODE="all"

usage() {
  cat <<USAGE
Usage: run_android_tests.sh [--abi ABI] [--api API_LEVEL] [--serial SERIAL] [--build-only|--run-only]

Environment variables:
  SEZO_ANDROID_ABI        Android ABI (default: arm64-v8a)
  SEZO_ANDROID_API_LEVEL  Android API level (default: 24)
  SEZO_ANDROID_BUILD_DIR  Build output dir (default: build/sezo-tests-android/<abi>)
  SEZO_ANDROID_SERIAL     adb device serial
  ANDROID_NDK_HOME / ANDROID_NDK_ROOT / ANDROID_NDK  Path to Android NDK
  ADB                     adb binary (default: adb)
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --abi)
      ABI="$2"
      shift 2
      ;;
    --api)
      API_LEVEL="$2"
      shift 2
      ;;
    --serial)
      SERIAL="$2"
      shift 2
      ;;
    --build-only)
      MODE="build"
      shift
      ;;
    --run-only)
      MODE="run"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

NDK=""
for var in ANDROID_NDK_HOME ANDROID_NDK_ROOT ANDROID_NDK; do
  if [[ -n "${!var-}" ]]; then
    NDK="${!var}"
    break
  fi
done

if [[ -z "$NDK" ]]; then
  echo "Android NDK not found. Set ANDROID_NDK_HOME, ANDROID_NDK_ROOT, or ANDROID_NDK." >&2
  exit 1
fi

if ! command -v "$ADB_BIN" >/dev/null 2>&1; then
  echo "adb not found in PATH (set ADB=/path/to/adb if needed)." >&2
  exit 1
fi

resolve_serial() {
  if [[ -n "$SERIAL" ]]; then
    return 0
  fi

  mapfile -t devices < <("$ADB_BIN" devices | awk 'NR>1 && $2=="device" {print $1}')

  if [[ ${#devices[@]} -eq 0 ]]; then
    echo "No adb devices detected. Connect a device or start an emulator." >&2
    exit 1
  fi

  if [[ ${#devices[@]} -eq 1 ]]; then
    SERIAL="${devices[0]}"
    return 0
  fi

  echo "Multiple adb devices detected. Set SEZO_ANDROID_SERIAL to choose one:" >&2
  for device in "${devices[@]}"; do
    echo "  - ${device}" >&2
  done
  exit 1
}

resolve_serial

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

ADB_ARGS=()
if [[ -n "$SERIAL" ]]; then
  ADB_ARGS+=("-s" "$SERIAL")
fi

find_libcxx_shared() {
  local abi="$1"
  local api="$2"
  local triple=""

  case "$abi" in
    armeabi-v7a) triple="arm-linux-androideabi" ;;
    arm64-v8a) triple="aarch64-linux-android" ;;
    x86) triple="i686-linux-android" ;;
    x86_64) triple="x86_64-linux-android" ;;
    *)
      echo "Unsupported ABI: $abi" >&2
      return 1
      ;;
  esac

  local legacy_path="$NDK/sources/cxx-stl/llvm-libc++/libs/${abi}/libc++_shared.so"
  if [[ -f "$legacy_path" ]]; then
    echo "$legacy_path"
    return 0
  fi

  local sysroot_base="$NDK/toolchains/llvm/prebuilt"
  local candidate
  candidate=$(find "$sysroot_base" -path "*/sysroot/usr/lib/${triple}/libc++_shared.so" -print -quit 2>/dev/null || true)
  if [[ -n "$candidate" ]]; then
    echo "$candidate"
    return 0
  fi

  candidate=$(find "$sysroot_base" -path "*/sysroot/usr/lib/${triple}/${api}/libc++_shared.so" -print -quit 2>/dev/null || true)
  if [[ -n "$candidate" ]]; then
    echo "$candidate"
    return 0
  fi

  candidate=$(find "$sysroot_base" -path "*/sysroot/usr/lib/${triple}/*/libc++_shared.so" -print -quit 2>/dev/null || true)
  if [[ -n "$candidate" ]]; then
    echo "$candidate"
    return 0
  fi

  echo "Could not locate libc++_shared.so for ABI ${abi}." >&2
  return 1
}

build_tests() {
  cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-${API_LEVEL}" \
    -DANDROID_STL=c++_shared

  cmake --build "$BUILD_DIR"
}

run_tests() {
  local bin_path
  bin_path=$(find "$BUILD_DIR" -maxdepth 3 -type f -name sezo_engine_tests -print -quit)
  if [[ -z "$bin_path" ]]; then
    echo "sezo_engine_tests binary not found in ${BUILD_DIR}." >&2
    exit 1
  fi

  local libcxx
  libcxx=$(find_libcxx_shared "$ABI" "$API_LEVEL")

  "$ADB_BIN" "${ADB_ARGS[@]}" push "$bin_path" /data/local/tmp/sezo_engine_tests >/dev/null
  "$ADB_BIN" "${ADB_ARGS[@]}" push "$libcxx" /data/local/tmp/libc++_shared.so >/dev/null
  "$ADB_BIN" "${ADB_ARGS[@]}" shell chmod 755 /data/local/tmp/sezo_engine_tests

  "$ADB_BIN" "${ADB_ARGS[@]}" shell "LD_LIBRARY_PATH=/data/local/tmp /data/local/tmp/sezo_engine_tests"
}

case "$MODE" in
  build)
    build_tests
    ;;
  run)
    run_tests
    ;;
  all)
    build_tests
    run_tests
    ;;
  *)
    echo "Unknown mode: $MODE" >&2
    exit 1
    ;;
esac
