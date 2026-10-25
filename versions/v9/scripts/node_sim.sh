#!/usr/bin/env bash
# 不装 ROS 跑一整场“真实节点”：node.cpp + llm.cpp + libcurl → 本地假网关；其余席位为冻结的 7.0。
# 需要：C++17 编译器、libcurl 开发包、python3。macOS 与 Ubuntu 都可以。
# 用法：./scripts/node_sim.sh [种子] [局数] [每Tick毫秒] [standard|practice|none]
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build_node_sim"
CXX="${CXX:-c++}"
PORT="${MOCK_PORT:-18080}"
mkdir -p "$OUT/o9" "$OUT/o7"
F=(-std=c++17 -O2)
for f in types grid parse bot events battle; do
  "$CXX" "${F[@]}" -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/src/rmtc9/$f.cpp" -o "$OUT/o9/$f.o"
  "$CXX" "${F[@]}" -I"$ROOT/baseline_v7/src" -I"$ROOT/third_party/nlohmann/include" -c "$ROOT/baseline_v7/src/rmtc7/$f.cpp" -o "$OUT/o7/$f.o"
done
INC=(-I"$ROOT/tools/node_sim/stub" -I"$ROOT/src" -I"$ROOT/third_party/nlohmann/include" -I"$ROOT/ros2_ws/src/rmtc_bot9/include" -I"$ROOT/tools/bench")
if [[ "${NODE:-9}" == "7" ]]; then
  # 对照：编译 7.0 原版节点（V7_ROOT 默认 ../校内7.0），决策核心用 baseline_v7（与 7.0 源码逐字相同）
  V7="${V7_ROOT:-$ROOT/../校内7.0}"
  NINC=(-I"$ROOT/tools/node_sim/stub" -I"$ROOT/baseline_v7/src" -I"$ROOT/third_party/nlohmann/include" -I"$V7/ros2_ws/src/rmtc_bot7/include")
  "$CXX" "${F[@]}" "${NINC[@]}" -c "$V7/ros2_ws/src/rmtc_bot7/src/node.cpp" -o "$OUT/node.o"
  "$CXX" "${F[@]}" "${NINC[@]}" -c "$V7/ros2_ws/src/rmtc_bot7/src/llm.cpp" -o "$OUT/llm.o"
  "$CXX" "${F[@]}" "${NINC[@]}" -c "$V7/ros2_ws/src/rmtc_bot7/src/curl_transport.cpp" -o "$OUT/curl.o"
  "$CXX" "${F[@]}" "${INC[@]}" -I"$V7/ros2_ws/src/rmtc_bot7/include" -DNODE_SIM_V7 -c "$ROOT/tools/node_sim/node_sim_main.cpp" -o "$OUT/main.o"
  rm -f "$OUT/o9/bot.o"  # 7.0 节点不链接 9.0 决策核心的 Bot（参考裁判仍用 rmtc9 类型）
else
  "$CXX" "${F[@]}" "${INC[@]}" -c "$ROOT/ros2_ws/src/rmtc_bot9/src/node.cpp" -o "$OUT/node.o"
  "$CXX" "${F[@]}" "${INC[@]}" -c "$ROOT/ros2_ws/src/rmtc_bot9/src/llm.cpp" -o "$OUT/llm.o"
  "$CXX" "${F[@]}" "${INC[@]}" -c "$ROOT/ros2_ws/src/rmtc_bot9/src/curl_transport.cpp" -o "$OUT/curl.o"
  "$CXX" "${F[@]}" "${INC[@]}" -c "$ROOT/tools/node_sim/node_sim_main.cpp" -o "$OUT/main.o"
fi
"$CXX" "${F[@]}" "${INC[@]}" -c "$ROOT/tools/bench/referee.cpp" -o "$OUT/referee.o"
"$CXX" "${F[@]}" -I"$ROOT/src" -I"$ROOT/baseline_v7/src" -I"$ROOT/third_party/nlohmann/include" -I"$ROOT/tools/bench" \
  -c "$ROOT/tools/bench/v7_adapter.cpp" -o "$OUT/v7_adapter.o"
"$CXX" "$OUT"/*.o "$OUT"/o9/*.o "$OUT"/o7/*.o -lcurl -lpthread -o "$OUT/node_sim"

cd "$OUT"
rm -f node_sim_calls.jsonl
MOCK_PORT="$PORT" python3 "$ROOT/tools/node_sim/mock_gateway.py" &
GW=$!
trap 'kill $GW 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do curl -s "http://127.0.0.1:$PORT/healthz" >/dev/null 2>&1 && break; sleep 0.1; done
RMTC_AI_GATEWAY="http://127.0.0.1:$PORT" RMTC_TEAM_TOKEN="sim-token" ./node_sim "${1:-83000}" "${2:-3}" "${3:-20}" "${4:-standard}"
python3 - <<'PY'
import json
rows=[json.loads(l) for l in open("node_sim_calls.jsonl")] if __import__("os").path.exists("node_sim_calls.jsonl") else []
ok=[r for r in rows if r["status"]==200]
print(json.dumps({"gateway_calls":len(rows),"gateway_2xx":len(ok),"first_2xx_tick":min((r["tick"] for r in ok if r["tick"] is not None),default=None)},ensure_ascii=False))
PY
