#!/usr/bin/env bash
# Compiles the GLSL shaders in resources/shaders/ to SPIR-V.
#
# The engine consumes SPIR-V directly (raw Vulkan shader modules), so this is a build-time step,
# not a runtime dependency. The generated .spv files are committed so a normal
# engine build does not require glslang.
#
# Requires: glslangValidator (https://github.com/KhronosGroup/glslang)
#   Linux:   apt install glslang-tools
#   macOS:   brew install glslang
#   Windows: choco install glslang
#
# Usage: tools/compile_shaders.sh [glslangValidator-path]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHADER_DIR="$ROOT/resources/shaders"
GLSLANG="${1:-glslangValidator}"

if ! command -v "$GLSLANG" >/dev/null 2>&1 && [ ! -x "$GLSLANG" ]; then
    echo "error: glslangValidator not found (pass its path as the first argument)" >&2
    exit 1
fi

for src in "$SHADER_DIR"/*.vert "$SHADER_DIR"/*.frag; do
    [ -e "$src" ] || continue
    base="$(basename "$src")"
    out="${base}.spv"          # e.g. pbr.vert.spv
    echo "compiling $base -> $out"
    "$GLSLANG" -V --target-env vulkan1.3 "$src" -o "$SHADER_DIR/$out"
done

echo "done. generated SPIR-V in $SHADER_DIR"