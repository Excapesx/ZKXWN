# 校内10.1

程序版本 **10.1.0**，从10.0提交 `e1685e5` 复制后开发；ROS包和可执行程序仍叫 `rmtc_bot9`。

本版实现第二阶段分时策略：安全开局多争资源，中期比较当前机会，尾盘减少没有明确得分目标的追猎。测试未证明稳定涨分，保留全部正负结果。10.0源码独立保留。

## 第二阶段怎么分时

|500 Tick对局|行为|
|---|---|
|0–99，opening|未满120资源价值、HP≥60、当前没有预计来火/补刀机会且无狂暴时，资源基础估值乘1.20；存在8步内不明显晚到的资源且无有益事件目标时，降低远处追猎并允许采集路线与普通攻击比较。|
|100–349，adaptive|资源基础估值恢复原比例；按资源竞争、任务、攻击、生存和事件候选比较。没有资源时可在开局提前做其他事情，不强制等满100 Tick。|
|350–499，finish|有安全可达的未封顶资源或未完成坐标任务时，追猎/拦截/巡逻候选乘0.70；即时攻击和事件不乘此系数。原有任务临近结束时的1.8/2.6倍紧迫度保留。|

时段随本场 `maxTicks` 缩放：开局为 `min(100,maxTicks/5)`，尾段为最后 `min(150,3×maxTicks/10)` Tick。短场采用至少1 Tick的整数下限。这些都是赛前编入代码的规则。自动行为变化不修改 `decision_type` 或引入现场调参通道。

资源已满120后，将阻止对手采集的基础估值降到原来的1/4；资源任务奖励独立计算。危险逃离、出生保护、死亡等待、堡垒驻留等既有时序仍在普通目标比较之前处理。第一阶段算法未改。

## 这次验证到了什么

Windows MSVC Release/Debug各13组CTest通过，新增17条阶段/安全检查。64张新生成地图、每张四席轮换，每版第二阶段256局，10.1平均 **41.098**，10.0平均 **41.141**；各组差值区间均跨零。第一阶段40图均分38.95/40，逐图与10.0相同。非法动作0。

[完整验证报告](修复与验证报告.md) · [更新日志](更新日志.md) · [分时策略说明](分时策略说明.md) · [规则核对](v1.2规则适配说明.md) · [程序学习入口](程序逻辑与学习指南.md)

## 下载后在Ubuntu运行

```bash
cd ~/ZKXWN/versions/v10.1
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

需要Ubuntu22.04、ROS2 Humble、C++17、CMake、Python3、colcon和libcurl开发依赖。先停止旧节点，在新终端启动本版，确认输出 `HERO 10.1.0`。日志会显示 `phase`、`resource_scale`、`pursuit_scale`、`resource_focus`；处于某阶段不等于该Tick已经采集到资源。

复现一组离线比较：

```bash
bash scripts/bench.sh --a 10.1 --b 10.0 --mode seat --stage 2 --field 778 --events standard --maps 16 --seed 102010 --width 18 --height 18 --vision 4 --threads 3
```

源码 `src/rmtc9/phase_policy.hpp` 定义时间和基础系数；`bot.cpp` 用合法感知判断机会、候选和安全；CMake只管理编译和链接。冻结的10.0核心只用于离线对照，见 `baseline_v100/source_manifest.json`。

规则PDF为v1.2；随包协议/练习裁判仍为v1.1。当前未执行10.1的真实ROS构建、官方裁判整局或真实统一LLM验收；离线事件使用理想结构化计划，不能替代正式资格。[实机记录模板](正式验收与技术报告模板.md)。
