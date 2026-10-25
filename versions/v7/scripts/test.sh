#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build_core" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build_core" -j2
ctest --test-dir "$ROOT/build_core" --output-on-failure
