#!/usr/bin/env bash
# 本地核心检查：编译并运行六组测试（不需要 ROS）。
# 有 cmake 就用 CMake + CTest；没有 cmake（例如默认的 macOS）就直接用 c++ 编译器。
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if command -v cmake >/dev/null 2>&1; then
  cmake -S "$ROOT" -B "$ROOT/build_core" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build_core" -j2
  exec ctest --test-dir "$ROOT/build_core" --output-on-failure
fi
echo "未找到 cmake，改用 ${CXX:-c++} 直接编译（结果与 CTest 相同的六组测试）"
CXX="${CXX:-c++}"
OUT="$ROOT/build_core"
mkdir -p "$OUT"
CORE="$ROOT/src/rmtc8/types.cpp $ROOT/src/rmtc8/grid.cpp $ROOT/src/rmtc8/parse.cpp $ROOT/src/rmtc8/bot.cpp $ROOT/src/rmtc8/events.cpp $ROOT/src/rmtc8/battle.cpp"
F="-std=c++17 -O2 -I$ROOT/src -I$ROOT/third_party/nlohmann/include"
"$CXX" $F "$ROOT/tests/core_tests.cpp" $CORE -o "$OUT/core_tests"
"$CXX" $F -I"$ROOT/ros2_ws/src/rmtc_bot8/include" "$ROOT/tests/llm_worker_tests.cpp" "$ROOT/ros2_ws/src/rmtc_bot8/src/llm.cpp" -lpthread -o "$OUT/llm_worker_tests"
for t in v4_tests v6_tests v8_tests; do
  "$CXX" $F -I"$ROOT/tools/bench" "$ROOT/tests/$t.cpp" "$ROOT/tools/bench/referee.cpp" $CORE -o "$OUT/$t"
done
"$CXX" $F "$ROOT/tests/v7_tests.cpp" $CORE -o "$OUT/v7_tests"
fail=0
for t in core_tests llm_worker_tests v4_tests v6_tests v7_tests v8_tests; do
  echo "== $t"
  if ! "$OUT/$t" | tail -3; then fail=1; fi
done
exit $fail
