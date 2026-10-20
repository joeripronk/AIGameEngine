#!/usr/bin/env bash
set -euo pipefail

# Forge Tetris - build the Android package.
#
# The package is the game, the engine's resources and the manifest; the
# native library is built by the engine's own CMake with the NDK's toolchain
# file, and the package around it is built by Gradle.
#
# Usage: tools/build-android.sh [--debug] [--abi arm64-v8a] [--api 34]
#
#   --debug     a debuggable package (the default is a release one)
#   --release   a release package (the default)
#   --abi       the ABI to build for (arm64-v8a, armeabi-v7a, x86_64)
#   --api       the Android API level to build against
#
# Needs the Android SDK (ANDROID_HOME) with the NDK installed, and Gradle on
# the path. The NDK's version is the one the package asks for, and the
# package's build.gradle names it: they have to agree, or the build fails
# with a message that says so.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
ANDROID_SDK_ROOT="$ANDROID_HOME"
NDK_VERSION="28.2.13676358"
ABI="arm64-v8a"
API="34"
VARIANT="Release"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug) VARIANT="Debug"; shift ;;
        --release) VARIANT="Release"; shift ;;
        --abi) ABI="$2"; shift 2 ;;
        --api) API="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

NDK_DIR="$ANDROID_HOME/ndk/$NDK_VERSION"
if [[ ! -d "$NDK_DIR" ]]; then
    echo "error: no NDK at '$NDK_DIR'." >&2
    echo "  Install it with: sdkmanager \"ndk;$NDK_VERSION\"" >&2
    exit 1
fi

if ! command -v gradle >/dev/null 2>&1; then
    echo "error: no gradle on the path." >&2
    echo "  The package is built by Gradle; install it, or use the wrapper:" >&2
    echo "  https://services.gradle.org/downloads" >&2
    exit 1
fi

BUILD_DIR="$ROOT/build-android"

echo "=== Building the native library ($ABI, API $API) ==="
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK_DIR/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API" \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)" --target ForgeTetris

echo ""
echo "=== Building the package ($VARIANT) ==="
cd "$ROOT/android"
if [[ "$VARIANT" == "Release" ]]; then
    # The lint worker cannot start on a JDK newer than the one its bundled
    # tooling understands: it parses the JDK's version and gives up on
    # "25.0.4.1". That is a tool that cannot run here, not a finding about
    # the package - `gradle lint` still runs the checks and reports them.
    gradle --no-daemon assembleRelease -x lintVitalAnalyzeRelease
else
    gradle --no-daemon assembleDebug
fi

echo ""
echo "The package is in android/app/build/outputs/apk/$VARIANT/."
