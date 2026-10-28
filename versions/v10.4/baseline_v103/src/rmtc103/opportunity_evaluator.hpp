#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "rmtc103/score_model.hpp"
#include "rmtc103/resource_race.hpp"
#include "rmtc103/round2_policy.hpp"

namespace rmtc103 {
struct CandidateAudit {
    std::string kind,id,rejected;
    Point target;
    int eta=1,rivalEta=-1,age=0,stops=1;
    double nominalGain=0,confidence=1,deathLoss=0,delayAuxLoss=0;
    double phaseAux=0,priority=0;
    double lostIfDelayed=0,missedResourceLoss=0;
    double deferredTaskAux=0;
    double rivalCommitment=0;
    std::vector<std::string> routeIds;
    std::vector<int> routeEtas;
    ScoreEstimate score;
};
struct OpportunityResult { std::vector<CandidateAudit> goals; int expansions=0,bfsRuns=0; };

inline double resource_confidence(int ours,int rival,int age) {
    const double timing=rival<0?.95:resource_race(ours,rival).availability;
    const double fresh=1.0/(1.0+std::max(0,age)/60.0);
    return std::clamp(timing*std::max(.40,fresh),0.0,1.0);
}

// Bounds the effect of a distance-only claim. A robot can choose another
// public resource or fight. This is an uncalibrated choice heuristic.
inline double choice_confidence(int ours,int rival,int age,double commitment) {
    if(kRound2Level==0)return resource_confidence(ours,rival,age);
    const double timing=rival<0?.95:(1-std::clamp(commitment,0.0,1.0))*.95+
        std::clamp(commitment,0.0,1.0)*resource_race(ours,rival).availability;
    const double fresh=1.0/(1.0+std::max(0,age)/60.0);
    return std::clamp(timing*std::max(.40,fresh),0.0,1.0);
}

inline double irreversible_resource_loss(double gain,int ours,int rival,int age,double choice,int delay) {
    if(rival<0 || delay<=0 || gain<=0)return 0;
    // Hold observation freshness fixed: uncertainty becoming older is not an
    // observed irreversible loss. Compare the same publicly contested pickup.
    return gain*std::max(0.0,choice_confidence(ours,rival,age,choice)-choice_confidence(ours+delay,rival,age,choice));
}

inline double direct_target_delay_loss(const Perception& p,const CandidateAudit& c,
    const Resource& target,int eta,int delay) {
    // If another pickup precedes this target, its score cap/mission effects
    // belong to the route outcome model. Do not sell a separate target loss.
    if(c.routeIds.empty() || c.routeIds.front()!=target.id)return 0;
    const double gain=resource_points(p.resourceValue+target.value)-resource_points(p.resourceValue);
    return irreversible_resource_loss(gain,eta,c.rivalEta,c.age,c.rivalCommitment,delay);
}

// At most 12 stops considered, depth<=3 and <=2000 expansions, BFS only on
// the public-memory block mask. Singleton distant goals remain eligible.
inline OpportunityResult short_resource_routes(const Perception&,const WorldMap&,
    const BlockMask&,const Field&,const std::vector<Field>&,int,const HazardField*);
inline OpportunityResult resource_opportunities(const Perception& p,const WorldMap& mem,
    const BlockMask& mask,const Field& field,const std::vector<Field>& enemies,int remaining,
    const HazardField* hazards=nullptr) {
    if(kRound2Level>=2)return short_resource_routes(p,mem,mask,field,enemies,remaining,hazards);
    OpportunityResult out;
    struct Node { Resource r;int d=0,rival=-1,age=0;double rank=0,commitment=1; };
    std::vector<Node> nodes;
    for(const auto& item:mem.resources()) {
        const auto& r=item.second;int d=field.at(r.pos);
        if(d<=0 || d>remaining)continue;
        int rival=-1;double commitment=0;double worstConfidence=1;
        for(const auto& e:enemies){
            const int q=e.at(r.pos);if(q<0)continue;
            double choice=.85;int alternative=1<<20;
            for(const auto& other:mem.resources())if(other.first!=r.id){
                const int z=e.at(other.second.pos);if(z>=0)alternative=std::min(alternative,z);
            }
            if(alternative+1<q)choice=.25;
            else if(alternative<=q+1)choice=.55;
            const double candidate=choice_confidence(d,q,0,choice);
            if((kRound2Level==0 && (rival<0 || q<rival)) ||
               (kRound2Level>0 && candidate<worstConfidence)) {
                rival=q;commitment=choice;worstConfidence=candidate;
            }
        }
        int age=r.observedTick<0?0:std::max(0,p.tick-r.observedTick);
        auto gain=collection_score(p,mem.normalCollected(),mem.highCollected(),{r});
        double confidence=choice_confidence(d,rival,age,commitment);
        nodes.push_back({r,d,rival,age,gain.utility()*confidence/(d+1.0),commitment});
    }
    std::set<Point> reachPositions;
    for(const auto& m:p.missions)if(m.type=="reach" && m.position && !score_mission_done(p,m.id) &&
        reachPositions.insert(*m.position).second) {
        int d=field.at(*m.position);if(d<=0 || d>remaining)continue;
        bool represented=false;for(const auto& n:nodes)if(n.r.pos==*m.position)represented=true;
        if(represented)continue;
        Resource waypoint{"@reach:"+m.id,*m.position,0,p.tick};
        auto gain=collection_score(p,mem.normalCollected(),mem.highCollected(),{waypoint});
        nodes.push_back({waypoint,d,-1,0,gain.points()/(d+1.0)});
    }
    std::stable_sort(nodes.begin(),nodes.end(),[](const Node& a,const Node& b){return a.rank>b.rank;});
    if(nodes.size()>12)nodes.resize(12);
    const int n=static_cast<int>(nodes.size());if(!n)return out;
    std::vector<Field> between;between.reserve(n);
    for(const auto& node:nodes){between.push_back(bfs(mask,node.r.pos,std::min(24,remaining)));++out.bfsRuns;}
    std::vector<CandidateAudit> best(n);
    std::vector<bool> found(n,false);
    std::array<int,3> order{};
    auto evaluate=[&](int depth,int time) {
        std::vector<Resource> prefix;
        ScoreEstimate expected,delayed,previous;
        int spent=0,last=-1;double joint=1,delayedJoint=1;int first=order[0];
        for(int k=0;k<depth;++k) {
            const int j=order[k];spent+=last<0?nodes[j].d:between[last].at(nodes[j].r.pos);
            if(hazards && (hazards->lethalAt(nodes[j].r.pos,p.tick+spent,false) ||
                hazards->blockedAt(nodes[j].r.pos,p.tick+spent) ||
                hazards->damageAt(nodes[j].r.pos,p.tick+spent,false)>=p.hp))return;
            const double confidence=nodes[j].r.value>0?choice_confidence(spent,nodes[j].rival,nodes[j].age,nodes[j].commitment):1.0;
            const double delayedConfidence=nodes[j].r.value>0?choice_confidence(spent+1,nodes[j].rival,nodes[j].age+1,nodes[j].commitment):1.0;
            delayedJoint=std::min(delayedJoint,delayedConfidence);
            joint=std::min(joint,confidence);prefix.push_back(nodes[j].r);
            const auto current=collection_score(p,mem.normalCollected(),mem.highCollected(),prefix);
            expected.resource+=(current.resource-previous.resource)*confidence;
            const double reachGain=current.reachTaskConditional-previous.reachTaskConditional;
            const double collectionTaskGain=current.taskConditional-previous.taskConditional-reachGain;
            expected.taskConditional+=collectionTaskGain*joint+reachGain;
            expected.reachTaskConditional+=reachGain;
            delayed.resource+=(current.resource-previous.resource)*delayedConfidence;
            delayed.taskConditional+=collectionTaskGain*delayedJoint+reachGain;
            expected.progressAux+=(current.progressAux-previous.progressAux)*joint;
            // Denial is a small, explicit auxiliary and only for a visible contest.
            if(nodes[j].rival>=0)expected.denialAux+=.10*.25*nodes[j].r.value*confidence;
            previous=current;last=j;
        }
        CandidateAudit c;c.kind=nodes[first].r.value>0?"resource_plan":"task_plan";c.id=nodes[first].r.id;c.target=nodes[first].r.pos;
        c.eta=time;c.rivalEta=nodes[first].rival;c.age=nodes[first].age;c.stops=depth;
        c.nominalGain=previous.points();c.confidence=joint;c.score=expected;
        c.rivalCommitment=nodes[first].commitment;
        {int arrival=0,prior=-1;for(int k=0;k<depth;++k){int j=order[k];arrival+=prior<0?nodes[j].d:between[prior].at(nodes[j].r.pos);c.routeIds.push_back(nodes[j].r.id);c.routeEtas.push_back(arrival);prior=j;}}
        c.lostIfDelayed=std::max(0.0,expected.points()-delayed.points());
        c.priority=expected.utility()/(time+1.0);
        if(!found[first] || c.priority>best[first].priority) {best[first]=std::move(c);found[first]=true;}
    };
    for(int a=0;a<n && out.expansions<2000;++a) {
        order[0]=a;evaluate(1,nodes[a].d);++out.expansions;
        if(nodes[a].d>=24)continue;
        for(int b=0;b<n && out.expansions<2000;++b)if(b!=a) {
            int gap=between[a].at(nodes[b].r.pos),t=nodes[a].d+gap;
            if(gap<=0 || t>std::min(24,remaining))continue;
            order[1]=b;evaluate(2,t);++out.expansions;
            for(int c=0;c<n && out.expansions<2000;++c)if(c!=a && c!=b) {
                int d=between[b].at(nodes[c].r.pos),end=t+d;
                if(d<=0 || end>std::min(24,remaining))continue;
                order[2]=c;evaluate(3,end);++out.expansions;
            }
        }
    }
    for(int i=0;i<n;++i)if(found[i])out.goals.push_back(std::move(best[i]));
    return out;
}
} // namespace rmtc103
#include "rmtc103/short_routes.hpp"
