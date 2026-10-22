// OWNER: A
#include "rmtc_bot/perception.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rmtc_bot {
namespace {
using Json = nlohmann::json;
int integer(const Json& j) {
    if (!j.is_number_integer()) throw std::runtime_error("Expected integer");
    if (j.is_number_unsigned() && j.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Unsigned integer out of supported range");
    const auto value = j.get<std::int64_t>();
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::runtime_error("Integer out of supported range");
    return static_cast<int>(value);
}
Vec2 coordinate(const Json& j) {
    if (!j.is_array() || j.size() != 2) throw std::runtime_error("Expected coordinate pair");
    return {integer(j.at(0)), integer(j.at(1))};
}
double nonnegative(const Json& j) {
    if (!j.is_number()) throw std::runtime_error("Expected number");
    const double value = j.get<double>();
    if (!std::isfinite(value) || value < 0) throw std::runtime_error("Expected finite nonnegative number");
    return value;
}
bool inside(Vec2 p, int w, int h) { return p.x >= 0 && p.y >= 0 && p.x < w && p.y < h; }
}
std::string action_kind_to_string(ActionKind kind) { return rmtc::decision::name(kind); }
bool valid_direction(const std::string& d) { return d == "up" || d == "right" || d == "down" || d == "left"; }
bool AvoidRegion::contains(Vec2 p) const {
    if (shape == Shape::Cross) return p.x == rect.x1 || p.y == rect.y1;
    return p.x >= rect.x1 && p.x <= rect.x2 && p.y >= rect.y1 && p.y <= rect.y2;
}
Vec2 parse_relative_position(const std::string& text) {
    if (text.size() != 89) throw std::runtime_error("Relative position must have 9 rows of 9 characters");
    int ones = 0, twos = 0;
    Vec2 delta;
    for (int row = 0; row < 9; ++row) {
        for (int col = 0; col < 9; ++col) {
            const char c = text[static_cast<std::size_t>(row) * 10 + col];
            if (c == '1') { ++ones; if (row != 4 || col != 4) throw std::runtime_error("Invalid self marker"); }
            else if (c == '2') { ++twos; delta = {col - 4, row - 4}; }
            else if (c != '0') throw std::runtime_error("Invalid relative position character");
        }
        if (row != 8 && text[static_cast<std::size_t>(row) * 10 + 9] != '\n') throw std::runtime_error("Invalid relative position row separator");
    }
    if (ones != 1 || twos > 1) throw std::runtime_error("Invalid relative position markers");
    return delta;
}
Perception parse_perception(const std::string& text) {
    try {
        const auto j = Json::parse(text);
        Perception p;
        p.tick = integer(j.at("tick"));
        p.session_id = j.at("session_id").get<std::string>();
        if (p.tick < 0 || p.session_id.empty() || p.session_id.size() > 1024) throw std::runtime_error("Invalid tick or session");
        p.map_width = integer(j.at("map").at("width"));
        p.map_height = integer(j.at("map").at("height"));
        if (p.map_width < 1 || p.map_height < 1 ||
            static_cast<std::int64_t>(p.map_width) * p.map_height > 1000000)
            throw std::runtime_error("Map exceeds safe allocation limit");
        p.vision_radius = integer(j.at("vision_radius"));
        if (p.vision_radius < 0 || p.vision_radius > 4) throw std::runtime_error("Invalid vision radius");
        const auto& self = j.at("self");
        p.self_pos = coordinate(self.at("position"));
        if (!inside(p.self_pos, p.map_width, p.map_height)) throw std::runtime_error("Self coordinate outside map");
        p.hp = integer(self.at("hp")); p.alive = self.at("alive").get<bool>();
        if (p.hp < 0) throw std::runtime_error("Negative HP");
        p.defending = self.value("defending", false);
        // Wire field is protected. is_protected is ONLY the C++ member name.
        p.is_protected = self.value("protected", false);
        p.respawn_in = self.contains("respawn_in") ? integer(self["respawn_in"]) : 0;
        if (p.respawn_in < 0) throw std::runtime_error("Negative respawn interval");
        p.self_team = self.value("team", std::string{});
        p.position_reason = self.value("position_reason", std::string{});
        if (self.contains("last_action") && !self["last_action"].is_null()) {
            const auto& a = self["last_action"];
            LastAction result;
            result.tick = integer(a.at("tick")); result.action = a.at("action").get<std::string>();
            result.moved = a.value("moved", false);
            result.effective_direction = a.value("effective_direction", std::string{});
            if (a.contains("error") && a["error"].is_string()) result.error = a["error"].get<std::string>();
            p.last_action = result;
        }
        if(!j.at("visible_entities").is_array()) throw std::runtime_error("Expected entity list");
        for (const auto& e : j.at("visible_entities")) {
            Entity entity;
            const auto type = e.at("type").get<std::string>();
            if (type == "robot") entity.type = EntityType::Robot;
            else if (type == "resource_normal") entity.type = EntityType::ResourceNormal;
            else if (type == "resource_high") entity.type = EntityType::ResourceHigh;
            else if (type == "obstacle") entity.type = EntityType::Obstacle;
            else throw std::runtime_error("Unsupported entity type; incomplete observation");
            entity.rel = parse_relative_position(e.at("position").get<std::string>());
            entity.pos = {p.self_pos.x + entity.rel.x, p.self_pos.y + entity.rel.y};
            if (!inside(entity.pos, p.map_width, p.map_height)) throw std::runtime_error("Entity coordinate outside map");
            if (std::abs(entity.rel.x) > p.vision_radius || std::abs(entity.rel.y) > p.vision_radius)
                throw std::runtime_error("Entity outside declared vision");
            entity.id = e.value("id", std::string{}); entity.team = e.value("team", std::string{});
            if (entity.type == EntityType::Robot) {
                entity.hp = integer(e.at("hp")); entity.defending = e.value("defending", false);
                entity.is_protected = e.value("protected", false);
            } else if (entity.type != EntityType::Obstacle) {
                entity.value = nonnegative(e.at("value"));
                if (entity.value <= 0 || entity.id.empty()) throw std::runtime_error("Invalid resource");
            }
            p.entities.push_back(entity);
        }
        if (j.contains("score")) {
            const auto& score = j["score"];
            p.score.resource_value = nonnegative(score.at("resource_value"));
            p.score.effective_damage = nonnegative(score.at("effective_damage"));
            p.score.mission_completed = score.contains("mission_completed") ? integer(score["mission_completed"]) : 0;
            p.score.invalid_actions = score.contains("invalid_actions") ? integer(score["invalid_actions"]) : 0;
            if (p.score.mission_completed < 0 || p.score.invalid_actions < 0) throw std::runtime_error("Negative score counter");
            if (score.contains("mission_completed_ids")) p.score.mission_completed_ids = score["mission_completed_ids"].get<std::vector<std::string>>();
        }
        p.mission_text = j.value("mission_text", std::string{});
        p.event_text = j.value("event_text", std::string{});
        if(j.contains("missions") && !j["missions"].is_array()) throw std::runtime_error("Expected mission list");
        if (j.contains("missions")) for (const auto& m : j["missions"]) {
            Mission mission;
            mission.id = m.at("id").get<std::string>(); mission.type = m.at("type").get<std::string>();
            if (m.contains("target")) mission.target = nonnegative(m["target"]);
            if (m.contains("position")) {
                mission.position = coordinate(m["position"]);
                if (!inside(*mission.position, p.map_width, p.map_height)) throw std::runtime_error("Task outside map");
            }
            p.missions.push_back(mission);
        }
        return p;
    } catch (const std::exception&) {
        // Never echo raw received data or exception text that could contain a token.
        throw std::runtime_error("Invalid public perception JSON or field schema");
    }
}
std::string action_to_json(const Action& action, std::int64_t tick, const std::string& sid) {
    if (tick < 0 || sid.empty()) throw std::runtime_error("Missing action context");
    const auto kind = action_kind_to_string(action.kind);
    if (kind == "invalid") throw std::runtime_error("Invalid action kind");
    Json result{{"tick", tick}, {"session_id", sid}, {"action", kind}};
    if (action.kind == ActionKind::Move || action.kind == ActionKind::Attack) {
        if (!valid_direction(action.direction)) throw std::runtime_error("Missing action direction");
        result["direction"] = action.direction;
    }
    return result.dump();
}
bool action_allowed(const Action& a, const Perception& p, int stage) {
    if (stage != 1 && stage != 2) return false;
    if (!p.alive || p.hp <= 0) return a.kind == ActionKind::Wait;
    if (stage == 1 && a.kind != ActionKind::Move && a.kind != ActionKind::Wait) return false;
    if (p.is_protected && a.kind == ActionKind::Attack) return false;
    if ((a.kind == ActionKind::Move || a.kind == ActionKind::Attack) && !valid_direction(a.direction)) return false;
    return action_kind_to_string(a.kind) != "invalid";
}
} // namespace rmtc_bot
