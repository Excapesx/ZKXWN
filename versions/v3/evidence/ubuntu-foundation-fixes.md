# Ubuntu 底层修复回归（2026-10-23）

基于仓库提交 `02a1165` 的本地修复。修改范围为 v3 底层与测试，没有调整战斗或 LLM 决策权重。

## 修复

- 零视野时，以已知空地相邻的未知格作为探测目标；不将未知格写成已知空地。正常视野沿用原有探索策略。原先只有自身为已知空地，又跳过距离为零的目标，导致一直等待。
- ROS 接入在 `tick >= max_ticks` 时不发送动作，也不更新场次/动作缓存。最后结算帧不再触发多余的 wait。
- 显式提供的 `vision_radius` 必须为 0–4 的整数；越界、错误类型拒收。缺省值 4 的兼容行为保留。

## 实测

- Ubuntu / ROS 2 Humble Release 构建成功。
- 核心检查 162 项、模拟 LLM 线程检查 7 项全部通过。新增零视野双阶段空地图遍历、单格边界和视野值合法性检查。
- ROS 收发回归通过：动作上下文、同 Tick 重发、回执输入、非法视野拒收、旧 Tick 丢弃、换场次、死亡 wait、结束帧不发送。
- 官方 v1.1 练习裁判第一阶段：9 张地图，每张 100 Tick、100ms/Tick，同一个机器人进程连续换场次。9/9 严格检查通过；全部无非法动作、缺失动作或缺失回执；最后发送和实际结算均为 Tick 99。

| 地图 | 资源值 | 检查 |
|---|---:|---|
| visible_detour | 20 | 通过 |
| unseen_far_corner | 20 | 通过 |
| sealed_high_resource | 20 | 通过 |
| rectangular_bottom_right | 30 | 通过 |
| narrow_view | 20 | 通过 |
| long_corridor | 20 | 通过 |
| zero_view_probe | 20 | 通过 |
| random_03 | 160 | 通过 |
| random_31 | 0 | 通过 |

零视野用例从 0 提升至 20；其他八张地图资源值与修复前相同。random_31 的最低门槛为 0，得 0 仅表示通过当前门槛，不表示采集策略理想。

零视野遍历测试覆盖无障碍地图；这不构成盲区有障碍时探索完备性的证明。未验收官方第二阶段整场、真实模型或 AI 资格。

## 复现（仓库根目录）

```bash
bash versions/v3/scripts/test.sh
bash versions/v3/scripts/build.sh
source /opt/ros/humble/setup.bash
ROS_DOMAIN_ID=194 ROS_LOCALHOST_ONLY=1 RMW_IMPLEMENTATION=rmw_fastrtps_cpp python3 versions/v3/scripts/test_ros_io.py
python3 scripts/test_multimap_referee.py \
  --bot-executable versions/v3/ros2_ws/install/rmtc_bot2/lib/rmtc_bot2/rmtc_bot2 \
  --practice-dir build/sync-validation/practice/package/rmtc_practice \
  --report-dir build/sync-validation/fix-referee
```

`--practice-dir` 指向已解压的官方练习包，其他机器需改为实际路径。原始复测日志和完整裁判记录在本机 `build/sync-validation/fix-*`；修复前记录仍保留，未覆盖。
