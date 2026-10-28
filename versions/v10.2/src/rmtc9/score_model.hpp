#pragma once
#include <algorithm>
#include <set>
#include <vector>
#include "rmtc9/grid.hpp"
#include "rmtc9/combat.hpp"

namespace rmtc9 {
// Task points are conditional on the organizer accepting this match's AI qualification.
// They are planning values, never a claim that qualification was obtained.
struct ScoreEstimate {
    double resource=0, combat=0, taskConditional=0;
    double reachTaskConditional=0; // included in taskConditional; not added twice
    double progressAux=0, denialAux=0, bufferAux=0;
    double points() const { return resource+combat+taskConditional; }
    double auxiliary() const { return progressAux+denialAux+bufferAux; }
    double utility() const { return points()+auxiliary(); }
};
inline double resource_points(double value) { return .25*std::clamp(value,0.0,120.0); }
inline double death_score_loss(double damage) {
    return combat_points(damage)-combat_points(std::max(0.0,damage-60));
}
inline double buffer_value(double before,double after) {
    // Bounded future protection heuristic, not extra current combat score.
    return .25*std::max(0.0,death_score_loss(before)-death_score_loss(after));
}
inline bool score_mission_done(const Perception& p,const std::string& id) {
    return std::find(p.completedIds.begin(),p.completedIds.end(),id)!=p.completedIds.end();
}
inline ScoreEstimate collection_score(const Perception& p,int normal,int high,
                                       const std::vector<Resource>& resources) {
    ScoreEstimate out;double value=0;int addNormal=0,addHigh=0;
    std::set<std::string> ids;
    std::set<Point> positions;
    for(const auto& r:resources) {
        // Zero-value stops represent a public coordinate-task waypoint, not a resource.
        if(r.value>0 && !ids.insert(r.id).second)continue;
        if(!positions.insert(r.pos).second)continue;
        if(r.value>0){value+=r.value;(r.value>=20?addHigh:addNormal)++;}
    }
    out.resource=resource_points(p.resourceValue+value)-resource_points(p.resourceValue);
    std::set<std::string> missions;
    for(const auto& m:p.missions) {
        if(!missions.insert(m.id).second || score_mission_done(p,m.id) || m.target<=0)continue;
        double before=0,after=0;
        if(m.type=="normal_resources"){before=normal;after=normal+addNormal;}
        else if(m.type=="high_resources"){before=high;after=high+addHigh;}
        else if(m.type=="resource_value"){before=p.resourceValue;after=p.resourceValue+value;}
        else if(m.type=="reach" && m.position) {
            if(positions.count(*m.position)){out.taskConditional+=5;out.reachTaskConditional+=5;}
            continue;
        } else continue;
        if(before<m.target && after>=m.target)out.taskConditional+=5;
        else if(after>before)out.progressAux+=.25*std::max(0.0,
            std::min(after/m.target,1.0)-std::min(before/m.target,1.0));
    }
    out.progressAux=std::min(.5,out.progressAux);
    return out;
}
} // namespace rmtc9
