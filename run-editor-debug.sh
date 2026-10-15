#!/usr/bin/env bash
set -euo pipefail

# Forge Editor - Build (Debug) and run under gdb with verbosity options
# Usage: ./run-editor-debug.sh [--debug] [--release] [--dist]
#        ./run-editor-debug.sh --args "--some-editor-arg"
#
# Pass extra args after --args to forward them to the editor.
#
# Verbosity options (auto-applied in batch mode):
#   - pagination off, line wrap off
#   - logging to session.log
#   - full backtrace + thread dump + registers on crash
#   - local variables dump on crash

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
echo "=== Running Forge Editor under gdb (verbose, auto) ==="

# Build the gdb command list
GDB_ARGS=()

# Common verbosity options
GDB_ARGS+=(-ex "set pagination off")
GDB_ARGS+=(-ex "set width 0")
GDB_ARGS+=(-ex "set logging enabled on")
GDB_ARGS+=(-ex "set logging overwrite on")
GDB_ARGS+=(-ex "set logging enabled off")  # turn off mid-execution (stdout captured by bash)

# Load the executable
GDB_ARGS+=(-ex "file $BUILD_DIR/bin/ForgeEditor")

# Run the program (with optional extra args)
if [ "$#" -gt 0 ] && [ "$1" = "--args" ]; then
    shift
    GDB_ARGS+=(-ex "run $@")
    EXTRA_ARGS=("$@")
else
    GDB_ARGS+=(-ex "run")
    EXTRA_ARGS=()
fi

# On crash: full backtrace, all threads, registers, locals
GDB_ARGS+=(-ex "if \$stack_available")
GDB_ARGS+=(-ex "bt full")
GDB_ARGS+=(-ex "thread apply all bt")
GDB_ARGS+=(-ex "info registers")
GDB_ARGS+=(-ex "end")

# Forward extra args to the editor
#if [ "${#EXTRA_ARGS[@]}" -gt 0 ]; then
    gdb -batch "${GDB_ARGS[@]}" </dev/null
#else
#    gdb -batch "${GDB_ARGS[@]}"
#fi
