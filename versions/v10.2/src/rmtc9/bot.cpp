// rmtc9/bot.cpp
#include "rmtc9/bot.hpp"
#include "rmtc9/combat.hpp"
#include "rmtc9/battle.hpp"
#include "rmtc9/resource_race.hpp"
#include "rmtc9/phase_policy.hpp"
#include "rmtc9/policy_variant.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace rmtc9 {
namespace {

// 8.0 的固定参数。全部写死在源码里：规则只允许现场切换 decision_type，
// 不能有任何隐藏的环境变量或参数通道。数值来自离线调参（见 对抗验证报告.md）。
// 第一阶段：观察点路线
constexpr int kViewCount = 4;          // 每次至多选 4 个观察点
constexpr int kViewMinCells = 8;       // 观察点至少要能新看到 8 个未知格
constexpr double kViewBeta = 0.6;      // 观察点期望价值的折扣
constexpr double kViewPad = 3.0;       // 每个预计露面的资源额外预留 3 Tick 去捡
constexpr double kKnownWeight = 1000;  // 已知资源相对观察点的权重（先保证吃下已知资源）
constexpr int kStickTicks = 2;         // 坚持原目标的容忍：路线最多长 2 Tick
constexpr int kViewMaxNodes = 13;      // 观察点路线的节点上限（13 个以内精确 DP）
// 第二阶段
constexpr double kHuntCap = 560.0;     // 500计分上限外再保留一次死亡扣60的缓冲
constexpr double kSeekW = 0.15;        // 看不见敌人时巡逻点的权重
constexpr int kSeekFrom = 40;          // 开局 40 Tick 内不巡逻（先抢资源）
constexpr double kRouteBoost = 1.2;    // 开局路线目标的加成
constexpr double kHiddenDecay = 60.0;  // 未露面资源随时间衰减（别人也在采）
constexpr int kRouteHorizon = 25;      // 开局路线只看 25 Tick 内
constexpr double kRouteBeta = 0.6;
constexpr int kPassiveTicks = 3;       // 3 observations without damage: soft, short-lived evidence only

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
    CandidateAudit audit;
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
    diagnostics_ = {};
    auditedReceiptTick_ = -1;
    sightings_.clear();hunt_ticks_=0;hunt_block_until_=-1;prior_damage_=0;enemy_spawn_.clear();patrol_visit_.clear();passive_.clear();prev_aligned_.clear();prev_tick_=-1;
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
        diagnostics_ = {};
        auditedReceiptTick_ = -1;
        tour_tick_ = -1000;
        tour_order_.clear();failed_edges_.clear();last_move_tick_=-1;
        has_last_goal_ = false;
        seen_teams_.clear();
    }
    if(structural){sightings_.clear();hunt_ticks_=0;hunt_block_until_=-1;prior_damage_=0;enemy_spawn_.clear();patrol_visit_.clear();passive_.clear();prev_aligned_.clear();prev_tick_=-1;}
    if(p.effectiveDamage>prior_damage_+1e-6)hunt_ticks_=0;
    prior_damage_=p.effectiveDamage;
    for(auto it=sightings_.begin();it!=sightings_.end();){
        bool visible=false;for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.team==it->first&&e.hp>0)visible=true;
        const auto q=it->second.robot.pos;
        if(!visible && (p.tick-it->second.tick>60 || (p.alive&&p.observationComplete&&std::abs(q.x-p.self.x)<=p.vision&&std::abs(q.y-p.self.y)<=p.vision)))it=sightings_.erase(it);else ++it;
    }
    for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&e.team!=p.team&&e.protectedSpawn)
        enemy_spawn_[e.team]=e.pos;  // 出生保护中的车在（或紧挨）复活点：记成复活点，供后期搜索
    for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&e.team!=p.team){
        auto& old=sightings_[e.team];Point velocity;
        if(old.tick==p.tick-1&&manhattan(old.robot.pos,e.pos)<=1)velocity={e.pos.x-old.robot.pos.x,e.pos.y-old.robot.pos.y};
        old={e,p.tick,velocity};
    }
    // Learn only when a shot could actually have reached us. A screen, protection,
    // healing, movement, missing frame or changed geometry invalidates the evidence.
    std::map<std::string,Point> aligned;
    if (p.alive && p.observationComplete && !p.protectedSpawn && !p.positionRespawn &&
        plan_.healUntil <= p.tick && !plan_.reflect.active(p.tick)) {
        for (const auto& e : p.entities) {
            if (e.type != EntityType::Robot || e.hp <= 0 || e.team == p.team || e.protectedSpawn ||
                dir_between(e.pos,p.self) < 0 || manhattan(e.pos,p.self) > 2) continue;
            bool screened=false;
            for (const auto& other : p.entities)
                if (other.type == EntityType::Robot && other.hp > 0 && other.team != e.team &&
                    dir_between(e.pos,other.pos) == dir_between(e.pos,p.self) &&
                    manhattan(e.pos,other.pos) < manhattan(e.pos,p.self)) screened=true;
            if (!screened) aligned[e.team]=e.pos;
        }
    }
    for (auto it=passive_.begin();it!=passive_.end();) {
        const auto current=aligned.find(it->first);
        if (current==aligned.end() || current->second!=it->second.enemy || p.self!=it->second.self ||
            p.tick-it->second.tick>6 || p.hp<prev_hp_ || prev_tick_!=p.tick-1)
            it=passive_.erase(it);
        else ++it;
    }
    if (prev_tick_==p.tick-1 && prev_alive_ && p.alive && !p.positionRespawn && p.self==prev_self_ && p.hp==prev_hp_) {
        for (const auto& previous : prev_aligned_) {
            const auto current=aligned.find(previous.first);
            if (current!=aligned.end() && current->second==previous.second) {
                auto& evidence=passive_[previous.first];
                evidence.count=std::min(kPassiveTicks,evidence.count+1);
                evidence.tick=p.tick;evidence.enemy=current->second;evidence.self=p.self;
            }
        }
    }
    prev_aligned_=std::move(aligned);
    prev_self_=p.self;
    prev_tick_ = p.tick;
    prev_hp_ = p.hp;
    prev_alive_ = p.alive;
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
    if(p.lastActionTick>=0 && p.lastActionTick!=auditedReceiptTick_) {
        auditedReceiptTick_=p.lastActionTick;
        if(p.lastActionKind.empty())++diagnostics_.unknownReceiptKinds;
        else if(p.lastActionKind=="move" && !p.lastMoved)++diagnostics_.failedMoveReceipts;
    }
    diagnostics_.frontiers = static_cast<int>(mem_.frontiers(p.vision == 0).size());
    diagnostics_.goals = diagnostics_.unsafeGoals = diagnostics_.visibleEnemies = 0;
    diagnostics_.resourceAhead=diagnostics_.resourceTied=diagnostics_.resourceLate=0;
    diagnostics_.resourceRaces.clear();
    diagnostics_.candidates.clear();diagnostics_.rejectionCounts.clear();diagnostics_.candidatesSeen=0;
    diagnostics_.plannerExpansions=diagnostics_.plannerBfs=0;
    diagnostics_.resourcePoints=resource_points(p.resourceValue);
    diagnostics_.combatPoints=combat_points(p.effectiveDamage);
    diagnostics_.resourceSpace=30-diagnostics_.resourcePoints;
    diagnostics_.combatSpace=15-diagnostics_.combatPoints;
    diagnostics_.taskPointsConditional=5.0*std::set<std::string>(p.completedIds.begin(),p.completedIds.end()).size();
    const auto phase=phase_policy(p.tick,cfg_.maxTicks,p.resourceValue,p.hp);
    diagnostics_.strategyPhase=cfg_.stage==2?(kUseFixedPhases?phase.name:"opportunity"):"none";
    diagnostics_.openingEnd=cfg_.stage==2?phase.openingEnd:0;
    diagnostics_.endgameStart=cfg_.stage==2?phase.endgameStart:0;
    diagnostics_.resourceScale=diagnostics_.pursuitScale=1.0;
    diagnostics_.resourceFocus=false;
    for (const auto& e : p.entities)
        if (e.type == EntityType::Robot && e.hp > 0 && e.team != p.team) ++diagnostics_.visibleEnemies;
    diagnostics_.combatObjective = wants_combat(p);
    if (!p.alive || p.hp <= 0) { ++diagnostics_.consecutiveWaits; return {Action::wait(), "dead_wait", 0}; }
    if (cfg_.maxTicks <= p.tick) return {Action::wait(), "finished", 0};
    const auto decision=cfg_.stage == 1 ? stage1(p) : stage2(p);
    diagnostics_.consecutiveWaits = decision.action.kind == Act::Wait ? diagnostics_.consecutiveWaits + 1 : 0;
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

    // 8.0：资源还没全部露面时，把“观察点”和已知资源放进同一条定向越野路线。
    {
        const Decision viewed = stage1_view_tour(p);
        if (!viewed.reason.empty()) return viewed;
    }

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
    const bool reusable = remaining > 18 && (tour_generation_==mem_.generation()) && (sig == tour_sig_) && (p.tick - tour_tick_ < 4) &&
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
        tour_length_ = tour.length;
        tour_rest_ = tour.order.empty() ? 0 : tour.length - start[static_cast<std::size_t>(tour.order.front())];
        if (tour_order_.empty()) return explore(p, "stage1_no_tour");
    }

    for (int idx : tour_order_) {
        if (idx < 0 || idx >= n) continue;
        const Point goal = nodes[static_cast<std::size_t>(idx)].pos;
        const int d = field_.at(goal);
        if (d <= 0) continue;  // 已站上去或不可达
        const int8_t dir = field_.firstAt(goal);
        if (!safe_step(p,dir)) continue;
        if (dir == kFirstWait) return {Action::wait(), "stage1_wait_for_window", static_cast<double>(d)};
        last_goal_ = goal;
        has_last_goal_ = true;
        return {Action::move(kDirX[dir], kDirY[dir]), "stage1_tour", static_cast<double>(d)};
    }
    return explore(p, "stage1_tour_done");
}


