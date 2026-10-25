// tools/bench/opponent_adapter.cpp —— 把校内4.0 的真实决策代码接进仿真裁判
//
// 完全复用 4.0 的 rmtc2::Bot（含 EventTracker / HazardField / combat 折算），
// 只做感知结构搬运，不改一行决策逻辑，保证"对手就是 4.0 本人"。
#include "opponent_adapter.hpp"

#include "rmtc2/bot.hpp"

namespace {

rmtc2::EntityType to_v4_type(rmtc3::EntityType t) {
    switch (t) {
    case rmtc3::EntityType::Robot:
        return rmtc2::EntityType::Robot;
    case rmtc3::EntityType::ResNormal:
        return rmtc2::EntityType::ResNormal;
    case rmtc3::EntityType::ResHigh:
        return rmtc2::EntityType::ResHigh;
    default:
        return rmtc2::EntityType::Obstacle;
    }
}

rmtc2::Perception to_v4(const rmtc3::Perception& in) {
    rmtc2::Perception p;
    p.tick = in.tick;
    p.session = in.session;
    p.mapW = in.mapW;
    p.mapH = in.mapH;
    p.vision = in.vision;
    p.self = rmtc2::Point{in.self.x, in.self.y};
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
        rmtc2::Entity out;
        out.type = to_v4_type(e.type);
        out.id = e.id;
        out.team = e.team;
        out.pos = rmtc2::Point{e.pos.x, e.pos.y};
        out.hp = e.hp;
        out.value = e.value;
        out.defending = e.defending;
        out.protectedSpawn = e.protectedSpawn;
        p.entities.push_back(out);
    }
    for (const auto& m : in.missions) {
        rmtc2::Mission out;
        out.id = m.id;
        out.type = m.type;
        out.target = m.target;
        if (m.position) out.position = rmtc2::Point{m.position->x, m.position->y};
        p.missions.push_back(out);
    }
    return p;
}

class V4Bot final : public IPolicy {
public:
    explicit V4Bot(int stage, int maxTicks) {
        rmtc2::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
    }
    rmtc3::Action act(const rmtc3::Perception& p) override {
        const rmtc2::Decision d = bot_.decide(to_v4(p));
        rmtc3::Action out;
        switch (d.action.kind) {
        case rmtc2::Act::Move:
            out = rmtc3::Action::move(d.action.dx, d.action.dy);
            break;
        case rmtc2::Act::Attack:
            out = rmtc3::Action::attack(d.action.dx, d.action.dy);
            break;
        case rmtc2::Act::Defend:
            out = rmtc3::Action::defend();
            break;
        default:
            out = rmtc3::Action::wait();
            break;
        }
        return out;
    }
    const char* name() const override { return "v4"; }

private:
    rmtc2::Bot bot_;
};

}  // namespace

std::unique_ptr<IPolicy> make_v4_bot(int stage, int maxTicks) {
    return std::make_unique<V4Bot>(stage, maxTicks);
}
