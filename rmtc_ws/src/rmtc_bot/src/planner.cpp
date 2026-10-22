// OWNER: A
#include "rmtc_bot/planner.hpp"
#include <algorithm>
#include <array>
#include <queue>

namespace rmtc_bot {
void Planner::begin_tick(const Perception& p,const std::optional<LLMPlan>& plan,int max_ticks) {
    tick_=static_cast<int>(p.tick); max_ticks_=max_ticks; regions_.clear(); occupied_.clear();
    if(plan && plan->session_id==p.session_id) regions_=plan->regions;
    for(const auto& e:p.entities) if(e.type==EntityType::Robot&&e.hp>0) occupied_.push_back(e.pos);
}
std::optional<rmtc::decision::Route> Planner::plan(const rmtc::decision::RouteQuery& q) const {
    using namespace rmtc::decision;
    const auto inside=[&](Vec2 p){return p.x>=0&&p.y>=0&&p.x<q.width&&p.y<q.height;};
    if(q.width!=memory_.width()||q.height!=memory_.height()||!inside(q.from)||!inside(q.goal))return {};
    if(q.from==q.goal)return Route{0,{}};
    const int horizon=std::max(0,max_ticks_-tick_);
    const std::size_t cells=static_cast<std::size_t>(q.width)*q.height;
    bool timed=!regions_.empty();
    for(const auto& row:memory_.grid()) for(const auto& c:row) if(c.temporary_until>tick_)timed=true;
    // A bounded fallback treats forecast danger conservatively if a huge map is supplied.
    if(cells*static_cast<std::size_t>(horizon+1)>2000000)timed=false;
    const auto blocked=[&](Vec2 p,int action_tick,int arrival_tick){
        if(std::find(occupied_.begin(),occupied_.end(),p)!=occupied_.end())return true;
        if(std::find(q.blocked.begin(),q.blocked.end(),p)!=q.blocked.end()) {
            const auto& c=memory_.grid()[p.y][p.x];
            if(c.temporary_until<0||action_tick<c.temporary_until)return true;
        }
        for(const auto& r:regions_) {
            if(!r.contains(p))continue;
            if(r.wall&&memory_.initial_spawn()&&p==*memory_.initial_spawn())continue;
            if((!timed&&r.end_tick>tick_)||r.active(action_tick)||r.active(arrival_tick))return true;
        }
        return false;
    };
    struct Node {Vec2 p;int steps;std::optional<Direction> first;bool first_wait;};
    std::queue<Node> queue;
    const auto index=[&](Vec2 p,int t){return (timed?static_cast<std::size_t>(t)*cells:0)+static_cast<std::size_t>(p.y)*q.width+p.x;};
    std::vector<bool> seen(cells*(timed?static_cast<std::size_t>(horizon+1):1),false);
    queue.push({q.from,0,{},false});seen[index(q.from,0)]=true;
    constexpr std::array<Direction,4> dirs{Direction::Up,Direction::Right,Direction::Down,Direction::Left};
    std::size_t expanded=0;
    while(!queue.empty()&&expanded++<200000) {
        const auto n=queue.front();queue.pop();if(n.steps>=horizon)continue;
        // A forecast burst acts before movement: leaving on its activation Tick is too late.
        bool already_unsafe=false;
        for(const auto& r:regions_) if(!r.wall&&r.active(tick_+n.steps)&&r.contains(n.p))already_unsafe=true;
        if(already_unsafe)continue;
        for(int option=0;option<(timed?5:4);++option) {
            const bool wait=option==4;
            const auto p=wait?n.p:step(n.p,dirs[option]);
            const int next=n.steps+1;
            if(!inside(p)||blocked(p,tick_+n.steps,tick_+next)||seen[index(p,next)])continue;
            const auto first=n.steps==0?(wait?std::optional<Direction>{}:std::optional<Direction>{dirs[option]}):n.first;
            const bool first_wait=n.steps==0?wait:n.first_wait;
            if(p==q.goal)return Route{next,first,first_wait};
            seen[index(p,next)]=true;queue.push({p,next,first,first_wait});
        }
    }
    return {};
}
std::optional<std::string> Planner::next_step(Vec2 from,Vec2 to) const {
    auto route=plan({from,to,memory_.width(),memory_.height(),memory_.walls()});
    if(!route)return {};
    if(route->wait_first||!route->first_step)return std::string("wait");
    return std::string(rmtc::decision::name(*route->first_step));
}
} // namespace rmtc_bot
