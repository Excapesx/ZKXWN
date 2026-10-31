# ZKXWN · RMTC

默认参赛程序是 `versions/v10.1.2`，使用 C++17 和 ROS 2 Humble。需要 Ubuntu 22.04、CMake、colcon、Python 3、Fast DDS 和 libcurl 开发包

```bash
cd ~/ZKXWN/versions/v10.1.2
bash scripts/test.sh              # 回归测试
python3 scripts/verify_delivery.py
bash scripts/build.sh             # 编译 ROS 节点
bash scripts/contest.sh           # 按提示输入本场参数
```

可先用 `export RMTC_TEAM_NAME="登记队名"` 设置队名。启动时输入官方 ROS 域号、A/B/C/D 席位与比赛阶段；第二阶段另输入官方 AI 网关和本场令牌，令牌隐藏输入。第一阶段只允许 A，第二阶段每场更换对应席位和令牌。`decision_config.json` 默认 `balanced`，需要切换时只按裁判允许的窗口和登记流程修改 `decision_type`

`src/rmtc9` 包含地图、BFS、采集和战斗策略；`ros2_ws/src/rmtc_bot9` 负责 ROS 收发和异步 AI；`scripts` 是运行工具；`tests` 是回归测试。历史源码保留在 `versions`，沿革见 [CHANGELOG.md](CHANGELOG.md)，默认版本指针见 [FINAL_VERSION](FINAL_VERSION)

模型由官方网关选择，默认关闭思考、JSON 输出、`max_tokens=512`。正式第二阶段须确认本场本席位真实 AI 成功调用，并通过动作回执核对接入。现场只填裁判发放的网关与令牌，个人 DeepSeek Key 仅用于练习

`versions/v10.1se` 是 LLM 语义兜底实验候选：模型可在结构化任务为空时返回 `mission_goals`；结构化任务始终优先。它尚未替换比赛默认版 `v10.1.2`

2026-10-31 根据 v1.2 同分排名规则及大样本对照，改选 v10.1.2：模拟前二比例更高，均分与资源采集没有明显突破。此轮未验证真实 LLM 完整对局，上场前仍须完成现场接入检查；封存后按官方流程操作。v9.3.1 等历史源码继续保留。
