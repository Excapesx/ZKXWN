# HERO 10.3 第二轮研究候选

本版针对10.2的资源机会与短路线评估做修正。最后边界修正后，新80地图混合模拟平均40.933分，10.2为40.913、10.1为41.017；差异及同场对照区间均跨零，没有稳定提升证据。当前不建议用它替换10.1。修改前测试完整保留并单列。

改动集中在src/rmtc9/opportunity_evaluator.hpp、short_routes.hpp及bot.cpp：按公开可达距离与其他资源选择估计竞争，合并途中资源/任务，在不同采集结果上处理封顶，并避免虚构或重复扣机会损失。分时乘数与第一轮计分规则保留。round2_policy.hpp封存本轮默认分支B3；正式ROS构建不提供运行时切换这些实验参数。

阅读顺序：更新日志.md → 修复与验证报告.md → evidence/diagnosis/机制与案例.md。完整逐帧原始日志保存在HERO/work/round2_v103_20261028，随本版附公开帧摘录、开发快照、源码差异、命令、参数、新地图逐局结果和构建测试日志。

## 本地检查与启动

```bash
cd ~/ZKXWN/versions/v10.3
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export ROS_DOMAIN_ID=27 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
bash scripts/run.sh A 2
```

verify_delivery检查文件摘要；test运行核心回归；build在Ubuntu编译ROS功能包；export使裁判与机器人使用相同ROS通信域；run启动A席第二阶段机器人。本轮未在Ubuntu执行这些步骤，真实ROS和官方裁判待用户实操。

## 复现离线对照

```bash
bash scripts/bench.sh --a 10.3 --b 10.1 --mode seat --field 778 --events standard --maps 16 --seed 103290000 --width 18 --height 18 --vision 4 --threads 2 --output replay.jsonl
```

其他场景与冻结的10.2对照命令见evidence/holdout_v2/batch.json。CMake实验：RMTC_ABLATION_LEVEL固定2；RMTC_ROUND2_LEVEL=0/1/2/3依次为B0/B1/B2/B3，RMTC_ENABLE_OPPORTUNITY_COST=OFF仅用于B3无成本对照。推荐为每个实验使用独立构建目录，勿覆盖旧版。第一轮的去固定阶段分支不属于本轮结论。

本目录是尚未提交的10.3.0候选，Git HEAD仍指向旧提交；verify_delivery显示git_clean=false符合待审阅状态。历史10.2说明只作历史参考。真实ROS、官方整局、真实LLM及正式任务资格未验。本轮未生成桌面包或ZIP，未提交或推送。
