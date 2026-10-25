// rmtc7/bot.cpp
#include "rmtc7/bot.hpp"
#include "rmtc7/combat.hpp"
#include "rmtc7/battle.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace rmtc7 {
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
        h = (h ^ static_cast<unsigned>(static_cast<unsigned>(kv.second.pos.x)*131u + static_cast<unsigned>(kv.second.pos.y) + static_cast<unsigned>(kv.second.value)*100003u)) * 16777619u;
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
    sightings_.clear();hunt_ticks_=0;hunt_block_until_=-1;prior_damage_=0;
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
    if(structural){sightings_.clear();hunt_ticks_=0;hunt_block_until_=-1;prior_damage_=0;}
    if(p.effectiveDamage>prior_damage_+1e-6)hunt_ticks_=0;
    prior_damage_=p.effectiveDamage;
    for(auto it=sightings_.begin();it!=sightings_.end();){
        bool visible=false;for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.team==it->first&&e.hp>0)visible=true;
        const auto q=it->second.robot.pos;
        if(!visible && (p.tick-it->second.tick>60 || (p.alive&&p.observationComplete&&std::abs(q.x-p.self.x)<=p.vision&&std::abs(q.y-p.self.y)<=p.vision)))it=sightings_.erase(it);else ++it;
    }
    for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&e.team!=p.team){
        auto& old=sightings_[e.team];Point velocity;
        if(old.tick==p.tick-1&&manhattan(old.robot.pos,e.pos)<=1)velocity={e.pos.x-old.robot.pos.x,e.pos.y-old.robot.pos.y};
        old={e,p.tick,velocity};
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
    // 临时墙/致死区交给时间展开 BFS 处理：只在“到达该格的 Tick”仍生效时才绕开。
    // 旧版把任何未来 3 Tick 的危险格都写进静态 mask，会误把 10–30 Tick 的
    // 世界分裂墙当成永久墙，或者只挡 3 Tick 后在墙后继续规划。
    // 视野内其它车辆占位（移动阶段开始时占格会失败）
    for (const auto& e : p.entities) {
        if (e.type == EntityType::Robot && e.hp > hz_.damageAt(e.pos,p.tick,e.protectedSpawn) && !hz_.lethalAt(e.pos,p.tick,e.protectedSpawn) && e.team != p.team) mask_.set(e.pos, true);
    }
    for(auto it=failed_edges_.begin();it!=failed_edges_.end();)if(it->second<=p.tick)it=failed_edges_.erase(it);else ++it;
    if(p.tick==last_move_tick_+1&&p.lastActionTick==last_move_tick_&&!p.lastMoved&&p.self==last_move_from_)
        failed_edges_[{last_move_from_,last_move_to_}]=p.tick+2;
    for(const auto& edge:failed_edges_)if(edge.first.first==p.self)mask_.set(edge.first.second,true);
    mask_.set(p.self,false); // permit routes out of a hazardous origin
    const int remaining = std::max(0, cfg_.maxTicks - p.tick);
    field_ = hz_.any() ? bfs_timed(mask_, hz_, p.self, p.tick, remaining)
                       : bfs(mask_, p.self, remaining);
}

