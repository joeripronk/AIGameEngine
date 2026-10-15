#!/usr/bin/env bash
set -euo pipefail

# Forge Editor - Build (Debug) and run under gdb
# Usage: ./run-editor-debug.sh [--debug] [--release] [--dist]
#        ./run-editor-debug.sh --args "--some-editor-arg"
#
# Pass extra args after --args to forward them to the editor.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE="${1:-Debug}"
shift 2>/dev/null || true

case "$BUILD_TYPE" in
    Debug|Release|Dist) ;;
    *) echo "Usage: $0 [Debug|Release|Dist] [--args ...]" && exit 1 ;;
esac

NPROC=$(nproc)
BUILD_DIR="build-${BUILD_TYPE,,}"

echo "=== Building Forge Editor ($BUILD_TYPE) ==="
mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$NPROC" --target ForgeEditor
echo "  ✓ Editor built"

echo ""
echo "=== Running Forge Editor under gdb ==="
if [ "$#" -gt 0 ] && [ "$1" = "--args" ]; then
    shift
    gdb --args "$BUILD_DIR/bin/ForgeEditor" "$@"
else
    gdb --args "$BUILD_DIR/bin/ForgeEditor"
fi
