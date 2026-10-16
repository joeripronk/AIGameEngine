#!/usr/bin/env bash
set -euo pipefail

# Forge Tetris - Build under ASan (AddressSanitizer) and run with gdb
# Usage: ./run-tetris-debug.sh [--args ...]
#
# Pass extra args after --args to forward them to the game.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build-tetris"

echo "=== Building Forge Tetris (ASAN) ==="
mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build "$BUILD_DIR" -j"$(nproc)" --target ForgeTetris
echo "  ✓ ForgeTetris built"

echo ""

# Build the gdb command list
GDB_ARGS=()

# Common verbosity options
GDB_ARGS+=(-ex "set pagination off")
GDB_ARGS+=(-ex "set width 0")
GDB_ARGS+=(-ex "set logging enabled on")
GDB_ARGS+=(-ex "set logging overwrite on")
GDB_ARGS+=(-ex "set logging enabled off")

if [ "$#" -gt 0 ] && [ "$1" = "--args" ]; then
    shift
    GDB_ARGS+=(-ex "run $@")
else
    GDB_ARGS+=(-ex "run")
fi

# On crash: full backtrace, all threads, registers
GDB_ARGS+=(-ex "if \$stack_available")
GDB_ARGS+=(-ex "bt full")
GDB_ARGS+=(-ex "thread apply all bt")
GDB_ARGS+=(-ex "info registers")
GDB_ARGS+=(-ex "end")

# Run under ASan + gdb for crash diagnostics
export ASAN_OPTIONS="detect_leaks=0"
gdb -batch -ex "file $BUILD_DIR/bin/ForgeTetris" "${GDB_ARGS[@]}" </dev/null