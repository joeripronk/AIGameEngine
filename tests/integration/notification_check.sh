#!/usr/bin/env bash
set -uo pipefail

# Forge editor notification check.
#
# The editor reports what it did - a scene opened, a project opened, a file
# changed on disk - in a notification panel, and that panel is where the
# editor's own ImGui discipline is easiest to get wrong. A style colour
# scoped to the function that draws the window outlives the window's End(),
# and ImGui asserts on the unmatched pop: the editor aborts, in Debug, the
# moment anything is reported.
#
# Nothing else catches it. The unit tests never draw a panel, and the
# startup check only asks whether the editor survives its idle frame loop -
# which it does, right up until the first notification. So this check makes
# the editor report something, and then asks whether it is still alive.
#
# Usage: tests/integration/notification_check.sh [path-to-editor-binary]
# Exit:  0  the editor reported and kept running
#        0  SKIP - this machine has no way to open a window (CI-safe)
#        1  the editor died while reporting

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BIN="${1:-build/bin/ForgeEditor}"

echo "=== Forge editor notification check ==="

if [ ! -x "$BIN" ]; then
    echo "SKIP: $BIN is not built - this check needs the editor target (./build.sh Debug)"
    exit 0
fi

# A watched asset. Touching it is what makes the editor report something:
# the asset watcher notices the change and the editor notifies.
WATCHED="resources/scripts/Mover.lua"
if [ ! -f "$WATCHED" ]; then
    echo "SKIP: $WATCHED is not there - the check needs a watched asset to touch"
    exit 0
fi

use_xvfb=0
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        echo "No display - running under xvfb-run"
        use_xvfb=1
        for icd in /usr/share/vulkan/icd.d/lvp_icd*.json /usr/share/vulkan/icd.d/*llvmpipe*.json; do
            if [ -f "$icd" ]; then
                export VK_ICD_FILENAMES="$icd" VK_DRIVER_FILES="$icd"
                export LP_NUM_THREADS="${LP_NUM_THREADS:-1}"
                export LAVAPIPE_NUM_THREADS="${LAVAPIPE_NUM_THREADS:-1}"
                break
            fi
        done
    else
        echo "SKIP: no display and no xvfb-run - headless CI cannot open a window"
        exit 0
    fi
fi

LOG="$(mktemp)"
cmd=("$BIN")
[ "$use_xvfb" -eq 1 ] && cmd=(xvfb-run -a "${cmd[@]}")

"${cmd[@]}" >"$LOG" 2>&1 &
EDITOR_PID=$!

# Give it time to start, then make it report something.
sleep 10
if ! kill -0 "$EDITOR_PID" 2>/dev/null; then
    echo "FAIL: the editor exited before it had anything to report"
    tail -n 12 "$LOG"
    rm -f "$LOG"
    exit 1
fi

echo "Touching $WATCHED to make the editor report a change..."
touch "$WATCHED"
sleep 6

if kill -0 "$EDITOR_PID" 2>/dev/null; then
    echo "OK: the editor reported the change and is still running."
    kill "$EDITOR_PID" 2>/dev/null
    wait "$EDITOR_PID" 2>/dev/null
    rm -f "$LOG"
    exit 0
fi

wait "$EDITOR_PID"
status=$?
echo "FAIL: the editor died (status $status) while reporting a change."
echo "--- last lines of the editor's output ---"
tail -n 12 "$LOG"
echo "--------------------------------------------"
if grep -q "Missing PopStyleColor" "$LOG"; then
    echo "The cause is a style colour still pushed when the window ended."
    echo "A scoped style must not outlive the ImGui window it is used in:"
    echo "see the rule on Forge::UI::ScopedStyleColor in src/Forge/UI/UI.h."
fi
rm -f "$LOG"
exit 1
