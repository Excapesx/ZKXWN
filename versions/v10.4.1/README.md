# 校内10.4.1 网关请求适配（2026-10-29）

基于桌面校内10.4维护版，保留原版策略和此前工程修复。此次未改变资源、战斗、路线或分时权重。

用户转发的最新官方答疑确认：网关除model外直接转发请求字段，不主动关闭思考。请求顶层新增thinking.type=disabled和response_format.type=json_object。保留max_tokens=1024，stream=false，以及原有JSON提示、响应结构验证、跨场取消、重试和配额处理。

网关控制model，客户端继续省略model，不提供自由选模型入口。POST /v1/chat/completions及本场Bearer令牌认证保持原实现；不在文档或日志中写入令牌。没有新增temperature等未经验证的参数。

JSON模式只能约束输出形式，不能保证事件语义正确；仍须通过本地计划校验。关闭思考可能缩短响应时间，但本次未连接真实网关，不能给出实际延迟或正式任务资格结论。

版本10.4.1；来源为原提交6f87322及桌面维护修改。本候选尚未提交或推送仓库。桌面旧版本保持原状。验证结果见本目录网关适配验证报告.md。

---

> 2026-10-29：桌面维护修复版，详见[修复说明](桌面修复说明_20261029.md)。默认决策核心未改；工程修复及测试扩展尚未提交仓库，勿视为原6f87322的逐文件副本。

# HERO 10.4 第三轮分时验证候选

结论为证据不足，默认保留10.3原乘数C0。10.4增加分时审计、同状态离线探针和可复现实验，默认参赛决策与10.3一致，没有稳定强度提升证据。

第三轮只比较资源/追击/直接坐标任务的数值乘数：C0保留、C1偏离1缩半、C2置1；结构性时间开关、截止时间、安全、第一轮计分、第二轮路线和机会损失均保留。C_R/C_P/C_M分别在C2上仅恢复一类原乘数。C2不是完全取消时间行为。

开发30图×4席C0/C1/C2为40.878/40.686/40.648；独立80图×4席为40.752/40.791/40.833，方向不一致且差值区间包含0。只保留资源的C_R独立均分40.760，其微升只来自一张图。尚不据此更换默认。详见修复与验证报告.md、evidence/分时影响审计.md和evidence/analysis.json。

10.3基线来自经清单核对的本地候选文件系统，冻结核心在baseline_v103；原版本目录及实验分支均保留。10.4默认使用C0原乘数，完整来源、源码摘要和测试数据随包保存。

## 本地构建与运行

```bash
cd ~/ZKXWN/versions/v10.4
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

依次核对文件摘要、运行回归、编译ROS节点、设置与裁判一致的通信域，再启动A席第二阶段。Ubuntu和真实ROS步骤本轮未执行。

## 离线复现

```bash
bash scripts/bench.sh --a C0 --b 10.3 --mode seat --field 778 --events standard --maps 16 --seed 104280000 --width 18 --height 18 --vision 4 --threads 2 --output C0.jsonl
bash scripts/bench.sh --a C1 --b 10.3 --mode seat --field 778 --events standard --maps 16 --seed 104280000 --width 18 --height 18 --vision 4 --threads 2 --output C1.jsonl
bash scripts/bench.sh --a C2 --b 10.3 --mode seat --field 778 --events standard --maps 16 --seed 104280000 --width 18 --height 18 --vision 4 --threads 2 --output C2.jsonl
```

其他场景命令、种子、快照与摘要见evidence。PhaseProbe只编入离线工具；ROS生产构建明确关闭探针，没有新增ROS参数、环境变量或模型API。普通arena不会对每帧执行三次复制探针；rmtc_phase_trace才会这样做。完整开发轨迹以raw_traces.zip存于evidence/traces，未压缩原件保存在HERO/work/round3_v104_20261028。

Windows Release/Debug各17组通过，含161条本轮数值/边界断言和生产作用域检查。真实ROS/DDS、官方裁判、真实LLM及正式任务资格仍待用户实操。本轮未创建桌面包、提交或推送。


## 现场操作入口

见[参赛操作卡](参赛操作卡.md)，已构建后执行`bash scripts/contest.sh`逐项填写本场参数。精简包在仓库`releases/contest-v10.4.1`；详细README要求待用户提供。
