# 2026-10-27 资源采集复测证据

结论和测试边界见 [完整报告](../../../../docs/RESOURCE_COLLECTION_AUDIT_2026-10-27.md)。本目录保存测试证据，不是正式策略更新。

- `analysis.json`：各可执行文件 A 侧的配对比较、地图级 bootstrap、资源诊断汇总。
- `current_*` / `parent_*`：当前 `482c107` 与直接父提交 `d717fc5` 的结果。
- `holdout_*`：第二批独立种子的结果；无此前缀为首批种子。
- `*.stdout.json`：基准程序汇总；这里自带的 A-B 是与冻结 9.1 对照，不是本报告的父版本差值。
- `*.jsonl`：逐局成绩；`*.command.json`：运行命令。
- `diagnose.cpp`、`diagnostic_*.jsonl`：只供测试使用的资源发现/拾取归因。全图只供事后诊断，不能接入参赛策略。
- `path_view.patch`：同长度最短路优先增加视野的独立候选补丁；`path_view_unit_tests.json`：5 组核心回归结果。
- `view_large.patch`、`view_cost.patch`、`route_compete.patch`：其他实验的独立补丁，均未应用至正式源码。
- `parent.patch`：从当前 bot.cpp 恢复直接父版本，用于单变量对照。
- `official500_report.json`、`official500_config.json`：500 ms/Tick 真实练习包运行结果及配置。
- `official500_communication.jsonl.gz`、`official500_frames.jsonl.gz`：原始通信和全图回放；仅为测试证据。
- `current_unit_tests.log`、`ros-build.log`：正式当前版本测试与构建记录。
- `manifest.json`：测试时 HEAD、正式 bot.cpp 哈希及证据文件校验值。

实验构建、运行和分析脚本按当时工作目录 `build/collection-audit/` 编写；复制到该目录后，在仓库根目录运行。构建前需要 `bash versions/v9.2/scripts/test.sh` 生成 CMake 静态库和对象文件。不要将各个 `.patch` 连续叠加；它们都是从当前正式策略独立派生的反事实实验。

测试没有调用真实 LLM，不能作为正式 AI 资格证明；随机图、固定基线对手和练习图均不能替代正式赛成绩。
