#!/bin/sh
# Build Dvlce Capsule on macOS: Xcode (or its command line tools) + CMake.  Universal binary (arm64 + x86_64).
#   ./build-mac.sh          core + tests + plugin (AU, VST3, Standalone)
#   ./build-mac.sh core     core + tests only (no JUCE)
# Environment (all optional): BUILD_DIR, JUCE_DIR (a local JUCE 8.0.13 checkout), PYTHON.
set -e
cd "$(dirname "$0")"
BUILD_DIR=${BUILD_DIR:-build}
PLUGIN=ON
if [ "$1" = "core" ]; then BUILD_DIR="$BUILD_DIR-core"; PLUGIN=OFF; fi
JUCEARG=""
if [ -n "$JUCE_DIR" ]; then JUCEARG="-DFETCHCONTENT_SOURCE_DIR_JUCE=$JUCE_DIR"; fi
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DDVLCE_CAPSULE_BUILD_PLUGIN=$PLUGIN $JUCEARG
cmake --build "$BUILD_DIR" -j 4
# C++ core vs the Python prototype (needs numpy and scipy; skipped when they are missing).
PYTHON=${PYTHON:-python3}
if "$PYTHON" -I core/tests/make_refs.py "$BUILD_DIR/capsule_refs.bin"; then
    "$BUILD_DIR/dvlce_capsule_core_tests" "$BUILD_DIR/capsule_refs.bin"
else
    echo "python with numpy + scipy not found: running the C++ tests without the prototype comparison"
    "$BUILD_DIR/dvlce_capsule_core_tests"
fi
