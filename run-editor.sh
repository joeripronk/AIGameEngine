#!/usr/bin/env bash
set -euo pipefail

# Forge Editor - Build (Debug) and run
# Usage: ./run-editor.sh [--debug] [--release] [--dist]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-Debug}"

case "$BUILD_TYPE" in
    Debug|Release|Dist) ;;
    *) echo "Usage: $0 [Debug|Release|Dist]" && exit 1 ;;
esac

NPROC=$(nproc)
BUILD_DIR="build-${BUILD_TYPE,,}"

echo "=== Building Forge Editor ($BUILD_TYPE) ==="
mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$NPROC" --target ForgeEditor
echo "  ✓ Editor built"

echo ""
echo "=== Running Forge Editor ==="
exec "$BUILD_DIR/bin/ForgeEditor"
