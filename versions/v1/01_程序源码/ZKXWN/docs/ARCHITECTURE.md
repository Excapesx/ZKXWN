# 当前骨架与底层开发入口

> 面向负责底层的 A。  
> 先看这张图，再按“你负责的文件”往下读。

---

## 1. 一句话

当前程序不是空骨架，已经有一条完整链路：

```text
收裁判 JSON → 解析感知 → 更新地图 → BFS 找路 → 决策引擎选动作 → 合法性检查 → 发动作
```

你负责的是**前半段**：通信、感知、地图、找路、第一阶段接线、构建和测试。  
队友负责**后半段**：`decision/src/engine.cpp` 里的评分/攻击/任务选择，以及 LLM。

---

## 2. 逻辑图

```text
┌─────────────────────────────────────────────────────────────┐
│ 裁判                                                         │
│ /team_X/perception   std_msgs/String，data 是 JSON           │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ BotNode（src/ros_io.cpp）                                    │
│ - 订阅 /team_X/perception                                    │
│ - 发布 /team_X/action                                        │
│ - 发布 /team_X/registration                                  │
│ - 收到消息后调用 runtime_->handle(msg->data)                 │
└──────────────────────────────┬──────────────────────────────┘
                               │ JSON 文本
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ BotRuntime::handle（src/runtime.cpp）                        │
│ 1. parse_perception()          解析 JSON                     │
│ 2. 检查 team / tick / session  防错席、旧 Tick、旧场次        │
│ 3. 新场次 reset                清空地图、缓存、LLM           │
│ 4. 同 Tick 复用 / 旧 Tick 丢弃                               │
│ 5. memory.update(p)            更新地图                      │
│ 6. stage2：读 LLM 最新计划、合并、发请求                      │
│ 7. 选择 Stage1Policy / Stage2Policy                          │
│ 8. 调 policy.decide()                                        │
│ 9. action_allowed()            最后一道合法性检查            │
│ 10. action_to_json()           生成动作 JSON                 │
└───────┬───────────────────────────────┬─────────────────────┘
        │                               │
        ▼                               ▼
┌───────────────────────┐     ┌──────────────────────────────┐
│ WorldMemory           │     │ LLMClient（仅第二阶段）       │
│ src/memory.cpp        │     │ src/llm_client.cpp           │
│ - 障碍 / 资源 / 去过  │     │ - 后台线程                   │
│ - 临时墙 / 初始出生点 │     │ - 有界队列 / 超时 / 重试     │
└──────────┬────────────┘     └──────────────┬───────────────┘
           │                                  │
           ▼                                  ▼
┌─────────────────────────────────────────────────────────────┐
│ Planner（src/planner.cpp）                                   │
│ - BFS 最短路                                                 │
│ - 避开已知墙、当前车辆、模型给出的危险区域                   │
│ - 支持“先等一会儿再走”的时序路线                             │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ Stage1Policy / Stage2Policy（src/policies/）                 │
│ - 把 Perception + WorldMemory + LLMPlan 转成 GameState       │
│ - 调 DecisionEngine::decide()                                │
│ - 把 Decision 转成底座 Action                                │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ DecisionEngine（decision/src/engine.cpp）                    │
│ - 候选动作：等待 / 移动 / 攻击 / 防御                        │
│ - 资源收益、任务收益、伤害、死亡风险评分                     │
│ - 返回 Decision（动作 + submit + 原因）                      │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 回到 BotRuntime                                              │
│ action_allowed() → action_to_json() → BotNode 发布             │
│ /team_X/action   JSON：tick + session_id + action + direction│
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 每一层负责什么

| 层 | 文件 | 你需不需要改 | 说明 |
|---|---|---|---|
| 入口 | `src/main.cpp` | 一般不用 | 只负责 `rclcpp::spin` |
| ROS 通信 | `src/ros_io.cpp` / `include/rmtc_bot/ros_io.hpp` | **你要负责** | 订阅、发布、QoS、注册、回调 |
| 感知解析 | `src/perception.cpp` | **你要负责** | JSON、9×9 相对坐标、动作 JSON、合法性检查 |
| 地图记忆 | `src/memory.cpp` | **你要负责** | 障碍、资源、去过哪、临时墙 |
| 找路 | `src/planner.cpp` | **你要负责** | BFS、避障、时序等待 |
| 运行时 | `src/runtime.cpp` | 共用 | 串起感知、记忆、策略、LLM、动作 |
| 第一阶段 | `src/policies/stage1_policy.cpp` | **你负责接线** | 目前只是调用决策引擎，不写复杂策略 |
| 第二阶段 | `src/policies/stage2_policy.cpp` | 队友负责 | 任务/战斗入口 |
| 决策引擎 | `decision/src/engine.cpp` | **队友负责** | 动作评分、攻击、防御、任务选择 |
| LLM | `src/llm_client.cpp` / `src/curl_transport.cpp` | 队友负责 | 后台请求、解析、降级 |
| 测试 | `test/test_core.cpp` | 两个人共用 | 现有 100 个场景 |
| 回放 | `src/replay.cpp` | 两个人共用 | 不启动 ROS 也能测 |

---

## 4. 你负责的“底层”具体是什么

不是从零写，而是接管并加固这五块：

1. **通信稳不稳**
   - 能不能收到 `/team_X/perception`
   - 能不能发 `/team_X/action`
   - QoS 对不对
   - `tick` / `session_id` 有没有原样回填

2. **感知解析准不准**
   - 9×9 字符串有没有正确转成绝对坐标
   - 实体越界、同格、地图边缘有没有处理
   - JSON 字段缺失/类型错误会不会崩

3. **地图记忆对不对**
   - 障碍有没有记住
   - 资源消失有没有删除
   - 阵亡时不能把空视野当成空地图
   - 临时墙到期有没有恢复成未知

4. **BFS 找路对不对**
   - 能不能绕静态墙
   - 能不能避开当前车辆
   - 有时序危险区时能不能等待/绕行
   - 找不到路时会不会安全降级

5. **第一阶段接线**
   - `Stage1Policy` 把感知/地图交给决策引擎
   - 确认只发 `move` / `wait`
   - 确认采集分数正常

---

## 5. 你从哪开始写

### 第一步：先跑通，不要改代码

```bash
cd ~/ZKXWN
source /opt/ros/humble/setup.bash
./scripts/build.sh
./scripts/test.sh
```

然后看一遍：

```text
src/ros_io.cpp
src/perception.cpp
src/memory.cpp
src/planner.cpp
```

### 第二步：先加“观察”，不要加策略

第一个小任务建议：**加一个调试开关，打印每 Tick 的关键状态**。

在 `ros_io.cpp` 里加一个 ROS 参数，比如：

```bash
ros2 run rmtc_bot rmtc_bot --ros-args \
  -p team:=A -p stage:=1 -p debug:=true
