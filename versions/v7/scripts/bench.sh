#!/usr/bin/env bash
# Build and run the 7.0 vs 6.0 arena.
# Put 校内6.0 next to 校内7.0, or set V6_ROOT.
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
V6="${V6_ROOT:-$ROOT/../校内6.0}"
cmake -S "$ROOT" -B "$ROOT/build_arena" -DCMAKE_BUILD_TYPE=Release \
  -DRMTC_BUILD_BENCH=ON -DRMTC_V6_ROOT="$V6"
cmake --build "$ROOT/build_arena" --parallel 2 --target rmtc_arena
exec "$ROOT/build_arena/rmtc_arena" "$@"
