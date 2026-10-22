#!/usr/bin/env bash
set -eo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source /opt/ros/humble/setup.bash
cd "$ROOT/rmtc_ws"

colcon test --packages-select rmtc_bot --event-handlers console_direct+
colcon test-result --verbose
