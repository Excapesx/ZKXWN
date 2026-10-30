# HERO 10.1.1 · 最终参赛组合

本版整合已经测试的“本机10.1策略 + 网关与启动修复 + 默认512输出限制”，默认balanced。完整开发版在本目录，精简参赛包在[releases/contest-v10.1.1](../../releases/contest-v10.1.1/README.md)

相较仓库原接入版10.1.1，本版保留本机主力的2000伤害阈值：对抗分达标后，仍考虑超额有效伤害在同分排名中的价值。128张第二阶段地图、四席轮换的1024场模拟中，均分40.9612对40.9519，资源价值同为52.03125；保守前二比例67.38%对64.06%，平均死亡多0.8359次。收益主要在同分排名，采集瓶颈没有因此解决，也不保证对所有对手更强

## 先编译，再启动

需要Ubuntu22.04、ROS2 Humble、C++17、CMake、colcon、Python3及libcurl开发依赖

```bash
cd ~/ZKXWN/versions/v10.1.1
python3 scripts/verify_delivery.py
bash scripts/doctor.sh
bash scripts/build.sh
bash scripts/contest.sh
```

contest.sh依次读取本场ROS_DOMAIN_ID、席位、阶段，第二阶段再读取官方网关和隐藏输入的令牌。不会保存令牌，也不会自动安装依赖。运行标识HERO-10.1.1，ROS包与程序名仍为rmtc_bot9

第一阶段100ms×100Tick，第二阶段500ms×500Tick。脚本清理旧模型/网关/令牌/策略配置环境，使用本目录decision_config.json。下一场Ctrl+C停止后按新席位和新令牌重新启动

## AI和决策配置

- 模型由官方网关统一为deepseek-flash，请求省略model；清空RMTC_MODEL，不要另外指定模型
- thinking.type=disabled、response_format.type=json_object、max_tokens=512、stream=false
- 公告变化触发请求，保留90次计划预算；C++每Tick决策，异步调用不阻塞动作窗
- 根目录decision_config.json仅含decision_type，默认balanced。balanced/collect/fight赛前登记后按最新官方通知仅在允许窗口修改，按流程重启；不在单场中改源码、Prompt或其他参数

512依据参赛手册及群内示例，没有真实模型的512/1024对照。网关没有字段白名单不能直接推出1024违规。JSON模式也不能保证不会截断，仍需解析校验和旧计划回退

## 测试与证据

```bash
bash scripts/test.sh
```

保留原14组回归，新增v1011_gateway_budget：500Tick持续公告、正常JSON、坏JSON、401、请求字段及动作连续性。精简包只保留运行所需文件，不含测试和历史基线

- [最终组合与测试报告](最终组合与测试报告.md)：逐场数据、规则依据及局限
- [当前来源说明](10.1.1来源说明.json)：18个策略文件哈希及上游来源
- [新实验原始证据](evidence/final_selection_20261030)：128图结果、设计、来源、HTTP与ROS审计
- [正式整合记录](evidence/final_release_20261030)：本次构建与回归日志
- [历史接入版说明](历史接入版说明)：上游旧560阈值版的来源和结果，仅供历史核对

原组合候选在官方v1.1练习裁判上两阶段40/45分，动作/回执齐全、非法动作0，使用mock AI。正式资格、真实模型延迟/事件理解、现场异机DDS及完整v1.2整场仍需联调。Ready、能移动和mock成功不能证明正式AI资格，缺资格会使任务分归零

正式封存时对应实际代码目录、Git提交和二进制。verify_delivery.py校验源码清单，run_identity.py打印启动摘要，它们都不会单独证明二进制与源码一致

## 复跑选型实验

完整仓库可运行 `python3 scripts/reproduce_selection.py`，从当前策略和冻结560阈值基线源码重新构建，在仓库build目录输出新记录。默认每场景32图；`--maps 1`仅快速检查，不替代128图统计。工具不连接ROS或AI，不能用于正式事件输入。原始1024场记录仍见evidence/final_selection_20261030

文件清单严格核对源码。仅根目录decision_config.json允许按已登记枚举改变decision_type，检查工具会报告配置是否变化；它不会替你取得现场切换许可。新增键或未登记枚举仍拒绝启动
