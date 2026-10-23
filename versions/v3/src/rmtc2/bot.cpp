// rmtc2/bot.cpp
#include "rmtc2/bot.hpp"
#include "rmtc2/combat.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace rmtc2 {
namespace {

int dir_between(Point a, Point b) {
    if (a.x == b.x && b.y < a.y) return 0;
    if (a.y == b.y && b.x > a.x) return 1;
    if (a.x == b.x && b.y > a.y) return 2;
    if (a.y == b.y && b.x < a.x) return 3;
    return -1;
}

unsigned resource_signature(const std::map<std::string, Resource>& res) {
    unsigned h = 2166136261u;
    for (const auto& kv : res) {
        h = (h ^ static_cast<unsigned>(std::hash<std::string>{}(kv.first))) * 16777619u;
        h = (h ^ static_cast<unsigned>(kv.second.pos.x * 131 + kv.second.pos.y)) * 16777619u;
    }
    return h;
}

struct Goal {
    Point pos;
    double score = 0;
    std::string reason;
};

}  // namespace

void Bot::configure(const Config& config) {
    cfg_ = config;
    if (cfg_.maxTicks <= 0) cfg_.maxTicks = cfg_.stage == 1 ? 100 : 500;
    mem_.reset(1, 1);
    plan_ = Plan{};
    tour_tick_ = -1000;
    has_last_goal_ = false;
}

void Bot::set_profile(double aggression, int hunt_radius) {
    aggression_ = std::max(0.2, std::min(2.5, aggression));
    hunt_radius_ = std::max(1, std::min(10, hunt_radius));
}

void Bot::set_plan(const Plan& plan) {
    if (!plan.valid) return;
    plan_ = plan;
}

void Bot::clear_plan() { plan_ = Plan{}; }

void Bot::prepare(const Perception& p) {
    const bool structural = mem_.update(p);
    if (structural) {
        tour_tick_ = -1000;
        tour_order_.clear();
        has_last_goal_ = false;
        seen_teams_.clear();
    }
    if (!p.team.empty()) seen_teams_.insert(p.team);
    for (const auto& e : p.entities) {
        if (e.type == EntityType::Robot && !e.team.empty()) seen_teams_.insert(e.team);
    }
    if (plan_.valid && plan_.session != p.session) plan_ = Plan{};

    const int w = mem_.width();
    const int h = mem_.height();
    hz_.reset(w, h);
    if (plan_.valid) hz_.apply(plan_);

    mask_.reset(w, h);
    for (const auto& wall : mem_.walls()) mask_.set(wall, true);
    // 未来 3 Tick 内会禁行/致死的格子，规划时直接绕开
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Point c{x, y};
            for (int t = p.tick; t <= p.tick + 3; ++t) {
                if (hz_.blockedAt(c, t)) {
                    mask_.set(c, true);
                    break;
                }
            }
        }
    }
    // 视野内其它车辆占位（移动阶段开始时占格会失败）
    for (const auto& e : p.entities) {
        if (e.type == EntityType::Robot && e.hp > 0 && e.team != p.team) mask_.set(e.pos, true);
    }
    mask_.set(p.self,false); // permit routes out of a hazardous origin
    const int remaining = std::max(0, cfg_.maxTicks - p.tick);
    field_ = bfs(mask_, p.self, remaining);
}

Decision Bot::decide(const Perception& p) {
    if (!p.valid()) return {Action::wait(), "invalid_perception", 0};
    prepare(p);
    if (!p.alive || p.hp <= 0) return {Action::wait(), "dead_wait", 0};
    if (cfg_.maxTicks <= p.tick) return {Action::wait(), "finished", 0};
    return cfg_.stage == 1 ? stage1(p) : stage2(p);
}

