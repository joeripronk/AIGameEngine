#!/usr/bin/env bash
set -uo pipefail

# Forge window-resize integration check.
#
# The resize is the one thing a unit test cannot answer: it needs a real
# window, a real swapchain and a real presentation. The question is simple -
# when the window changes shape, does the frame follow it?
#
# A renderer that keeps the old size draws the scene into a corner of the
# window with the rest of it black, and the overlay - which lays itself out
# in the new size - is drawn at the old scale and spills out of the image.
# That is not a theory: it is what happened on Linux, where the X11
# presentation path does not report a resize and the pending rebuild was
# cleared by the present at the end of the frame.
#
# So the check asks the game to resize its own window, captures the frame
# that follows, and reads the capture: the image must be the new size, and
# the part of it that is new must be drawn rather than black.
#
# Usage: tests/integration/resize_check.sh [path-to-tetris-binary]
# Exit:  0  the frame follows the window
#        0  SKIP - this machine cannot open a window (CI-safe)
#        1  the frame did not follow the window
#
# The game resizes its own window (FG_RESIZE_TO) because there is no window
# manager to drag it with on a headless machine, and because the request
# goes through the same path a user's drag does.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BIN="${1:-build/bin/ForgeTetris}"

# The window the game opens, and the size it is asked to change to. The
# second is bigger on purpose: the part of the image that is new is the
# part that was black when the renderer kept the old size.
OLD_WIDTH=1600
OLD_HEIGHT=900
NEW_WIDTH=1900
NEW_HEIGHT=1040

echo "=== Forge resize check ==="

if [ ! -x "$BIN" ]; then
    echo "SKIP: $BIN is not built - this check needs the ForgeTetris target"
    exit 0
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP: python3 is not available to read the captured frame"
    exit 0
fi

use_xvfb=0
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        echo "No display - running under xvfb-run"
        use_xvfb=1

        # Under a synthetic X server the vendor GPU driver is a known crash
        # source, so the software rasteriser is chosen deliberately: the
        # check answers the engine's question, not the driver's. One
        # raster thread: the software renderer spawns one per core by
        # default, and a check that forces it then pegs the machine.
        for icd in /usr/share/vulkan/icd.d/lvp_icd*.json /usr/share/vulkan/icd.d/*llvmpipe*.json; do
            if [ -f "$icd" ]; then
                export VK_ICD_FILENAMES="$icd" VK_DRIVER_FILES="$icd"
                export LP_NUM_THREADS="${LP_NUM_THREADS:-1}"
                export LAVAPIPE_NUM_THREADS="${LAVAPIPE_NUM_THREADS:-1}"
                echo "Forcing software Vulkan: $icd (1 raster thread)"
                break
            fi
        done
    else
        echo "SKIP: no display and no xvfb-run - headless CI cannot open a window"
        exit 0
    fi
fi

CAPTURE="$(mktemp -d)/resize.ppm"
rm -f "$CAPTURE"

echo "Launching $BIN (it resizes its own window to ${NEW_WIDTH}x${NEW_HEIGHT})..."
export FG_AUTOSTART=1
export FG_TARGET_FPS=30
export FG_RESIZE_TO="${NEW_WIDTH}x${NEW_HEIGHT}"
export FG_CAPTURE_PATH="$CAPTURE"
# The capture is taken well after the resize (frame 30): the frame that
# follows the resize is the one that answers the question.
export FG_CAPTURE_FRAME=90
export FG_AUTO_EXIT_FRAMES=140

cmd=("$BIN")
if [ "$use_xvfb" -eq 1 ]; then
    # The window has to fit on the screen it is opened on: 1900x1040 does
    # not fit on the 1280x1024 default.
    cmd=(xvfb-run -a -s "-screen 0 1920x1080x24" "${cmd[@]}")
fi

status=0
timeout -s INT 180 "${cmd[@]}" >/tmp/forge-resize-out.log 2>&1 || status=$?

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ] && [ "$status" -ne 130 ]; then
    echo "FAIL: the game exited with status $status"
    tail -n 15 /tmp/forge-resize-out.log | sed 's/^/  /'
    exit 1
fi

if [ ! -f "$CAPTURE" ]; then
    echo "FAIL: no frame was captured - the game did not reach the capture"
    tail -n 15 /tmp/forge-resize-out.log | sed 's/^/  /'
    exit 1
fi

# The capture is a binary PPM: the header names the size, the pixels
# follow. The size is the swapchain's, and the pixels say whether the
# frame was drawn into all of it.
python3 - "$CAPTURE" "$OLD_WIDTH" "$OLD_HEIGHT" "$NEW_WIDTH" "$NEW_HEIGHT" <<'PY'
import sys

path, old_w, old_h, new_w, new_h = sys.argv[1], *map(int, sys.argv[2:6])

with open(path, 'rb') as handle:
    data = handle.read()

# P6\n<width> <height>\n255\n
fields = []
index = 0
while len(fields) < 4 and index < len(data):
    while index < len(data) and data[index:index+1].isspace():
        index += 1
    if data[index:index+1] == b'#':
        while index < len(data) and data[index:index+1] != b'\n':
            index += 1
        continue
    start = index
    while index < len(data) and not data[index:index+1].isspace():
        index += 1
    fields.append(data[start:index])
index += 1  # the single whitespace after the header

if fields[0] != b'P6':
    print(f"FAIL: {path} is not a binary PPM ({fields[0]!r})")
    sys.exit(1)

width, height = int(fields[1]), int(fields[2])
print(f"captured frame: {width}x{height} (the window was resized to {new_w}x{new_h})")

if width != new_w or height != new_h:
    print(f"FAIL: the frame is {width}x{height}, not {new_w}x{new_h} - "
          "the renderer kept the size the window started at")
    sys.exit(1)

pixels = data[index:]
stride = width * 3

# The part of the image that is new: the columns to the right of the old
# width, and the rows below the old height. A renderer that kept the old
# size leaves them black - the swapchain image is smaller than the window
# and the rest of the window is never written.
def region_is_drawn(x0, x1, y0, y1):
    brightest = 0
    for y in range(y0, y1):
        row = pixels[y*stride:(y+1)*stride]
        for x in range(x0, x1):
            brightest = max(brightest, row[x*3], row[x*3+1], row[x*3+2])
    return brightest

right = region_is_drawn(old_w, width, 0, height)
bottom = region_is_drawn(0, old_w, old_h, height)

if right <= 8 and bottom <= 8:
    print(f"FAIL: the new part of the frame is black (right={right}, bottom={bottom}) - "
          "the scene was not drawn into the resized image")
    sys.exit(1)

print(f"OK: the frame follows the window (the new part is drawn: right={right}, bottom={bottom})")
PY
status=$?

if [ "$status" -ne 0 ]; then
    echo "--- last lines of the game's log ---"
    tail -n 20 /tmp/forge-resize-out.log | sed 's/^/  /'
fi
exit "$status"
