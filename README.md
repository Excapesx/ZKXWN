# ZKXWN · RMTC校内赛

当前交接版：[校内10.1](versions/v10.1/README.md)，程序 **10.1.0**，ROS包/程序仍为 `rmtc_bot9`。

10.1按公开Tick和局势自动调整第二阶段倾向：0–99安全采集、中段比较机会、350 Tick后优先可得分目标。事件、生存和即时攻击继续参与判断；资源未满的安全开局基础估值提高20%。

本轮第二阶段同地图配对每版256场，10.1均分41.098，10.0均分41.141，未证明稳定涨分。Release/Debug各13组回归通过，真实ROS/LLM/官方整局待验。[验证报告](versions/v10.1/修复与验证报告.md)。

- [10.1更新日志](versions/v10.1/更新日志.md) · [分时策略](versions/v10.1/分时策略说明.md) · [学习入口](versions/v10.1/程序逻辑与学习指南.md)
- [实机记录模板](versions/v10.1/正式验收与技术报告模板.md) · [全部版本](docs/VERSION_HISTORY.md)

```bash
cd ~/ZKXWN/versions/v10.1
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

需要Ubuntu22.04、ROS2 Humble及C++17构建依赖；先停止旧节点，在新终端确认启动输出 `HERO 10.1.0`。仓库更新不会自动同步虚拟机。默认配置仅balanced/collect/fight，模型名留空，真实网关及令牌按官方通知配置，不能写入仓库或日志。

v1.2规则明确伤害500达对抗满分；旧D/300提取稿已过时。规则PDF为v1.2，随包协议/练习二进制仍v1.1。历史版独立保留，正式资格和实机结果按每一版实际证据确认。

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

所有历史版的成绩按其自身测试环境理解，不能当作当前版本已验收。

## 根目录旧工程

根目录 `decision/`、`rmtc_ws/`、`scripts/` 保留早期工程，不能与 `versions/v10.1` 混编。固定 1.0 源码见 `versions/v1/01_程序源码/ZKXWN`；[旧工程说明](README_RMTC_CPP.md)和[旧3.0结构导览](docs/PROJECT_STRUCTURE.md)属于历史资料，当前入口以10.1 README为准。
