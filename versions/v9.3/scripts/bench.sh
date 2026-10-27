#!/usr/bin/env bash
# 9.3 vs frozen 9.1/8.0/7.0, offline referee only.
# ./scripts/bench.sh --a 9.3 --b 9.1 --field 111 --maps 16 --seed 20261027
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build_arena"
if command -v cmake >/dev/null 2>&1; then
  cmake -S "$ROOT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$OUT" --parallel 2 --target rmtc_arena >/dev/null
else
  CXX="${CXX:-c++}"; mkdir -p "$OUT/o9" "$OUT/o8" "$OUT/o7" "$OUT/o91"
  for f in types grid parse bot events battle; do
    "$CXX" -std=c++17 -O2 -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/src/rmtc9/$f.cpp" -o "$OUT/o9/$f.o"
    for version in 7 8 91; do
      "$CXX" -std=c++17 -O2 -I"$ROOT/baseline_v$version/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/baseline_v$version/src/rmtc$version/$f.cpp" -o "$OUT/o$version/$f.o"
    done
  done
  "$CXX" -std=c++17 -O2 -I"$ROOT/src" -I"$ROOT/baseline_v7/src" -I"$ROOT/baseline_v8/src" -I"$ROOT/baseline_v91/src" -I"$ROOT/third_party/nlohmann/include" -I"$ROOT/tools/bench" \
    "$ROOT/tools/bench/bench_main.cpp" "$ROOT/tools/bench/referee.cpp" "$ROOT/tools/bench/v7_adapter.cpp" "$ROOT/tools/bench/v8_adapter.cpp" "$ROOT/tools/bench/v91_adapter.cpp" "$OUT"/o9/*.o "$OUT"/o8/*.o "$OUT"/o7/*.o "$OUT"/o91/*.o -pthread -o "$OUT/rmtc_arena"
fi
exec "$OUT/rmtc_arena" "$@"