```

打开后只打印一行：

```text
tick=12 session=... self=(3,4) hp=100 alive=1
entities=resource_high@(5,4) obstacle@(6,4)
action=move right
last_action=moved=1 dir=right
```

目的：以后任何问题都能从这一行看出是“感知错、地图错、找路错、还是动作错”。

注意：不要打印 token，不要打印完整原始 JSON。

### 第三步：补底层测试

先补这几种，不要碰决策引擎：

- 实体在自身同一格（没有 `2`）→ 相对位移 `[0,0]`
- 实体在地图边缘 → 不越界、不崩溃
- 障碍记忆：看到墙 → 记住；墙消失 → 不永久保留
- 资源消失：视野内资源不见 → 从记忆删除
- 阵亡空视野 → 不清空地图
- BFS 绕墙 → 能找到路
- 临时墙到期 → 变成未知，不是永久墙

测试写在 `test/test_core.cpp`，用现有 fixtures。

### 第四步：再改一个底层功能

推荐按这个顺序，难度从低到高：

1. **`WorldMemory::nearest_unvisited` 从曼哈顿距离改成 BFS 距离**  
   现在它只看直线距离，可能选到一个绕不过去的 frontier。  
   改完用“墙后面的未知格”做测试。

2. **资源收集进度更稳**  
   现在用“分数增量 + 当前位置”估算收集了几个普通/高价值资源，机器人收集后移动可能对不上。  
   这只影响目标选择，不影响最终分数；可以慢慢改。

3. **回放工具**  
   加一个 `scripts/replay.sh`，把练习日志/感知 JSON 回放一遍，方便队友不启动裁判也能调策略。

4. **性能检查**  
   在回调里记录 `callback_us`，确认 100ms 的第一阶段不会超时。

---

## 6. 你暂时不要动的地方

- `decision/src/engine.cpp`：队友负责，动作评分/攻击/任务逻辑都在这里。
- `src/llm_client.cpp`、`src/curl_transport.cpp`：队友负责。
- `src/policies/stage2_policy.cpp`：队友负责。
- `src/policies/stage1_policy.cpp`：你只做接线，不要把策略写进去。
- `types.hpp` / `policy.hpp` / `runtime.hpp`：改之前先和队友说，接口一改两边都要动。

---

## 7. 第一天的完成标准

- [ ] `./scripts/build.sh` 通过
- [ ] `./scripts/test.sh` 通过
- [ ] 能说清楚 `parse_perception → memory.update → planner.plan → DecisionEngine` 这条链
- [ ] 加了一个 `debug` 开关，能打印每 Tick 摘要
- [ ] 补了 3–5 个底层测试
- [ ] 没有改 `decision/src/engine.cpp`

做到这些，再开始改 `nearest_unvisited` 或资源进度。
