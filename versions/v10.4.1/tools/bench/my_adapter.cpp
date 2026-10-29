// tools/bench/my_adapter.cpp —— 把"我方历史版本"接进 10.4 的离线裁判
//
// 对手一：校内2.0（命名空间 rmtc2，无 events.cpp）
// 对手二：校内5.0（命名空间 rmtc3，4.0 基座 + 带预算的集合 DP 路线）
//
// 只做公开感知字段搬运：不读裁判真值、不读对手内部状态、不改任何决策逻辑。
#include <memory>

#include "my_adapter.hpp"

#include "rmtc2/bot.hpp"
#include "rmtc2/parse.hpp"
#include "rmtc3/bot.hpp"
#include "rmtc3/parse.hpp"

namespace {

// ---------------------------------------------------------------------------
// 校内2.0
// ---------------------------------------------------------------------------
rmtc2::EntityType to_v2_type(rmtc9::EntityType t) {
    switch (t) {
    case rmtc9::EntityType::Robot: return rmtc2::EntityType::Robot;
    case rmtc9::EntityType::ResNormal: return rmtc2::EntityType::ResNormal;
    case rmtc9::EntityType::ResHigh: return rmtc2::EntityType::ResHigh;
    default: return rmtc2::EntityType::Obstacle;
    }
}

rmtc2::Perception to_v2(const rmtc9::Perception& in) {
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
    p.resourceValue = in.resourceValue;
    p.effectiveDamage = in.effectiveDamage;
    p.missionsCompleted = in.missionsCompleted;
    p.completedIds = in.completedIds;
    p.eventText = in.eventText;
    p.missionText = in.missionText;
    for (const auto& e : in.entities) {
        rmtc2::Entity out;
        out.type = to_v2_type(e.type);
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

rmtc9::Action to_v9(const rmtc2::Action& a) {
    switch (a.kind) {
    case rmtc2::Act::Move: return rmtc9::Action::move(a.dx, a.dy);
    case rmtc2::Act::Attack: return rmtc9::Action::attack(a.dx, a.dy);
    case rmtc2::Act::Defend: return rmtc9::Action::defend();
    default: return rmtc9::Action::wait();
    }
}

class My2Bot final : public IPolicy {
public:
    My2Bot(int stage, int maxTicks) {
        rmtc2::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
    }
    rmtc9::Action act(const rmtc9::Perception& p) override { return to_v9(bot_.decide(to_v2(p)).action); }
    bool plan(const std::string& content, const std::string& session, int tick, int maxTicks, int w, int h) override {
        rmtc2::Plan plan;
        if (!rmtc2::parse_plan(content, session, tick, maxTicks, w, h, &plan)) return false;
        bot_.set_plan(plan);
        return true;
    }
    const char* name() const override { return "my2.0"; }

private:
    rmtc2::Bot bot_;
};

// ---------------------------------------------------------------------------
// 校内5.0
// ---------------------------------------------------------------------------
rmtc3::EntityType to_v5_type(rmtc9::EntityType t) {
    switch (t) {
    case rmtc9::EntityType::Robot: return rmtc3::EntityType::Robot;
    case rmtc9::EntityType::ResNormal: return rmtc3::EntityType::ResNormal;
    case rmtc9::EntityType::ResHigh: return rmtc3::EntityType::ResHigh;
    default: return rmtc3::EntityType::Obstacle;
    }
}

rmtc3::Perception to_v5(const rmtc9::Perception& in) {
    rmtc3::Perception p;
    p.tick = in.tick;
    p.session = in.session;
    p.mapW = in.mapW;
    p.mapH = in.mapH;
    p.vision = in.vision;
    p.self = rmtc3::Point{in.self.x, in.self.y};
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
        rmtc3::Entity out;
        out.type = to_v5_type(e.type);
        out.id = e.id;
        out.team = e.team;
        out.pos = rmtc3::Point{e.pos.x, e.pos.y};
        out.hp = e.hp;
        out.value = e.value;
        out.defending = e.defending;
        out.protectedSpawn = e.protectedSpawn;
        p.entities.push_back(out);
    }
    for (const auto& m : in.missions) {
        rmtc3::Mission out;
        out.id = m.id;
        out.type = m.type;
        out.target = m.target;
        if (m.position) out.position = rmtc3::Point{m.position->x, m.position->y};
        p.missions.push_back(out);
    }
    return p;
}

rmtc9::Action to_v9(const rmtc3::Action& a) {
    switch (a.kind) {
    case rmtc3::Act::Move: return rmtc9::Action::move(a.dx, a.dy);
    case rmtc3::Act::Attack: return rmtc9::Action::attack(a.dx, a.dy);
    case rmtc3::Act::Defend: return rmtc9::Action::defend();
    default: return rmtc9::Action::wait();
    }
}

class My5Bot final : public IPolicy {
public:
    My5Bot(int stage, int maxTicks) {
        rmtc3::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
    }
    rmtc9::Action act(const rmtc9::Perception& p) override { return to_v9(bot_.decide(to_v5(p)).action); }
    bool plan(const std::string& content, const std::string& session, int tick, int maxTicks, int w, int h) override {
        rmtc3::Plan plan;
        if (!rmtc3::parse_plan(content, session, tick, maxTicks, w, h, &plan)) return false;
        bot_.set_plan(plan);
        return true;
    }
    const char* name() const override { return "my5.0"; }

private:
    rmtc3::Bot bot_;
};

}  // namespace

std::unique_ptr<IPolicy> make_my2_bot(int stage, int maxTicks) {
    return std::make_unique<My2Bot>(stage, maxTicks);
}

std::unique_ptr<IPolicy> make_my5_bot(int stage, int maxTicks) {
    return std::make_unique<My5Bot>(stage, maxTicks);
}
