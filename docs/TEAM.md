# 两个人怎么接手

> 目标：不再各写一套。底座一套，策略两块；每天合并，不复制。

## 1. 分工

| 谁 | 主要负责 | 具体文件 |
|---|---|---|
| **A（底层）** | 通信、感知、地图、BFS、第一阶段、构建、测试、打包 | `ros_io.*`、`perception.*`、`memory.*`、`planner.*`、`policies/stage1_policy.*`、`CMakeLists.txt`、`scripts/` |
| **B（策略）** | 决策引擎、第二阶段、战斗、LLM、事件 | `decision/src/engine.cpp`、`policies/stage2_policy.*`、`combat.*`、`llm_client.*`、`curl_transport.cpp` |
| **共用** | 接口、数据、运行时、报告 | `types.hpp`、`policy.hpp`、`runtime.*`、`INTERFACES.md`、`README.md` |

改共用文件之前，先在群里说一句；不要两个人同时改同一个文件。

## 2. 第一周先读什么

### A 的阅读顺序

1. `src/main.cpp`：入口。
2. `src/ros_io.cpp`：收感知、发动作、QoS。
3. `src/perception.cpp`：JSON、9×9 相对坐标。
4. `src/memory.cpp`：地图、资源、障碍。
5. `src/planner.cpp`：BFS。
6. `src/policies/stage1_policy.cpp`：第一阶段怎么调决策引擎。

### B 的阅读顺序

1. `decision/LEARNING.md`：先看阵亡分支。
2. `decision/src/engine.cpp`：按“阵亡 → 资源 → 任务 → 攻击/防御 → 评分”读。
3. `src/policies/stage2_policy.cpp`：第二阶段入口。
4. `src/llm_client.cpp`：异步、解析、降级。
5. `src/curl_transport.cpp`：HTTP 细节。

## 3. 每天怎么合并

```bash
# 早上
git pull

# 写完后
git add .
git commit -m "feat: ..."
git push
```

- 小步提交，别攒一周。
- 编译通过再提交。
- 每次合并后跑一次 `./scripts/test.sh`。
- 改接口（`types.hpp`、`policy.hpp`、`runtime.hpp`）必须两个人都知道。

## 4. 两个人不要做的事

- 不要各写一个 `rmtc_bot`。
- 不要复制 `memory/planner/perception` 再改一份。
- 不要各调一套 LLM Prompt。
- 不要同时改 `main.cpp`、`types.hpp`、`config.hpp`。
- 不要用同一个 ROS 域、端口、席位并行测试。
- 不要最后一天才合并。

## 5. 并行测试建议

- A：`ROS_DOMAIN_ID=27`，练习端端口 `8080`，席位 A。
- B：`ROS_DOMAIN_ID=37`，练习端端口 `8081`，席位 B。
- 不要两个人同时控制同一个席位。

## 6. 提交前

- [ ] `./scripts/build.sh` 通过
- [ ] `./scripts/test.sh` 通过
- [ ] 第一阶段 100 Tick 跑过
- [ ] 第二阶段 500 Tick 跑过
- [ ] 阵亡、保护、重开、暂停恢复、进程重启都试过
- [ ] 没有 token、日志、build/install 进入 Git
- [ ] `decision_config.json` 只包含 `decision_type`
- [ ] 两个人都能讲清楚自己负责的模块
