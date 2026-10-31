#!/usr/bin/env bash
# 编译 ROS 2 功能包（Ubuntu 22.04 / ROS 2 Humble）
set -eo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source /opt/ros/humble/setup.bash
cd "$ROOT/ros2_ws"
colcon build --symlink-install --packages-select rmtc_bot9 --cmake-args -DCMAKE_BUILD_TYPE=Release
echo
echo "编译完成。加载环境："
echo "  source $ROOT/ros2_ws/install/setup.bash"
