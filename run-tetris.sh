#!/usr/bin/env bash
set -euo pipefail

# Forge Tetris - Build (Debug) and run the game
# Usage: ./run-tetris.sh [--args ...]
#
# ForgeTetris is its own executable under games/tetris; the engine knows
# nothing about it.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build-tetris"

echo "=== Building Forge Tetris (Debug) ==="
mkdir -p "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" -j"$(nproc)" --target ForgeTetris
echo "  ✓ ForgeTetris built"

echo ""
echo "=== Running Tetris ==="
exec "$BUILD_DIR/bin/ForgeTetris" "$@"