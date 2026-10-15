#!/usr/bin/env bash
set -euo pipefail

# Forge Engine - Build and run tests
# Usage: ./build-tests.sh [Debug|Release|Dist]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-Debug}"

case "$BUILD_TYPE" in
    Debug|Release|Dist) ;;
    *) echo "Usage: $0 [Debug|Release|Dist]" && exit 1 ;;
esac

NPROC=$(nproc)
BUILD_DIR="build-${BUILD_TYPE,,}"

echo "=== Building Forge Engine ($BUILD_TYPE) ==="
mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$NPROC"
echo "  ✓ Built"

echo ""
echo "=== Running Tests ($BUILD_TYPE) ==="
cd "$BUILD_DIR"
ctest --output-on-failure -j"$NPROC"
