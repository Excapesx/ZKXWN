# ZKXWN · RMTC校内赛

当前参赛组合：[v10.1.1完整开发版](versions/v10.1.1/README.md) · [精简参赛包](releases/contest-v10.1.1/README.md) · [操作卡](releases/contest-v10.1.1/参赛操作卡.md)

2026-10-30整合：保留已验证的本机10.1策略（2000伤害阈值），加入官方网关关闭思考/JSON输出及启动修复，默认max_tokens=512、decision_type=balanced。运行标识HERO-10.1.1，ROS包/程序名仍为rmtc_bot9

128张第二阶段地图、1024场模拟，资源与总分基本不变，保守前二64.06%到67.38%，收益主要是同分排名。正式动作周期的v1.1练习两阶段40/45分、非法动作0，使用mock AI；真实官方网关、正式AI资格及现场网络仍需联调。[完整报告](versions/v10.1.1/最终组合与测试报告.md)

```bash
cd ~/ZKXWN/releases/contest-v10.1.1
python3 scripts/verify_delivery.py
bash scripts/build.sh
bash scripts/contest.sh
```

以下为历史版本与研究记录；其中“最新”“未验”等表述仅描述当时版本，当前入口以上方v10.1.1说明为准


2026-10-30 新增：[10.1.1](versions/v10.1.1/README.md)（保留10.1策略，移入最新网关与启动修复）· [精简参赛包](releases/contest-v10.1.1/README.md)。不宣称唯一最强；详细README待后续要求。


最新接入维护版：[10.4.1完整开发包](versions/v10.4.1/README.md) · [精简参赛包](releases/contest-v10.4.1/README.md) · [比赛操作卡](releases/contest-v10.4.1/参赛操作卡.md)。增加关闭思考/JSON输出请求，保留10.4默认策略；正式域号、席位、网关和令牌待现场填写。真实ROS与官方网关未验，详细README待后续要求。

以下为此前10.4研究记录。

最新研究候选：[校内10.4](versions/v10.4/README.md)，程序 **10.4.0**。它检验固定时段乘数，默认保留10.3原策略；没有证明稳定优于10.3或10.1，因此不是已确认的强度升级。稳定对照包括[10.1](versions/v10.1/README.md)和[10.2](versions/v10.2/README.md)，ROS包/程序仍为 `rmtc_bot9`。

10.4的80张新地图对照中，C0默认、C1减半、C2取消数值乘数的均分为40.752、40.791、40.833；地图级差值区间包含0或与0相接。第三轮建议保留C0默认。[完整验证](versions/v10.4/修复与验证报告.md)。

- [10.4更新日志](versions/v10.4/更新日志.md) · [版本索引](docs/VERSION_HISTORY.md)
- Windows Release/Debug各17组通过；真实ROS/官方整场/LLM本版未验。规则PDF为v1.2，随包协议/练习二进制仍v1.1。

```bash
cd ~/ZKXWN/versions/v10.4
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

需要Ubuntu22.04/Humble与C++17构建依赖，先停旧节点、用新终端确认HERO10.4.0。默认decision_type仍仅balanced/collect/fight，正式模型和网关按官方通知，令牌不写仓库或日志。仓库更新不会自动同步虚拟机。

## 版本入口

| 版本 | 主要工作 | ROS包及程序名 |
| --- | --- | --- |
| [1.0](versions/v1/README.md) | 最初的C++底座、独立决策库及v1.1策略整合 | `rmtc_bot` |
| [2.0](versions/v2/README.md) | 独立纯C++核心、预算路线和异步LLM原型 | `rmtc_bot2` |
| [3.0](versions/v3/README.md) | 场次/线程隔离、独立危险窗口、战斗收益修正 | `rmtc_bot2` |
| [4.0](versions/v4/README.md) | 事件时序、复活、解析、战斗及协议边界完善 | `rmtc_bot2` |
| [5.0](versions/v5/README.md) | 第二阶段集合路线DP实验 | `rmtc_bot3` |
| [6.0](versions/v6/README.md) | 资源竞争、短程路线、三种同时战斗结算情景 | `rmtc_bot6` |
| [7.0](versions/v7/README.md) | 坏实体隔离、时间寻路、事件历史及HTTP资格处理 | `rmtc_bot7` |
| [8.0](versions/v8/README.md) | 观察点路线、主动搜索、容错计划及模型请求调度 | `rmtc_bot8` |
| [9.0](versions/v9/README.md) | 修复8.0的7项反例，保留完整源码、规则和证据 | `rmtc_bot9` |
| [9.1](versions/v9.1/README.md) | 答疑适配：默认模型限制、输出预算、提交一致性检查 | `rmtc_bot9` |
| [9.2](versions/v9.2/README.md) | 实录重放定位目标断档、缓冲追击/守分、偏移堡垒、诊断日志 | `rmtc_bot9` |
| [9.3](versions/v9.3/README.md) | 同长度路径扩大视野、资源诊断及配对复测 | `rmtc_bot9` |
| [10.0](versions/v10.0/README.md) | 到达时间竞争、保守路线比较、尾盘预算、动作回执核验及完整默认配置 | `rmtc_bot9` |
| [10.1](versions/v10.1/README.md) | 前期资源/中期局势/尾盘兑现的自动分时策略及公开状态约束 | `rmtc_bot9` |
| [10.2](versions/v10.2/README.md) | 规则分增量、公开机会/联合路线及可解释辅助估值的研究候选 | `rmtc_bot9` |
| 10.3 | 资源竞争、短组合路线与机会损失研究；冻结核心保存在10.4基线中，独立候选目录未随本次提交 | `rmtc_bot9` |
| [10.4](versions/v10.4/README.md) | 检验固定时段乘数；默认保留10.3策略，结论为证据不足 | `rmtc_bot9` |

所有历史版的成绩按其自身测试环境理解，不能当作当前版本已验收。

## 根目录旧工程

根目录 `decision/`、`rmtc_ws/`、`scripts/` 保留早期工程，不能与 `versions/v10.1` 混编。固定 1.0 源码见 `versions/v1/01_程序源码/ZKXWN`；[旧工程说明](README_RMTC_CPP.md)和[旧3.0结构导览](docs/PROJECT_STRUCTURE.md)属于历史资料，10.1稳定对照与10.2候选分别按各自README运行。
