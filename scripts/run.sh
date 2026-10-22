#!/usr/bin/env bash
set -eo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SEAT="${1:-A}"
STAGE="${2:-1}"
LLM="${RMTC_LLM_ENABLED:-0}"
if [[ "$LLM" == "1" || "$LLM" == "true" ]]; then
  LLM_BOOL=true
else
  LLM_BOOL=false
fi

if [[ ! -f "$ROOT/rmtc_ws/install/setup.bash" ]]; then
  echo "Workspace is not built yet. Run: $ROOT/scripts/build.sh" >&2
  exit 1
fi

source /opt/ros/humble/setup.bash
source "$ROOT/rmtc_ws/install/setup.bash"

exec ros2 run rmtc_bot rmtc_bot --ros-args \
  -p team:="$SEAT" \
  -p stage:="$STAGE" \
  -p llm_enabled:="$LLM_BOOL"
