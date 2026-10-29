// rmtc3/bot.hpp —— 2.0 决策主体
//
// 第一阶段预算路线；第二阶段分数收益、距离折算及明确的档位偏好。
// 第二阶段每帧一个距离场；第一阶段重规划另算目标间距离。
#pragma once

#include <set>
#include <string>
#include <vector>

#include "rmtc3/grid.hpp"
#include "rmtc3/types.hpp"
#include "rmtc3/events.hpp"

namespace rmtc3 {

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
    // 5.0 新增：把"每 Tick 局部贪心"换成"整条路线全局规划"之后只用队首
    void plan_route(const Perception& p, int remaining);
    void invalidate_route();
    double res_points(double value) const;
    double mission_points(const Perception& p, int normalAdd, int highAdd, double valueAdd, bool reachAdd) const;
    double mission_urgency(const Perception& p) const;
    bool mission_done(const Perception& p, const std::string& id) const;
    const Mission* mission_of(const Perception& p, const char* type) const;
    int incoming_damage(const Perception& p, bool defend) const;
    double attack_value(const Perception& p, const Entity& e) const;
    double attack_action_value(const Perception& p,const std::vector<const Entity*>& targets) const;
    bool safe_step(const Perception& p,int dir) const;

    Config cfg_;
    WorldMap mem_;
    HazardField hz_;
    BlockMask mask_;
    Field field_;
    Plan plan_;
    Plan raw_plan_;
    EventTracker events_;
    int observed_tick_=-1;
    unsigned tour_generation_=0;
    int last_move_tick_=-1;
    Point last_move_from_,last_move_to_;
    std::map<std::pair<Point,Point>,int> failed_edges_;

    // 缓存：避免每 Tick 重跑定向越野 DP
    unsigned tour_sig_ = 0;
    int tour_tick_ = -1000;
    std::vector<int> tour_order_;
    // 5.0 新增：阶段二路线缓存
    std::vector<Point> route_;
    std::vector<double> route_pts_;
    unsigned route_generation_ = 0;
    int route_tick_ = -1000;
    int route_missions_ = -1;
    unsigned route_sig_ = 0;

    Point last_goal_{0, 0};
    bool has_last_goal_ = false;
    std::set<std::string> seen_teams_;
    double aggression_ = 1.0;
    int hunt_radius_ = 5;
    int route_span_ = 10;  // 单次 DP 的节点上限
};

}  // namespace rmtc3
