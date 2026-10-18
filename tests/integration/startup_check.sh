#!/usr/bin/env bash
set -uo pipefail

# Forge startup integration check.
#
# Launches the real editor binary and answers the one question unit tests
# cannot: does the engine get through startup on *this* machine? A window
# that never appears has a short list of causes - no display, no Vulkan
# loader/ICD, missing shaders, a driver that fails device init - and each one
# gets a printed hint naming the check that failed and the fix.
#
# Usage: tests/integration/startup_check.sh [path-to-editor-binary] [seconds]
# Exit:  0  startup OK (the editor ran its frame loop for the full window)
#        0  SKIP - this machine has no way to open a window (CI-safe)
#        1  startup problem, classified, with troubleshooting hints
#
# Run inside a desktop session to get the real answer; run in CI to get the
# graceful skip. xvfb-run is used automatically when there is no display and
# the X server wrapper exists, which covers software rendering (lavapipe).

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BIN="${1:-build/bin/ForgeEditor}"
SECONDS_ALIVE="${2:-25}"

log_tail() {
    if [ -f Forge.log ]; then
        echo "--- last lines of Forge.log ---"
        tail -n 12 Forge.log
        echo "---------------------------------"
    fi
}

hints() {
    echo ""
    echo "Troubleshooting map:"
    echo "  no display            export DISPLAY (X11) - or install xvfb and re-run"
    echo "                        under: xvfb-run -a $0"
    echo "  no Vulkan loader      libvulkan.so.1 missing: install the Vulkan runtime"
    echo "                        (apt: libvulkan1 / Vulkan SDK), check: vulkaninfo --summary"
    echo "  no ICD / no device    install a driver (apt: mesa-vulkan-drivers, nvidia) -"
    echo "                        check: ls /usr/share/vulkan/icd.d  and vulkaninfo --summary"
    echo "  software fallback     force lavapipe when the GPU driver misbehaves:"
    echo "                        export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
    echo "  missing shaders       the engine loads resources/shaders/*.spv at startup:"
    echo "                        run tools/compile_shaders.sh (needs glslangValidator)"
    echo "  assert / SIGTRAP      Debug builds trap on FG_CORE_ASSERT - read Forge.log"
    echo "                        for the assertion line, then run ./run-editor-debug.sh"
    echo "  crash / SIGSEGV       ./run-editor-debug.sh gives a full backtrace in gdb.txt"
    echo "  sanitizers            the ASan build catches memory bugs at startup:"
    echo "                        ASAN_OPTIONS=detect_leaks=0 build-asan/bin/ForgeEditor"
}

echo "=== Forge startup check ==="

# --- preflight: everything that must be true before the binary can start ---

if [ ! -x "$BIN" ]; then
    echo "SKIP: $BIN is not built - this check needs the editor target (./build.sh Debug)"
    exit 0
fi

if [ ! -f resources/shaders/pbr.vert.spv ] || [ ! -f resources/shaders/pbr.frag.spv ]; then
    echo "FAIL: engine shaders are not compiled"
    hints
    exit 1
fi

stale=$(find resources/shaders -name '*.vert' -o -name '*.frag' | while read -r src; do
    [ "$src" -nt "$src.spv" ] 2>/dev/null && echo "  $src is newer than $src.spv"
done)
if [ -n "$stale" ]; then
    echo "FAIL: compiled shaders are stale:"
    echo "$stale"
    echo "  hint: run tools/compile_shaders.sh"
    exit 1
fi

use_xvfb=0
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        echo "No display - running under xvfb-run"
        use_xvfb=1

        # A vendor GPU driver under a synthetic X server is a known crash
        # source (it passes enumeration and dies on first use). Under xvfb,
        # pick the software driver deliberately so the check answers the
        # engine question, not the driver question.
        for icd in /usr/share/vulkan/icd.d/lvp_icd*.json /usr/share/vulkan/icd.d/*llvmpipe*.json; do
            if [ -f "$icd" ]; then
                export VK_ICD_FILENAMES="$icd" VK_DRIVER_FILES="$icd"
                echo "Forcing software Vulkan: $icd"
                break
            fi
        done
    else
        echo "SKIP: no display and no xvfb-run - headless CI cannot open a window"
        exit 0
    fi
fi

if command -v vulkaninfo >/dev/null 2>&1; then
    if ! vulkaninfo --summary >/dev/null 2>&1; then
        echo "FAIL: the Vulkan loader is present but enumerates no device"
        log_tail
        hints
        exit 1
    fi
elif ! ldconfig -p 2>/dev/null | grep -q libvulkan; then
    echo "FAIL: no Vulkan loader (libvulkan.so.1) on this machine"
    hints
    exit 1
fi

# --- launch: the editor must survive its frame loop for the whole window ---

echo "Launching $BIN for ${SECONDS_ALIVE}s (a running editor stays open until killed)..."
cmd=("$BIN")
[ "$use_xvfb" -eq 1 ] && cmd=(xvfb-run -a "${cmd[@]}")

status=0
timeout -s INT "$SECONDS_ALIVE" "${cmd[@]}" >/tmp/forge-startup-out.log 2>&1 || status=$?

case "$status" in
    124|130|137)
        echo "STARTUP OK: the editor opened a window and ran ${SECONDS_ALIVE}s of frames."
        exit 0
        ;;
esac

echo "STARTUP PROBLEM: the editor exited on its own with status $status."
case "$status" in
    1)   echo "  plain failure - the engine reported the reason in its log" ;;
    132) echo "  SIGILL - illegal instruction (corrupt code path / CPU feature mismatch)" ;;
    133) echo "  SIGTRAP - a Debug-build FG_ASSERT trapped; read Forge.log for the assert" ;;
    134) echo "  SIGABRT - abort/assert; read Forge.log for the assert" ;;
    139) echo "  SIGSEGV - crash; get the backtrace with ./run-editor-debug.sh" ;;
    127) echo "  binary not executable / missing interpreter" ;;
    126) echo "  binary present but not runnable" ;;
esac

if grep -qiE "shader|spirv|\.spv" /tmp/forge-startup-out.log Forge.log 2>/dev/null; then
    echo "  the log mentions shaders - run tools/compile_shaders.sh"
fi
if grep -qiE "VK_ERROR|physical device|swapchain|extension|validation" /tmp/forge-startup-out.log Forge.log 2>/dev/null; then
    echo "  the log mentions Vulkan - check vulkaninfo --summary and your driver"
fi
if grep -qiE "glfwCreateWindow|window" /tmp/forge-startup-out.log Forge.log 2>/dev/null; then
    echo "  the log mentions the window - check DISPLAY and the X/wayland session"
fi

log_tail
cat /tmp/forge-startup-out.log | sed 's/^/[stdout] /' | tail -n 8
hints
exit 1
