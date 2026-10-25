# 交接记录 · v0.3 整理

日期：2026-10-22

## 1. 这次整理做了什么

- 把 v0.2 源码放进 `~/ZKXWN` 仓库，保持 `decision/`、`rmtc_ws/`、`third_party/` 同级。
- 增加根目录 `README.md`、构建/运行/测试脚本。
- 修正 README 里作者机器上的旧路径：
  - `~/rmtc_practice_env/environment.bash` → `/opt/ros/humble/setup.bash`
  - `/home/xu` → `/home/excapesx`
- 补充 `libcurl4-openssl-dev` 安装说明。
- `decision_type` 不再从 ROS 参数或 `RMTC_DECISION_TYPE` 环境变量读取；只从 `RMTC_DECISION_CONFIG` 指向的 JSON 读取。
- 增加阶段防呆：
  - `stage=1` 但感知有 `missions` → 拒绝动作；
  - `stage=2` 但感知没有 `missions` → 拒绝动作。
- 增加 LLM 首次失败后的有限重试（每 25 Tick，直到成功或被 401/配额锁死）。
- `package.xml` 去掉无效的 `nlohmann_json`、`libcurl-dev` 依赖，改为 `libcurl`；JSON 使用仓库内 `third_party`。

## 2. 已验证

| 项目 | 结果 |
|---|---|
| Ubuntu 22.04 + ROS 2 Humble 编译 | 通过 |
| 本地场景测试 | 59 + 41 = 100 通过 |
| 第一阶段练习端，100 Tick | 40/40，0 非法动作 |
| 第二阶段练习端，500 Tick、50ms/Tick、B/C/D 陪练 | 42.5 分，3 个任务，0 非法动作 |
| 离线回放 | 正常输出动作 |

这些是练习环境结果，不是正式成绩。

## 3. 还没验证

- 正式 500ms/Tick 的完整第二阶段。
- 真实统一 AI 网关、正式 token、真实模型输出。
- 双机/多机 DDS、有线网络。
- Docker 镜像和主办方提交格式。
- 堡垒、辣椒等全部复杂事件。

## 4. 下一步

1. 按 `README.md` 编译。
2. 跑 `scripts/test.sh`。
3. 用练习端跑第一阶段和第二阶段。
4. 接真实 LLM 验证一次成功调用。
5. 两个人按 `docs/TEAM.md` 阅读和分工。
6. 准备技术报告、镜像、前置检录视频。

## 5. 规则提醒

- 正式 token 不要提交到 Git。
- 事件公告只能交给 LLM，不能用关键词/正则解析。
- 代码封存后不改源码/Prompt/决策参数。
- `decision_type` 只能按裁判登记流程切换。
