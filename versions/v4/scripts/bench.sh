#!/usr/bin/env bash
# 本地对抗基准：重新编译 2.0 与 1.0 的真实决策代码，跑同图同规则比赛
# 用法：LEGACY=/path/to/ZKXWN ./scripts/bench.sh --stage 2 --matches 300 --robots 4
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
LEGACY="${LEGACY:-$ROOT/../..}"
if [[ ! -f "$LEGACY/decision/src/engine.cpp" ]]; then
  echo "找不到 1.0 源码：$LEGACY（用 LEGACY=... 指定）" >&2
  exit 1
fi
mkdir -p "$ROOT/build_bench"
c++ -std=c++17 -O2 -pthread \
  -I"$ROOT/src" -I"$ROOT/tools/bench" \
  -I"$LEGACY/rmtc_ws/src/rmtc_bot/include" -I"$LEGACY/decision/include" \
  -I"$ROOT/third_party/nlohmann/include" \
  "$ROOT/src/rmtc2/events.cpp" "$ROOT/src/rmtc2/types.cpp" "$ROOT/src/rmtc2/grid.cpp" "$ROOT/src/rmtc2/parse.cpp" "$ROOT/src/rmtc2/bot.cpp" \
  "$ROOT/tools/bench/referee.cpp" "$ROOT/tools/bench/legacy_adapter.cpp" "$ROOT/tools/bench/bench_main.cpp" \
  "$LEGACY/decision/src/engine.cpp" \
  "$LEGACY/rmtc_ws/src/rmtc_bot/src/memory.cpp" "$LEGACY/rmtc_ws/src/rmtc_bot/src/planner.cpp" \
  "$LEGACY/rmtc_ws/src/rmtc_bot/src/config.cpp" "$LEGACY/rmtc_ws/src/rmtc_bot/src/policy.cpp" \
  "$LEGACY/rmtc_ws/src/rmtc_bot/src/perception.cpp" "$LEGACY/rmtc_ws/src/rmtc_bot/src/combat.cpp" \
  "$LEGACY/rmtc_ws/src/rmtc_bot/src/policies/stage1_policy.cpp" \
  "$LEGACY/rmtc_ws/src/rmtc_bot/src/policies/stage2_policy.cpp" \
  -o "$ROOT/build_bench/bench"
"$ROOT/build_bench/bench" "$@"
