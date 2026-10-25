#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/offline"
cmake -S "$ROOT/rmtc_ws/src/rmtc_bot" -B "$BUILD" \
  -DRMTC_WITH_ROS=OFF -DRMTC_WITH_CURL=OFF -DBUILD_TESTING=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "$BUILD" --output-on-failure
