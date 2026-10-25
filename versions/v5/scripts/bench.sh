#!/usr/bin/env bash
# 本地对抗基准：5.0 (rmtc3) vs 4.0 的真实决策代码 (rmtc2)
# 用法：LEGACY=/path/to/校内4.0 ./scripts/bench.sh --stage 2 --matches 200 --robots 4
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
LEGACY="${LEGACY:-$ROOT/../校内4.0}"
if [[ ! -f "$LEGACY/src/rmtc2/bot.cpp" ]]; then
  echo "找不到 4.0 源码：$LEGACY（用 LEGACY=... 指定）" >&2
  exit 1
fi
mkdir -p "$ROOT/build_bench"
c++ -std=c++17 -O2 -pthread \
  -I"$ROOT/src" -I"$ROOT/tools/bench" -I"$ROOT/third_party/nlohmann/include" \
  -I"$LEGACY/src" -I"$LEGACY/third_party/nlohmann/include" \
  "$ROOT/src/rmtc3/types.cpp" "$ROOT/src/rmtc3/grid.cpp" "$ROOT/src/rmtc3/parse.cpp" \
  "$ROOT/src/rmtc3/bot.cpp" "$ROOT/src/rmtc3/events.cpp" \
  "$ROOT/tools/bench/referee.cpp" "$ROOT/tools/bench/opponent_adapter.cpp" "$ROOT/tools/bench/bench_main.cpp" \
  "$LEGACY/src/rmtc2/types.cpp" "$LEGACY/src/rmtc2/grid.cpp" "$LEGACY/src/rmtc2/parse.cpp" \
  "$LEGACY/src/rmtc2/bot.cpp" "$LEGACY/src/rmtc2/events.cpp" \
  -o "$ROOT/build_bench/bench"
"$ROOT/build_bench/bench" "$@"
