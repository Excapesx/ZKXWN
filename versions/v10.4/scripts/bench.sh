#!/usr/bin/env bash
# 10.4 vs frozen 10.1/10.0/9.3/9.1/8.0/7.0, offline referee only.
# bash scripts/bench.sh --a 10.4 --b 10.1 --field 778 --maps 16 --seed 430000
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build_arena"
if command -v cmake >/dev/null 2>&1; then
  cmake -S "$ROOT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release -DRMTC_ABLATION_LEVEL=2 -DRMTC_ROUND2_LEVEL=3 -DRMTC_ENABLE_OPPORTUNITY_COST=ON -DRMTC_PHASE_ALPHA_PERCENT=100 -DRMTC_PHASE_RESTORE_MASK=0 >/dev/null
  cmake --build "$OUT" --parallel 2 --target rmtc_arena >/dev/null
else
  CXX="${CXX:-c++}"; mkdir -p "$OUT/o9" "$OUT/o8" "$OUT/o7" "$OUT/o91" "$OUT/o93" "$OUT/o100" "$OUT/o101" "$OUT/o102" "$OUT/o103"
  for f in types grid parse bot events battle; do
    "$CXX" -std=c++17 -O2 -DRMTC_ROUND2_LEVEL=3 -DRMTC_ENABLE_PHASE_PROBES=1 -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/src/rmtc9/$f.cpp" -o "$OUT/o9/$f.o"
    for version in 7 8 91 93 100 101 102 103; do
      "$CXX" -std=c++17 -O2 -I"$ROOT/baseline_v$version/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/baseline_v$version/src/rmtc$version/$f.cpp" -o "$OUT/o$version/$f.o"
    done
  done
  "$CXX" -std=c++17 -O2 -DRMTC_ROUND2_LEVEL=3 -DRMTC_ENABLE_PHASE_PROBES=1 -I"$ROOT/src" -I"$ROOT/baseline_v7/src" -I"$ROOT/baseline_v8/src" -I"$ROOT/baseline_v91/src" -I"$ROOT/baseline_v93/src" -I"$ROOT/baseline_v100/src" -I"$ROOT/baseline_v101/src" -I"$ROOT/baseline_v102/src" -I"$ROOT/baseline_v103/src" -I"$ROOT/third_party/nlohmann/include" -I"$ROOT/tools/bench" \
    "$ROOT/tools/bench/bench_main.cpp" "$ROOT/tools/bench/referee.cpp" "$ROOT/tools/bench/v7_adapter.cpp" "$ROOT/tools/bench/v8_adapter.cpp" "$ROOT/tools/bench/v91_adapter.cpp" "$ROOT/tools/bench/v93_adapter.cpp" "$ROOT/tools/bench/v100_adapter.cpp" "$ROOT/tools/bench/v101_adapter.cpp" "$ROOT/tools/bench/v102_adapter.cpp" "$ROOT/tools/bench/v103_adapter.cpp" "$OUT"/o9/*.o "$OUT"/o8/*.o "$OUT"/o7/*.o "$OUT"/o91/*.o "$OUT"/o93/*.o "$OUT"/o100/*.o "$OUT"/o101/*.o "$OUT"/o102/*.o "$OUT"/o103/*.o -pthread -o "$OUT/rmtc_arena"
fi
exec "$OUT/rmtc_arena" "$@"
