#!/usr/bin/env bash
set -euo pipefail

# Forge Engine - Build all configurations (Debug, Release, Dist)
# Usage: ./build.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

NPROC=$(nproc)
echo "=== Building Forge Engine (nproc=$NPROC) ==="

# Debug
echo ""
echo "--- Debug ---"
mkdir -p build-debug
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j"$NPROC"
echo "  ✓ Debug built"

# Release
echo ""
echo "--- Release ---"
mkdir -p build-release
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j"$NPROC"
echo "  ✓ Release built"

# Dist
echo ""
echo "--- Dist ---"
mkdir -p build-dist
cmake -S . -B build-dist -DCMAKE_BUILD_TYPE=Dist
cmake --build build-dist -j"$NPROC"
echo "  ✓ Dist built"

echo ""
echo "=== Done. Binaries in build-*/bin/ ==="