// ---------------------------------------------------------------------------
// 第一阶段：预算内最大化收集价值
// ---------------------------------------------------------------------------
Decision Bot::stage1(const Perception& p) {
    const int remaining = cfg_.maxTicks - p.tick;

    std::vector<Resource> nodes;
    std::vector<int> start;
    for (const auto& kv : mem_.resources()) {
        const int d = field_.at(kv.second.pos);
        if (d >= 0 && d <= remaining && kv.second.value > 0) {
            nodes.push_back(kv.second);
            start.push_back(d);
        }
    }
    if (nodes.empty()) return explore(p, "stage1_explore");

    const int n = static_cast<int>(nodes.size());
    const unsigned sig = resource_signature(mem_.resources());
    const bool reusable = (sig == tour_sig_) && (p.tick - tour_tick_ < 4) &&
                          (static_cast<int>(tour_order_.size()) <= n) && !tour_order_.empty();
    if (!reusable) {
        std::vector<std::vector<int>> between(n, std::vector<int>(n, -1));
        for (int i = 0; i < n; ++i) {
            const Field f = bfs(mask_, nodes[static_cast<std::size_t>(i)].pos, remaining);
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                between[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                    f.at(nodes[static_cast<std::size_t>(j)].pos);
            }
        }
        std::vector<double> values(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) values[static_cast<std::size_t>(i)] = nodes[static_cast<std::size_t>(i)].value;
        const Tour tour = solve_tour(start, between, values, remaining);
        tour_order_ = tour.order;
        tour_sig_ = sig;
        tour_tick_ = p.tick;
        if (tour_order_.empty()) return explore(p, "stage1_no_tour");
    }

    for (int idx : tour_order_) {
        if (idx < 0 || idx >= n) continue;
        const Point goal = nodes[static_cast<std::size_t>(idx)].pos;
        const int d = field_.at(goal);
        if (d <= 0) continue;  // 已站上去或不可达
        const int8_t dir = field_.firstAt(goal);
        if (dir < 0) continue;
        last_goal_ = goal;
        has_last_goal_ = true;
        return {Action::move(kDirX[dir], kDirY[dir]), "stage1_tour", static_cast<double>(d)};
    }
    return explore(p, "stage1_tour_done");
}

// ---------------------------------------------------------------------------
// 第二阶段：资源 / 任务 / 对抗统一折算成比分增量
// ---------------------------------------------------------------------------
bool Bot::mission_done(const Perception& p, const std::string& id) const {
    return std::find(p.completedIds.begin(), p.completedIds.end(), id) != p.completedIds.end();
}

const Mission* Bot::mission_of(const Perception& p, const char* type) const {
    for (const auto& m : p.missions) {
        if (m.type == type) return &m;
    }
    return nullptr;
}

double Bot::mission_urgency(const Perception& p) const {
    const int remaining = cfg_.maxTicks - p.tick;
    bool undone = false;
    for (const auto& m : p.missions) {
        if (!mission_done(p, m.id)) undone = true;
    }
    if (!undone) return 1.0;
    if (remaining < 90) return 2.6;
    if (remaining < 180) return 1.8;
    return 1.0;
}

double Bot::resource_gain(const Perception& p, const Resource& r) const {
    const double ureg = mission_urgency(p);
    const double r2 = p.resourceValue;
    const double capLeft = std::max(0.0, 120.0 - r2);
    // 场上人少时资源总量够分，120 封顶可达 -> 资源边际价值更高
    const double scarcity = seen_teams_.size() <= 2 ? 1.25 : 1.0;
    double gain = capLeft > 0 ? scarcity * 0.25 * std::min<double>(r.value, capLeft) : 0.002 * r.value;

    const Mission* mn = mission_of(p, "normal_resources");
    if (mn && !mission_done(p, mn->id) && r.value < 20) {
        const int collected = mem_.normalCollected();
        const double need = mn->target - collected;
        if (need > 0) gain += ureg * (need <= 1.0 ? 5.0 : 2.0);
    }
    const Mission* mh = mission_of(p, "high_resources");
    if (mh && !mission_done(p, mh->id) && r.value >= 20) gain += ureg * 7.0;

    const Mission* mv = mission_of(p, "resource_value");
    if (mv && !mission_done(p, mv->id) && r2 < mv->target) {
        gain += ureg * 5.0 * std::min<double>(r.value, mv->target - r2) / std::max(1.0, mv->target - r2);
    }
    return gain;
}

