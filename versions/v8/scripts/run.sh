#!/usr/bin/env bash
# 启动选手节点：./scripts/run.sh <席位A-D> <阶段1|2>
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SEAT="${1:-A}"
STAGE="${2:-1}"
if [[ ! -f "$ROOT/ros2_ws/install/setup.bash" ]]; then
  echo "尚未编译，先执行 $ROOT/scripts/build.sh" >&2
  exit 1
fi
source /opt/ros/humble/setup.bash
source "$ROOT/ros2_ws/install/setup.bash"
export RMTC_DECISION_CONFIG="${RMTC_DECISION_CONFIG:-$ROOT/ros2_ws/src/rmtc_bot8/config/decision_config.json}"
exec ros2 run rmtc_bot8 rmtc_bot8 --ros-args -p team:="$SEAT" -p stage:="$STAGE"
