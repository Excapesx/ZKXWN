# 10.0验证证据

`accepted_*`才是最终策略对照：三个第二阶段场景各16张图×4席位（每版192局）、第一阶段100张20×20图。`peer`为三个冻结9.3对手，`mixed`为冲锋/贪心/采集；标准事件使用理想结构化计划，无真实LLM。seed依次820110、850010、850110和850310。

`final_*`、`release_*`、`c2_*`和`holdout_*`为开发期间候选结果，并非最终10.0成绩。后续候选看过部分地图，只有对应新种子是当时未见的地图；不要把全部结果混成独立样本。`development`保留被否决的决策源码快照，防止只展示有利结果。

`tests_release.txt`及`tests_debug.txt`是最终Windows回归日志；生产节点使用ROS替身与注入HTTP测试，不等于实际DDS。`recorded_replay.json`使用历史公开感知，只能比较在旧观测下的动作，不能推算新对局得分。`historical_bag_audit.json`只读旧9.2录制的核验，不能当10.0官方实测。

`baseline_v93/source_manifest.json`记录冻结9.3原始与命名空间转换后的SHA256。新版本包清单在根文件清单.json。真实ROS/LLM/官方v1.2整局状态见本版报告。
