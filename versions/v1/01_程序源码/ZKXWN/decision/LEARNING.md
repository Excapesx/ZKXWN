# 决策模块第一课

本课先看懂三件事：输入的比赛状态是什么，函数如何判断，输出动作是什么意思。源代码已在 Windows 上独立编译验证；本目录尚未复制到 Ubuntu。

## 先理解两行代码

```cpp
DecisionEngine engine(planner, Mode::TaskFocus);
Decision result = engine.decide(state);
```

第一行是“建立一个决策器”，给它一个找路工具 `planner`，选择任务采集模式 `Mode::TaskFocus`。`DecisionEngine` 是类的名字，`engine` 是这次建立的对象名，括号里是建立它需要的参数。

第二行是“把当前比赛状态交给决策器，把返回结果存起来”。`engine.decide(state)` 调用对象的决策函数；`state` 是输入，`Decision` 是结果类型，`result` 是保存结果的变量，`=` 表示将右边的值交给左边。

自然语言连起来就是：先准备好一个能找路的任务采集决策器，每回合把当前情况交给它，取出本回合该做的动作。

## 五个演示的含义

| 演示输出 | 自然语言 |
|---|---|
| `Dead: wait` | 自己已经阵亡，这回合等待复活 |
| `Reach mission: move right` | 有未完成的到点任务，本例下一步向右 |
| `Mission done, collect: move up` | 任务已完成，转向本例上方的资源 |
| `Cannot dodge this tick's shot: defend` | 本例只剩 15 HP，预计挨一发普通攻击，防御比随后移动更有利 |
| `Safe ordinary attack: attack right` | 本例有可承受风险且可获有效伤害的右侧目标，进攻模式选择开火 |

这些坐标和敌人都是人为构造的练习，不代表正式地图、对手行为或保证能拿到的分数。

原因码 `dead_wait` 表示阵亡等待，`task_reach` 表示推进到点任务，`resource` 表示采资源，`defend_threat` 表示应对攻击威胁，`attack_score` 表示攻击收益。`goal=(6,3)` 是这个例子的目标坐标。

## 阵亡分支怎么读

在 `src/engine.cpp` 的 `decide` 函数中有：

```cpp
if (!s.self.alive || s.self.hp <= 0) {
    result.reason = "dead_wait";
    return save(result, "");
}
```

- `if (...)`：如果括号里的条件成立，就执行大括号中的内容。
- `s.self.alive`：从当前状态 `s` 中读取“自己是否存活”。点号表示逐层访问成员。
- `!`：取反，`!alive` 就是“不存活”。
- `||`：或者，左右任意一个条件成立就满足。
- `s.self.hp <= 0`：自己的血量小于或等于 0。
- `result.reason = "dead_wait"`：给这个结果记上“阵亡等待”的说明。
- `return`：结束本次函数，把结果交回调用者；`save` 同时缓存这回合的结果，防止重复处理。

这里返回的动作为什么是等待：结果刚创建时默认就是等待，并且在此分支前已经标记为可以提交；阵亡分支不再改成其他动作。

整段意思是：如果裁判说自己不存活，或者血量已归零，就返回等待，并告诉底座原因是阵亡。

## 后续在 Ubuntu 运行时的四条命令

前提：把本模块复制进 Ubuntu，并在终端进入其文件夹。以下是操作说明，不是已经在 Ubuntu 执行成功的记录。

```bash
cmake -S . -B build -DBUILD_TESTING=ON
```

`cmake` 是构建配置工具。`-S .` 表示源码在当前文件夹，`.` 是当前目录；`-B build` 表示将构建产物放进 `build` 文件夹；`-D变量=值` 是设置 CMake 变量，`BUILD_TESTING=ON` 表示启用测试。

自然语言：读取当前项目，准备编译用的文件，并开启场景验证。

```bash
cmake --build build -j 2
```

`--build build` 表示执行 `build` 目录中的构建任务；`-j 2` 表示最多并行两个编译任务。

自然语言：把 C++ 源码编译成能运行的程序。

```bash
ctest --test-dir build --output-on-failure
```

`ctest` 是 CMake 配套的测试工具。`--test-dir build` 表示到构建目录里找测试；`--output-on-failure` 表示失败时显示详细输出。

自然语言：检查构造的场景是否得到预期动作，失败时告诉我们具体哪里不一致。

```bash
./build/decision_demo
```

`./` 表示从当前目录出发，`build/decision_demo` 是刚编译的演示程序。

自然语言：运行五个小场景，打印动作与原因。它不会向裁判发消息。

## 本轮学习目标

先能解释阵亡为什么等待，再能说明 15 HP 案例为什么防御。随后再看“任务完成后转向资源”，最后学习不同模式为什么会选择不同动作。每次只读一条分支，再用演示或场景验证观察它。
