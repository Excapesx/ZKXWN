#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${RMTC_TEST_BUILD:-$ROOT/build/tests}"
cmake -S "$ROOT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release
cmake --build "$OUT" -j "${RMTC_BUILD_JOBS:-4}"
ctest --test-dir "$OUT" --output-on-failure
