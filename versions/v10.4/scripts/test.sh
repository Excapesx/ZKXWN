#!/usr/bin/env bash
# C++ policy/node regressions plus read-only bag-audit regression.
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build_core"
if command -v cmake >/dev/null 2>&1; then
  cmake -S "$ROOT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release -DRMTC_ABLATION_LEVEL=2 -DRMTC_ROUND2_LEVEL=3 -DRMTC_ENABLE_OPPORTUNITY_COST=ON -DRMTC_PHASE_ALPHA_PERCENT=100 -DRMTC_PHASE_RESTORE_MASK=0
  cmake --build "$OUT" -j2
  exec ctest --test-dir "$OUT" --output-on-failure
fi
CXX="${CXX:-c++}"
mkdir -p "$OUT"
CORE=(); for f in types grid parse bot events battle; do CORE+=("$ROOT/src/rmtc9/$f.cpp"); done
F=(-std=c++17 -O2 -DRMTC_ENABLE_PHASE_PROBES=1 -DRMTC_ROUND2_LEVEL=3 -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include")
"$CXX" "${F[@]}" "$ROOT/tests/core_tests.cpp" "${CORE[@]}" -o "$OUT/core_tests"
"$CXX" "${F[@]}" -I"$ROOT/ros2_ws/src/rmtc_bot9/include" "$ROOT/tests/llm_worker_tests.cpp" "$ROOT/ros2_ws/src/rmtc_bot9/src/llm.cpp" -pthread -o "$OUT/llm_worker_tests"
for t in v4_tests v6_tests v8_tests v92_tests v93_tests; do
  "$CXX" "${F[@]}" -I"$ROOT/tools/bench" "$ROOT/tests/$t.cpp" "$ROOT/tools/bench/referee.cpp" "${CORE[@]}" -o "$OUT/$t"
done
for t in v7_tests v9_tests v10_tests v102_tests v103_tests v104_tests; do
  "$CXX" "${F[@]}" "$ROOT/tests/$t.cpp" "${CORE[@]}" -o "$OUT/$t"
done
BASELINE101=(); for f in types grid parse bot events battle; do BASELINE101+=("$ROOT/baseline_v101/src/rmtc101/$f.cpp"); done
"$CXX" -std=c++17 -O2 -I"$ROOT/baseline_v101/src" -I"$ROOT/third_party/nlohmann/include" "$ROOT/tests/v101_tests.cpp" "${BASELINE101[@]}" -o "$OUT/v101_tests"
"$CXX" "${F[@]}" -I"$ROOT/tools/node_sim/stub" -I"$ROOT/ros2_ws/src/rmtc_bot9/include" \
 "$ROOT/tests/node_scope_tests.cpp" "$ROOT/ros2_ws/src/rmtc_bot9/src/node.cpp" "$ROOT/ros2_ws/src/rmtc_bot9/src/llm.cpp" "${CORE[@]}" -pthread -o "$OUT/node_scope_tests"
"$CXX" -std=c++17 -O2 -DRMTC_ENABLE_PHASE_PROBES=0 -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include" "$ROOT/tests/v104_production_scope.cpp" "${CORE[@]}" -o "$OUT/v104_production_scope"
fail=0
for t in core_tests llm_worker_tests v4_tests v6_tests v7_tests v8_tests v9_tests v92_tests v93_tests v10_tests v101_tests v102_tests v103_tests v104_tests node_scope_tests v104_production_scope; do
  echo "== $t"
  if ! "$OUT/$t"; then fail=1; fi
done
if ! python3 "$ROOT/tests/test_audit_bag.py"; then fail=1; fi
exit "$fail"
