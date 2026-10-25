#!/usr/bin/env bash
set -eo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source /opt/ros/humble/setup.bash
cd "$ROOT/rmtc_ws"

colcon build --symlink-install --packages-select rmtc_bot \
  --cmake-args -DCMAKE_BUILD_TYPE=Release

echo
echo "Build done. Source the workspace with:"
echo "  source $ROOT/rmtc_ws/install/setup.bash"
