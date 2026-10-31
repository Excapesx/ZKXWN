# v10.1se：LLM 语义兜底实验版

程序版本 **10.1se**，从 10.1.1 复制开发。ROS 包名和可执行文件仍为 `rmtc_bot9`。

## 这版改了什么

这版不是调分版，只做 LLM 语义增强，方向是“让模型多理解任务文本，但不让它接管策略”：

1. **任务文本兜底**
   - LLM 可以额外返回 `mission_goals`；
   - 只在感知里的结构化 `missions` 为空时使用；
   - 官方结构化 `missions` 非空时，模型结果完全忽略；
   - 只接受五种官方任务类型：
     `normal_resources`、`high_resources`、`resource_value`、`effective_damage`、`reach`；
   - `reach` 必须有合法坐标；其他类型必须有正目标值；
   - 未知类型、越界坐标、非法目标直接丢弃，最多接受 8 条。

2. **Prompt 增强**
   - 请求里加入 `structured_missions`，明确告诉模型“结构化任务优先”；
   - 只有 `structured_missions` 为空时，才允许从 `mission_text` 解析任务；
   - 增加 8 种 v1.2 事件的官方名称/别名提示：
     爆炸、幻术、天使恩泽、天下无双、世界分裂、火爆辣椒、洁身自好、潘潘的神。

3. **可观测性**
   - 计划日志：`事件计划已更新：N 个事件，M 个任务候选`；
   - 25 Tick 日志新增 `mission_fallback=0/1`；
   - 只有结构化任务为空且实际使用兜底时才为 1。

4. **安全边界**
   - 不改变 `decision_type`；
   - 不新增运行时参数或环境变量；
   - 不读取全图、不读取其他队感知；
   - 模型输出仍经过 `parse_plan` 和本地动作合法性检查；
   - 结构化任务永远优先，模型不能覆盖官方任务。

## 验证

本机 CTest：

```text
16/16 tests passed
```

新增 `v101se_semantic_fallback` 覆盖：

- 五种任务类型的解析；
- 未知类型、越界坐标、非法目标被丢弃；
- 事件与任务并存；
- 坏事件不会被任务兜底掩盖；
- 空对象仍是合法空计划；
- 结构化任务优先于模型兜底；
- `configure` 清空旧兜底；
- 兜底过滤。

整局集成（练习裁判 + 真实 ROS 节点 + 本地 OpenAI 兼容服务）：

| 场景 | 结构化任务 | `mission_fallback` | 结果 |
|---|---|---:|---|
| 配置里 `missions=[]` | 空 | **1** | 模型 3 条任务被使用，0 非法动作 |
| 正常配置 | 3 条 | **0** | 官方任务优先，模型结果被忽略 |

两组都收到 HTTP 2xx，`local_ai_success=1`，没有非法动作。

## 运行

```bash
cd ~/ZKXWN/versions/v10.1se
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export RMTC_AI_GATEWAY='http://<裁判机IP>:8000'
export RMTC_TEAM_TOKEN='<本场令牌>'
unset RMTC_MODEL
bash scripts/ai_preflight.sh          # 只查网关，不耗配额
bash scripts/ai_preflight.sh --spend  # 发一次对话请求，会耗 1 次配额
bash scripts/run.sh A 2
```

## 限制

- 未连接官方统一 AI 网关；真实模型对 `mission_goals` 的遵循程度未验证。
- 未在正式 24×24 地图和真实 DDS 网络上完成整局验收。
- 这版没有证明能提高平均分；它只是为“结构化任务缺失/任务措辞变化”增加一条安全兜底。
- 兜底任务 ID 由本地生成；如果官方 `mission_completed_ids` 与结构化 `missions` 同时缺失，完成判定可能无法按 ID 对齐。本版优先保证“不覆盖官方任务”，不保证这种双重缺失场景的完成计数最优。
- 正式资格仍取决于本场本席位至少一次真实 HTTP 2xx 调用。
