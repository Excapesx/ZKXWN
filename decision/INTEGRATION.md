# 决策与底座的接口草案

本文件描述本仓库的标准 C++ 代码约定；v0.2 的实际底座已在 `../rmtc_ws/src/rmtc_bot/` 接通。尚未获得队友对其可能另有的本地源码接口的确认，后续改字段命名须保留语义与规则边界。

## C++ 使用方式

包含 `rmtc_decision/engine.hpp`，使用 `rmtc::decision` 命名空间。底座的寻路类继承 `Planner` 并实现 `plan(const RouteQuery&) const`。

```cpp
DecisionEngine engine(planner, Mode::TaskFocus);
Decision result = engine.decide(state);
```

`planner` 必须比 `engine` 活得久，同一席位复用同一个 engine。核心不是线程安全对象，应在同一个决策线程调用；后台 LLM 结果由底座同步后以快照传入。

## 输入字段来源

| 内部字段 | 来源和要求 |
|---|---|
| `session_id`、`tick` | 当前本队感知原值；无有效值时不提交 |
| `stage`、`max_ticks` | 明确的启动/场次配置，官方示例感知没有这两个字段 |
| `width`、`height` | 感知 `map.width/height` |
| `self.position/hp/alive` | 当前感知，复活后直接更新，不能累计位移推算 |
| `self.protected_spawn` | 感知 `self.protected` |
| `self.initial_spawn` | 开局合法定位的记忆，不能以复活格覆盖它 |
| `score` | 当前本队感知的资源价值、伤害和已完成任务 ID |
| `missions` | 感知 `missions`；类型转为枚举，任务点用绝对坐标；Reach 默认目标 1 |
| `Mission.observed_progress` | 可选的内部进度估计，不是假设裁判存在同名字段；未知时留空 |
| `resources` | 当前可见及仍可信的合法记忆；重访确认消失时要删除 |
| `enemies` | 合法可见敌车，必须附 `observed_tick`；旧回合位置不作为当前目标或占位 |
| `known_walls` | 从合法感知确认的静态障碍，不因一次 `moved=false` 永久加墙 |
| `temporary_blocks` | 带场次和半开时间区间的临时阻挡；不得从裁判内部配置获取 |
| `exploration_targets` | 底座从合法地图记忆生成的探索点，不能来自观赛全图 |
| `effects` | 校验后的内部效果快照，需注明场次及时间；未验证或其他场次的数据会被忽略 |

实体原始 `position` 是每个实体自己的 9×9 相对图，应在底座中转换为绝对坐标。`0` 不是空地，也不是已探索标记。

`effects.validated=true` 只表示适配层完成来源、字段、坐标和时间检查；不表示完成真实 AI 调用或获得正式任务计分资格。该资格仍以裁判本场记录为准。禁止在适配层用关键词/正则代替 LLM 理解事件。

## 寻路交接

`RouteQuery` 提供起点、目标、地图尺寸及当前阻挡格；资源格不是阻挡。`blocked` 包含已知墙、生效临时墙和本 Tick 可见存活敌车占位。底座可以结合自身地图记忆进一步限制未知区域，但不能忽略本次传入的阻挡。

无路返回 `std::nullopt`；有路返回真实路径长度和第一步的世界方向。已在目标格时距离为 0、第一步为空。不同格时距离必须大于 0，并有第一步；时序路线允许 `wait_first=true` 且第一步为空，距离包括等待。核心会检查第一步是否出界、被占及受阻，但不能证明底座返回的整条路径正确。生产 Planner 只在已确认临时墙到期后放开相应阻挡，不放开静态墙或当前车辆占位。

MOVE 不能跟进其他车辆本 Tick 将离开的格子；不能因猜测它会走就把它的当前占位清掉。资源与任务目标可能需要绕障，不能用曼哈顿距离冒充 BFS 路径长度。

## 输出与职责

- `submit=false`：数据无效、回合过期、比赛结束，或属于已退场场次，不发布动作。
- `action`：唯一动作；MOVE/ATTACK 带方向，WAIT/DEFEND 不带方向。枚举需转换为官方小写字符串。
- `goal`：供人调试的目标，不是额外动作字段。
- `expected_position`：正常执行后的预期位置，只供解释，下一帧必须以裁判实际位置覆盖。
- `reason`：原因码，用于日志与教学。
- `utility`：策略排序值，不是正式得分，也不是胜率。
- `reused_tick=true`：同回合结果复用，不重复推进策略；如仍在当前窗口可重发同一动作，窗口外不补发。

底座填充最新感知的 `tick`、`session_id`，转换方向字符串，做最终协议合法性检查后发布。`last_action` 的核对及拒绝日志由底座负责。保护期、世界分裂、幻术仍须结合实际回执验证。

幻术映射本版归决策模块：寻路返回希望的世界方向，决策返回已经反转的 MOVE 指令。ATTACK 方向不反转。队友如果已有方向映射，集成时须明确只保留一处。

## 尚待双方确认

第一阶段策略文件最终归属、队友本地结构体与仓库同步方式，以及现场登记的 decision_type 枚举仍需两人确认。本仓库已包含 ROS 节点、JSON 发布器和本地练习回执；正式前置检录仍须按主办方要求提交并复核，不能由本地代码存在推定完成。

# v0.2 已有适配实现

实际适配位于 `../rmtc_ws/src/rmtc_bot/src/policy.cpp`、`runtime.cpp`；生产 BFS 在 `planner.cpp`，不使用此目录演示 BFS。两阶段共享本模块，不再各写一份战斗规则。

接口新增：`Route.wait_first` 为 true 时首步为空，表示等待一个 Tick 后继续路线；路线距离包含等待。`GameState.dangers` 只接收模型结构校验后的带场次/时间窗口危险区；`advisory_target/priority` 是软建议，仍须经过路线、收益、风险和合法性检查。

