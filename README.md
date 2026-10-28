# ZKXWN · RMTC校内赛

最新研究候选：[校内10.2](versions/v10.2/README.md)，程序 **10.2.0**。稳定对照保留[10.1](versions/v10.1/README.md)，ROS包/程序均仍为 `rmtc_bot9`。

10.2将规则分增量与辅助偏好分列，增加公开机会、资源/任务联合路线、时间与风险比较和候选审计。它尚未证明优于10.1：新地图各320场均分41.495对41.716；同场96局40.340对41.247。当前不建议直接当作已提升的正式替代版。[完整验证](versions/v10.2/修复与验证报告.md)。

- [10.2更新日志](versions/v10.2/更新日志.md) · [模型与日志](versions/v10.2/收益模型与日志说明.md) · [版本索引](docs/VERSION_HISTORY.md)
- Windows Release/Debug各14组通过；真实ROS/官方整场/LLM本版未验。规则PDF为v1.2，随包协议/练习二进制仍v1.1。

```bash
cd ~/ZKXWN/versions/v10.2
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

需要Ubuntu22.04/Humble与C++17构建依赖，先停旧节点、用新终端确认HERO10.2.0及variant2。默认decision_type仍仅balanced/collect/fight，正式模型和网关按官方通知，令牌不写仓库或日志。仓库更新不会自动同步虚拟机。

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

所有历史版的成绩按其自身测试环境理解，不能当作当前版本已验收。

## 根目录旧工程

根目录 `decision/`、`rmtc_ws/`、`scripts/` 保留早期工程，不能与 `versions/v10.1` 混编。固定 1.0 源码见 `versions/v1/01_程序源码/ZKXWN`；[旧工程说明](README_RMTC_CPP.md)和[旧3.0结构导览](docs/PROJECT_STRUCTURE.md)属于历史资料，10.1稳定对照与10.2候选分别按各自README运行。
