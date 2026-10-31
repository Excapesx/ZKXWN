# HERO 校内9.3.1 · 指定最终版本

9.3决策策略，整合10.1.2网关/ROS接入与运行工具，并保留任务点安全原地结算修复。得分对照和未验范围见[验证报告](验证报告.md)。本版不混入10.1.x资源乘数和持续追击参数。

Ubuntu22.04 / ROS2 Humble / C++17 / CMake / colcon / Python3 / libcurl开发包。

```bash
cd ~/ZKXWN/versions/v9.3.1
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export RMTC_TEAM_NAME="待填：登记队名"
bash scripts/contest.sh
```

自然语言说明：先核对文件，再运行回归检查，编译参赛节点，设置登记队名，最后按提示填本场接入信息并启动。第一阶段只能选A；第二阶段按裁判分配席位。

正式ROS域号、席位、官方网关、本场令牌均待填，以裁判通知为准；令牌隐藏输入，不要写进仓库。ROS包/程序名仍为rmtc_bot9。LLM使用官方默认模型，关闭思考、JSON输出、512 token；不要设置个人模型。正式任务为2普通、1高价值、到达当场公开坐标，坐标由感知读取，[12,12]仅练习基准。

`decision_config.json`默认balanced。仅按裁判允许流程切换赛前已登记的decision_type，其他策略实现及参数须符合封存规则。停止程序使用Ctrl+C。

Windows离线回归及模拟通过不代替正式设备检录；赛前还需真实ROS动作回执及本场真实AI成功记录。原9.3、10.1.2保留供对照。
