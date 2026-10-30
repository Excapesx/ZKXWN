# HERO 10.1.1

保留10.1原决策，移入10.4.1官方网关请求与启动维护修复。不能据此宣称模拟策略强于10.1，也未确认10.1是唯一最强版本。

新增thinking.type=disabled、response_format.type=json_object，保留max_tokens=1024、stream=false，模型由统一官方网关选择；保留会话隔离、异步工作、校验与原重试策略。席位错配现在有限频告警。默认队名和启动版本为HERO-10.1.1。

在Ubuntu22.04/Humble进入本目录：

```bash
python3 scripts/verify_delivery.py
bash scripts/build.sh
bash scripts/contest.sh
```

[参赛操作卡](参赛操作卡.md)说明正式域号、席位、阶段、网关和令牌在哪里填写；未知值等待官方通知，令牌隐藏输入不写文件。ROS包/程序名仍为rmtc_bot9。详细README要求待用户后续提供。

src/下核心文件与10.1逐字节相同；未调整资源/战斗/分时参数。此前安静对手的非致命威胁折减、任务进度估值仍保留，后续应单独实验，不能混入本次接入修复。

本次验证结果见[10.1.1验证报告](10.1.1验证报告.md)与[来源](10.1.1来源说明.json)。此前10.1报告和规则保留其原版本含义，不能充当10.1.1真实接入验收。无真实ROS/官方网关/LLM资格验证。精简参赛目录为仓库releases/contest-v10.1.1。
