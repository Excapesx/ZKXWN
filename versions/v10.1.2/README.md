# HERO 校内10.1.2

基于10.1.1的任务结算修复版。任务按裁判公开missions读取：普通资源、高价值资源、到达指定坐标。任务完成由官方completedIds确认。[验证报告](验证报告.md)说明改动、同分模拟及未验证范围。

Ubuntu22.04、ROS2 Humble、C++17、CMake、colcon、Python3及libcurl开发依赖。

```bash
cd ~/ZKXWN/versions/v10.1.2
python3 scripts/verify_delivery.py
bash scripts/test.sh
bash scripts/build.sh
export RMTC_TEAM_NAME="待填：登记队名"
bash scripts/contest.sh
```

启动时按提示填写官方ROS域号、席位和阶段；第二阶段填写当场官方AI网关、本场令牌，令牌隐藏输入。模型由官方网关选择，关闭思考，JSON输出，max_tokens=512。现场仍需确认本场本席位真实AI成功调用及动作回执。任务坐标不要自行填成练习默认值。

也可在桌面目录复制到Ubuntu后进入对应目录执行同样脚本。旧10.1.1保留用于回退。
