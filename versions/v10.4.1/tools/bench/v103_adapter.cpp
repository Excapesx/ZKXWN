// Frozen 10.3 adapter: public Perception conversion only; no hidden match data.
#include <memory>

#include "opponent_adapter.hpp"
#include "rmtc103/bot.hpp"
#include "rmtc103/parse.hpp"

namespace {

rmtc103::EntityType to_v103_type(rmtc9::EntityType t) {
    switch (t) {
    case rmtc9::EntityType::Robot: return rmtc103::EntityType::Robot;
    case rmtc9::EntityType::ResNormal: return rmtc103::EntityType::ResNormal;
    case rmtc9::EntityType::ResHigh: return rmtc103::EntityType::ResHigh;
    default: return rmtc103::EntityType::Obstacle;
    }
}

rmtc103::Point to_v103(rmtc9::Point p) { return rmtc103::Point{p.x, p.y}; }

rmtc103::Perception to_v103(const rmtc9::Perception& in) {
    rmtc103::Perception p;
    p.tick = in.tick;
    p.session = in.session;
    p.mapW = in.mapW;
    p.mapH = in.mapH;
    p.vision = in.vision;
    p.self = to_v103(in.self);
    p.team = in.team;
    p.hp = in.hp;
    p.alive = in.alive;
    p.defending = in.defending;
    p.protectedSpawn = in.protectedSpawn;
    p.respawnIn = in.respawnIn;
    p.positionInitial = in.positionInitial;
    p.positionRespawn = in.positionRespawn;
    p.observationComplete = in.observationComplete;
    p.lastActionTick = in.lastActionTick;
    p.lastActionKind = in.lastActionKind;
    p.lastMoved = in.lastMoved;
    p.invalidActions = in.invalidActions;
    p.resourceValue = in.resourceValue;
    p.effectiveDamage = in.effectiveDamage;
    p.missionsCompleted = in.missionsCompleted;
    p.completedIds = in.completedIds;
    p.eventText = in.eventText;
    p.missionText = in.missionText;
    for (const auto& e : in.entities) {
        rmtc103::Entity out;
        out.type = to_v103_type(e.type);
        out.id = e.id;
        out.team = e.team;
        out.pos = to_v103(e.pos);
        out.hp = e.hp;
        out.value = e.value;
        out.defending = e.defending;
        out.protectedSpawn = e.protectedSpawn;
        p.entities.push_back(out);
    }
    for (const auto& m : in.missions) {
        rmtc103::Mission out;
        out.id = m.id;
        out.type = m.type;
        out.target = m.target;
        if (m.position) out.position = to_v103(*m.position);
        p.missions.push_back(out);
    }
    return p;
}

class V103Bot final : public IPolicy {
public:
    V103Bot(int stage, int maxTicks) {
        rmtc103::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
    }
    rmtc9::Action act(const rmtc9::Perception& p) override {
        const rmtc103::Decision d = bot_.decide(to_v103(p));
        switch (d.action.kind) {
        case rmtc103::Act::Move: return rmtc9::Action::move(d.action.dx, d.action.dy);
        case rmtc103::Act::Attack: return rmtc9::Action::attack(d.action.dx, d.action.dy);
        case rmtc103::Act::Defend: return rmtc9::Action::defend();
        default: return rmtc9::Action::wait();
        }
    }
    bool plan(const std::string& content, const std::string& session, int tick, int maxTicks, int w, int h) override {
        // 与 7.0 节点 drain_llm 相同：parse_plan 成功才 set_plan（内部 merge_plans）。
        rmtc103::Plan plan;
        if (!rmtc103::parse_plan(content, session, tick, maxTicks, w, h, &plan)) return false;
        bot_.set_plan(plan);
        return true;
    }
    const char* name() const override { return "v10.3"; }
    PolicyAudit audit() const override {
        PolicyAudit a;const auto& d=bot_.diagnostics();a.switches=d.targetSwitches;a.detailed=true;
        if(d.selectedValid && d.selected.kind=="resource_plan"){a.targetId=d.selected.id;a.target={d.selected.target.x,d.selected.target.y};}
        return a;
    }

private:
    rmtc103::Bot bot_;
};

}  // namespace

std::unique_ptr<IPolicy> make_v103_bot(int stage, int maxTicks) {
    return std::make_unique<V103Bot>(stage, maxTicks);
}
