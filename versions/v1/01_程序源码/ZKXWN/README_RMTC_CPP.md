# 先从这里看：RMTC C++ 集成版

这份源码把队友提供的 C++ 骨架要求和之前的决策模块接成一个程序。

## 阅读顺序

1. `RMTC_CPP_集成说明.md`：本轮改动、实际运行结果和待办。
2. `rmtc_ws/src/rmtc_bot/README.md`：编译、启动及每条命令的自然语言解释。
3. `rmtc_ws/src/rmtc_bot/INTERFACES.md`：两人统一的接口。
4. `decision/LEARNING.md`：从“阵亡就等待”开始读决策代码。

## 解压后的目录

```text
源码根目录/
  decision/                 原来的标准 C++ 决策引擎
  rmtc_ws/src/rmtc_bot/      ROS 底座、地图、BFS、模型和策略适配
  third_party/nlohmann/     离线 JSON 依赖及 MIT 许可证
```

保持这三个目录的相对位置。构建 ROS 包时在 `rmtc_ws` 中操作；标准 C++ 回放不需要启动裁判。

源码交接包不含令牌、裁判配置、地图真值、编译目录和运行日志。
