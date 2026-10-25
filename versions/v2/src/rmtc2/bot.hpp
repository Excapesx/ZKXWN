// rmtc2/bot.hpp —— 2.0 决策主体
//
// 与 1.0 的三个结构性差异：
//   1. 第一阶段用"定向越野"精确求解（DP），目标函数是真实得分（收集总价值），
//      不再用 exp(-到达时间) 折扣，也不再限制只访问 6 个目标；
//   2. 第二阶段把每个候选动作折算成"真实比分增量"，而不是混用经验权重；
//      资源 30 分封顶 120，对抗 15 分封顶 500，任务每项 5 分，三者同一单位比较；
//   3. 每 Tick 只做一次 BFS 距离场，所有目标查询 O(1)，
//      不再对每个候选目标各跑一次 BFS（1.0 在时间维度展开时的性能隐患）。
#pragma once

#include <set>
#include <string>
#include <vector>

#include "rmtc2/grid.hpp"
#include "rmtc2/types.hpp"

namespace rmtc2 {

struct Decision {
    Action action;
    std::string reason;
    double value = 0;
};

class Bot {
public:
    void configure(const Config& config);
    // 现场可切换的决策档位（只影响强度参数，不改变算法结构）
    void set_profile(double aggression, int hunt_radius);
    void set_plan(const Plan& plan);
    void clear_plan();
    Decision decide(const Perception& p);
    const WorldMap& map() const { return mem_; }
    bool has_plan() const { return plan_.valid; }

private:
    void prepare(const Perception& p);
    Decision stage1(const Perception& p);
    Decision stage2(const Perception& p);
    Decision explore(const Perception& p, const char* why);
    double resource_gain(const Perception& p, const Resource& r) const;
    double mission_urgency(const Perception& p) const;
    bool mission_done(const Perception& p, const std::string& id) const;
    const Mission* mission_of(const Perception& p, const char* type) const;
    double attack_value(const Perception& p, const Entity& e) const;

    Config cfg_;
    WorldMap mem_;
    HazardField hz_;
    BlockMask mask_;
    Field field_;
    Plan plan_;

    // 缓存：避免每 Tick 重跑定向越野 DP
    unsigned tour_sig_ = 0;
    int tour_tick_ = -1000;
    std::vector<int> tour_order_;
    Point last_goal_{0, 0};
    bool has_last_goal_ = false;
    std::set<std::string> seen_teams_;
    double aggression_ = 1.0;
    int hunt_radius_ = 5;
};

}  // namespace rmtc2
