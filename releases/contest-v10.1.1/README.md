# HERO 10.1.1 精简参赛包

本机10.1策略（2000伤害阈值）+官方网关请求修复+默认512输出限制，默认balanced。与versions/v10.1.1运行源码逐字节一致，不含历史对手、离线测试和完整证据

```bash
python3 scripts/verify_delivery.py
bash scripts/doctor.sh
bash scripts/build.sh
bash scripts/contest.sh
```

[参赛操作卡](参赛操作卡.md)说明本场接入信息、隐藏令牌输入和策略窗口。完整测试与来源见[开发版](../../versions/v10.1.1/README.md)。真实官方网关及AI资格待现场确认，练习45分和mock成功不能当作正式成绩或资格
