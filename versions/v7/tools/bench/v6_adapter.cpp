// Adapter: run the 7.0 decision core inside a 6.0-typed simulation.
#include <memory>

#include "opponent_adapter.hpp"
#include "rmtc6/bot.hpp"

namespace {

rmtc6::EntityType to_v6_type(rmtc7::EntityType t) {
    switch (t) {
    case rmtc7::EntityType::Robot: return rmtc6::EntityType::Robot;
    case rmtc7::EntityType::ResNormal: return rmtc6::EntityType::ResNormal;
    case rmtc7::EntityType::ResHigh: return rmtc6::EntityType::ResHigh;
    default: return rmtc6::EntityType::Obstacle;
    }
}

rmtc6::Perception to_v6(const rmtc7::Perception& in) {
    rmtc6::Perception p;
    p.tick = in.tick;
    p.session = in.session;
    p.mapW = in.mapW;
    p.mapH = in.mapH;
    p.vision = in.vision;
    p.self = rmtc6::Point{in.self.x, in.self.y};
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
        rmtc6::Entity out;
        out.type = to_v6_type(e.type);
        out.id = e.id;
        out.team = e.team;
        out.pos = rmtc6::Point{e.pos.x, e.pos.y};
        out.hp = e.hp;
        out.value = e.value;
        out.defending = e.defending;
        out.protectedSpawn = e.protectedSpawn;
        p.entities.push_back(out);
    }
    for (const auto& m : in.missions) {
        rmtc6::Mission out;
        out.id = m.id;
        out.type = m.type;
        out.target = m.target;
        if (m.position) out.position = rmtc6::Point{m.position->x, m.position->y};
        p.missions.push_back(out);
    }
    return p;
}

class V6Bot final : public IPolicy {
public:
    V6Bot(int stage, int maxTicks) {
        rmtc6::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
    }
    rmtc7::Action act(const rmtc7::Perception& p) override {
        const rmtc6::Decision d = bot_.decide(to_v6(p));
        rmtc7::Action out;
        switch (d.action.kind) {
        case rmtc6::Act::Move: out = rmtc7::Action::move(d.action.dx, d.action.dy); break;
        case rmtc6::Act::Attack: out = rmtc7::Action::attack(d.action.dx, d.action.dy); break;
        case rmtc6::Act::Defend: out = rmtc7::Action::defend(); break;
        default: out = rmtc7::Action::wait(); break;
        }
        return out;
    }
    const char* name() const override { return "v7"; }

private:
    rmtc6::Bot bot_;
};

}  // namespace

std::unique_ptr<IPolicy> make_v6_bot(int stage, int maxTicks) {
    return std::make_unique<V6Bot>(stage, maxTicks);
}