double Bot::attack_value(const Perception& p, const Entity& e) const {
    if (e.hp <= 0 || e.protectedSpawn) return -1;
    const double d = p.effectiveDamage;

    if (plan_.valid && plan_.reflect.active(p.tick) && e.defending) {
        // 目标防御 + 反伤：本次伤害为 0，自己吃反伤
        const double loss = std::min<double>(plan_.reflectDamage, p.hp);
        return -0.03 * std::min(60.0, d) * (loss >= p.hp ? 1.0 : 0.2) - 0.05;
    }

    int base = e.defending ? 10 : 20;
    // The plan describes OUR vulnerability; do not double damage to an arbitrary enemy.
    const bool kill = base >= e.hp;

    const auto solo=resolve_damage_batch(e.hp,{{p.team,static_cast<double>(base),true}});
    std::vector<DamageClaim> claims{{p.team,static_cast<double>(base),true}};
    for(const auto& rival:p.entities) {
        if(rival.type!=EntityType::Robot || rival.hp<=0 || rival.protectedSpawn ||
           rival.team==p.team || rival.team==e.team) continue;
        const int distance=manhattan(rival.pos,e.pos);
        if(distance<1 || distance>2 || dir_between(rival.pos,e.pos)<0) continue;
        bool screened=dir_between(rival.pos,p.self)==dir_between(rival.pos,e.pos) && manhattan(rival.pos,p.self)<distance;
        for(const auto& other:p.entities)
            if(other.type==EntityType::Robot && other.hp>0 && other.team!=rival.team && other.team!=e.team &&
               dir_between(rival.pos,other.pos)==dir_between(rival.pos,e.pos) && manhattan(rival.pos,other.pos)<distance) screened=true;
        if(!screened) claims.push_back({rival.team,static_cast<double>(base),true});
    }
    const auto shared=resolve_damage_batch(e.hp,claims);
    const auto credit=[&](const DamageBatch& batch) {
        auto it=batch.awards.find(p.team);
        return it==batch.awards.end()?0.0:it->second.attack_credit+it->second.kill_credit;
    };
    const double delta=std::min(credit(solo),credit(shared));
    const bool fatal=incoming_damage(p,false)>=p.hp;
    const double after=forecast_adjusted_damage(d,delta,0,fatal?1:0);
    double pts=combat_points(after)-combat_points(d);
    const Mission* m=mission_of(p,"effective_damage");
    if(!fatal && m && !mission_done(p,m->id) && d<m->target)
        pts+=5.0*std::min(delta,std::max(0.0,m->target-d))/std::max(1.0,m->target-d);
    // Profile is a declared heuristic preference; official credit remains untouched.
    if(pts>0) pts*=aggression_;
    if(kill && pts>0) pts+=0.2; // small tempo heuristic, explicitly not an official point

    return pts;
}

int Bot::incoming_damage(const Perception& p,bool defend) const {
    if(p.protectedSpawn) return 0;
    int total=0;
    for(const auto& e:p.entities) {
        if(e.type!=EntityType::Robot || e.hp<=0 || e.protectedSpawn || e.team==p.team) continue;
        int distance=manhattan(e.pos,p.self);
        if(distance<1 || distance>2 || dir_between(e.pos,p.self)<0) continue;
        bool screened=false;
        for(const auto& o:p.entities)
            if(o.type==EntityType::Robot && o.hp>0 && o.team!=e.team &&
               dir_between(e.pos,o.pos)==dir_between(e.pos,p.self) && manhattan(e.pos,o.pos)<distance) screened=true;
        if(!screened) total += defend?10:20;
    }
    if(plan_.valid && plan_.vulnerable.active(p.tick)) total*=2;
    return total; // upper-bound scenario, not a prediction of all enemy actions
}

