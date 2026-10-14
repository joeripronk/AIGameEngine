#!/bin/bash
# Deploy script for Forge Engine runtime distribution
# Usage: ./tools/deploy.sh [build_type] [target_platform]
#   build_type: Debug, Release, Dist (default: Release)
#   target_platform: linux, windows, macos (default: current platform)

set -euo pipefail

BUILD_TYPE="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
TARGET_PLATFORM="${2:-$(uname -s | tr '[:upper:]' '[:lower:]')}"

# Determine platform
if [[ "$TARGET_PLATFORM" == *"linux"* ]]; then
    PLATFORM="linux"
    EXT=""
    RUNTIME="ForgeRuntime"
elif [[ "$TARGET_PLATFORM" == *"windows"* ]]; then
    PLATFORM="windows"
    EXT=".exe"
    RUNTIME="ForgeRuntime.exe"
elif [[ "$TARGET_PLATFORM" == *"darwin"* || "$TARGET_PLATFORM" == *"macos"* ]]; then
    PLATFORM="macos"
    EXT=""
    RUNTIME="ForgeRuntime"
else
    echo "Error: Unknown platform '$TARGET_PLATFORM'"
    exit 1
fi

echo "=== Forge Engine Deploy ==="
echo "Platform: $PLATFORM"
echo "Build type: $BUILD_TYPE"

# Check build exists
if [ ! -f "$BUILD_DIR/bin/$RUNTIME$EXT" ]; then
    echo "Error: Build not found at $BUILD_DIR/bin/$RUNTIME$EXT"
    echo "Run 'cmake --build build --config $BUILD_TYPE' first"
    exit 1
fi

# Create deployment directory
DEPLOY_DIR="$BUILD_DIR/deploy/$PLATFORM-$BUILD_TYPE"
mkdir -p "$DEPLOY_DIR"

echo "Deploying to: $DEPLOY_DIR"

# Copy runtime binary
cp "$BUILD_DIR/bin/$RUNTIME$EXT" "$DEPLOY_DIR/"

# Copy resources
if [ -d "$ROOT_DIR/resources" ]; then
    cp -r "$ROOT_DIR/resources" "$DEPLOY_DIR/"
fi

# Copy runtime config (if exists)
if [ -f "$ROOT_DIR/runtime/config.json" ]; then
    cp "$ROOT_DIR/runtime/config.json" "$DEPLOY_DIR/"
fi

# Platform-specific: copy DLLs for Windows
if [[ "$PLATFORM" == "windows" ]]; then
    # Copy required DLLs from build
    for dll in "$BUILD_DIR/bin/"*.dll; do
        [ -f "$dll" ] && cp "$dll" "$DEPLOY_DIR/"
    done
    # Copy Lua DLL if exists
    if [ -f "$BUILD_DIR/bin/lua54.dll" ]; then
        cp "$BUILD_DIR/bin/lua54.dll" "$DEPLOY_DIR/"
    fi
fi

# Create README
cat > "$DEPLOY_DIR/README.txt" << 'EOF'
Forge Engine Runtime
====================

This is a distribution build of the Forge Engine runtime.

To run:
  Linux:   ./ForgeRuntime
  macOS:   ./ForgeRuntime
  Windows: ForgeRuntime.exe

The runtime loads scenes from the 'resources/' directory.
Set the FG_ENV_MAP environment variable to use a custom environment map for IBL.

Example:
  FG_ENV_MAP=/path/to/hdri.exr ./ForgeRuntime

EOF

# Create archive
ARCHIVE_NAME="ForgeRuntime-$PLATFORM-$BUILD_TYPE-$(date +%Y%m%d).tar.gz"
cd "$BUILD_DIR/deploy"
tar -czf "$ARCHIVE_NAME" "$PLATFORM-$BUILD_TYPE"

echo ""
echo "=== Deploy Complete ==="
echo "Archive: $BUILD_DIR/deploy/$ARCHIVE_NAME"
echo "Directory: $DEPLOY_DIR"
