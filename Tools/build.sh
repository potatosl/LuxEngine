#!/usr/bin/env bash
# LuxEngine Linux / macOS Build Helper Script
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
CONFIG="${1:-Debug}"

echo "========================================"
echo "  LuxEngine Cross-Platform Build System"
echo "  Configuration: $CONFIG"
echo "  OS: $(uname -s) ($(uname -m))"
echo "========================================"

BUILD_DIR="$ROOT_DIR/build/unix-${CONFIG,,}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G "Ninja" -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR"

echo ""
echo "[SUCCESS] LuxEngine built successfully!"
echo "Run samples: $BUILD_DIR/bin/SampleBasic2D, SamplePhysics2D, SampleInputBindings, SampleForestSurvivor"
