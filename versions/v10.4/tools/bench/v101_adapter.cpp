// Frozen 10.1 adapter: public Perception conversion only; no hidden match data.
#include <memory>

#include "opponent_adapter.hpp"
#include "rmtc101/bot.hpp"
#include "rmtc101/parse.hpp"

namespace {

rmtc101::EntityType to_v101_type(rmtc9::EntityType t) {
    switch (t) {
    case rmtc9::EntityType::Robot: return rmtc101::EntityType::Robot;
    case rmtc9::EntityType::ResNormal: return rmtc101::EntityType::ResNormal;
    case rmtc9::EntityType::ResHigh: return rmtc101::EntityType::ResHigh;
    default: return rmtc101::EntityType::Obstacle;
    }
}

rmtc101::Point to_v101(rmtc9::Point p) { return rmtc101::Point{p.x, p.y}; }

rmtc101::Perception to_v101(const rmtc9::Perception& in) {
    rmtc101::Perception p;
    p.tick = in.tick;
    p.session = in.session;
    p.mapW = in.mapW;
    p.mapH = in.mapH;
    p.vision = in.vision;
    p.self = to_v101(in.self);
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
        rmtc101::Entity out;
        out.type = to_v101_type(e.type);
        out.id = e.id;
        out.team = e.team;
        out.pos = to_v101(e.pos);
        out.hp = e.hp;
        out.value = e.value;
        out.defending = e.defending;
        out.protectedSpawn = e.protectedSpawn;
        p.entities.push_back(out);
    }
    for (const auto& m : in.missions) {
        rmtc101::Mission out;
        out.id = m.id;
        out.type = m.type;
        out.target = m.target;
        if (m.position) out.position = to_v101(*m.position);
        p.missions.push_back(out);
    }
    return p;
}

class V101Bot final : public IPolicy {
public:
    V101Bot(int stage, int maxTicks) {
        rmtc101::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
    }
    rmtc9::Action act(const rmtc9::Perception& p) override {
        const rmtc101::Decision d = bot_.decide(to_v101(p));
        switch (d.action.kind) {
        case rmtc101::Act::Move: return rmtc9::Action::move(d.action.dx, d.action.dy);
        case rmtc101::Act::Attack: return rmtc9::Action::attack(d.action.dx, d.action.dy);
        case rmtc101::Act::Defend: return rmtc9::Action::defend();
        default: return rmtc9::Action::wait();
        }
    }
    bool plan(const std::string& content, const std::string& session, int tick, int maxTicks, int w, int h) override {
        // 与 7.0 节点 drain_llm 相同：parse_plan 成功才 set_plan（内部 merge_plans）。
        rmtc101::Plan plan;
        if (!rmtc101::parse_plan(content, session, tick, maxTicks, w, h, &plan)) return false;
        bot_.set_plan(plan);
        return true;
    }
    const char* name() const override { return "v10.1"; }

private:
    rmtc101::Bot bot_;
};

}  // namespace

std::unique_ptr<IPolicy> make_v101_bot(int stage, int maxTicks) {
    return std::make_unique<V101Bot>(stage, maxTicks);
}