Decision Bot::decide(const Perception& p) {
    if (!p.valid()) return {Action::wait(), "invalid_perception", 0};
    prepare(p);
    if (!p.alive || p.hp <= 0) return {Action::wait(), "dead_wait", 0};
    if (cfg_.maxTicks <= p.tick) return {Action::wait(), "finished", 0};
    const auto decision=cfg_.stage == 1 ? stage1(p) : stage2(p);
    if(decision.reason=="hunt" || decision.reason=="intercept"){
        if(++hunt_ticks_>=8){hunt_block_until_=p.tick+12;hunt_ticks_=0;}
    }
    else if(decision.action.kind!=Act::Move)hunt_ticks_=0;
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

double Bot::collection_gain(const Perception& p,const std::vector<Resource>& resources) const {
    double value=0;int normal=0,high=0;
    for(const auto& r:resources){value+=r.value;if(r.value>=20)++high;else ++normal;}
    const double capLeft=std::max(0.0,120.0-p.resourceValue);
    double gain=.25*std::min(value,capLeft) + .10*value; // explicit resource-denial preference, not own-score delta
    const double urgency=mission_urgency(p);
    for(const auto& m:p.missions){
        if(mission_done(p,m.id))continue;
        double need=0,add=0;
        if(m.type=="normal_resources"){need=m.target-mem_.normalCollected();add=normal;}
        else if(m.type=="high_resources"){need=m.target-mem_.highCollected();add=high;}
        else if(m.type=="resource_value"){need=m.target-p.resourceValue;add=value;}
        if(need>0 && add>0)gain+=urgency*5.0*std::min(1.0,add/need);
    }
    return gain;
}
double Bot::resource_gain(const Perception& p,const Resource& r) const {
    return collection_gain(p,{r});
}

double Bot::attack_value(const Perception& p,const Entity& target) const {
    return attack_action_value(p,{&target});
}
double Bot::attack_action_value(const Perception& p,const std::vector<const Entity*>& targets) const {
    const double stress=aggression_<.9?.8:(aggression_>1.1?.25:.5);
    const BattleMode modes[]={BattleMode::Hold,BattleMode::FocusSelf,BattleMode::Opportunistic};
    const double weights[]={(1-stress)*.5,stress,(1-stress)*.5};
    double points=0,killChance=0,buffer=0;
    for(int mode=0;mode<3;++mode){
        auto result=forecast_battle(p,plan_,hz_,targets,modes[mode]);
        const double after=forecast_adjusted_damage(p.effectiveDamage,result.credit,0,result.fatal?1:0);
        double value=combat_points(after)-combat_points(p.effectiveDamage);
        if(!result.fatal)for(const auto& mission:p.missions)
            if(mission.type=="effective_damage" && !mission_done(p,mission.id) && p.effectiveDamage<mission.target)
                value+=5*std::min(result.credit,mission.target-p.effectiveDamage)/std::max(1.0,mission.target-p.effectiveDamage);
        points+=weights[mode]*value;
        if(result.kills>0)killChance+=weights[mode];
        if(!result.fatal&&after>=500)buffer+=weights[mode]*result.credit;
    }
    if(points>0)points*=aggression_;
    points+=.0001*buffer;
    if(points>0)points+=.2*killChance;
    return points;
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
        if(e.type!=EntityType::Robot || e.hp<=0 || e.protectedSpawn || e.team==p.team || hz_.lethalAt(e.pos,p.tick,e.protectedSpawn) || hz_.damageAt(e.pos,p.tick,e.protectedSpawn)>=e.hp) continue;
        int distance=manhattan(e.pos,p.self);
        if(distance<1 || distance>2 || dir_between(e.pos,p.self)<0) continue;
        bool screened=false;
        for(const auto& o:p.entities)
            if(o.type==EntityType::Robot && o.hp>hz_.damageAt(o.pos,p.tick,o.protectedSpawn) && !hz_.lethalAt(o.pos,p.tick,o.protectedSpawn) && o.team!=e.team &&
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

    // 世界分裂：被十字线击杀并立即复活的那一 Tick，裁判强制 WAIT。
    // 旧版看到自己在出生点就照常规划，发出的 MOVE 会被 E_STATE 拒绝并计入无效动作。
    if (p.positionRespawn) {
        for (const auto& e : plan_.events) {
            if (e.kind == "split" && std::abs(e.start - p.tick) <= 1)
                return {Action::wait(), "split_forced_wait", 0};
        }
    }
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

    for(const auto& kv:mem_.resources())if(kv.second.pos==p.self &&
        resource_gain(p,kv.second)>0 && incoming_damage(p,true)+hz_.damageAt(p.self,tick,p.protectedSpawn)<p.hp)
        return {Action::defend(),"collect_underfoot",resource_gain(p,kv.second)};

    // 2) 攻击候选
    double bestAttack = -1;
    int bestAttackDir = -1;
    for (int dir = 0; !p.protectedSpawn && dir < 4; ++dir) {
        const Entity* target = nullptr;
        int bestDist = 3;
        for (const auto& e : p.entities) {
            if (e.type != EntityType::Robot || e.hp <= hz_.damageAt(e.pos,tick,e.protectedSpawn) || hz_.lethalAt(e.pos,tick,e.protectedSpawn) || e.team == p.team) continue;
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
    goals.reserve(64);
    // Information is an opportunity estimate, not a claim of unseen resources.
    // Compete with known distant goals instead of exploring only after everything is exhausted.
    if(p.vision>0 && remaining>20)for(const auto& c:mem_.frontiers()){
        const int d=field_.at(c);if(d<=0||d+2>=remaining)continue;
        int unknown=0;
        for(int y=c.y-p.vision;y<=c.y+p.vision;++y)
            for(int x=c.x-p.vision;x<=c.x+p.vision;++x)
                if(mem_.inside({x,y})&&mem_.at({x,y})==kUnknown)++unknown;
        if(unknown>0)goals.push_back({c,0.10*unknown/(d+2.0),"information"});
    }
    BlockMask enemyMask=mask_;
    for(const auto& e:p.entities)if(e.type==EntityType::Robot)enemyMask.set(e.pos,false);
    std::vector<Field> enemyFields;
    for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&e.team!=p.team)
        enemyFields.push_back(bfs(enemyMask,e.pos,remaining));
    for (const auto& kv : mem_.resources()) {
        const int d = field_.at(kv.second.pos);
        if (d <= 0) continue;
        double gain = resource_gain(p, kv.second);
        if (gain <= 0) continue;
        // 争夺评估：只在"看得见的敌人"上做，属于协议允许的信息
        int enemyBest = 1 << 20;
        for (const auto& ef:enemyFields) {
            const int ed=ef.at(kv.second.pos);if(ed>=0)enemyBest=std::min(enemyBest,ed);
        }
        if (enemyBest < (1 << 20)) {
            if (enemyBest + 1 < d) gain *= 0.20;
            else if (d <= enemyBest && enemyBest<=d+3) gain *= 1.65;
        }
        double rate=gain/(d+1.0);
        // A two-stop receding horizon sees nearby clusters without committing
        // to a long route that assumes every contested resource remains ours.
        if(d+2<remaining && mem_.resources().size()<=24){
            const auto between=bfs(mask_,kv.second.pos,std::min(12,remaining-d));
            for(const auto& other:mem_.resources()){
                if(other.first==kv.first)continue;
                const int gap=between.at(other.second.pos);if(gap<=0||d+gap>=remaining)continue;
                const double total=collection_gain(p,{kv.second,other.second});
                double after=std::max(0.0,total-resource_gain(p,kv.second));
                int rivalArrival=1<<20;for(const auto& ef:enemyFields){int ed=ef.at(other.second.pos);if(ed>=0)rivalArrival=std::min(rivalArrival,ed);}
                if(rivalArrival+1<d+gap)after*=.2;
                rate=std::max(rate,(gain+.75*after)/(d+gap+1.0));
            }
        }
        goals.push_back({kv.second.pos, rate, "resource"});
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
        Point anchor = event.a;
        if (event.kind == "fortress") anchor = {(event.a.x + event.b.x) / 2, (event.a.y + event.b.y) / 2};
        const int distance=field_.at(anchor);
        if(distance<=0||tick+distance>=event.end)continue;
        if((event.kind=="heal"||event.kind=="chili")&&tick+distance<event.start)continue;
        if(event.kind=="heal"&&p.hp<80&&plan_.healUntil<=tick) {
            // A heal point is worth roughly the HP it restores, but only while we can still use it.
            double healValue = 0.12 * std::min(100 - p.hp, event.heal * event.duration) / (distance + 1.0);
            if (p.hp < 50) healValue *= 1.5;
            goals.push_back({anchor, healValue, "heal_entry"});
        }
        if(event.kind=="fortress"&&!plan_.fury.active(tick)&&tick+distance+event.dwell<event.end)
            goals.push_back({anchor,(8.0 + (p.hp<60?4.0:0.0))/(distance+event.dwell+1.0),"fortress_entry"});
        if(event.kind=="chili") {
            double bonus=0;
            for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&!e.protectedSpawn&&e.pos!=event.a&&(e.pos.x==event.a.x||e.pos.y==event.a.y)&&e.hp<=event.damage)
                bonus+=combat_points(p.effectiveDamage+60)-combat_points(p.effectiveDamage);
            if(bonus>0)goals.push_back({event.a,1.5*bonus/(distance+1.0),"chili_trigger_estimate"});
        }
    }

    // Revisit recent public sightings when no current target can be acquired.
    // The position is a decaying hypothesis, discarded immediately when disproved by sight.
    if(damageScore<560)for(const auto& item:sightings_){
        const auto& seen=item.second;const int age=tick-seen.tick;
        if(age<=0||age>40)continue;
        for(int dir=0;dir<4;++dir){Point target{seen.robot.pos.x+2*kDirX[dir],seen.robot.pos.y+2*kDirY[dir]};
            int d=field_.at(target);if(d<=0||d+age>=remaining)continue;
            goals.push_back({target,0.7*std::exp(-age/20.0)/(d+1.0),"sighting_search"});
        }
    }

    // 3b) 追猎：只有在资源边际收益接近 0、或对手残血值得补刀时才主动接战
    if (damageScore < 500.0) {
        for (const auto& e : p.entities) {
            if (e.type != EntityType::Robot || e.hp <= 0 || e.protectedSpawn || e.team == p.team) continue;
            if (manhattan(e.pos, p.self) > hunt_radius_) continue;
            if(tick<hunt_block_until_ && e.hp>20)continue;
            const auto seen=sightings_.find(e.team);
            if(seen!=sightings_.end() && seen->second.velocity!=Point{0,0}){
                const Point vel=seen->second.velocity;
                for(int look=1;look<=3;++look){
                    Point anticipated{e.pos.x+look*vel.x,e.pos.y+look*vel.y};
                    if(!mem_.inside(anticipated)||mem_.isWall(anticipated))break;
                    for(int dir=0;dir<4;++dir){
                        Point c{anticipated.x+2*kDirX[dir],anticipated.y+2*kDirY[dir]};int d=field_.at(c);
                        if(d>0 && d<=look+2)goals.push_back({c,(e.hp<=20?2.4:0.9)/(d+1.0),"intercept"});
                    }
                }
            }

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
                    goals.push_back({c, (expected + (step==2 ? 0.6 : 0.0)) / (d + 1.0), "hunt"});
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
        if (s>0 && has_last_goal_ && g.pos == last_goal_) s *= 1.12;
        if (s > chosenScore) {
            chosenScore = s;
            chosen = g;
            hasGoal = true;
        }
    }

    if (fatal && !defendFatal && defendValue >= std::max(bestAttack, hasGoal ? chosenScore : -1e18)) {
        return {Action::defend(), "defend_threat", defendValue};
    }
    // Compare losses as well as gains: when every passive action is lethal,
    // firing back can lose fewer points than silently dying. Movement is later.
    const double passiveValue = hasGoal ? chosenScore : (fatal ? -deathCost : 0.0);
    if (bestAttackDir >= 0 && (bestAttack > 0 || (fatal && defendFatal && bestAttack > -deathCost)) && bestAttack >= passiveValue) {
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
    // Commit to a frontier for several ticks. Recomputing the best frontier every
    // tick makes the robot pace between two equally attractive edges.
    const auto frontiers = mem_.frontiers(p.vision == 0);
    Point best{0, 0};
    int bestDist = std::numeric_limits<int>::max();
    double bestInfo=-1;
    bool found = false;
    for (const auto& c : frontiers) {
        const int d = field_.at(c);
        if (d <= 0 || !safe_step(p,field_.firstAt(c))) continue;
        if(cfg_.stage==1 || p.vision==0){
            if(d>=bestDist)continue;
        }else{
            int unknown=0;
            for(int y=c.y-p.vision;y<=c.y+p.vision;++y)
                for(int x=c.x-p.vision;x<=c.x+p.vision;++x)
                    if(mem_.inside({x,y})&&mem_.at({x,y})==kUnknown)++unknown;
            const double information=unknown/(d+2.0);
            if(information<=bestInfo)continue;
            bestInfo=information;
        }
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

}  // namespace rmtc7