// 观察点路线：贪心集合覆盖选出至多 K 个观察点，让它们的 9×9 视野覆盖尽量多的未知格；
// 观察点价值 = 未露面资源的平均密度 × 新覆盖格数 × β，并按“看到后还要去捡”加时间垫。
// 已知资源与观察点一起交给定向越野（n<=13 精确 DP），沿路线第一站走一步。
// 规则公开了资源总数（标准 16 个），未露面个数为 0 时这里直接让位给原路线。
Decision Bot::stage1_view_tour(const Perception& p) {
    const int remaining = cfg_.maxTicks - p.tick;
    // 只在标准尺度启用：视野 >=3、地图 <=450 格（第一阶段标准 20×20、视野 4）。
    // 视野 2 或更大的地图上观察点估值不可靠，退回 7.0 的已知资源路线（离线测试见报告）。
    if (p.vision < 3 || remaining <= 2 || mem_.width() * mem_.height() > 450) return {Action::wait(), "", 0};
    int knownNormal = 0, knownHigh = 0;
    for (const auto& kv : mem_.resources()) (kv.second.value >= 20 ? knownHigh : knownNormal) += 1;
    const int hiddenHigh = std::max(0, cfg_.totalHigh - knownHigh - mem_.highCollected());
    const int hiddenNormal = std::max(0, cfg_.totalNormal - knownNormal - mem_.normalCollected());
    const int hidden = hiddenHigh + hiddenNormal;
    if (hidden <= 0) return {Action::wait(), "", 0};
    const double hiddenValue = 20.0 * hiddenHigh + 10.0 * hiddenNormal;
    const int w = mem_.width(), h = mem_.height();
    std::vector<char> uncovered(static_cast<std::size_t>(w) * h, 0);
    int unknownCount = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (mem_.at({x, y}) == kUnknown) { uncovered[static_cast<std::size_t>(y) * w + x] = 1; ++unknownCount; }
    if (unknownCount == 0) return {Action::wait(), "", 0};
    const double density = hiddenValue / unknownCount;

    struct Node { Point pos; double value; bool view; int pad; };
    std::vector<Node> views;
    const int r = p.vision;
    const auto cells_at = [&](Point c) {
        int cells = 0;
        for (int yy = std::max(0, c.y - r); yy <= std::min(h - 1, c.y + r); ++yy)
            for (int xx = std::max(0, c.x - r); xx <= std::min(w - 1, c.x + r); ++xx)
                cells += uncovered[static_cast<std::size_t>(yy) * w + xx];
        return cells;
    };
    for (int k = 0; k < kViewCount; ++k) {
        int bestCells = 0; Point bestPos{}; double bestScore = 0;
        // 防摆动：上一 Tick 追的观察点只要仍有足够新格子，就优先保留为第一个观察点。
        if (k == 0 && has_view_goal_ && mem_.inside(view_goal_) && field_.at(view_goal_) > 0) {
            const int kept = cells_at(view_goal_);
            if (kept >= kViewMinCells) { bestCells = kept; bestPos = view_goal_; bestScore = 1e9; }
        }
        for (int y = 0; bestScore < 1e9 && y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const Point c{x, y};
                if (mem_.at(c) == kWall) continue;
                const int d = field_.at(c);
                if (d <= 0 || d + 1 > remaining) continue;
                const int cells = cells_at(c);
                if (cells < kViewMinCells) continue;
                // 同样覆盖量取更近的点；轻微偏向离已知区域近的点（更可能可达）
                const double score = cells - 0.02 * d;
                if (score > bestScore) { bestScore = score; bestCells = cells; bestPos = c; }
            }
        if (bestCells <= 0) break;
        for (int yy = std::max(0, bestPos.y - r); yy <= std::min(h - 1, bestPos.y + r); ++yy)
            for (int xx = std::max(0, bestPos.x - r); xx <= std::min(w - 1, bestPos.x + r); ++xx)
                uncovered[static_cast<std::size_t>(yy) * w + xx] = 0;
        const double expectCount = static_cast<double>(hidden) * bestCells / unknownCount;
        views.push_back({bestPos, density * bestCells * kViewBeta, true,
                         static_cast<int>(std::lround(kViewPad * expectCount))});
    }
    if (views.empty()) return {Action::wait(), "", 0};

    std::vector<Node> nodes;
    std::vector<std::pair<double, Node>> res;
    for (const auto& kv : mem_.resources()) {
        const int d = field_.at(kv.second.pos);
        if (d > 0 && d <= remaining && kv.second.value > 0)
            res.push_back({kv.second.value / (d + 1.0), {kv.second.pos, static_cast<double>(kv.second.value), false, 0}});
    }
    std::stable_sort(res.begin(), res.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const int maxNodes = kViewMaxNodes;
    const int keepRes = std::min<int>(static_cast<int>(res.size()), maxNodes - static_cast<int>(views.size()));
    for (int i = 0; i < keepRes; ++i) nodes.push_back(res[static_cast<std::size_t>(i)].second);
    for (const auto& v : views) if (static_cast<int>(nodes.size()) < maxNodes) nodes.push_back(v);
    const int n = static_cast<int>(nodes.size());
    std::vector<int> start(static_cast<std::size_t>(n));
    std::vector<std::vector<int>> between(n, std::vector<int>(n, -1));
    for (int i = 0; i < n; ++i) {
        start[static_cast<std::size_t>(i)] = field_.at(nodes[static_cast<std::size_t>(i)].pos) + nodes[static_cast<std::size_t>(i)].pad;
        const Field f = bfs(mask_, nodes[static_cast<std::size_t>(i)].pos, remaining);
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            const int d = f.at(nodes[static_cast<std::size_t>(j)].pos);
            between[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = d < 0 ? -1 : d + nodes[static_cast<std::size_t>(j)].pad;
        }
    }
    std::vector<double> values(static_cast<std::size_t>(n));
    // 已知资源权重远大于观察点：路线先保证吃下已知资源，观察点只占用富余时间。
    // 否则观察点的“期望价值”每 Tick 都被重新卖一次，已知资源会被一直拖到来不及。
    for (int i = 0; i < n; ++i)
        values[static_cast<std::size_t>(i)] = (nodes[static_cast<std::size_t>(i)].view ? 10.0 : kKnownWeight) * nodes[static_cast<std::size_t>(i)].value;
    Tour tour = solve_tour(start, between, values, remaining);
    // 防摆动：若“先去上一 Tick 的目标”得到的路线价值不变、长度只多 kStickTicks 以内，就坚持原目标。
    if (has_last_goal_ && !tour.order.empty() && nodes[static_cast<std::size_t>(tour.order.front())].pos != last_goal_) {
        for (int i = 0; i < n; ++i) {
            if (nodes[static_cast<std::size_t>(i)].pos != last_goal_) continue;
            std::vector<int> forced(static_cast<std::size_t>(n), -1);
            forced[static_cast<std::size_t>(i)] = start[static_cast<std::size_t>(i)];
            const Tour kept = solve_tour(forced, between, values, remaining);
            if (!kept.order.empty() && kept.value >= tour.value && kept.length <= tour.length + kStickTicks) tour = kept;
            break;
        }
    }
    for (int idx : tour.order) {
        const Point goal = nodes[static_cast<std::size_t>(idx)].pos;
        if (field_.at(goal) <= 0) continue;
        const int8_t dir = field_.firstAt(goal);
        if (dir < 0 || !safe_step(p, dir)) continue;
        if (dir == kFirstWait) return {Action::wait(), "stage1_wait_for_window", static_cast<double>(tour.value)};
        last_goal_ = goal;
        has_last_goal_ = true;
        has_view_goal_ = nodes[static_cast<std::size_t>(idx)].view;
        view_goal_ = goal;
        return {Action::move(kDirX[dir], kDirY[dir]), nodes[static_cast<std::size_t>(idx)].view ? "stage1_view" : "stage1_view_collect",
                static_cast<double>(tour.value)};
    }
    return {Action::wait(), "", 0};
}


// 第二阶段开局路线：与第一阶段相同的“观察点 + 已知资源”定向越野，但
//   * 已知资源按 7.0 的比分收益（含任务）计值，并按可见敌人的到达距离打折（被抢风险）；
//   * 未露面资源数 = 规则总数 − 本场见过的个数，再随时间衰减（别人也在采）。
// 只给出一个目标及其速率，交给第二阶段的统一选择，不越过攻击/防御判断。
bool Bot::stage2_route_goal(const Perception& p, const std::vector<Field>& enemyFields, Point* goalOut, double* rateOut, bool* viewOut) {
    const int remaining = cfg_.maxTicks - p.tick;
    if (p.vision < 3 || remaining <= 2 ||
        (kUseFixedPhases && p.tick >= phase_policy(p.tick,cfg_.maxTicks,p.resourceValue,p.hp).openingEnd) ||
        mem_.width() * mem_.height() > 1024) return false;
    const double decay = std::max(0.0, 1.0 - p.tick / kHiddenDecay);
    const double hiddenHigh = std::max(0, cfg_.totalHigh - mem_.everSeenHigh()) * decay;
    const double hiddenNormal = std::max(0, cfg_.totalNormal - mem_.everSeenNormal()) * decay;
    const double hidden = hiddenHigh + hiddenNormal;
    const int w = mem_.width(), h = mem_.height();
    std::vector<char> uncovered(static_cast<std::size_t>(w) * h, 0);
    int unknownCount = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (mem_.at({x, y}) == kUnknown) { uncovered[static_cast<std::size_t>(y) * w + x] = 1; ++unknownCount; }
    struct Node { Point pos; double value; bool view; int pad; };
    std::vector<Node> nodes;
    std::vector<std::pair<double, Node>> res;
    for (const auto& kv : mem_.resources()) {
        const int d = field_.at(kv.second.pos);
        if (d <= 0 || d > remaining) continue;
        double gain = resource_gain(p, kv.second);
        if (gain <= 0) continue;
        int enemyBest=1<<20;
        for (const auto& ef:enemyFields) { const int d=ef.at(kv.second.pos); if(d>=0) enemyBest=std::min(enemyBest,d); }
        if (enemyBest<(1<<20)) {
            if (enemyBest+1<d) gain*=.20;
            else if(d<=enemyBest && enemyBest<=d+3) gain*=1.65;
        }
        res.push_back({gain / (d + 1.0), {kv.second.pos, gain, false, 0}});
    }
    std::stable_sort(res.begin(), res.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<Node> views;
    if (hidden > 0.25 && unknownCount > 0) {
        // 未露面资源的期望“比分收益”：按 7.0 的折算（价值×0.35，另含任务的边际）
        double missionBonus = 0;
        for (const auto& m : p.missions) {
            if (mission_done(p, m.id)) continue;
            if (m.type == "high_resources" && mem_.highCollected() < m.target) missionBonus += 5.0 * hiddenHigh / std::max(1.0, hidden);
            if (m.type == "normal_resources" && mem_.normalCollected() < m.target) missionBonus += 2.5 * hiddenNormal / std::max(1.0, hidden);
        }
        const double perResource = 0.25 * (20.0 * hiddenHigh + 10.0 * hiddenNormal) / hidden + std::min(.5,missionBonus);
        const double density = hidden * perResource / unknownCount;  // 每个未知格的期望收益
        const int r = p.vision;
        const auto cells_at = [&](Point c) {
            int cells = 0;
            for (int yy = std::max(0, c.y - r); yy <= std::min(h - 1, c.y + r); ++yy)
                for (int xx = std::max(0, c.x - r); xx <= std::min(w - 1, c.x + r); ++xx)
                    cells += uncovered[static_cast<std::size_t>(yy) * w + xx];
            return cells;
        };
        for (int k = 0; k < kViewCount; ++k) {
            int bestCells = 0; Point bestPos{}; double bestScore = 0;
            if (k == 0 && has_view_goal_ && mem_.inside(view_goal_) && field_.at(view_goal_) > 0) {
                const int kept = cells_at(view_goal_);
                if (kept >= kViewMinCells) { bestCells = kept; bestPos = view_goal_; bestScore = 1e9; }
            }
            for (int y = 0; bestScore < 1e9 && y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const Point c{x, y};
                    if (mem_.at(c) == kWall) continue;
                    const int d = field_.at(c);
                    if (d <= 0 || d + 1 > remaining || d > kRouteHorizon) continue;
                    const int cells = cells_at(c);
                    if (cells < kViewMinCells) continue;
                    const double score = cells - 0.02 * d;
                    if (score > bestScore) { bestScore = score; bestCells = cells; bestPos = c; }
                }
            if (bestCells <= 0) break;
            for (int yy = std::max(0, bestPos.y - r); yy <= std::min(h - 1, bestPos.y + r); ++yy)
                for (int xx = std::max(0, bestPos.x - r); xx <= std::min(w - 1, bestPos.x + r); ++xx)
                    uncovered[static_cast<std::size_t>(yy) * w + xx] = 0;
            const double expectCount = hidden * bestCells / unknownCount;
            views.push_back({bestPos, density * bestCells * kRouteBeta, true, static_cast<int>(std::lround(kViewPad * expectCount))});
        }
    }
    const int maxNodes = 13;
    const int keepRes = std::min<int>(static_cast<int>(res.size()), maxNodes - static_cast<int>(views.size()));
    for (int i = 0; i < keepRes; ++i) nodes.push_back(res[static_cast<std::size_t>(i)].second);
    for (const auto& v : views) if (static_cast<int>(nodes.size()) < maxNodes) nodes.push_back(v);
    if (nodes.empty()) return false;
    const int n = static_cast<int>(nodes.size());
    const int budget = std::min(remaining, kRouteHorizon);
    std::vector<int> start(static_cast<std::size_t>(n));
    std::vector<std::vector<int>> between(n, std::vector<int>(n, -1));
    for (int i = 0; i < n; ++i) {
        start[static_cast<std::size_t>(i)] = field_.at(nodes[static_cast<std::size_t>(i)].pos) + nodes[static_cast<std::size_t>(i)].pad;
        const Field f = bfs(mask_, nodes[static_cast<std::size_t>(i)].pos, budget);
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            const int d = f.at(nodes[static_cast<std::size_t>(j)].pos);
            between[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = d < 0 ? -1 : d + nodes[static_cast<std::size_t>(j)].pad;
        }
    }
    // 收益随到达时间衰减（越晚越可能被别人拿走）：用“到达越晚价值越低”的近似——
    // 定向越野本身只看总价值，这里把预算限制在 kRouteHorizon 内来体现。
    std::vector<double> values(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) values[static_cast<std::size_t>(i)] = 100.0 * nodes[static_cast<std::size_t>(i)].value;
    std::vector<int> rivals(static_cast<std::size_t>(n), -1);
    for (int i=0;i<n;++i) if (!nodes[i].view) {
        int best=1<<20;
        for (const auto& ef:enemyFields) { const int d=ef.at(nodes[i].pos); if(d>=0) best=std::min(best,d); }
        if (best<(1<<20)) rivals[i]=best;
    }
    const Tour exact=solve_tour(start,between,values,budget);
    ArrivalTour tour{exact.order,static_cast<double>(exact.value),exact.length};
    if (!enemyFields.empty()) {
        const ArrivalTour raced=solve_arrival_tour(start,between,values,rivals,budget);
        const auto attainable=[&](const std::vector<int>& order) {
            double gain=0; int spent=0,last=-1;
            for (int i:order) {
                const int step=last<0?start[i]:between[last][i];
                if(step<0 || step>budget-spent) break;
                spent+=step; gain+=values[i]*resource_race(spent,rivals[i]).availability; last=i;
            }
            return gain;
        };
        // Do not throw away the exact known-value route for small differences
        // in an uncalibrated opponent-choice estimate. Change only with a clear
        // attainable-value margin and a first stop we can reach ahead of a rival.
        if (!raced.order.empty()) {
            const int first=raced.order.front();
            bool keepsKnown = raced.length<=exact.length;
            for (int kept:exact.order) if (!nodes[kept].view &&
                std::find(raced.order.begin(),raced.order.end(),kept)==raced.order.end()) keepsKnown=false;
            if (keepsKnown && !nodes[first].view && rivals[first]>=start[first]+2 &&
                raced.value>1.2*attainable(exact.order)+1e-6)
                tour=raced;
        }
    }
    for (int idx : tour.order) {
        const auto& node = nodes[static_cast<std::size_t>(idx)];
        const int d = field_.at(node.pos);
        if (d <= 0) continue;
        *goalOut = node.pos;
        const double firstGain=node.value;
        *rateOut = node.view ? tour.value / 100.0 / std::max(1, tour.length + 1) : std::max(firstGain / (d + 1.0), tour.value / 100.0 / std::max(1, tour.length + 1));
        *viewOut = node.view;
        if (node.view) { has_view_goal_ = true; view_goal_ = node.pos; } else has_view_goal_ = false;
        return true;
    }
    return false;
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
    if(!kUseFixedPhases)return 1.0;
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

double Bot::resource_priority_scale(const Perception& p) const {
    if(!kUseFixedPhases)return 1.0;
    const auto phase=phase_policy(p.tick,cfg_.maxTicks,p.resourceValue,p.hp);
    if (phase.resourceScale<=1.0 || incoming_damage(p,false)>0 || plan_.fury.active(p.tick)) return 1.0;
    for(const auto& e:p.entities)if(e.type==EntityType::Robot && e.team!=p.team &&
        e.hp>0 && e.hp<=20 && !e.protectedSpawn && manhattan(e.pos,p.self)<=2 &&
        dir_between(p.self,e.pos)>=0)return 1.0;
    return phase.resourceScale;
}

double Bot::collection_gain(const Perception& p,const std::vector<Resource>& resources) const {
    auto estimate=collection_score(p,mem_.normalCollected(),mem_.highCollected(),resources);
    for(const auto& r:resources)for(const auto& e:p.entities)
        if(e.type==EntityType::Robot && e.hp>0 && manhattan(e.pos,r.pos)<=8) {
            estimate.denialAux+=.025*std::max(0,r.value);break;
        }
    // Phase scaling is a separate priority preference, never an actual score claim.
    return estimate.utility()+(resource_priority_scale(p)-1)*estimate.resource;
}
double Bot::resource_gain(const Perception& p,const Resource& r) const {
    return collection_gain(p,{r});
}

double Bot::attack_value(const Perception& p,const Entity& target) const {
    return attack_action_value(p,{&target});
}
double Bot::attack_action_value(const Perception& p,const std::vector<const Entity*>& targets) const {
    return attack_estimate(p,targets).priority;
}
CandidateAudit Bot::attack_estimate(const Perception& p,const std::vector<const Entity*>& targets) const {
    CandidateAudit out;out.kind="attack";out.id=targets.empty()?"":targets.front()->team;
    out.target=targets.empty()?p.self:targets.front()->pos;
    const double stress=aggression_<.9?.8:(aggression_>1.1?.25:.5);
    const BattleMode modes[]={BattleMode::Hold,BattleMode::FocusSelf,BattleMode::Opportunistic};
    const double weights[]={(1-stress)*.5,stress,(1-stress)*.5};
    std::set<std::string> passiveTeams;
    for(const auto& kv:passive_)if(is_passive(kv.first))passiveTeams.insert(kv.first);
    const bool lethalRisk=incoming_damage(p,false)+hz_.damageAt(p.self,p.tick,p.protectedSpawn)>=p.hp;
    for(int mode=0;mode<3;++mode) {
        const auto result=forecast_battle(p,plan_,hz_,targets,modes[mode],
            (lethalRisk || modes[mode]==BattleMode::FocusSelf)?nullptr:&passiveTeams);
        const double preDeath=p.effectiveDamage+result.credit;
        const double after=forecast_adjusted_damage(p.effectiveDamage,result.credit,0,result.fatal?1:0);
        out.score.combat+=weights[mode]*(combat_points(after)-combat_points(p.effectiveDamage));
        if(result.fatal)out.deathLoss+=weights[mode]*(combat_points(preDeath)-combat_points(after));
        if(!result.fatal) {
            out.score.bufferAux+=weights[mode]*buffer_value(p.effectiveDamage,after);
            std::set<std::string> ids;
            for(const auto& m:p.missions)if(ids.insert(m.id).second && m.type=="effective_damage" &&
                !mission_done(p,m.id) && p.effectiveDamage<m.target) {
                if(after>=m.target)out.score.taskConditional+=weights[mode]*5;
                else out.score.progressAux+=weights[mode]*.25*std::min(1.0,result.credit/std::max(1.0,m.target));
            }
        }
    }
    out.nominalGain=out.score.points();
    const double raw=out.score.utility();
    out.phaseAux=raw>0?(aggression_-1)*raw:0;
    out.priority=raw+out.phaseAux;
    return out;
}

bool Bot::is_passive(const std::string& team) const {
    const auto it = passive_.find(team);
    return kPassiveTicks > 0 && it != passive_.end() && it->second.count >= kPassiveTicks &&
           observed_tick_-it->second.tick<=6;
}

bool Bot::safe_step(const Perception& p,int dir) const {
    if(dir<0||dir>kFirstWait)return false;
    Point next=p.self;
    if(dir!=kFirstWait) next={p.self.x+kDirX[dir],p.self.y+kDirY[dir]};
    if(mask_.blocked(next) || (dir!=kFirstWait && (hz_.blockedAt(next,p.tick) || hz_.lethalAt(next,p.tick))))return false;
    const bool protectedNext=p.protectedSpawn&&mem_.spawn()&&next==*mem_.spawn();
    const int hp=p.hp-hz_.damageAt(p.self,p.tick,p.protectedSpawn)-incoming_damage(p,false);
    return !hz_.lethalAt(next,p.tick+1,protectedNext)&&hz_.damageAt(next,p.tick+1,protectedNext)<std::max(1,hp);
}

bool Bot::wants_combat(const Perception& p) const {
    if (p.effectiveDamage < kHuntCap) return true;
    for (const auto& m : p.missions)
        if (m.type == "effective_damage" && !mission_done(p,m.id) && p.effectiveDamage < m.target) return true;
    return false;
}

// Exposure if currently visible opponents stay put and choose to fire next tick.
// It is a risk estimate, not knowledge of their next action or hidden buffs.
int Bot::projected_fire(const Perception& p, Point next) const {
    if (p.protectedSpawn && next == p.self) return 0;
    int total = 0;
    for (const auto& e : p.entities) {
        if (e.type != EntityType::Robot || e.hp <= 0 || e.team == p.team || e.protectedSpawn) continue;
        const int distance = manhattan(e.pos,next), dir = dir_between(e.pos,next);
        if (distance < 1 || distance > 2 || dir < 0) continue;
        bool screened = false;
        for (const auto& other : p.entities)
            if (other.type == EntityType::Robot && other.hp > 0 && other.team != e.team &&
                dir_between(e.pos,other.pos) == dir && manhattan(e.pos,other.pos) < distance) screened = true;
        if (!screened) total += 20;
    }
    if (plan_.vulnerable.active(p.tick+1)) total *= 2;
    return total;
}

int Bot::incoming_damage(const Perception& p,bool defend) const {
    if(p.protectedSpawn || (defend&&plan_.reflect.active(p.tick))) return 0;
    int total=0, quiet=0;
    for(const auto& e:p.entities) {
        if(e.type!=EntityType::Robot || e.hp<=0 || e.protectedSpawn || e.team==p.team || hz_.lethalAt(e.pos,p.tick,e.protectedSpawn) || hz_.damageAt(e.pos,p.tick,e.protectedSpawn)>=e.hp) continue;
        int distance=manhattan(e.pos,p.self);
        if(distance<1 || distance>2 || dir_between(e.pos,p.self)<0) continue;
        bool screened=false;
        for(const auto& o:p.entities)
            if(o.type==EntityType::Robot && o.hp>hz_.damageAt(o.pos,p.tick,o.protectedSpawn) && !hz_.lethalAt(o.pos,p.tick,o.protectedSpawn) && o.team!=e.team &&
               dir_between(e.pos,o.pos)==dir_between(e.pos,p.self) && manhattan(e.pos,o.pos)<distance) screened=true;
        if(!screened) { total += defend?10:20; if(is_passive(e.team))quiet += defend?10:20; }
    }
    if(plan_.valid && plan_.vulnerable.active(p.tick)) {total*=2;quiet*=2;}
    // Quiet history may soften nonlethal opportunity costs, never a lethal bound.
    return total+hz_.damageAt(p.self,p.tick,p.protectedSpawn)>=p.hp ? total : total-quiet;
}

Decision Bot::stage2(const Perception& p) {
    const int remaining = cfg_.maxTicks - p.tick;
    if (remaining <= 0) return {Action::wait(), "finished", 0};
    const double damageScore = p.effectiveDamage;
    const int tick = p.tick;
    const auto phase=phase_policy(tick,cfg_.maxTicks,p.resourceValue,p.hp);
    diagnostics_.strategyPhase=kUseFixedPhases?phase.name:"opportunity";
    diagnostics_.resourceScale=resource_priority_scale(p);
    diagnostics_.openingEnd=phase.openingEnd;
    diagnostics_.endgameStart=phase.endgameStart;

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

    // Reaching any point inside the announced fortress is sufficient. Do not
    // walk through its center or leave for an unrelated patrol during capture.
    if (!plan_.fury.active(tick)) for (const auto& event : plan_.events) {
        if (event.kind != "fortress" || !event.active(tick) || !event.contains(p.self) ||
            tick + 1 >= event.end ||
            incoming_damage(p,true) + hz_.damageAt(p.self,tick,p.protectedSpawn) >= p.hp ||
            hz_.lethalAt(p.self,tick+1,p.protectedSpawn)) continue;
        if (bestAttackDir >= 0 && bestAttack > 0)
            return {Action::attack(kDirX[bestAttackDir],kDirY[bestAttackDir]), "fortress_fire", bestAttack};
        return {Action::defend(), "fortress_dwell", 0};
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
        if(unknown>0)goals.push_back({c,(kUseOpportunities?std::min(1.0,.025*unknown):.10*unknown)/(d+2.0),"information"});
    }
    BlockMask enemyMask=mask_;
    for(const auto& e:p.entities)if(e.type==EntityType::Robot)enemyMask.set(e.pos,false);
    std::vector<Field> enemyFields;
    for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&e.team!=p.team)
        enemyFields.push_back(bfs(enemyMask,e.pos,remaining));
    if(kUseOpportunities) {
        const auto opportunities=resource_opportunities(p,mem_,mask_,field_,enemyFields,remaining,&hz_);
        diagnostics_.plannerExpansions=opportunities.expansions;
        diagnostics_.plannerBfs=opportunities.bfsRuns;
        for(auto c:opportunities.goals) {
            const int firstEta=field_.at(c.target);
            if(c.kind=="resource_plan")diagnostics_.resourceRaces.push_back({c.id,"confidence_heuristic",firstEta,c.rivalEta,c.confidence});
            if(c.rivalEta>=0) {
                if(c.rivalEta>firstEta)++diagnostics_.resourceAhead;
                else if(c.rivalEta==firstEta)++diagnostics_.resourceTied;
                else ++diagnostics_.resourceLate;
            }
            c.phaseAux=(resource_priority_scale(p)-1)*c.score.resource/(c.eta+1.0);
            c.priority+=c.phaseAux;
            goals.push_back({c.target,c.priority,c.kind,c});
        }
        for(const auto& item:mem_.resources()) {
            int d=field_.at(item.second.pos);
            if(d<0 || d>remaining) {
                CandidateAudit c;c.kind="resource_plan";c.id=item.first;c.target=item.second.pos;c.eta=d;
                c.rejected=d<0?"unreachable_public_map":"deadline";diagnostics_.record(c);
            }
        }
    } else {
    for (const auto& kv : mem_.resources()) {
        const int d = field_.at(kv.second.pos);
        if (d <= 0) {
            if (d<0) diagnostics_.resourceRaces.push_back({kv.first,"no_current_route",d,-1,0});
            continue;
        }
        double gain = resource_gain(p, kv.second);
        if (gain <= 0) continue;
        // 争夺评估：只在"看得见的敌人"上做，属于协议允许的信息
        int enemyBest = 1 << 20;
        for (const auto& ef:enemyFields) {
            const int ed=ef.at(kv.second.pos);if(ed>=0)enemyBest=std::min(enemyBest,ed);
        }
        const ResourceRace race = resource_race(d, enemyBest < (1<<20) ? enemyBest : -1);
        diagnostics_.resourceRaces.push_back({kv.first,race.reason,d,race.rivalEta,race.availability});
        if (race.rivalEta >= 0) {
            if (race.rivalEta > d) ++diagnostics_.resourceAhead;
            else if (race.rivalEta == d) ++diagnostics_.resourceTied;
            else ++diagnostics_.resourceLate;
        }
        // A rival is not committed to this resource. Keep the established
        // short-race urgency; use arrival-dependent discounts for later stops.
        if (race.rivalEta>=0) {
            if (race.rivalEta+1<d) gain*=.20;
            else if (d<=race.rivalEta && race.rivalEta<=d+3) gain*=1.65;
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
    }
    // 开局路线只在“没有更要紧的事”时参与：能开火（bestAttack>0）、血量低于 50、或有天使/堡垒/辣椒
    // 事件目标时不出路线目标——探图的期望价值是粗估，不能压过补刀、回血和事件。
    bool eventGoal = false;
    for (const auto& event : plan_.events)
        if ((event.kind == "heal" || event.kind == "fortress" || event.kind == "chili") && event.end > tick) eventGoal = true;
    bool finishShot=false;
    for (const auto& e:p.entities) if(e.type==EntityType::Robot && e.team!=p.team &&
        e.hp>0 && e.hp<=20 && manhattan(e.pos,p.self)<=2 && dir_between(p.self,e.pos)>=0) finishShot=true;
    bool urgentResource=false;
    bool nearbyResource=false;
    for (const auto& race:diagnostics_.resourceRaces)
        if(race.ourEta>0) {
            if(race.ourEta<=2 && race.rivalEta>=race.ourEta && race.rivalEta<=race.ourEta+2) urgentResource=true;
            if(race.ourEta<=8 && (race.rivalEta<0 || race.rivalEta+1>=race.ourEta)) nearbyResource=true;
        }
    // The clock opens an opportunity; actual known resources, HP, fire and
    // events decide whether to use it. Immediate finishing shots retain priority.
    const bool openingFocus=phase.opening && nearbyResource && p.resourceValue<120 &&
        p.hp>=60 && incoming_damage(p,false)==0 && !finishShot && !eventGoal && !plan_.fury.active(tick);
    diagnostics_.resourceFocus=kUseFixedPhases && openingFocus;
    const bool collectionWindow = (urgentResource || (kUseFixedPhases && openingFocus)) && p.resourceValue<120 &&
        p.hp>=60 && incoming_damage(p,false)==0 && bestAttack<(openingFocus?2.0:1.25) && !finishShot;
    // Known-target lookahead does not replace the existing observation tour.
    // Discovery value stays an auxiliary estimate rather than invented score.
    const bool needsObservation=!mem_.frontiers().empty();
    bool hasResourcePlan=false;
    for(const auto& g:goals)if(g.reason=="resource_plan" && g.audit.score.utility()>0)hasResourcePlan=true;
    if ((!kUseOpportunities || (needsObservation && !hasResourcePlan)) && (bestAttack <= 0 || collectionWindow) && p.hp >= 50 && !eventGoal) {
        Point routeGoal{};
        double routeRate = 0;
        bool routeView = false;
        if (stage2_route_goal(p, enemyFields, &routeGoal, &routeRate, &routeView))
            goals.push_back({routeGoal, kRouteBoost * routeRate, routeView ? "route_view" : "route_collect"});
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
        if (event.kind == "fortress") {
            int nearest = std::numeric_limits<int>::max();
            for (int y=event.a.y; y<=event.b.y; ++y) for (int x=event.a.x; x<=event.b.x; ++x) {
                const int d=field_.at({x,y});
                if (d>0 && d<nearest && safe_step(p,field_.firstAt({x,y}))) { nearest=d; anchor={x,y}; }
            }
        }
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
            int potentialKills=0;
            for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.hp>0&&!e.protectedSpawn&&e.pos!=event.a&&(e.pos.x==event.a.x||e.pos.y==event.a.y)&&e.hp<=event.damage)
                ++potentialKills;
            const double bonus=combat_points(p.effectiveDamage+60.0*potentialKills)-combat_points(p.effectiveDamage);
            if(bonus>0)goals.push_back({event.a,1.5*bonus/(distance+1.0),"chili_trigger_estimate"});
        }
    }

    // Revisit recent public sightings when no current target can be acquired.
    // The position is a decaying hypothesis, discarded immediately when disproved by sight.
    if(wants_combat(p))for(const auto& item:sightings_){
        const auto& seen=item.second;const int age=tick-seen.tick;
        if(age<=0||age>40)continue;
        if(damageScore>=500 && seen.robot.hp>=p.hp && !plan_.fury.active(tick))continue;
        for(int dir=0;dir<4;++dir){Point target{seen.robot.pos.x+2*kDirX[dir],seen.robot.pos.y+2*kDirY[dir]};
            int d=field_.at(target);if(d<=0||d+age>=remaining)continue;
            goals.push_back({target,0.7*std::exp(-age/20.0)/(d+1.0),"sighting_search"});
        }
    }

    // 3b) 追猎。8.0：半径从 5 格放大到整个视野（10 格），7.0 后期 70% 以上的 Tick 在漫游。
    if (wants_combat(p)) {
        for (const auto& e : p.entities) {
            if (e.type != EntityType::Robot || e.hp <= 0 || e.protectedSpawn || e.team == p.team) continue;
            if (manhattan(e.pos, p.self) > hunt_radius_) continue;
            if(tick<hunt_block_until_ && e.hp>20)continue;
            if(damageScore>=500 && e.hp>20 && e.hp>=p.hp && !plan_.fury.active(tick))continue;
            const double credit=std::min(20,e.hp)+(e.hp<=20?60.0:0.0);
            double expected=combat_points(damageScore+credit)-combat_points(damageScore)+buffer_value(damageScore,damageScore+credit);
            if(e.defending)expected*=.6;
            const auto seen=sightings_.find(e.team);
            if(seen!=sightings_.end() && seen->second.velocity!=Point{0,0}){
                const Point vel=seen->second.velocity;
                for(int look=1;look<=3;++look){
                    Point anticipated{e.pos.x+look*vel.x,e.pos.y+look*vel.y};
                    if(!mem_.inside(anticipated)||mem_.isWall(anticipated))break;
                    for(int dir=0;dir<4;++dir){
                        Point c{anticipated.x+2*kDirX[dir],anticipated.y+2*kDirY[dir]};int d=field_.at(c);
                        if(d>0 && d<=look+2)goals.push_back({c,(expected+.15)/(d+look+1.0),"intercept"});
                    }
                }
            }
            if(manhattan(e.pos,p.self)<=2 && dir_between(p.self,e.pos)>=0 && bestAttack>0)continue;
            for (int dir = 0; dir < 4; ++dir) {
                for (int step = 1; step <= 2; ++step) {
                    const Point c{e.pos.x + kDirX[dir] * step, e.pos.y + kDirY[dir] * step};
                    if (!mem_.inside(c) || c == p.self) continue;
                    const int d = field_.at(c);
                    if (d <= 0) continue;
                    goals.push_back({c, (expected + (step==2 ? 0.15 : 0.0)) / (d + 1.0), "hunt"});
                }
            }
        }
    }
    // 搜索：看不见敌人时去敌人最可能出现的地方（目击预测已在上面；这里补任务点/中心与四象限巡逻点）。
    // A distant/protected visible enemy must not disable every patrol goal.
    if (p.vision > 0 && wants_combat(p) && tick >= kSeekFrom) {
        const int w = mem_.width(), h = mem_.height();
        std::vector<Point> patrol;
        for (const auto& m : p.missions) if (m.type == "reach" && m.position) patrol.push_back(*m.position);
        patrol.push_back({w / 2, h / 2});
        patrol.push_back({w / 4, h / 4});
        patrol.push_back({3 * w / 4, h / 4});
        patrol.push_back({w / 4, 3 * h / 4});
        patrol.push_back({3 * w / 4, 3 * h / 4});
        for (const auto& kv : enemy_spawn_) patrol.push_back(kv.second);
        // 没确认过的复活点：按本车出生点做镜像猜测（练习包与随机图都是四角对称出生）。
        // 停在复活点不动的对手（例如资源采完就原地等待的程序）只能这样找到。
        if (mem_.spawn()) {
            const Point s0 = *mem_.spawn();
            for (const Point g : {Point{w - 1 - s0.x, s0.y}, Point{s0.x, h - 1 - s0.y}, Point{w - 1 - s0.x, h - 1 - s0.y}}) {
                bool known = false;
                for (const auto& kv : enemy_spawn_) if (manhattan(kv.second, g) <= 2) known = true;
                if (!known && g != s0) patrol.push_back(g);
            }
        }
        for (const auto& q : patrol) {
            if (!mem_.inside(q) || mem_.isWall(q)) continue;
            const int d = field_.at(q);
            if (d <= 0) { patrol_visit_[{q.x, q.y}] = tick; continue; }
            const auto it = patrol_visit_.find({q.x, q.y});
            const int since = it == patrol_visit_.end() ? 999 : tick - it->second;
            const double fresh = std::min(1.0, since / 40.0);
            if (fresh <= 0) continue;
            goals.push_back({q, kSeekW * fresh / (d + 1.0), "seek"});
        }
    }

    // Reduce speculative pursuit only when there is a concrete scoring target.
    // Immediate attack values and all hazard/fortress overrides are unchanged.
    bool unfinishedCollectionMission=false,scoreableGoal=false;
    for(const auto& m:p.missions)if(!mission_done(p,m.id) &&
        (m.type=="normal_resources" || m.type=="high_resources" || m.type=="resource_value"))unfinishedCollectionMission=true;
    for(const auto& g:goals)if(((g.reason=="resource" || g.reason=="resource_plan") &&
        (p.resourceValue<120 || unfinishedCollectionMission)) || g.reason=="reach" || g.reason=="task_plan") {
        if(g.score>0 && safe_step(p,field_.firstAt(g.pos)))scoreableGoal=true;
    }
    const double pursuitScale=kUseFixedPhases && (openingFocus || (phase.endgame && scoreableGoal))?.70:1.0;
    diagnostics_.pursuitScale=pursuitScale;
    for(auto& g:goals)if(g.reason=="hunt" || g.reason=="intercept" ||
        g.reason=="sighting_search" || g.reason=="seek")g.score*=pursuitScale;

    // An immediate attack can postpone an irreversible resource opportunity.
    // Compare the SAME route delayed by one tick, not an arbitrary resource multiplier.
    const Goal* urgentAlternative=nullptr;
    if(kUseOpportunities && !finishShot && !plan_.fury.active(tick))for(const auto& g:goals)
        if(g.reason=="resource_plan" && g.audit.score.resource>0 && field_.at(g.pos)<=8 &&
            safe_step(p,field_.firstAt(g.pos)) && (!urgentAlternative || g.score>urgentAlternative->score))urgentAlternative=&g;
    const double missedForAttack=urgentAlternative?urgentAlternative->audit.lostIfDelayed:0;
    Field urgentReverse;
    if(urgentAlternative)urgentReverse=bfs(mask_,urgentAlternative->pos,remaining);
    if(bestAttack>0 && incoming_damage(p,false)+hz_.damageAt(p.self,tick,p.protectedSpawn)<p.hp)
        bestAttack-=missedForAttack;

    for(auto& g:goals)if(g.audit.kind.empty()) {
        auto& c=g.audit;c.kind=g.reason;c.target=g.pos;c.eta=std::max(1,field_.at(g.pos));
        if(g.reason=="resource" || g.reason=="route_collect") {
            for(const auto& item:mem_.resources())if(item.second.pos==g.pos) {
                c.id=item.first;c.score=collection_score(p,mem_.normalCollected(),mem_.highCollected(),{item.second});break;
            }
        } else if(g.reason=="reach"){c.score.taskConditional=5;c.score.reachTaskConditional=5;}
        c.nominalGain=c.score.points();c.priority=g.score;
        c.phaseAux=g.score-c.score.utility()/(c.eta+1.0);
    }
    if(bestAttackDir>=0) {
        std::vector<const Entity*> targets;
        for(const auto& e:p.entities)if(e.type==EntityType::Robot && e.hp>0 && e.team!=p.team &&
            ((!plan_.fury.active(tick) && dir_between(p.self,e.pos)==bestAttackDir && manhattan(p.self,e.pos)<=2) ||
             (plan_.fury.active(tick) && std::abs(e.pos.x-p.self.x)<=2 && std::abs(e.pos.y-p.self.y)<=2)))targets.push_back(&e);
        if(!targets.empty()) {
            if(!plan_.fury.active(tick)) {
                auto nearest=*std::min_element(targets.begin(),targets.end(),[&](const Entity* a,const Entity* b){return manhattan(a->pos,p.self)<manhattan(b->pos,p.self);});
                targets={nearest};
            }
            auto audit=attack_estimate(p,targets);
            audit.missedResourceLoss=std::max(0.0,audit.priority-bestAttack);audit.priority=bestAttack;
            diagnostics_.record(audit);
        }
    }

    // A coordinate task has the same 5 points now or after a short resource
    // route. Reserve recoverable continuation credit instead of counting it
    // as a unique advantage of going straight there now. This affects priority
    // only, not the nominal rule-score forecast. A 10% margin stays for uncertainty.
    double recoverableReach=0;
    if(kUseOpportunities)for(const auto& g:goals)
        if(g.reason=="resource_plan" && g.audit.score.resource>0 &&
            g.audit.score.reachTaskConditional>0 && g.audit.eta+3<remaining &&
            safe_step(p,field_.firstAt(g.pos)))
            recoverableReach=std::max(recoverableReach,.90*g.audit.score.reachTaskConditional);

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
    diagnostics_.goals = static_cast<int>(goals.size());
    for (auto& g : goals) {
        const int first=field_.firstAt(g.pos);
        if(!safe_step(p,first)){
            ++diagnostics_.unsafeGoals;g.audit.rejected="unsafe_first_step";
            diagnostics_.record(g.audit);continue;
        }
        double s = fatal ? -deathCost : g.score;
        if(!fatal && recoverableReach>0 && g.audit.score.reachTaskConditional>0) {
            g.audit.deferredTaskAux=std::min(recoverableReach,g.audit.score.reachTaskConditional);
            s-=g.audit.deferredTaskAux/(g.audit.eta+1.0);
        }
        Point next=p.self;
        if(first!=kFirstWait)next={p.self.x+kDirX[first],p.self.y+kDirY[first]};
        s-=0.005*hz_.damageAt(next,tick+1,false);
        if (!fatal && !kUseOpportunities && damageScore>=500) {
            const int exposure=projected_fire(p,next);
            const int after=p.hp-incoming_damage(p,false)-hz_.damageAt(p.self,tick,p.protectedSpawn);
            s-=0.004*std::max(0,exposure-20)*(p.hp<50?2.0:1.0);
            // Extra exposure matters most once score is capped and there is no
            // collection mission left to justify a low-HP detour through fire.
            if (p.hp<=40 && p.resourceValue>=120 && std::all_of(p.missions.begin(),p.missions.end(),[&](const Mission& m){return mission_done(p,m.id);})) s-=0.003*exposure;
            if (exposure>20 && exposure>=after) s-=2.0;
        }
        if(!fatal && kUseOpportunities && first!=kFirstWait) {
            const int exposure=projected_fire(p,next);
            const int after=p.hp-incoming_damage(p,false)-hz_.damageAt(p.self,tick,p.protectedSpawn);
            if(exposure>0 && exposure>=after) {
                const double risk=.5; // stationary-visible-opponent stress scenario, not calibrated probability
                g.audit.deathLoss=risk*death_score_loss(damageScore);
                g.audit.delayAuxLoss=risk*std::min(.75,3*std::max(0.0,g.score));
                s-=(g.audit.deathLoss+g.audit.delayAuxLoss)/(g.audit.eta+1.0);
            }
        }
        if(!fatal && urgentAlternative && g.pos!=urgentAlternative->pos) {
            const int now=field_.at(urgentAlternative->pos),later=urgentReverse.at(next);
            const int delay=first==kFirstWait?1:later<0?2:std::clamp(1+later-now,0,2);
            g.audit.missedResourceLoss=delay*urgentAlternative->audit.lostIfDelayed;
            s-=g.audit.missedResourceLoss/(g.audit.eta+1.0);
        }
        if (s>0 && has_last_goal_ && g.pos == last_goal_) s *= 1.12;
        g.audit.phaseAux=s-(g.audit.score.utility()-g.audit.deathLoss-g.audit.delayAuxLoss-g.audit.missedResourceLoss-g.audit.deferredTaskAux)/(g.audit.eta+1.0);
        g.audit.priority=s;
        diagnostics_.record(g.audit);
        if (s > chosenScore) {
            chosenScore = s;
            chosen = g;
            hasGoal = true;
        }
    }

    if (hasGoal && !fatal && chosenScore<=0) hasGoal=false;
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
        int8_t dir = field_.firstAt(chosen.pos);
        // Experiment: only break shortest-path ties when no events/enemies need risk scoring.
        if (dir >= 0 && !hz_.any() && diagnostics_.visibleEnemies == 0) {
            const int distance = field_.at(chosen.pos);
            const Field reverse = bfs(mask_, chosen.pos, distance);
            const auto coverage = [&](int candidate) {
                const Point next{p.self.x + kDirX[candidate], p.self.y + kDirY[candidate]};
                int gain = 0;
                for (int y=next.y-p.vision;y<=next.y+p.vision;++y)
                    for (int x=next.x-p.vision;x<=next.x+p.vision;++x)
                        if (mem_.inside({x,y}) && mem_.at({x,y}) == kUnknown) ++gain;
                return gain;
            };
            int bestCoverage = coverage(dir);
            for (int candidate=0;candidate<4;++candidate) {
                const Point next{p.self.x+kDirX[candidate],p.self.y+kDirY[candidate]};
                if (!safe_step(p,candidate) || reverse.at(next) != distance-1) continue;
                const int gain = coverage(candidate);
                if (gain > bestCoverage) { bestCoverage=gain; dir=static_cast<int8_t>(candidate); }
            }
        }
        if(dir==kFirstWait) return {Action::defend(), "wait_for_window", chosenScore};
        if (dir >= 0) {
            if(has_last_goal_ && chosen.pos!=last_goal_)++diagnostics_.targetSwitches;
            last_goal_ = chosen.pos;
            has_last_goal_ = true;
            const int send = (plan_.valid && plan_.inverted.active(tick)) ? (dir + 2) % 4 : dir;
            return {Action::move(kDirX[send], kDirY[send]), chosen.reason, chosenScore};
        }
    }
    if(!fatal)for(const auto& event:plan_.events)if(event.kind=="fortress"&&event.active(tick)&&event.contains(p.self)&&!plan_.fury.active(tick))
        return {Action::defend(),"fortress_dwell",0};
    if (!fatal) {
        int bestDir=-1, bestExposure=projected_fire(p,p.self);
        for(int dir=0;dir<4;++dir) {
            if(!safe_step(p,dir))continue;
            const Point next{p.self.x+kDirX[dir],p.self.y+kDirY[dir]};
            const int exposure=projected_fire(p,next);
            if(exposure<bestExposure){bestExposure=exposure;bestDir=dir;}
        }
        if(bestDir>=0) {
            const int send=plan_.inverted.active(tick)?(bestDir+2)%4:bestDir;
            return {Action::move(kDirX[send],kDirY[send]),"reduce_exposure",0};
        }
        if(!wants_combat(p)) return {Action::defend(),"score_guard",0};
    }
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
    if (!found) return {Action::wait(), cfg_.stage==2 ? (frontiers.empty()?"explore_no_frontier":"explore_no_safe_frontier") : why, 0};
    const int8_t dir = field_.firstAt(best);
    if(dir==kFirstWait) return {cfg_.stage==1?Action::wait():Action::defend(), "wait_for_window", 0};
    if (dir < 0) return {Action::wait(), cfg_.stage==2?"explore_invalid_first_step":why, 0};
    const int send=(plan_.valid && plan_.inverted.active(p.tick))?(dir+2)%4:dir;
    return {Action::move(kDirX[send], kDirY[send]), why, 0};
}

}  // namespace rmtc9
