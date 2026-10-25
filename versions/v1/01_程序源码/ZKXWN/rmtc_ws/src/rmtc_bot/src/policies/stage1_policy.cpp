// OWNER: A
#include "rmtc_bot/policies/stage1_policy.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rmtc_bot {
Action Stage1Policy::decide(const Perception& p, WorldMemory& m, const std::optional<LLMPlan>&) {
    planner_.begin_tick(p,std::nullopt,config_.max_ticks);
    auto c=config_; c.stage=1;
    const auto state=make_game_state(c,p,m,std::nullopt);
    const auto previous=last_;
    last_=engine_.decide(state);
    if(last_.reused_tick && previous.submit) {
        last_=previous;last_.reused_tick=true;
        return bridge_action(last_.action);
    }
    if(!last_.submit || !p.alive || p.hp<=0 || last_.reused_tick) return bridge_action(last_.action);

    // Plan a short collection tour using only remembered public observations.
    // Replan each tick; no full-map or future-resource information is available.
    using namespace rmtc::decision;
    struct Target { Resource resource; Route route; };
    std::vector<Target> targets;
    for(const auto& r:state.resources) {
        if(r.position==p.self_pos) return bridge_action(last_.action);
        auto route=planner_.plan({p.self_pos,r.position,state.width,state.height,state.known_walls});
        if(route && route->distance>0 && route->first_step)
            targets.push_back({r,*route});
    }
    if(targets.size()<2) return bridge_action(last_.action);
    std::stable_sort(targets.begin(),targets.end(),[](const Target& a,const Target& b){
        return a.resource.value/(a.route.distance+1)>b.resource.value/(b.route.distance+1);
    });
    // Bound work even if an unusually large map contains many remembered items.
    if(targets.size()>16)targets.resize(16);
    const int n=static_cast<int>(targets.size()), remaining=config_.max_ticks-static_cast<int>(p.tick);
    std::vector<std::vector<int>> distances(n,std::vector<int>(n,-1));
    for(int i=0;i<n;++i)for(int j=0;j<n;++j)if(i!=j) {
        auto route=planner_.plan({targets[i].resource.position,targets[j].resource.position,
                                  state.width,state.height,state.known_walls});
        if(route)distances[i][j]=route->distance;
    }
    struct Tour {std::uint32_t seen;int tail,first,elapsed;double reward;};
    std::vector<Tour> beam;
    for(int i=0;i<n;++i) {
        const int d=targets[i].route.distance;
        beam.push_back({std::uint32_t{1}<<i,i,i,d,targets[i].resource.value*std::exp(-d/50.0)});
    }
    const auto better=[](const Tour& a,const Tour& b){
        if(a.reward!=b.reward)return a.reward>b.reward;
        if(a.elapsed!=b.elapsed)return a.elapsed<b.elapsed;
        return a.first<b.first;
    };
    Tour best=*std::min_element(beam.begin(),beam.end(),better);
    for(int depth=1;depth<6;++depth) {
        std::vector<Tour> next;
        for(const auto& tour:beam)for(int j=0;j<n;++j) {
            if(tour.seen&(std::uint32_t{1}<<j))continue;
            const int d=distances[tour.tail][j];
            if(d<0 || tour.elapsed+d>remaining)continue;
            const int arrival=tour.elapsed+d;
            Tour candidate{tour.seen|(std::uint32_t{1}<<j),j,tour.first,arrival,
                           tour.reward+targets[j].resource.value*std::exp(-arrival/50.0)};
            if(better(candidate,best))best=candidate;
            next.push_back(candidate);
        }
        if(next.empty())break;
        std::stable_sort(next.begin(),next.end(),better);
        if(next.size()>64)next.resize(64);
        beam=std::move(next);
    }
    const auto& chosen=targets[best.first];
    last_.action={ActionKind::Move,chosen.route.first_step};
    last_.goal=chosen.resource.position;
    last_.expected_position=step(p.self_pos,*chosen.route.first_step);
    last_.reason="collection_tour";last_.utility=best.reward;
    return bridge_action(last_.action);
}
}
