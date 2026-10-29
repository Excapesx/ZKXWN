#!/usr/bin/env bash
set -eo pipefail
source /opt/ros/humble/setup.bash
for name in c++ cmake colcon ros2; do command -v "$name"; done
ros2 pkg prefix rclcpp
ros2 pkg prefix std_msgs
pkg-config --modversion libcurl
echo "ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-unset}"
echo "ROS_LOCALHOST_ONLY=${ROS_LOCALHOST_ONLY:-unset}"
[[ -n "${RMTC_TEAM_TOKEN:-}" ]] && echo "Token configured (value hidden)" || echo "No token: local-only practice"
