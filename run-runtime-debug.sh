#!/usr/bin/env bash
set -euo pipefail

# Forge Runtime - Build and run under ASan (AddressSanitizer) for crash debugging
# Usage: ./run-runtime-debug.sh [--args ...]
#
# Pass extra args after --args to forward them to the runtime.
#
# Verbosity options (auto-applied in batch mode):
#   - pagination off, line wrap off
#   - logging to session.log
#   - full backtrace + thread dump + registers on crash
#   - local variables dump on crash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build-asan"

echo "=== Building Forge Runtime (ASAN) ==="
mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build "$BUILD_DIR" -j"$(nproc)" --target ForgeRuntime
echo "  ✓ Runtime built"

echo ""

# Build the gdb command list
GDB_ARGS=()

# Common verbosity options
GDB_ARGS+=(-ex "set pagination off")
GDB_ARGS+=(-ex "set width 0")
GDB_ARGS+=(-ex "set logging enabled on")
GDB_ARGS+=(-ex "set logging overwrite on")
GDB_ARGS+=(-ex "set logging enabled off")  # turn off mid-execution (stdout captured by bash)

# Run the program under ASan + gdb
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
gdb -batch -ex "file $BUILD_DIR/bin/ForgeRuntime" "${GDB_ARGS[@]}" </dev/null
