#!/bin/bash
# Build & run the JUCE-free engine self-test (fold + NAM weight-order + render +
# morph). No JUCE, no plugin build — just the DSP core against NeuralAmpModelerCore.
#
#   NAM_DIR=/path/to/NeuralAmpModelerCore ./tools/run_selftest.sh [bundle.json]
#
# If NAM_DIR is unset it is cloned into build/_nam. Uses system Eigen if present,
# otherwise NAM's vendored Eigen (needs the eigen submodule checked out).
set -euo pipefail
cd "$(dirname "$0")/.."

# Defaults to the shipped model package. A missing bundle is a hard error: this
# used to silently generate a random placeholder instead, which let CI pass while
# exercising none of the real assets.
BUNDLE="${1:-projects/AAOM/assets/nam_a2_256/nam_a2_256_bundle.json}"
if [ ! -f "$BUNDLE" ]; then
    echo "error: bundle not found: $BUNDLE" >&2
    echo "       pass a bundle path explicitly, or generate a placeholder with:" >&2
    echo "         python3 tools/make_bundle.py '$BUNDLE' --embed 256" >&2
    exit 1
fi

if [ -z "${NAM_DIR:-}" ]; then
    NAM_DIR="build/_nam"
    if [ ! -d "$NAM_DIR/NAM" ]; then
        git clone --recurse-submodules -j4 \
            https://github.com/sdatkinson/NeuralAmpModelerCore.git "$NAM_DIR"
    fi
fi

if [ -d /usr/include/eigen3 ]; then
    EIGEN=/usr/include/eigen3
else
    EIGEN="$NAM_DIR/Dependencies/eigen"
fi

OUT="build/aaom_selftest"
mkdir -p build

# Compile all NAM core sources except the A2 fast path (generic WaveNet only).
NAM_SRCS=$(ls "$NAM_DIR"/NAM/*.cpp "$NAM_DIR"/NAM/wavenet/*.cpp | grep -v 'a2_fast\.cpp')

# _USE_MATH_DEFINES so M_PI resolves under MinGW/MSYS too, not just glibc.
g++ -std=c++20 -O2 -DNAM_SAMPLE_FLOAT -D_USE_MATH_DEFINES \
    -I projects/AAOM/dsp -I "$NAM_DIR" -I "$NAM_DIR/Dependencies/nlohmann" -I "$EIGEN" \
    tools/selftest.cpp projects/AAOM/dsp/MorphModel.cpp projects/AAOM/dsp/MorphEngine.cpp projects/AAOM/dsp/FftConvolver.cpp \
    $NAM_SRCS \
    -o "$OUT"

"$OUT" "$BUNDLE"
