# 双人接口约定 v0.2

消息路径：`RosIo → BotRuntime → parse_perception → WorldMemory → Policy → DecisionEngine → action_to_json → RosIo`。

1. `Policy::decide(const Perception&, WorldMemory&, const optional<LLMPlan>&)` 返回底座 Action；`last_decision().submit=false` 时不发布。地图只由 Runtime 每新 Tick 更新一次。
2. `GameState` 是标准 C++ 输入，不含 ROS、JSON 和凭据；`make_game_state` 是唯一结构转换处。任务类型按官方 `normal_resources/high_resources/resource_value/effective_damage/reach` 映射。
3. 坐标 x 向右、y 向下，从 0 开始；动作方向为小写英文。Planner 返回期望世界位移；DecisionEngine 只在 MOVE 上映射幻术，底座不得再反转。
4. `Route.wait_first=true` 表示时序路线第一步需要等待，此时 `first_step` 必须为空；普通路线返回首步方向。`distance` 包含等待的 Tick。
5. 当前车辆只使用本 Tick 的视野；历史车辆坐标不作为当前攻击目标。资源记忆来自此前公开视野，再次看见格内无资源则删除。
6. 未知地图可探索，0 占位符不能证明可走；有效且完整视野实体列表才能用于视野内空地推断。寻路遇到未知障碍，下一 Tick 依实际坐标与新视野重算。
7. 模型结果绑定发请求时的场次和 Tick；旧场回复丢弃。目标建议短期有效，事件区域/效果按各自绝对时段保留，死亡不重置事件时钟。
8. Runtime 是单线程调用入口；LLMClient 用互斥锁交接结果。模型线程不访问 ROS、WorldMemory 或 DecisionEngine。
9. 临时墙只在有模型时效依据且首次观察在该时段内时标记到期。到期变未知，不把先前静态墙直接擦掉。晚加入无法确认原出生点时保持未知。
10. 日志只记录状态码、Tick、动作回执摘要和计算时间；不输出令牌、原始 HTTP 响应或整段请求正文。

当前模型支持避险区域和四类时效效果；堡垒/辣椒等需要另补结构和计分规则，不能通过随意增加公告关键词分支来补。
