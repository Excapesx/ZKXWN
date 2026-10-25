# ZKXWN · RMTC校内赛

当前修复交接版：**[校内9.0](versions/v9/README.md)**。完整的1.0至9.0已分目录归档，运行前先进入对应版本。

- [9.0更新日志](versions/v9/更新日志.md)
- [9.0修复与验证报告](versions/v9/修复与验证报告.md)
- [程序逻辑与学习指南](versions/v9/程序逻辑与学习指南.md)
- [全部版本的验证范围和已知问题](docs/VERSION_HISTORY.md)
- [官方v1.1规则及协议](versions/v9/02_官方规则_v1.1_当前/) · [官方练习包](versions/v9/03_官方练习包_v1.1/)

## 使用9.0

```bash
cd ~/ZKXWN/versions/v9
bash ./scripts/test.sh
bash ./scripts/build.sh
bash ./scripts/run.sh A 2
```

依次进入9.0、运行检查、构建ROS节点、启动A席位第二阶段。需要Ubuntu 22.04、ROS 2 Humble、C++17、colcon和libcurl开发依赖。裁判另行启动，公开ROS域、模型网关和令牌以主办方要求为准；令牌不写入仓库。

9.0修复了重复事件计伤、临时墙等待、被动对手误判、旧公告跨场污染及3项解析边界。Windows Release和Debug各8组4449项断言通过；9组离线对照1668条配对记录已核对。第一阶段与8.0持平，部分混合对手场景约少0.38分，不能称全面更强。

本次9.0验证不包含Ubuntu真实ROS/libcurl构建、真实LLM资格及官方完整500Tick第二阶段。已有测试和离线得分不能代替这些验收。

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

所有版本README都补充了仓库路径、用途、构建/运行方式和证据边界。旧报告原文保留，不能把历史“已通过”当作当前新版本已验收。

## 根目录旧工程

根目录的`decision/`、`rmtc_ws/`、`scripts/`保留早期工程，不能与`versions/v9`混编。固定1.0源码见`versions/v1/01_程序源码/ZKXWN`；根目录旧说明见[README_RMTC_CPP.md](README_RMTC_CPP.md)。

[旧3.0项目结构导览](docs/PROJECT_STRUCTURE.md)属于历史记录，当前9.0以自身README为准。当前各版本清单记录仓库副本；桌面原始归档包保持不变。
