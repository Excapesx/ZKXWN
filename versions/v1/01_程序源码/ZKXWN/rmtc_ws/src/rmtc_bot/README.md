# RMTC C++ 底座与决策集成版 v0.2

给你和队友的入口。已有的 `decision/` 是“大脑”，这里补齐收消息、解码、地图记忆、BFS 寻路、后台模型请求和 ROS 节点。两阶段共用一套程序，不复制两份决策代码。

## 一、目前能做什么

- 接收本席位公开感知，把实体的 9×9 相对位置转换为绝对坐标。
- 保存合法视野记忆；看见资源消失会删除，阵亡时不会把空视野当成空地图；移动失败不会永久记墙。
- BFS 绕障碍、避开当前车辆；模型给出事件时段后，按时间规划绕行或等待，临时墙到期重新观察。
- 第一阶段选择资源和探索目标；第二阶段接入原决策引擎，综合任务、采集、攻击、防御和生存风险。
- 同 Tick 复用结果，旧 Tick 丢弃，新场次清空记忆与模型结果，终场帧不再发送动作。
- libcurl 后台线程调用统一网关，最多一条在途请求和一条待处理请求；主线程不等待网络。默认关闭 LLM。
- 模型只给结构化建议，主程序校验地图边界、时间和动作合法性，再生成带当前 Tick/场次的动作。

这是一版可以继续开发的实现。尚未覆盖堡垒、辣椒等全部复杂事件及对手行为预测；不能根据少量场景或一场练习推断正式成绩。事件公告没有模型回复时，程序不会自行用关键词或正则解释公告。

## 二、文件怎么分工

| 负责人 | 文件 | 通俗解释 |
|---|---|---|
| 队友／底座 | `perception.cpp`、`ros_io.cpp`、`memory.cpp`、`planner.cpp` | 接收、看懂格式、记住地图、计算路线 |
| 你／决策 | 仓库根目录 `decision/src/engine.cpp`、`policy.cpp`、`policies/` | 在候选动作里选择当前收益和风险合适的动作 |
| 你／模型 | `llm_client.cpp`、`curl_transport.cpp` | 请求模型理解公告；把返回值交给决策 |
| 两人先商量 | `types.hpp`、`policy.hpp`、`runtime.hpp`、配置和 CMake | 双方连接口，改动后一起同步 |

`combat.cpp` 只保留小型诊断函数，战斗评估统一在原决策引擎中，避免两份规则互相冲突。文件中的 OWNER A/B 指文档分工，不是比赛席位。

## 三、Ubuntu 编译：每条命令的含义

先把源码包解压到一个新目录，例如 `~/ZKXWN`。解压后，该目录应同时有 `decision/`、`rmtc_ws/`、`third_party/`。只复制 `rmtc_bot/` 会缺少决策模块。

如果这是新机器，先安装 HTTP 依赖（`nlohmann/json` 已经放在 `third_party/`，不用另外装）：

```bash
sudo apt install libcurl4-openssl-dev
```

### 1. 加载 ROS 环境

```bash
source /opt/ros/humble/setup.bash
```

自然语言：让当前终端知道 ROS 命令和库在哪里。

- `source`：把脚本里的设置应用到当前终端。
- `~`：你的家目录，当前机器是 `/home/excapesx`。
- 每个新终端都要加载一次；不要再用旧的 `~/rmtc_practice_env/environment.bash`，这个文件在当前机器上不存在。

### 2. 进入工作空间

```bash
cd ~/ZKXWN/rmtc_ws
```

自然语言：把当前操作位置切换到本项目的 ROS 工作空间。`cd` 是切换目录；请把路径换成你的实际解压目录。

### 3. 编译

```bash
colcon build --symlink-install --packages-select rmtc_bot --cmake-args -DCMAKE_BUILD_TYPE=Release
```

自然语言：把 C++ 源码编译成能够运行的 ROS 程序。

- `colcon build`：ROS 工作空间的构建工具和构建动作。
- `--symlink-install`：对适用的安装文件建立链接，方便开发。
- `--packages-select rmtc_bot`：只构建这个包。
- `--cmake-args`：后面参数交给 CMake。
- `-D`：设置 CMake 变量；`CMAKE_BUILD_TYPE=Release` 选择优化构建。

### 4. 加载刚编译的程序

```bash
source install/setup.bash
```

自然语言：告诉当前终端去哪里寻找刚编译的 `rmtc_bot`。`install/setup.bash` 是构建产生的环境脚本。

### 5. 启动自己的第一阶段程序

