#!/usr/bin/env bash
# 8 groups: 6 inherited groups plus 9.0 regressions and real-node callback tests.
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build_core"
if command -v cmake >/dev/null 2>&1; then
  cmake -S "$ROOT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$OUT" -j2
  exec ctest --test-dir "$OUT" --output-on-failure
fi
CXX="${CXX:-c++}"
mkdir -p "$OUT"
CORE=(); for f in types grid parse bot events battle; do CORE+=("$ROOT/src/rmtc9/$f.cpp"); done
F=(-std=c++17 -O2 -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include")
"$CXX" "${F[@]}" "$ROOT/tests/core_tests.cpp" "${CORE[@]}" -o "$OUT/core_tests"
"$CXX" "${F[@]}" -I"$ROOT/ros2_ws/src/rmtc_bot9/include" "$ROOT/tests/llm_worker_tests.cpp" "$ROOT/ros2_ws/src/rmtc_bot9/src/llm.cpp" -pthread -o "$OUT/llm_worker_tests"
for t in v4_tests v6_tests v8_tests; do
  "$CXX" "${F[@]}" -I"$ROOT/tools/bench" "$ROOT/tests/$t.cpp" "$ROOT/tools/bench/referee.cpp" "${CORE[@]}" -o "$OUT/$t"
done
for t in v7_tests v9_tests; do
  "$CXX" "${F[@]}" "$ROOT/tests/$t.cpp" "${CORE[@]}" -o "$OUT/$t"
done
"$CXX" "${F[@]}" -I"$ROOT/tools/node_sim/stub" -I"$ROOT/ros2_ws/src/rmtc_bot9/include" \
 "$ROOT/tests/node_scope_tests.cpp" "$ROOT/ros2_ws/src/rmtc_bot9/src/node.cpp" "$ROOT/ros2_ws/src/rmtc_bot9/src/llm.cpp" "${CORE[@]}" -pthread -o "$OUT/node_scope_tests"
fail=0
for t in core_tests llm_worker_tests v4_tests v6_tests v7_tests v8_tests v9_tests node_scope_tests; do
  echo "== $t"
  if ! "$OUT/$t"; then fail=1; fi
done
exit "$fail"
