# ZKXWN · RMTC C++ 主程序

策略负责人入口：[决策/第二阶段/战斗/LLM交接](docs/STRATEGY_HANDOVER.md) · [v1.1优化方案](docs/STRATEGY_OPTIMIZATION_PLAN.md)。本次战斗收益更新尚未完成Ubuntu回归与整场验收，历史成绩对应原提交。

这是 RMTC 机甲大师图灵对决赛的 C++ 主程序仓库，基于 `rmtc_cpp_v0.2.0_source` 整理。

**先看这里，不要直接照抄旧交接文档里的 `/home/xu/...` 路径。**

---

## 1. 目录

```text
ZKXWN/
  decision/                  # 决策引擎（攻击/防御/资源/任务/事件）
  rmtc_ws/src/rmtc_bot/      # ROS 2 包：通信、感知、地图、BFS、策略适配、LLM
  third_party/nlohmann/      # 离线 JSON 依赖
  scripts/                   # 构建/运行/测试脚本
  docs/                      # 分工、交接、接口说明
  README_RMTC_CPP.md         # 原始交接入口
  RMTC_CPP_集成说明.md       # v0.2 原始集成记录（历史路径已标注）
```

`decision/`、`rmtc_ws/`、`third_party/` 必须保持兄弟关系，不能只复制 `rmtc_bot/`。

---

## 2. 环境

- Ubuntu 22.04
- ROS 2 Humble
- C++17
- `libcurl4-openssl-dev`
- `nlohmann/json` 已放在 `third_party/`，不用另外安装

如果新机器缺依赖：

```bash
sudo apt update
sudo apt install libcurl4-openssl-dev
```

---

## 3. 编译

```bash
cd ~/ZKXWN
source /opt/ros/humble/setup.bash
./scripts/build.sh
```

或者手动：

```bash
cd ~/ZKXWN/rmtc_ws
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select rmtc_bot \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

---

## 4. 跑程序

练习端默认使用本机 ROS 域 `27`。第一次打开终端时建议先设置：

```bash
export ROS_DOMAIN_ID=27
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
```

正式比赛使用主办方公布的 `ROS_DOMAIN_ID`，不要沿用练习值。

### 第一阶段

```bash
cd ~/ZKXWN
./scripts/run.sh A 1
```

等价于：

```bash
source /opt/ros/humble/setup.bash
source ~/ZKXWN/rmtc_ws/install/setup.bash
ros2 run rmtc_bot rmtc_bot --ros-args \
  -p team:=A -p stage:=1 -p llm_enabled:=false
```

### 第二阶段

```bash
cd ~/ZKXWN
./scripts/run.sh A 2
```

第二阶段如果要开真实 LLM：

```bash
export RMTC_LLM_ENABLED=1
export RMTC_TEAM_TOKEN='本场席位令牌'
export RMTC_AI_GATEWAY='http://裁判机IP:8000/v1/chat/completions'
export RMTC_MODEL='主办方模型名'
./scripts/run.sh A 2
```

`decision_type` 只从下面的 JSON 文件读，不要再传 `-p decision_type:=...`：

```bash
export RMTC_DECISION_CONFIG="$HOME/ZKXWN/rmtc_ws/src/rmtc_bot/config/decision_config.json"
```

示例在 `config/decision_config.example.json`，只允许：

```json
{"decision_type": "balanced"}
```

可选值：`balanced`、`fight`、`collect`。

> 正式比赛修改 `decision_type` 必须按裁判登记的切换流程进行；不要用环境变量或 ROS 参数绕过。

---

## 5. 测试

最新第一阶段采集改进及前后对比见 [docs/COLLECTION_OPTIMIZATION.md](docs/COLLECTION_OPTIMIZATION.md)。

多地图测试结果和复现命令见 [docs/MULTIMAP.md](docs/MULTIMAP.md)：55 张闭环测试图和九张真实练习端测试图。

```bash
cd ~/ZKXWN
./scripts/test.sh
```

或者：

```bash
cd ~/ZKXWN/rmtc_ws
source /opt/ros/humble/setup.bash
colcon test --packages-select rmtc_bot --event-handlers console_direct+
colcon test-result --verbose
```

本地离线回放（不启动 ROS）：

```bash
cd ~/ZKXWN
./rmtc_ws/build/rmtc_bot/rmtc_replay \
  rmtc_ws/src/rmtc_bot/test/fixtures/perception_stage1.json 1
```

---

## 6. 当前状态

v1.1 规则适配及底层优化见 [docs/RULES_V1_1.md](docs/RULES_V1_1.md)，底层构建和测试入口见 [docs/FOUNDATION.md](docs/FOUNDATION.md)。新版练习端第一阶段默认节奏实测 40/40、0 非法动作，并验证暂停恢复；结构化记录见 [测试记录](docs/stage1_v1_1_result.json)。

下列为此前 v0.2/v0.3 记录，第二阶段成绩不代表 v1.1 验证结果：

已验证：

- Ubuntu 22.04 + ROS 2 Humble 编译通过。
- 100 个本地场景通过（59 个底座 + 41 个决策）。
- 第一阶段练习端默认 100 Tick：**40/40，0 非法动作**。
- 第二阶段 500 Tick、50ms/Tick、3 个陪练：**42.5 分，3 个任务，0 非法动作**（练习环境，非正式成绩）。

尚未验证：

- 正式 500ms/Tick 的完整第二阶段。
- 真实统一 AI 网关和正式 token。
- 双机/多机 DDS、有线网络、Docker 镜像。
- 全部复杂事件（堡垒、辣椒等）。

---

## 7. 两个人怎么分工

逻辑链路和底层入口见 `docs/ARCHITECTURE.md`；两个人怎么分工见 `docs/TEAM.md`。

技术报告模板见 `docs/TECH_REPORT_TEMPLATE.md`；v1.1 规则变化见 `docs/RULES_V1.1_DELTA.md`。

- **A（底层）**：`ros_io`、`perception`、`memory`、`planner`、`stage1`、CMake、测试、打包。
- **B（策略）**：`decision/src/engine.cpp`、`stage2`、`combat`、`llm_client`、`curl_transport`、第二阶段调优。
- 共用：`types.hpp`、`policy.hpp`、`runtime.hpp`、`INTERFACES.md`；改之前先商量。
- 每天合并，不要最后合并。

---

## 8. 规则提醒

- 不要把正式 token、密钥、日志提交到仓库。
- 不要写死席位、`session_id`、地图尺寸、资源坐标、事件时间。
- 阵亡只能 `wait`；出生保护不能 `attack`；第一阶段只能 `move`/`wait`。
- 随机事件公告只能交给 LLM 理解，不能用关键词/正则直接解析。
- 代码封存后不要改源码、Prompt、决策参数；只能按登记的 `decision_type` 切换。
