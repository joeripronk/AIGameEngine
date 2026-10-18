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
#   - logging to gdb.txt
#   - full backtrace + thread dump + registers on crash
#   - local variables dump on crash
#
# Startup problems are printed with hints: the preflight checks the things
# that make the editor exit before a window ever appears (display, Vulkan
# loader, stale shaders), and the post-run section classifies the exit
# status. For a full classified startup test run:
#   tests/integration/startup_check.sh

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

# Preflight: every check below is a reason the editor can exit before a
# window ever appears. The editor's own log will not tell you the display
# was missing - it exits before the logger that would say so exists.
echo ""
echo "=== Preflight ==="
PREFLIGHT_OK=1
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "  ✗ no DISPLAY / WAYLAND_DISPLAY - GLFW cannot open a window"
    echo "    hint: export DISPLAY=:0, or run under xvfb: xvfb-run -a $0 ${BUILD_TYPE}"
    PREFLIGHT_OK=0
fi
if command -v ldconfig >/dev/null 2>&1 && ! ldconfig -p 2>/dev/null | grep -q libvulkan; then
    echo "  ✗ no Vulkan loader (libvulkan.so.1) - install the Vulkan runtime"
    echo "    hint: apt install libvulkan1 mesa-vulkan-drivers, verify: vulkaninfo --summary"
    PREFLIGHT_OK=0
fi
if command -v vulkaninfo >/dev/null 2>&1 && ! vulkaninfo --summary >/dev/null 2>&1; then
    echo "  ✗ Vulkan loader present but enumerates no device"
    echo "    hint: install a driver (mesa-vulkan-drivers / nvidia) - software fallback:"
    echo "          export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
    PREFLIGHT_OK=0
fi
stale=""
for src in resources/shaders/*.vert resources/shaders/*.frag; do
    [ -f "$src" ] && [ "$src" -nt "$src.spv" ] 2>/dev/null && stale="$stale $src"
done
if [ -n "$stale" ]; then
    echo "  ✗ shader sources newer than their compiled .spv:$stale"
    echo "    hint: run tools/compile_shaders.sh - the editor loads .spv, not .frag"
    PREFLIGHT_OK=0
fi
if [ "$PREFLIGHT_OK" = 1 ]; then
    echo "  ✓ display, Vulkan loader and compiled shaders all present"
fi

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
status=0
gdb -batch "${GDB_ARGS[@]}" </dev/null || status=$?

echo ""
echo "=== Result ==="
case "$status" in
    0)
        echo "  Editor exited cleanly (0)."
        ;;
    1)
        # gdb -batch answers 1 when the program died by signal or non-zero exit.
        echo "  Editor exited with a failure (gdb status 1)."
        echo "  The backtrace printed above is the answer; gdb.txt holds the session."
        echo ""
        echo "  Troubleshooting the common cases:"
        echo "    instant exit, no window   DISPLAY/Vulkan problem - the preflight above,"
        echo "                              full classified check: tests/integration/startup_check.sh"
        echo "    SIGTRAP / SIGABRT         a Debug FG_ASSERT trapped - the assert text"
        echo "                              is the last line of Forge.log"
        echo "    SIGSEGV / use-after-free  bt full above; reproduce under sanitizers:"
        echo "                              cmake --build build-asan && ASAN_OPTIONS=detect_leaks=0 build-asan/bin/ForgeEditor"
        echo "    shader load errors        the editor loads resources/shaders/*.spv at"
        echo "                              startup: run tools/compile_shaders.sh"
        echo "    Vulkan device errors      vulkaninfo --summary ; software rendering:"
        echo "                              export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
        echo "    'no such file' on launch  run from the repo root - resources/ paths are"
        echo "                              relative to the working directory"
        echo ""
        echo "  Logs: Forge.log (engine), gdb.txt (this gdb session)."
        ;;
    2)
        echo "  gdb reported an error (status 2) - it never ran the editor."
        echo "  hint: check the binary path ($BUILD_DIR/bin/ForgeEditor) and that gdb is installed."
        ;;
    *)
        echo "  Editor exited with status $status."
        echo "  hint: reproduce under sanitizers (build-asan) and read Forge.log."
        ;;
esac

exit "$status"
