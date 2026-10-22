// SHARED
#include "rmtc_bot/policy.hpp"
#include <algorithm>
#include <limits>
namespace rmtc_bot {
namespace d = rmtc::decision;
Action bridge_action(const d::Action& a) {
    return {a.kind, a.direction ? d::name(*a.direction) : ""};
}
d::GameState make_game_state(const Config& c, const Perception& p, const WorldMemory& m,
                             const std::optional<LLMPlan>& plan) {
    d::GameState s;
    s.session_id=p.session_id; s.tick=static_cast<int>(p.tick); s.max_ticks=c.max_ticks;
    s.stage=c.stage==1 ? d::Stage::Collection : d::Stage::Battle;
    s.width=p.map_width; s.height=p.map_height;
    s.self={p.self_pos,static_cast<double>(p.hp),p.alive,p.is_protected,m.initial_spawn()};
    s.score={p.score.resource_value,p.score.effective_damage,p.score.mission_completed_ids};
    s.resources=m.resources(); s.known_walls=m.walls(); s.exploration_targets=m.frontiers();
    for (const auto& e:p.entities) if(e.type==EntityType::Robot && e.team!=c.team && e.hp>0)
        s.enemies.push_back({e.team,e.pos,static_cast<double>(e.hp),e.defending,e.is_protected,s.tick});
    for (const auto& task:p.missions) {
        d::Mission t; t.id=task.id; t.target=task.target; t.position=task.position;
        if(task.type=="reach") t.kind=d::MissionKind::Reach;
        else if(task.type=="normal_resources") { t.kind=d::MissionKind::NormalResources; t.observed_progress=m.normal_progress(); }
        else if(task.type=="high_resources") { t.kind=d::MissionKind::HighResources; t.observed_progress=m.high_progress(); }
        else if(task.type=="resource_value") t.kind=d::MissionKind::ResourceValue;
        else if(task.type=="effective_damage") t.kind=d::MissionKind::EffectiveDamage;
        else continue; // Unknown future types must not silently become reach missions.
        s.missions.push_back(t);
    }
    if(c.stage==2 && plan && plan->session_id==p.session_id && plan->source_tick<=s.tick) {
        if(plan->valid_until>s.tick) { s.advisory_target=plan->target; s.advisory_priority=plan->priority; }
        s.effects=plan->effects;
        for(const auto& r:plan->regions) {
            d::DangerZone z; z.shape=r.shape==AvoidRegion::Shape::Cross ? d::DangerZone::Shape::Cross : d::DangerZone::Shape::Rectangle;
            z.first={r.rect.x1,r.rect.y1}; z.last={r.rect.x2,r.rect.y2};
            z.window={r.start_tick,r.wall ? r.start_tick+1 : r.end_tick};
            z.session_id=p.session_id; z.validated=true; z.damage=r.damage;
            z.before_movement=r.before_movement; z.respects_protection=r.respects_protection;
            z.instant_death=r.instant_death || r.wall; z.excludes_initial_spawn=r.wall;
            if(r.wall) z.respects_protection=false;
            s.dangers.push_back(z);
        }
    }
    return s;
}
}
