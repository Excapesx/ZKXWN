#!/usr/bin/env bash
# 编译并运行测评工具 rmtc_arena（8.0 对冻结的 7.0；不需要 ROS，也不需要 7.0 文件夹）。
# 例：./scripts/bench.sh --a 8 --b 7 --field 777 --maps 100 --seed 83000
#     ./scripts/bench.sh --stage 1 --maps 100 --seed 71000 --width 20 --height 20
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build_arena"
if command -v cmake >/dev/null 2>&1; then
  cmake -S "$ROOT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$OUT" --parallel 2 --target rmtc_arena >/dev/null
else
  CXX="${CXX:-c++}"; mkdir -p "$OUT/o8" "$OUT/o7"
  for f in types grid parse bot events battle; do
    "$CXX" -std=c++17 -O2 -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/src/rmtc8/$f.cpp" -o "$OUT/o8/$f.o"
    "$CXX" -std=c++17 -O2 -I"$ROOT/baseline_v7/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/baseline_v7/src/rmtc7/$f.cpp" -o "$OUT/o7/$f.o"
  done
  "$CXX" -std=c++17 -O2 -I"$ROOT/src" -I"$ROOT/baseline_v7/src" -I"$ROOT/third_party/nlohmann/include" -I"$ROOT/tools/bench" \
    "$ROOT/tools/bench/bench_main.cpp" "$ROOT/tools/bench/referee.cpp" "$ROOT/tools/bench/v7_adapter.cpp" "$OUT"/o8/*.o "$OUT"/o7/*.o -lpthread -o "$OUT/rmtc_arena"
fi
exec "$OUT/rmtc_arena" "$@"