Decision Bot::stage2(const Perception& p) {
    const int remaining = cfg_.maxTicks - p.tick;
    if (remaining <= 0) return {Action::wait(), "finished", 0};
    const double damageScore = p.effectiveDamage;
    const int tick = p.tick;

    // 1) 逃命优先：下一 Tick 会死或站在致死格上
    {
        const bool lethal = hz_.lethalAt(p.self, tick) || hz_.lethalAt(p.self, tick + 1);
        const int incomingHarm = hz_.damageAt(p.self, tick) + hz_.damageAt(p.self, tick + 1);
        if (lethal || incomingHarm >= p.hp) {
            int bestDir = -1;
            int bestHarm = 1 << 20;
            for (int dir = 0; dir < 4; ++dir) {
                const Point n{p.self.x + kDirX[dir], p.self.y + kDirY[dir]};
                if (mask_.blocked(n)) continue;
                if (hz_.lethalAt(n, tick)) continue;
                const int harm = hz_.damageAt(n, tick);
                if (harm < bestHarm) {
                    bestHarm = harm;
                    bestDir = dir;
                }
            }
            if (bestDir >= 0 && (bestHarm < incomingHarm || lethal)) {
                const int send = (plan_.valid && plan_.inverted.active(tick)) ? (bestDir + 2) % 4 : bestDir;
                return {Action::move(kDirX[send], kDirY[send]), "escape_hazard", 1000.0};
            }
        }
    }

    // 2) 攻击候选
    double bestAttack = -1;
    int bestAttackDir = -1;
    for (int dir = 0; !p.protectedSpawn && dir < 4; ++dir) {
        const Entity* target = nullptr;
        int bestDist = 3;
        for (const auto& e : p.entities) {
            if (e.type != EntityType::Robot || e.hp <= 0 || e.team == p.team) continue;
            const Point delta{e.pos.x - p.self.x, e.pos.y - p.self.y};
            int dist = -1;
            if (dir == 0 && delta.x == 0 && delta.y < 0) dist = -delta.y;
            else if (dir == 1 && delta.y == 0 && delta.x > 0) dist = delta.x;
            else if (dir == 2 && delta.x == 0 && delta.y > 0) dist = delta.y;
            else if (dir == 3 && delta.y == 0 && delta.x < 0) dist = -delta.x;
            if (dist >= 1 && dist <= 2 && dist < bestDist) {
                bestDist = dist;
                target = &e;
            }
        }
        if (!target) continue;
        const double v = attack_value(p, *target);
        if (v > bestAttack) {
            bestAttack = v;
            bestAttackDir = dir;
        }
    }

    // 3) 目标候选
    std::vector<Goal> goals;
    goals.reserve(32);
    for (const auto& kv : mem_.resources()) {
        const int d = field_.at(kv.second.pos);
        if (d <= 0) continue;
        double gain = resource_gain(p, kv.second);
        if (gain <= 0) continue;
        // 争夺评估：只在"看得见的敌人"上做，属于协议允许的信息
        int enemyBest = 1 << 20;
        for (const auto& e : p.entities) {
            if (e.type != EntityType::Robot || e.hp <= 0 || e.team == p.team) continue;
            enemyBest = std::min(enemyBest, manhattan(e.pos, kv.second.pos));
        }
        if (enemyBest < (1 << 20)) {
            if (enemyBest + 1 < d) gain *= 0.35;
            else if (d + 1 < enemyBest) gain *= 1.15;
        }
        goals.push_back({kv.second.pos, gain / (d + 1.0), "resource"});
    }
    for (const auto& m : p.missions) {
        if (m.type != "reach" || !m.position || mission_done(p, m.id)) continue;
        const int d = field_.at(*m.position);
        if (d > 0) goals.push_back({*m.position, mission_urgency(p) * 5.0 / (d + 1.0), "reach"});
    }
    if (plan_.valid && plan_.advice && plan_.advicePriority >= 1) {
        const int d = field_.at(*plan_.advice);
        if (d > 0) goals.push_back({*plan_.advice, 0.8 * plan_.advicePriority / (d + 1.0), "advice"});
    }

    // 3b) 追猎：只有在资源边际收益接近 0、或对手残血值得补刀时才主动接战
    if (damageScore < 500.0) {
        for (const auto& e : p.entities) {
            if (e.type != EntityType::Robot || e.hp <= 0 || e.protectedSpawn || e.team == p.team) continue;
            if (manhattan(e.pos, p.self) > hunt_radius_) continue;
            double expected = 0.6;
            if (e.hp <= 20) expected = 2.4;
            else if (e.hp <= 40) expected = 1.2;
            if (e.defending) expected *= 0.6;
            for (int dir = 0; dir < 4; ++dir) {
                for (int step = 1; step <= 2; ++step) {
                    const Point c{e.pos.x + kDirX[dir] * step, e.pos.y + kDirY[dir] * step};
                    if (!mem_.inside(c) || c == p.self) continue;
                    const int d = field_.at(c);
                    if (d <= 0) continue;
                    goals.push_back({c, expected / (d + 1.0), "hunt"});
                }
            }
        }
    }

    // Attacks settle BEFORE movement. Moving cannot evade this tick's incoming fire.
    const bool fatal=incoming_damage(p,false)>=p.hp;
    const bool defendFatal=incoming_damage(p,true)>=p.hp;
    const double deathCost=combat_points(damageScore)-combat_points(
        forecast_adjusted_damage(damageScore,0,0,1));
    const double defendValue=defendFatal ? -deathCost : 0.0;

    // 5) 选择
    Goal chosen;
    bool hasGoal = false;
    double chosenScore = -1e18;
    for (const auto& g : goals) {
        double s = fatal ? -deathCost : g.score;
        if (has_last_goal_ && g.pos == last_goal_) s *= 1.12;
        if (s > chosenScore) {
            chosenScore = s;
            chosen = g;
            hasGoal = true;
        }
    }

    if (fatal && !defendFatal && defendValue >= std::max(bestAttack, hasGoal ? chosenScore : -1e18)) {
        return {Action::defend(), "defend_threat", defendValue};
    }
    if (bestAttackDir >= 0 && bestAttack > 0 && (!hasGoal || bestAttack >= chosenScore)) {
        const int send = (plan_.valid && plan_.inverted.active(tick)) ? (bestAttackDir + 2) % 4 : bestAttackDir;
        return {Action::attack(kDirX[send], kDirY[send]), "attack", bestAttack};
    }
    if (hasGoal) {
        const int8_t dir = field_.firstAt(chosen.pos);
        if (dir >= 0) {
            last_goal_ = chosen.pos;
            has_last_goal_ = true;
            const int send = (plan_.valid && plan_.inverted.active(tick)) ? (dir + 2) % 4 : dir;
            return {Action::move(kDirX[send], kDirY[send]), chosen.reason, chosenScore};
        }
    }
    return explore(p, "stage2_explore");
}

Decision Bot::explore(const Perception& p, const char* why) {
    const auto frontiers = mem_.frontiers();
    Point best{0, 0};
    int bestDist = std::numeric_limits<int>::max();
    bool found = false;
    for (const auto& c : frontiers) {
        const int d = field_.at(c);
        if (d <= 0 || d >= bestDist) continue;
        bestDist = d;
        best = c;
        found = true;
    }
    if (!found) return {Action::wait(), why, 0};
    const int8_t dir = field_.firstAt(best);
    if (dir < 0) return {Action::wait(), why, 0};
    const int send=(plan_.valid && plan_.inverted.active(p.tick))?(dir+2)%4:dir;
    return {Action::move(kDirX[send], kDirY[send]), why, 0};
}

}  // namespace rmtc2