```bash
ros2 run rmtc_bot rmtc_bot --ros-args -p team:=A -p stage:=1 -p llm_enabled:=false
```

自然语言：以 A 席位参加第一阶段，先用本地算法控制车辆。

- `ros2 run`：启动某个包里的可执行程序。
- 第一个 `rmtc_bot` 是包名，第二个是程序名。
- `--ros-args`：后面按 ROS 参数解释。
- 每个 `-p 名称:=取值` 设置一个参数；`team:=A` 指定自己的席位，`stage:=1` 明确第一阶段，`llm_enabled:=false` 关闭网络模型。
- 第一阶段默认 100 Tick，第二阶段默认 500 Tick；不要根据任务列表是否为空猜阶段。

裁判需在另一个终端启动。自己的 A 程序运行时，不能再启动另一辆 A 参考车。尚未收到感知时，节点安静等待是正常现象；按 `Ctrl+C` 停止自己这个节点。

第二阶段对应命令：

```bash
ros2 run rmtc_bot rmtc_bot --ros-args -p team:=A -p stage:=2 -p llm_enabled:=false
```

只把阶段参数改为 2；这是本地练习。真实 AI 成功资格需要之后单独接统一网关验证。

> 阶段参数必须显式写对。代码现在会做防呆：如果 `stage=1` 却收到任务列表，或 `stage=2` 却没有任务列表，会拒绝该帧动作并记录原因，不会静默用错策略。
>
> `decision_type` 只允许通过 `RMTC_DECISION_CONFIG` 指向的 `decision_config.json` 修改；不要再使用 `-p decision_type:=...` 或 `RMTC_DECISION_TYPE`（这两个入口已移除）。

## 四、没有 ROS 时，先练决策

在源码根目录执行：

```bash
cmake -S rmtc_ws/src/rmtc_bot -B build-core -DRMTC_WITH_ROS=OFF -DRMTC_WITH_CURL=OFF
cmake --build build-core
./build-core/rmtc_replay rmtc_ws/src/rmtc_bot/test/fixtures/perception_stage1.json 1
```

第一条选源码目录 `-S`、构建目录 `-B`，关闭 ROS 与真实 HTTP；第二条编译；第三条把一份假感知交给完整本地决策流程，最后的 `1` 表示第一阶段。预期输出是本场 Tick 0 向右移动的 JSON。

假数据放在 `test/fixtures/`，不含裁判全图。修改假数据练习不能替代真实比赛回执。

按骨架验收要求运行场景检查：

```bash
ctest --test-dir build-core --output-on-failure
```

`ctest` 运行构建时生成的检查程序；`--test-dir` 指定构建目录；`--output-on-failure` 只在失败时打印详细内容。结果见仓库根目录 `RMTC_CPP_集成说明.md`。

## 五、模型设置

令牌只从环境变量 `RMTC_TEAM_TOKEN` 读取，不作为 ROS 参数，不写进配置、源码或日志。网关用 `RMTC_AI_GATEWAY`，模型名用 `RMTC_MODEL`，输出上限用 `RMTC_MAX_TOKENS`；显式设置 `RMTC_LLM_ENABLED=1` 才启用。模型和输出限制按主办方赛前公布值配置，本地默认输出上限 0 表示省略字段，不代表已知正式限制。现场使用主办方给的值，不把任意外部服务当统一网关。

`RMTC_HTTP_TIMEOUT_MS` 控制单次网络最长等待，默认 2500ms；这是后台请求超时，不是每 Tick 的计算时间。401 或配额耗尽停止本场继续请求；502/504、明确的限流最多三次尝试。HTTP 成功计数仅用于本地诊断，正式资格以裁判为准。

模型返回示例见 `test/fixtures/llm_response.json`。矩形端点包含在区域内，时间采用 `[开始,结束)`；十字线用 `cross`；分裂墙用 `wall:true`；幻术等使用 `effects`。这些结构来自模型理解后的校验，不是公开感知里凭空增加的字段。

## 六、已登记的决策类型

启动时的 `decision_type` 支持 `balanced`（原任务采集型）、`fight`（进攻型）、`collect`（原生存型）。示例配置只有 `decision_type`，通过 `RMTC_DECISION_CONFIG` 指定文件路径后在启动时读取。没有运行中热更新、人工逐 Tick 操作或任意调参接口。

## 七、需要继续验证

整场裁判练习、长时间运行、真实统一网关、全部事件、多机器 DDS 和最终提交格式分别记录，不能相互替代。当前验收证据和未完成项以根目录集成说明为准。
