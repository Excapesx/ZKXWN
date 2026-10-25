#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
V4="${V4_ROOT:-$ROOT/../校内4.0}"
V5="${V5_ROOT:-$ROOT/../校内5.0}"
cmake -S "$ROOT" -B "$ROOT/build_arena" -DCMAKE_BUILD_TYPE=Release \
  -DRMTC_BUILD_BENCH=ON -DRMTC_V4_ROOT="$V4" -DRMTC_V5_ROOT="$V5"
cmake --build "$ROOT/build_arena" --parallel 2 --target rmtc_arena
exec "$ROOT/build_arena/rmtc_arena" "$@"
