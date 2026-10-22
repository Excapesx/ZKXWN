// SHARED: public-wire types; normalized coordinates are absolute.
#pragma once
#include "rmtc_decision/types.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rmtc_bot {
using Vec2 = rmtc::decision::Point;
using ActionKind = rmtc::decision::ActionKind;
struct Rect { int x1 = 0, y1 = 0, x2 = 0, y2 = 0; };
enum class EntityType { Robot, ResourceNormal, ResourceHigh, Obstacle };
struct Entity {
    EntityType type = EntityType::Obstacle;
    std::string id, team;
    Vec2 pos, rel;
    int hp = 0;
    bool defending = false, is_protected = false;
    double value = 0;
};
struct LastAction {
    std::int64_t tick = 0;
    std::string action, effective_direction, error;
    bool moved = false;
};
struct Mission {
    std::string id, type;
    double target = 1;
    std::optional<Vec2> position;
};
struct Score {
    double resource_value = 0, effective_damage = 0;
    int mission_completed = 0, invalid_actions = 0;
    std::vector<std::string> mission_completed_ids;
};
struct Perception {
    std::int64_t tick = 0;
    std::string session_id, self_team, position_reason;
    int vision_radius = 0, hp = 0, respawn_in = 0;
    Vec2 self_pos;
    bool alive = false, defending = false, is_protected = false;
    std::optional<LastAction> last_action;
    std::vector<Entity> entities;
    Score score;
    std::string mission_text, event_text;
    std::vector<Mission> missions;
    int map_width = 0, map_height = 0;
};
struct Action {
    ActionKind kind = ActionKind::Wait;
    std::string direction;
};
struct AvoidRegion {
    enum class Shape { Rectangle, Cross };
    Shape shape = Shape::Rectangle;
    Rect rect;
    int start_tick = 0, end_tick = 0;
    bool wall = false;
    double damage = 0;
    bool instant_death = false, before_movement = true, respects_protection = true;
    bool contains(Vec2 p) const;
    bool active(int tick) const { return start_tick <= tick && tick < end_tick; }
};
struct LLMPlan {
    std::string session_id; // Bound by caller request, never accepted from model text.
    int source_tick = 0, valid_until = 0;
    std::string intent, reason;
    std::optional<Vec2> target;
    int priority = 0;
    std::vector<AvoidRegion> regions;
    rmtc::decision::EffectSnapshot effects;
};
std::string action_kind_to_string(ActionKind kind);
bool valid_direction(const std::string& direction);
} // namespace rmtc_bot

