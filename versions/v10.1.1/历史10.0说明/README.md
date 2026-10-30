# 校内10.0

程序版本 **10.0.0**，基于提交 `98cd892` 的9.3。ROS包和程序仍为 `rmtc_bot9`，决策命名空间仍为 `rmtc9`。9.3独立版本保留，本目录可独立构建。

10.0增加资源到达时刻估计、保守的到达时间路线比较、临近结束时的预算重算，以及逐tick动作/回执和资源争夺日志。测试中出现退步的强制采集和过度保守竞价已经撤回；这是可解释性、交付完整性和受条件限制的策略升级，不承诺普遍涨分。

## 主要变化

- **资源竞争。** 用本队已观测地图估算自己与当前可见对手到达资源的步数，区分领先、同tick、晚到和当前不可达。`availability`是启发式估计，既不是实际概率，也不假定对手一定来抢。路线后续节点按累计到达时间估值；仅在预计价值明显更高且首资源有明确到达优势时采用候选路线，其余保留既有规划。
- **开局取舍。** 附近资源存在紧迫竞争、血量足够、当前没有预计来火且没有可补刀目标时，采集路线也能与普通攻击候选比较。致死风险、保护和事件判断优先；没有固定前若干tick禁攻规则。
- **封顶后生存。** 伤害达到500后仍考虑一次死亡缓冲；资源和全部任务已经达标且低血量时，进一步惩罚下一步进入可见火线的选择。攻击先于移动，不能靠移动躲掉本tick攻击。
- **第一阶段尾盘。** 剩18tick以内不复用旧资源路线缓存，重新核算真实BFS距离和剩余预算。强制放弃观察点的试验未采用；当前没有第一阶段稳定涨分证据。
- **联调和交付。** 新增只读bag核验、每tick动作及回执种类日志、资源ETA及候选原因日志、启动路径和二进制摘要。修复干净下载中缺默认配置的问题：默认档位放在已随包分发的 `config/decision_default.json`，可选值仍为balanced/collect/fight。

## 快速运行（Ubuntu）

```bash
cd ~/ZKXWN/versions/v10.0
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

`cd`进入本版目录；`verify_delivery.py`只核对交付清单和Git状态；`test.sh`运行离线回归；`build.sh`加载Humble并构建ROS包；`export`为当前终端设置与裁判相同的通信环境；`run.sh A 2`启动A席位第二阶段。启动前会核对实际ROS可执行文件属于本目录，并打印本地文件摘要；确认日志为 `HERO 10.0.0`。这些摘要便于追溯，但不能单独证明二进制一定由当前源码构建。

需要Ubuntu22.04、ROS2 Humble、C++17、CMake、Python3、colcon与libcurl开发依赖。切换9系列时新开终端并停止原程序，避免相同包名和席位冲突。真实网关由官方提供，模型名留空；令牌不写入仓库或日志。

## 模拟和录制核验

```bash
bash scripts/bench.sh --a 10.0 --b 9.3 --field 333 --maps 16 --seed 850010 --events standard --width 18 --height 18
python3 scripts/compare_recorded.py --build build_core
python3 scripts/audit_bag.py --bag ~/rmtc_demo_20261026/evidence/stage2_92_20261027 --seat A --output ~/rmtc_demo_20261026/evidence/audit10.json
```

离线工具的`9`或`10.0`代表本版，`9.3`代表冻结9.3，`333`是三个9.3对手；seat模式分别在相同地图与席位运行，duel才是同场。模拟事件由理想计划提供，不调用模型。历史感知重放不等于反事实比赛。`audit_bag.py`只读已结束的SQLite录制，检查场次、tick、动作、回执和间隔，不回放、不发送指令；间隔大或回执不一致只记录现象，不武断归因网络。

## 验收状态

详见[修复与验证报告](修复与验证报告.md)、[更新日志](更新日志.md)与[evidence](evidence/README.md)。新回归覆盖到达时间、预算、竞争诊断、场次隔离和同tick动作种类不一致，另含生产节点代码与假ROS/假HTTP测试。

本次未启动Ubuntu虚拟机，因此10.0的真实ROS构建、官方练习整局、统一LLM和v1.2正式裁判验收均待完成。规则PDF为v1.2，随包协议与官方练习包为v1.1；旧9.2/9.3的运行证据不记作10.0实测成绩。继承记录集中在 `历史9.3说明` 和 `历史9.2说明`。
