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
        h = (h ^ static_cast<unsigned>(kv.second.pos.x * 131 + kv.second.pos.y + kv.second.value*100003)) * 16777619u;
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
    plan_ = Plan{}; raw_plan_=Plan{}; events_.reset(); observed_tick_=-1;
    tour_order_.clear();seen_teams_.clear();tour_sig_=0;failed_edges_.clear();last_move_tick_=-1;
    tour_tick_ = -1000;
    has_last_goal_ = false;
}

void Bot::set_profile(double aggression, int hunt_radius) {
    aggression_ = std::max(0.2, std::min(2.5, aggression));
    hunt_radius_ = std::max(1, std::min(10, hunt_radius));
}

void Bot::set_plan(const Plan& plan) {
    if (!plan.valid) return;
    raw_plan_ = merge_plans(raw_plan_,plan,observed_tick_);
}

void Bot::clear_plan() { plan_ = Plan{}; raw_plan_=Plan{}; }

void Bot::prepare(const Perception& p) {
    if(raw_plan_.valid&&raw_plan_.session!=p.session)raw_plan_=Plan{};
    plan_=events_.apply(raw_plan_,p);
    observed_tick_=p.tick;
    const bool structural = mem_.update(p,&plan_);
    if (structural) {
        tour_tick_ = -1000;
        tour_order_.clear();failed_edges_.clear();last_move_tick_=-1;
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
    for(auto it=failed_edges_.begin();it!=failed_edges_.end();)if(it->second<=p.tick)it=failed_edges_.erase(it);else ++it;
    if(p.tick==last_move_tick_+1&&p.lastActionTick==last_move_tick_&&!p.lastMoved&&p.self==last_move_from_)
        failed_edges_[{last_move_from_,last_move_to_}]=p.tick+2;
    for(const auto& edge:failed_edges_)if(edge.first.first==p.self)mask_.set(edge.first.second,true);
    mask_.set(p.self,false); // permit routes out of a hazardous origin
    const int remaining = std::max(0, cfg_.maxTicks - p.tick);
    field_ = bfs(mask_, p.self, remaining);
}

Decision Bot::decide(const Perception& p) {
    if (!p.valid()) return {Action::wait(), "invalid_perception", 0};
    prepare(p);
    if (!p.alive || p.hp <= 0) return {Action::wait(), "dead_wait", 0};
    if (cfg_.maxTicks <= p.tick) return {Action::wait(), "finished", 0};
    const auto decision=cfg_.stage == 1 ? stage1(p) : stage2(p);
    if(decision.action.kind==Act::Move){
        const int sign=plan_.inverted.active(p.tick)?-1:1;
        last_move_tick_=p.tick;last_move_from_=p.self;
        last_move_to_={p.self.x+sign*decision.action.dx,p.self.y+sign*decision.action.dy};
    }
    return decision;
}

// ---------------------------------------------------------------------------
// 第一阶段：预算内最大化收集价值
// ---------------------------------------------------------------------------
Decision Bot::stage1(const Perception& p) {
    const int remaining = cfg_.maxTicks - p.tick;
    for(const auto& resource:mem_.resources())if(resource.second.pos==p.self&&resource.second.value>0)
        return {Action::wait(),"collect_underfoot",static_cast<double>(resource.second.value)};

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

    if(nodes.size()>24) {
        std::vector<std::size_t> rank(nodes.size());for(std::size_t i=0;i<rank.size();++i)rank[i]=i;
        std::stable_sort(rank.begin(),rank.end(),[&](std::size_t a,std::size_t b){return nodes[a].value/(start[a]+1.0)>nodes[b].value/(start[b]+1.0);});
        std::vector<Resource> selected;std::vector<int> distances;
        for(std::size_t i=0;i<24;++i){selected.push_back(nodes[rank[i]]);distances.push_back(start[rank[i]]);}
        nodes=std::move(selected);start=std::move(distances);
    }
    const int n = static_cast<int>(nodes.size());
    std::map<std::string,Resource> filtered;for(const auto& r:nodes)filtered[r.id]=r;
    const unsigned sig = resource_signature(filtered);
    const bool reusable = (tour_generation_==mem_.generation()) && (sig == tour_sig_) && (p.tick - tour_tick_ < 4) &&
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
        tour_generation_=mem_.generation();
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
    const double scarcity = 1.0; // unseen teams do not establish a two-player match
    double gain = capLeft > 0 ? scarcity * 0.25 * std::min<double>(r.value, capLeft) : 0.002 * r.value;

    const Mission* mn = mission_of(p, "normal_resources");
    if (mn && !mission_done(p, mn->id) && r.value < 20) {
        const int collected = mem_.normalCollected();
        const double need = mn->target - collected;
        if (need > 0) gain += ureg * (need <= 1.0 ? 5.0 : 2.0);
    }
    const Mission* mh = mission_of(p, "high_resources");
    if (mh && !mission_done(p,mh->id) && r.value>=20) {
        const double need=std::max(1.0,mh->target-mem_.highCollected());
        gain+=ureg*5.0/need;
    }

    const Mission* mv = mission_of(p, "resource_value");
    if (mv && !mission_done(p, mv->id) && r2 < mv->target) {
        gain += ureg * 5.0 * std::min<double>(r.value, mv->target - r2) / std::max(1.0, mv->target - r2);
    }
    return gain;
}

double Bot::attack_value(const Perception& p,const Entity& target) const {
    return attack_action_value(p,{&target});
}
double Bot::attack_action_value(const Perception& p,const std::vector<const Entity*>& targets) const {
    double credit=0;int reflected=0;bool hasKill=false;
    const bool fury=plan_.fury.active(p.tick);
    for(const Entity* target:targets) {
        const auto& e=*target;
        if(e.hp<=0||e.protectedSpawn)continue;
        if(plan_.reflect.active(p.tick)&&e.defending){reflected+=plan_.reflectDamage;continue;}
        const int base=fury?(e.defending?plan_.furyDefendDamage:plan_.furyDamage):(e.defending?10:20);
        const auto solo=resolve_damage_batch(e.hp,{{p.team,static_cast<double>(base),true}});
        std::vector<DamageClaim> claims{{p.team,static_cast<double>(base),true}};
        for(const auto& rival:p.entities) {
            if(rival.type!=EntityType::Robot||rival.hp<=0||rival.protectedSpawn||rival.team==p.team||rival.team==e.team)continue;
            const int dist=manhattan(rival.pos,e.pos);
            if(dist<1||dist>2||dir_between(rival.pos,e.pos)<0)continue;
            bool screen=dir_between(rival.pos,p.self)==dir_between(rival.pos,e.pos)&&manhattan(rival.pos,p.self)<dist;
            for(const auto& other:p.entities)if(other.type==EntityType::Robot&&other.hp>0&&other.team!=rival.team&&other.team!=e.team&&dir_between(rival.pos,other.pos)==dir_between(rival.pos,e.pos)&&manhattan(rival.pos,other.pos)<dist)screen=true;
            if(!screen)claims.push_back({rival.team,static_cast<double>(e.defending?10:20),true});
        }
        const auto shared=resolve_damage_batch(e.hp,claims);
        const auto own=[&](const DamageBatch& batch){auto i=batch.awards.find(p.team);return i==batch.awards.end()?0.0:i->second.attack_credit+i->second.kill_credit;};
        credit+=std::min(own(solo),own(shared));hasKill=hasKill||solo.killed;
    }
    const int environment=hz_.damageAt(p.self,p.tick,p.protectedSpawn);
    const bool fatalIdle=environment+reflected>=p.hp;
    const bool fatalStress=environment+reflected+incoming_damage(p,false)>=p.hp;
    const double afterIdle=forecast_adjusted_damage(p.effectiveDamage,credit,0,fatalIdle?1:0);
    const double afterStress=forecast_adjusted_damage(p.effectiveDamage,credit,0,fatalStress?1:0);
    // Explicit heuristic mixture: enemy actions are unknown. No per-HP blanket penalty.
    const double stress=aggression_<0.9?0.8:(aggression_>1.1?0.25:0.5);
    double pts=(1-stress)*combat_points(afterIdle)+stress*combat_points(afterStress)-combat_points(p.effectiveDamage);
    const auto* mission=mission_of(p,"effective_damage");
    if(!fatalStress&&mission&&!mission_done(p,mission->id)&&p.effectiveDamage<mission->target)
        pts+=5.0*std::min(credit,mission->target-p.effectiveDamage)/std::max(1.0,mission->target-p.effectiveDamage);
    if(pts>0)pts*=aggression_;
    if(afterStress>=500&&credit>0&&!fatalStress)pts+=0.0001*credit;
    if(hasKill&&pts>0)pts+=0.2;
    return pts;
}

bool Bot::safe_step(const Perception& p,int dir) const {
    if(dir<0||dir>3)return false;
    Point next{p.self.x+kDirX[dir],p.self.y+kDirY[dir]};
    if(mask_.blocked(next))return false;
    const bool protectedNext=p.protectedSpawn&&mem_.spawn()&&next==*mem_.spawn();
    const int hp=p.hp-hz_.damageAt(p.self,p.tick,p.protectedSpawn)-incoming_damage(p,false);
    return !hz_.lethalAt(next,p.tick+1,protectedNext)&&hz_.damageAt(next,p.tick+1,protectedNext)<std::max(1,hp);
}

int Bot::incoming_damage(const Perception& p,bool defend) const {
    if(p.protectedSpawn || (defend&&plan_.reflect.active(p.tick))) return 0;
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

    // Pre-move events precede both attacks and movement. Do not claim to escape them now.
    if(hz_.lethalAt(p.self,tick,p.protectedSpawn)||hz_.damageAt(p.self,tick,p.protectedSpawn)>=p.hp)
        return {Action::wait(),"pre_event_forced_death",0};
    {
        const bool lethal = hz_.lethalAt(p.self, tick + 1,p.protectedSpawn);
        const int incomingHarm = hz_.damageAt(p.self,tick+1,p.protectedSpawn);
        const int futureHp=std::max(1,p.hp-hz_.damageAt(p.self,tick,p.protectedSpawn)-incoming_damage(p,false));
        if (lethal || incomingHarm >= futureHp) {
            int bestDir = -1;
            int bestHarm = 1 << 20;
            for (int dir = 0; dir < 4; ++dir) {
                const Point n{p.self.x + kDirX[dir], p.self.y + kDirY[dir]};
                if (!safe_step(p,dir)) continue;
                if (hz_.lethalAt(n, tick)) continue;
                const int harm = hz_.damageAt(n,tick+1,false);
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

    if(!p.protectedSpawn&&plan_.fury.active(tick)) {
        std::vector<const Entity*> targets;
        for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&e.team!=p.team&&std::abs(e.pos.x-p.self.x)<=2&&std::abs(e.pos.y-p.self.y)<=2)targets.push_back(&e);
        if(!targets.empty()){bestAttack=attack_action_value(p,targets);bestAttackDir=0;}
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
    if (plan_.valid && plan_.advice && plan_.advicePriority >= 1 && tick-plan_.sourceTick<=30) {
        const int d = field_.at(*plan_.advice);
        if (d > 0) goals.push_back({*plan_.advice, 0.8 * plan_.advicePriority / (d + 1.0), "advice"});
    }

    // Typed beneficial events and legal chili triggers: heuristics, never direct score mutation.
    for(const auto& event:plan_.events) {
        if(event.end<=tick)continue;
        const int distance=field_.at(event.a);
        if(distance<=0||tick+distance>=event.end)continue;
        if((event.kind=="heal"||event.kind=="chili")&&tick+distance<event.start)continue;
        if(event.kind=="heal"&&p.hp<80&&plan_.healUntil<=tick)
            goals.push_back({event.a,0.03*std::min(100-p.hp,event.heal*event.duration)/(distance+1.0),"heal_entry"});
        if(event.kind=="fortress"&&!plan_.fury.active(tick)&&tick+distance+event.dwell<event.end)
            goals.push_back({event.a,1.5/(distance+event.dwell+1.0),"fortress_entry"});
        if(event.kind=="chili") {
            double bonus=0;
            for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&!e.protectedSpawn&&e.pos!=event.a&&(e.pos.x==event.a.x||e.pos.y==event.a.y)&&e.hp<=event.damage)
                bonus+=combat_points(p.effectiveDamage+60)-combat_points(p.effectiveDamage);
            if(bonus>0)goals.push_back({event.a,0.5*bonus/(distance+1.0),"chili_trigger_estimate"});
        }
    }

    // 3b) 追猎：只有在资源边际收益接近 0、或对手残血值得补刀时才主动接战
    if (damageScore < 500.0) {
        for (const auto& e : p.entities) {
            if (e.type != EntityType::Robot || e.hp <= 0 || e.protectedSpawn || e.team == p.team) continue;
            if (manhattan(e.pos, p.self) > hunt_radius_) continue;
            if(manhattan(e.pos,p.self)<=2 && dir_between(p.self,e.pos)>=0 && bestAttack>0)continue;
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
    const bool fatal=incoming_damage(p,false)+hz_.damageAt(p.self,p.tick,p.protectedSpawn)>=p.hp;
    const bool defendFatal=incoming_damage(p,true)+hz_.damageAt(p.self,p.tick,p.protectedSpawn)>=p.hp;
    const double deathCost=combat_points(damageScore)-combat_points(
        forecast_adjusted_damage(damageScore,0,0,1));
    const double defendValue=defendFatal ? -deathCost : 0.0;

    // 5) 选择
    Goal chosen;
    bool hasGoal = false;
    double chosenScore = -1e18;
    for (const auto& g : goals) {
        const int first=field_.firstAt(g.pos);
        if(!safe_step(p,first))continue;
        double s = fatal ? -deathCost : g.score;
        Point next{p.self.x+kDirX[first],p.self.y+kDirY[first]};
        s-=0.005*hz_.damageAt(next,tick+1,false);
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
        const int send = bestAttackDir;
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
    if(!fatal)for(const auto& event:plan_.events)if(event.kind=="fortress"&&event.active(tick)&&event.contains(p.self)&&!plan_.fury.active(tick))
        return {Action::defend(),"fortress_dwell",0};
    return explore(p, "stage2_explore");
}

Decision Bot::explore(const Perception& p, const char* why) {
    const auto frontiers = mem_.frontiers(p.vision == 0);
    Point best{0, 0};
    int bestDist = std::numeric_limits<int>::max();
    bool found = false;
    for (const auto& c : frontiers) {
        const int d = field_.at(c);
        if (d <= 0 || d >= bestDist || !safe_step(p,field_.firstAt(c))) continue;
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
