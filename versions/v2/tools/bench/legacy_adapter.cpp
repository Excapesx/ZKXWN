// tools/bench/legacy_adapter.cpp —— 把 1.0 的真实决策代码接进仿真裁判
//
// 完全复用 1.0 的 Stage1Policy / Stage2Policy / DecisionEngine / WorldMemory / Planner，
// 不重写、不近似，保证"对手就是 1.0 本人"。
#include "legacy_adapter.hpp"

#include "rmtc_bot/memory.hpp"
#include "rmtc_bot/perception.hpp"
#include "rmtc_bot/planner.hpp"
#include "rmtc_bot/policies/stage1_policy.hpp"
#include "rmtc_bot/policies/stage2_policy.hpp"

namespace {

rmtc_bot::ActionKind to_kind(rmtc2::Act a) {
    switch (a) {
    case rmtc2::Act::Move:
        return rmtc_bot::ActionKind::Move;
    case rmtc2::Act::Attack:
        return rmtc_bot::ActionKind::Attack;
    case rmtc2::Act::Defend:
        return rmtc_bot::ActionKind::Defend;
    default:
        return rmtc_bot::ActionKind::Wait;
    }
}

rmtc2::Point from_point(const rmtc_bot::Vec2& p) { return {p.x, p.y}; }

rmtc_bot::Perception to_legacy(const rmtc2::Perception& in) {
    rmtc_bot::Perception p;
    p.tick = in.tick;
    p.session_id = in.session;
    p.self_team = in.team;
    p.map_width = in.mapW;
    p.map_height = in.mapH;
    p.vision_radius = in.vision;
    p.self_pos = rmtc_bot::Vec2{in.self.x, in.self.y};
    p.hp = in.hp;
    p.alive = in.alive;
    p.defending = in.defending;
    p.is_protected = in.protectedSpawn;
    p.respawn_in = in.respawnIn;
    p.position_reason = in.positionInitial ? "initial" : "";
    p.mission_text = in.missionText;
    p.event_text = in.eventText;
    p.score.resource_value = in.resourceValue;
    p.score.effective_damage = in.effectiveDamage;
    p.score.mission_completed = in.missionsCompleted;
    p.score.mission_completed_ids = in.completedIds;
    for (const auto& e : in.entities) {
        rmtc_bot::Entity out;
        out.pos = rmtc_bot::Vec2{e.pos.x, e.pos.y};
        out.id = e.id;
        out.team = e.team;
        out.hp = e.hp;
        out.defending = e.defending;
        out.is_protected = e.protectedSpawn;
        out.value = e.value;
        switch (e.type) {
        case rmtc2::EntityType::Robot:
            out.type = rmtc_bot::EntityType::Robot;
            break;
        case rmtc2::EntityType::ResNormal:
            out.type = rmtc_bot::EntityType::ResourceNormal;
            break;
        case rmtc2::EntityType::ResHigh:
            out.type = rmtc_bot::EntityType::ResourceHigh;
            break;
        default:
            out.type = rmtc_bot::EntityType::Obstacle;
            break;
        }
        p.entities.push_back(out);
    }
    for (const auto& m : in.missions) {
        rmtc_bot::Mission lm;
        lm.id = m.id;
        lm.type = m.type;
        lm.target = m.target;
        if (m.position) lm.position = rmtc_bot::Vec2{m.position->x, m.position->y};
        p.missions.push_back(lm);
    }
    return p;
}

class LegacyBot final : public IPolicy {
public:
    LegacyBot(int stage, int maxTicks, char seat) : memory_(), planner_(memory_) {
        rmtc_bot::Config cfg;
        cfg.team = std::string(1, seat);
        cfg.team_name = "legacy";
        cfg.stage = stage;
        cfg.max_ticks = maxTicks;
        cfg.decision_type = "balanced";  // 1.0 的默认（TaskFocus）模式
        if (stage == 1) stage1_ = std::make_unique<rmtc_bot::Stage1Policy>(cfg, planner_);
        else stage2_ = std::make_unique<rmtc_bot::Stage2Policy>(cfg, planner_);
        memory_.reset(1, 1);
    }

    rmtc2::Action act(const rmtc2::Perception& p) override {
        rmtc_bot::Perception lp = to_legacy(p);
        memory_.update(lp);
        rmtc_bot::Action a;
        if (stage1_) a = stage1_->decide(lp, memory_, std::nullopt);
        else a = stage2_->decide(lp, memory_, std::nullopt);

        rmtc2::Action out;
        out.kind = rmtc2::Act::Wait;
        switch (a.kind) {
        case rmtc_bot::ActionKind::Move:
            out.kind = rmtc2::Act::Move;
            break;
        case rmtc_bot::ActionKind::Attack:
            out.kind = rmtc2::Act::Attack;
            break;
        case rmtc_bot::ActionKind::Defend:
            out.kind = rmtc2::Act::Defend;
            break;
        default:
            return out;
        }
        if (a.direction == "up") {
            out.dx = 0;
            out.dy = -1;
        } else if (a.direction == "right") {
            out.dx = 1;
            out.dy = 0;
        } else if (a.direction == "down") {
            out.dx = 0;
            out.dy = 1;
        } else if (a.direction == "left") {
            out.dx = -1;
            out.dy = 0;
        } else if (out.kind != rmtc2::Act::Defend) {
            out.kind = rmtc2::Act::Wait;
        }
        return out;
    }

    const char* name() const override { return "legacy_1.0"; }

private:
    rmtc_bot::WorldMemory memory_;
    rmtc_bot::Planner planner_;
    std::unique_ptr<rmtc_bot::Stage1Policy> stage1_;
    std::unique_ptr<rmtc_bot::Stage2Policy> stage2_;
};

}  // namespace

std::unique_ptr<IPolicy> make_legacy_bot(int stage, int maxTicks, char seat) {
    return std::make_unique<LegacyBot>(stage, maxTicks, seat);
}
