# ZKXWN · RMTC校内赛

当前交接版：**[校内9.1](versions/v9.1/README.md)**（程序9.1.0，ROS包名仍为`rmtc_bot9`）。1.0至9.0完整保留。

- [9.1更新日志](versions/v9.1/更新日志.md)
- [9.1修复与验证报告](versions/v9.1/修复与验证报告.md)
- [正式验收与技术报告模板](versions/v9.1/正式验收与技术报告模板.md)
- [程序逻辑与学习指南](versions/v9.1/程序逻辑与学习指南.md)
- [全部版本及证据边界](docs/VERSION_HISTORY.md)
- [官方v1.1规则及协议](versions/v9.1/02_官方规则_v1.1_当前/) · [官方练习包](versions/v9.1/03_官方练习包_v1.1/)

## 使用9.1

```bash
cd ~/ZKXWN/versions/v9.1
python3 scripts/verify_delivery.py
bash ./scripts/build.sh
bash ./scripts/run.sh A 2
```

依次进入9.1、只读核对交付文件和实际位置、编译ROS节点、启动A席位第二阶段。需要Ubuntu22.04、ROS2 Humble、C++17、colcon和libcurl开发依赖；裁判另行启动。模型名必须留空，网关和令牌只在运行环境设置；正式比赛只接主办方统一网关。

9.1按群答疑拒绝自选模型，请求始终不带`model`；输出预算默认1024，显式0也回退1024，负数报错。保留9.0决策与战斗核心、异步模型及默认15秒超时。关闭思考字段和“300k”单位未知，未猜参数；资源公平性问题尚未获答。

9.1本轮只做Windows编译、静态差异和交付清单核对，未新增或运行回归、对战。9.0的Release/Debug各4449项属于历史结果，不能当作9.1已通过。Ubuntu真实ROS/libcurl、官方500Tick、真实模型资格和复活保护边界仍待现场验收；请按模板留实际路径、完整提交号和日志。

同系列9.0/9.1包名相同，切换前停止旧节点并使用全新终端。仓库上传与桌面封装不会自动更新虚拟机。

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

所有版本README都补充了仓库路径、用途、构建/运行方式和证据边界。旧报告原文保留，不能把历史“已通过”当作当前新版本已验收。

## 根目录旧工程

根目录的`decision/`、`rmtc_ws/`、`scripts/`保留早期工程，不能与`versions/v9.1`混编。固定1.0源码见`versions/v1/01_程序源码/ZKXWN`；根目录旧说明见[README_RMTC_CPP.md](README_RMTC_CPP.md)。

[旧3.0项目结构导览](docs/PROJECT_STRUCTURE.md)属于历史记录，当前9.1以自身README为准。当前各版本清单记录仓库副本；桌面原始归档包保持不变。
