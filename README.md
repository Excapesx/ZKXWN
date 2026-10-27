# ZKXWN · RMTC校内赛

当前交接版：[校内10.0](versions/v10.0/README.md)，程序 **10.0.0**，ROS包/程序仍为 `rmtc_bot9`。历史版本独立保留。

10.0补充资源到达时间预测、受约束的候选路线比较、紧迫采集与战斗的条件比较、达标后低血量风险、第一阶段尾盘预算重算，以及逐tick动作/回执核验。更激进但多图退步的方案未纳入最终策略；本版不承诺普遍涨分，详见[验证报告](versions/v10.0/修复与验证报告.md)。

- [10.0更新日志](versions/v10.0/更新日志.md) · [学习入口](versions/v10.0/程序逻辑与学习指南.md)
- [实机验收记录模板](versions/v10.0/正式验收与技术报告模板.md) · [全部版本](docs/VERSION_HISTORY.md)
- [历史资源审计](docs/RESOURCE_COLLECTION_AUDIT_2026-10-27.md)

```bash
cd ~/ZKXWN/versions/v10.0
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

需要Ubuntu22.04、Humble、C++17、CMake、Python3、colcon、libcurl开发依赖。ROS域按裁判配置；先停旧节点，再新开终端加载本版，确认 `HERO 10.0.0`。GitHub更新不会自动同步另一台电脑或虚拟机。

10.0默认配置 `decision_default.json` 已随包分发，档位仍仅balanced/collect/fight。真实网关由官方提供，模型名留空；令牌通过环境提供，不能写进仓库或日志。启动输出本地路径与文件摘要用于追溯，不单独证明二进制来源。

规则PDF为v1.2，协议/官方练习二进制仍v1.1。本次真实ROS构建、统一LLM、官方裁判整局验收待完成；旧版实测不移记为10.0成绩。

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

所有历史版的成绩按其自身测试环境理解，不能当作当前版本已验收。

## 根目录旧工程

根目录 `decision/`、`rmtc_ws/`、`scripts/` 保留早期工程，不能与 `versions/v10.0` 混编。固定 1.0 源码见 `versions/v1/01_程序源码/ZKXWN`；[旧工程说明](README_RMTC_CPP.md)和[旧3.0结构导览](docs/PROJECT_STRUCTURE.md)属于历史资料，当前入口以10.0 README为准。
