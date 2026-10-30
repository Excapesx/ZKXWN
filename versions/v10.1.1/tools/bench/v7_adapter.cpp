// Adapter: run the frozen 7.0 decision core (baseline_v7/src/rmtc7) inside the 8.0 simulator.
#include <memory>

#include "opponent_adapter.hpp"
#include "rmtc7/bot.hpp"
#include "rmtc7/parse.hpp"

namespace {

rmtc7::EntityType to_v7_type(rmtc9::EntityType t) {
    switch (t) {
    case rmtc9::EntityType::Robot: return rmtc7::EntityType::Robot;
    case rmtc9::EntityType::ResNormal: return rmtc7::EntityType::ResNormal;
    case rmtc9::EntityType::ResHigh: return rmtc7::EntityType::ResHigh;
    default: return rmtc7::EntityType::Obstacle;
    }
}

rmtc7::Point to_v7(rmtc9::Point p) { return rmtc7::Point{p.x, p.y}; }

rmtc7::Perception to_v7(const rmtc9::Perception& in) {
    rmtc7::Perception p;
    p.tick = in.tick;
    p.session = in.session;
    p.mapW = in.mapW;
    p.mapH = in.mapH;
    p.vision = in.vision;
    p.self = to_v7(in.self);
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
    p.lastMoved = in.lastMoved;
    p.invalidActions = in.invalidActions;
    p.resourceValue = in.resourceValue;
    p.effectiveDamage = in.effectiveDamage;
    p.missionsCompleted = in.missionsCompleted;
    p.completedIds = in.completedIds;
    p.eventText = in.eventText;
    p.missionText = in.missionText;
    for (const auto& e : in.entities) {
        rmtc7::Entity out;
        out.type = to_v7_type(e.type);
        out.id = e.id;
        out.team = e.team;
        out.pos = to_v7(e.pos);
        out.hp = e.hp;
        out.value = e.value;
        out.defending = e.defending;
        out.protectedSpawn = e.protectedSpawn;
        p.entities.push_back(out);
    }
    for (const auto& m : in.missions) {
        rmtc7::Mission out;
        out.id = m.id;
        out.type = m.type;
        out.target = m.target;
        if (m.position) out.position = to_v7(*m.position);
        p.missions.push_back(out);
    }
    return p;
}

class V7Bot final : public IPolicy {
public:
    V7Bot(int stage, int maxTicks) {
        rmtc7::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
    }
    rmtc9::Action act(const rmtc9::Perception& p) override {
        const rmtc7::Decision d = bot_.decide(to_v7(p));
        switch (d.action.kind) {
        case rmtc7::Act::Move: return rmtc9::Action::move(d.action.dx, d.action.dy);
        case rmtc7::Act::Attack: return rmtc9::Action::attack(d.action.dx, d.action.dy);
        case rmtc7::Act::Defend: return rmtc9::Action::defend();
        default: return rmtc9::Action::wait();
        }
    }
    bool plan(const std::string& content, const std::string& session, int tick, int maxTicks, int w, int h) override {
        // 与 7.0 节点 drain_llm 相同：parse_plan 成功才 set_plan（内部 merge_plans）。
        rmtc7::Plan plan;
        if (!rmtc7::parse_plan(content, session, tick, maxTicks, w, h, &plan)) return false;
        bot_.set_plan(plan);
        return true;
    }
    const char* name() const override { return "v7"; }

private:
    rmtc7::Bot bot_;
};

}  // namespace

std::unique_ptr<IPolicy> make_v7_bot(int stage, int maxTicks) {
    return std::make_unique<V7Bot>(stage, maxTicks);
}
