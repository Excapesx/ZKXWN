#!/usr/bin/env bash
# 同一输入下 7.0 与 9.0 的实际输出对照（不需要 ROS）
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build_defects"; CXX="${CXX:-c++}"; mkdir -p "$OUT/o8" "$OUT/o7"
for f in types grid parse bot events battle; do
  "$CXX" -std=c++17 -O2 -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/src/rmtc9/$f.cpp" -o "$OUT/o8/$f.o"
  "$CXX" -std=c++17 -O2 -I"$ROOT/baseline_v7/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/baseline_v7/src/rmtc7/$f.cpp" -o "$OUT/o7/$f.o"
done
"$CXX" -std=c++17 -O2 -I"$ROOT/src" -I"$ROOT/baseline_v7/src" -I"$ROOT/third_party/nlohmann/include" \
  "$ROOT/tools/defect_cases.cpp" "$OUT"/o8/*.o "$OUT"/o7/*.o -o "$OUT/defect_cases"
exec "$OUT/defect_cases"
