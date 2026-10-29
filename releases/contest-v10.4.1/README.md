# HERO 10.4.1 精简参赛包

先阅读[参赛操作卡](参赛操作卡.md)。Ubuntu22.04/Humble：`bash scripts/build.sh`构建，`bash scripts/contest.sh`交互启动。正式域号、席位、网关与令牌均待现场填写，令牌不写文件。

保留完整决策核心、ROS节点、JSON依赖及许可证、配置、运行与交付校验工具。移出历史资料、旧版对手、离线模拟器、测试及研究CMake；因此不提供根目录CMake/test.sh，构建必须使用scripts/build.sh。完整开发包在versions/v10.4.1。

未删核心算法或安全机制。包内源文件将与完整包核对；既往18组测试针对完整包，并不等于本精简包已通过真实ROS/官方裁判/网关验收。交互启动尚需Ubuntu现场演练。详细README待用户后续要求。
