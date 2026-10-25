# A 负责的 C++ 底层：实现与验证

入口沿用 `BotNode → BotRuntime → 感知解析 → 地图记忆 → Stage1Policy → Planner → 动作发布`。第二阶段继续调用同一个 Planner；决策引擎仅同步 v1.1 的对抗计分门槛，战斗策略和 LLM 扩展仍由队友负责。

## 本次修改

- 静态 BFS：同一 Tick、相同起点、地图宽度和障碍集合下，多个资源/探索目标复用一次搜索结果。目标查询仍需 O(V+B) 构造、比较障碍索引，但不再逐目标重复遍历边；邻格障碍判断由线性查找改为 O(1)。缓存于 `begin_tick` 清空；不同起点或障碍集合也会重建。
- 静态搜索移除原来 200000 次展开限制，按合法地图大小和剩余 Tick 有界搜索。临时墙、事件等待继续使用原有时序 BFS；其容量与展开限制仍保留。
- ROS 发布层按通信协议第 4 节对同 Tick 重发感知重新提交缓存动作，支持暂停恢复；不重复推进地图或策略。
- 感知遇到未知实体类型时拒绝整帧，避免漏掉障碍后错误推断空地；异常帧不更新地图、不提交动作。分数计数必须是非负整数，复活等待不得为负，整数检查处理无符号溢出。

## 测试方式

纯 C++ 离线测试（不需要 ROS 或 curl）：

```bash
./scripts/test_offline.sh
```

ROS 构建和单元回归：

```bash
./scripts/build.sh
./scripts/test.sh
```

真实 ROS 节点的合成消息通信测试（选择未使用的 ROS 域，避免同时控制测试车）：

```bash
source /opt/ros/humble/setup.bash
ROS_DOMAIN_ID=193 ROS_LOCALHOST_ONLY=1 RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
  python3 scripts/test_ros_io.py
```

Python 仅用作测试驱动，机器人主程序仍为 C++17。通信测试检查动作上下文、移动、同 Tick 缓存重发、回执输入、坏帧、旧 Tick、换场、死亡等待和终局停止。合成回执不等于官方裁判实际执行证明。

新增回归包含 50 张固定随机种子的地图，逐目标与独立 BFS 距离对照；另覆盖缓存失效、剩余 Tick、车辆占位变化及 600×600 地图。正式练习端成绩和双机 DDS 仍需单独验证，不能用这些测试代替比赛验收。

## 规则版本核对

现已收到并核对 v1.1 三份 PDF 和新版 `dist.zip`。具体变更、兼容边界及测试结果见 [RULES_V1_1.md](RULES_V1_1.md)。

## 多地图验证

已补充 55 图闭环测试和九图真实练习端批量验证，详见 [MULTIMAP.md](MULTIMAP.md)。新增 `rmtc_multimap` 自动加入 CTest；视野半径 0 时会将相邻未知格作为探索目标。

第一阶段现加入六次采集的有界路线搜索，具体范围、收益和退步案例见 [COLLECTION_OPTIMIZATION.md](COLLECTION_OPTIMIZATION.md)。
