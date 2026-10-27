# ZKXWN · RMTC校内赛

当前交接版：[校内 v9.3](versions/v9.3/README.md)，程序 **9.3.0**，ROS 包/程序 `rmtc_bot9`。1.0—9.2 独立目录完整保留。

v9.3 在第二阶段同长度最短路中优先选择能揭开更多未知格的下一步；有可见敌人或危险场时保留原路径判断。四组同地图对照平均增加约 0.29—0.90 分，是小幅优化，尚无质变或正式赛胜率证明。

- [9.3 更新日志](versions/v9.3/更新日志.md) · [验证报告](versions/v9.3/修复与验证报告.md)
- [资源采集复测与下一步方案](docs/RESOURCE_COLLECTION_AUDIT_2026-10-27.md)
- [规则适配说明](versions/v9.3/v1.2规则适配说明.md) · [赛事规则 PDF](versions/v9.3/01_赛事规则_v1.2/)
- [学习入口](versions/v9.3/程序逻辑与学习指南.md) · [全部版本及证据边界](docs/VERSION_HISTORY.md)

赛事规则 PDF 为 v1.2，随包协议和官方练习二进制仍为 v1.1。真实统一 LLM 和官方 v1.2 裁判验收未完成；前轮官方练习 45 分属于 v9.2，不移记为 v9.3 成绩。

## 使用 v9.3

```bash
cd ~/ZKXWN/versions/v9.3
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
# 域号以本次裁判的实际配置为准
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

需要 Ubuntu 22.04 / ROS 2 Humble / C++17 / colcon / libcurl 开发依赖，裁判另开终端。网关令牌由环境提供，模型名必须留空。9 系列沿用相同包名，切换前停止旧节点并新开终端，确认启动日志 `HERO 9.3.0`。GitHub 更新不会自动更新其他电脑或虚拟机。

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

所有历史版的成绩按其自身测试环境理解，不能当作当前版本已验收。

## 根目录旧工程

根目录 `decision/`、`rmtc_ws/`、`scripts/` 保留早期工程，不能与 `versions/v9.3` 混编。固定 1.0 源码见 `versions/v1/01_程序源码/ZKXWN`；[旧工程说明](README_RMTC_CPP.md)和[旧3.0结构导览](docs/PROJECT_STRUCTURE.md)属于历史资料，当前入口以 v9.3 README 为准。
