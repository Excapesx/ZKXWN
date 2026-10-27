# ZKXWN · RMTC校内赛

当前交接版：[校内9.2](versions/v9.2/README.md)，程序9.2.0，ROS包/程序`rmtc_bot9`。1.0–9.1完整保留。

- [9.2更新日志](versions/v9.2/更新日志.md) · [修复与验证报告](versions/v9.2/修复与验证报告.md)
- [v1.2规则适配说明](versions/v9.2/v1.2规则适配说明.md) · [赛事规则PDF](versions/v9.2/01_赛事规则_v1.2/)
- [学习指南](versions/v9.2/程序逻辑与学习指南.md) · [实际验收与技术报告](versions/v9.2/正式验收与技术报告模板.md)
- [全部版本及证据边界](docs/VERSION_HISTORY.md)

9.2通过昨日9.1实录重放复现并修复后半场203次连续等待的目标断档；补强缓冲阶段追击、风险位置和偏移堡垒处理。Release/Debug各9组回归通过。离线对照整体接近9.1，尚无压倒性胜利证据；具体得分、死亡和限制按验证报告列出。

赛事规则PDF已到v1.2，随包通信协议与官方练习二进制仍是v1.1。真实统一LLM及官方v1.2裁判验收未完成；昨日Ubuntu实机成绩属于9.1，不移记为9.2成绩。

## 使用9.2

```bash
cd ~/ZKXWN/versions/v9.2
python3 scripts/verify_delivery.py
bash ./scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash ./scripts/run.sh A 2
```

依次进入版本目录、核对文件、编译ROS节点、按昨日练习环境配置通信、启动A席位二阶段。需要Ubuntu22.04/ROS2 Humble/C++17/colcon/libcurl开发依赖；裁判另开终端。正式ROS域和网关以主办方要求为准。模型名必须留空，只使用官方统一网关；默认输出1024，异步默认超时15秒。

9系列沿用相同包名，切换前停止旧节点并新开终端，确认日志`HERO 9.2.0`。GitHub提交和桌面封装不会自动更新虚拟机。

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

所有版本README都补充了仓库路径、用途、构建/运行方式和证据边界。旧报告原文保留，不能把历史“已通过”当作当前新版本已验收。

## 根目录旧工程

根目录的`decision/`、`rmtc_ws/`、`scripts/`保留早期工程，不能与`versions/v9.2`混编。固定1.0源码见`versions/v1/01_程序源码/ZKXWN`；根目录旧说明见[README_RMTC_CPP.md](README_RMTC_CPP.md)。

[旧3.0项目结构导览](docs/PROJECT_STRUCTURE.md)属于历史记录，当前9.2以自身README为准。当前各版本清单记录仓库副本；桌面原始归档包保持不变。
