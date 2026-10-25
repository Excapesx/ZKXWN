// rmtc6/bot.hpp —— 2.0 决策主体
//
// 第一阶段预算路线；第二阶段分数收益、距离折算及明确的档位偏好。
// 第二阶段每帧一个距离场；第一阶段重规划另算目标间距离。
#pragma once

#include <set>
#include <string>
#include <vector>

#include "rmtc6/grid.hpp"
#include "rmtc6/types.hpp"
#include "rmtc6/events.hpp"

namespace rmtc6 {

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
    double collection_gain(const Perception& p,const std::vector<Resource>& resources) const;
    double resource_gain(const Perception& p, const Resource& r) const;
    double mission_urgency(const Perception& p) const;
    bool mission_done(const Perception& p, const std::string& id) const;
    const Mission* mission_of(const Perception& p, const char* type) const;
    int incoming_damage(const Perception& p, bool defend) const;
    double attack_value(const Perception& p, const Entity& e) const;
    double attack_action_value(const Perception& p,const std::vector<const Entity*>& targets) const;
    bool safe_step(const Perception& p,int dir) const;

    struct Sighting { Entity robot; int tick=-1; Point velocity; };
    std::map<std::string,Sighting> sightings_;
    int hunt_ticks_=0,hunt_block_until_=-1;double prior_damage_=0;
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
    Point last_goal_{0, 0};
    bool has_last_goal_ = false;
    std::set<std::string> seen_teams_;
    double aggression_ = 1.0;
    int hunt_radius_ = 5;
};

}  // namespace rmtc6
