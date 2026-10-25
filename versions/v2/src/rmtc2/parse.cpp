// rmtc2/parse.cpp
#include "rmtc2/parse.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace rmtc2 {
namespace {
using Json = nlohmann::json;

bool int_field(const Json& j, const char* key, int* out) {
    if (!j.contains(key)) return false;
    const auto& v = j[key];
    if (!v.is_number_integer()) return false;
    const auto raw = v.get<long long>();
    if (raw < std::numeric_limits<int>::min() || raw > std::numeric_limits<int>::max()) return false;
    *out = static_cast<int>(raw);
    return true;
}

bool num_field(const Json& j, const char* key, double* out) {
    if (!j.contains(key) || !j[key].is_number()) return false;
    const double v = j[key].get<double>();
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

bool point_field(const Json& j, const char* key, Point* out) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 2) return false;
    if (!j[key][0].is_number_integer() || !j[key][1].is_number_integer()) return false;
    out->x = static_cast<int>(j[key][0].get<long long>());
    out->y = static_cast<int>(j[key][1].get<long long>());
    return true;
}

// 9x9 相对位置图 -> 相对位移。中心固定为 '1'，'2' 为实体；同格时无 '2'。
bool relative_delta(const std::string& raw, Point* delta) {
    std::string text;
    text.reserve(raw.size());
    for (char c : raw) {
        if (c != '\r') text.push_back(c);
    }
    if (text.size() != 89) return false;
    int dx = 0;
    int dy = 0;
    bool selfFound = false;
    for (int row = 0; row < 9; ++row) {
        if (row < 8 && text[static_cast<std::size_t>(row) * 10 + 9] != '\n') return false;
        for (int col = 0; col < 9; ++col) {
            const char c = text[static_cast<std::size_t>(row) * 10 + col];
            if (c == '1') {
                if (row != 4 || col != 4) return false;
                selfFound = true;
            } else if (c == '2') {
                dx = col - 4;
                dy = row - 4;
            } else if (c != '0') {
                return false;
            }
        }
    }
    if (!selfFound) return false;
    *delta = {dx, dy};
    return true;
}

bool valid_window(int start, int end, int maxTicks) {
    return start >= 0 && end > start && end <= maxTicks + 400;
}
}  // namespace

bool parse_perception(const std::string& text, Perception* out, std::string* err) {
    if (!out) return false;
    const auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    Json j;
    try {
        j = Json::parse(text);
    } catch (const std::exception&) {
        return fail("E_PARSE");
    }
    if (!j.is_object()) return fail("E_SCHEMA");

    Perception p;
    if (!int_field(j, "tick", &p.tick) || p.tick < 0) return fail("E_TICK");
    if (!j.contains("session_id") || !j["session_id"].is_string()) return fail("E_SESSION");
    p.session = j["session_id"].get<std::string>();
    if (p.session.empty()) return fail("E_SESSION");

    if (!j.contains("map") || !j["map"].is_object()) return fail("E_SCHEMA");
    if (!int_field(j["map"], "width", &p.mapW) || !int_field(j["map"], "height", &p.mapH)) return fail("E_SCHEMA");
    if (p.mapW <= 0 || p.mapH <= 0) return fail("E_SCHEMA");
    if (static_cast<long long>(p.mapW) * p.mapH > 1000000LL) return fail("E_SCHEMA");

    int vision = 4;
    int_field(j, "vision_radius", &vision);
    p.vision = std::max(0, std::min(4, vision));

    if (!j.contains("self") || !j["self"].is_object()) return fail("E_SCHEMA");
    const auto& self = j["self"];
    if (!point_field(self, "position", &p.self)) return fail("E_SCHEMA");
    if (p.self.x < 0 || p.self.y < 0 || p.self.x >= p.mapW || p.self.y >= p.mapH) return fail("E_SCHEMA");
    int hp = 0;
    if (!int_field(self, "hp", &hp)) return fail("E_SCHEMA");
    p.hp = std::max(0, hp);
    p.team = self.value("team", std::string{});
    p.alive = self.value("alive", false);
    p.defending = self.value("defending", false);
    p.protectedSpawn = self.value("protected", false);
    int_field(self, "respawn_in", &p.respawnIn);
    const std::string reason = self.value("position_reason", std::string{});
    p.positionInitial = (reason == "initial");

    if (j.contains("visible_entities")) {
        if (!j["visible_entities"].is_array()) return fail("E_SCHEMA");
        p.entities.reserve(j["visible_entities"].size());
        for (const auto& e : j["visible_entities"]) {
            if (!e.is_object() || !e.contains("type") || !e["type"].is_string()) continue;
            const std::string type = e["type"].get<std::string>();
            Entity entity;
            if (type == "robot") entity.type = EntityType::Robot;
            else if (type == "resource_normal") entity.type = EntityType::ResNormal;
            else if (type == "resource_high") entity.type = EntityType::ResHigh;
            else if (type == "obstacle") entity.type = EntityType::Obstacle;
            else continue;  // 未知类型跳过，不拖垮整帧

            Point delta;
            if (!e.contains("position") || !e["position"].is_string()) continue;
            if (!relative_delta(e["position"].get<std::string>(), &delta)) continue;
            entity.pos = {p.self.x + delta.x, p.self.y + delta.y};
            if (entity.pos.x < 0 || entity.pos.y < 0 || entity.pos.x >= p.mapW || entity.pos.y >= p.mapH) continue;
            entity.id = e.value("id", std::string{});
            entity.team = e.value("team", std::string{});
            if (entity.type == EntityType::Robot) {
                int ehp = 0;
                if (!int_field(e, "hp", &ehp)) continue;
                entity.hp = std::max(0, ehp);
                entity.defending = e.value("defending", false);
                entity.protectedSpawn = e.value("protected", false);
            } else if (entity.type != EntityType::Obstacle) {
                double v = 0;
                if (!num_field(e, "value", &v) || v <= 0) {
                    v = entity.type == EntityType::ResHigh ? 20 : 10;
                }
                entity.value = static_cast<int>(std::lround(v));
            }
            p.entities.push_back(std::move(entity));
        }
    }

    if (j.contains("score") && j["score"].is_object()) {
        const auto& s = j["score"];
        double v = 0;
        if (num_field(s, "resource_value", &v)) p.resourceValue = std::max(0.0, v);
        double d = 0;
        if (num_field(s, "effective_damage", &d)) p.effectiveDamage = std::max(0.0, d);
        int_field(s, "mission_completed", &p.missionsCompleted);
        if (s.contains("mission_completed_ids") && s["mission_completed_ids"].is_array()) {
            for (const auto& id : s["mission_completed_ids"]) {
                if (id.is_string()) p.completedIds.push_back(id.get<std::string>());
            }
        }
    }
    p.missionText = j.value("mission_text", std::string{});
    p.eventText = j.value("event_text", std::string{});

    if (j.contains("missions") && j["missions"].is_array()) {
        for (const auto& m : j["missions"]) {
            if (!m.is_object()) continue;
            Mission mission;
            mission.id = m.value("id", std::string{});
            mission.type = m.value("type", std::string{});
            double target = 1;
            if (num_field(m, "target", &target) && target > 0) mission.target = target;
            Point pos;
            if (point_field(m, "position", &pos)) mission.position = pos;
            if (mission.id.empty() || mission.type.empty()) continue;
            p.missions.push_back(std::move(mission));
        }
    }

    if (!p.valid()) return fail("E_SCHEMA");
    *out = std::move(p);
    return true;
}

std::string action_json(const Action& action, int tick, const std::string& session) {
    Json j;
    j["tick"] = tick;
    j["session_id"] = session;
    switch (action.kind) {
    case Act::Move:
        j["action"] = "move";
        break;
    case Act::Attack:
        j["action"] = "attack";
        break;
    case Act::Defend:
        j["action"] = "defend";
        break;
    default:
        j["action"] = "wait";
        break;
    }
    if (action.directional()) {
        for (int i = 0; i < 4; ++i) {
            if (kDirX[i] == action.dx && kDirY[i] == action.dy) {
                j["direction"] = kDirName[i];
                break;
            }
        }
    }
    return j.dump();
}

std::string first_json_object(const std::string& text) {
    for (std::size_t start = 0; start < text.size(); ++start) {
        if (text[start] != '{') continue;
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (std::size_t i = start; i < text.size(); ++i) {
            const char c = text[i];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{') ++depth;
            else if (c == '}') {
                --depth;
                if (depth == 0) return text.substr(start, i - start + 1);
            }
        }
    }
    return {};
}

bool parse_plan(const std::string& content, const std::string& session, int sourceTick,
                int maxTicks, int mapW, int mapH, Plan* out) {
    if (!out) return false;
    const std::string body = first_json_object(content);
    if (body.empty()) return false;
    Json j;
    try {
        j = Json::parse(body);
    } catch (const std::exception&) {
        return false;
    }
    if (!j.is_object()) return false;

    Plan plan;
    plan.session = session;
    plan.sourceTick = sourceTick;

    if (j.contains("hazards")) {
        if (!j["hazards"].is_array() || j["hazards"].size() > 32) return false;
        for (const auto& h : j["hazards"]) {
            if (!h.is_object()) continue;
            Hazard z;
            const std::string shape = h.value("shape", std::string("rect"));
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (shape == "cross") {
                Point p;
                if (!point_field(h, "a", &p)) continue;
                z.cross = true;
                z.a = p;
                z.b = p;
                x1 = x2 = p.x;
                y1 = y2 = p.y;
            } else {
                Point a, b;
                if (!point_field(h, "a", &a) || !point_field(h, "b", &b)) continue;
                x1 = std::min(a.x, b.x);
                y1 = std::min(a.y, b.y);
                x2 = std::max(a.x, b.x);
                y2 = std::max(a.y, b.y);
                z.a = {x1, y1};
                z.b = {x2, y2};
            }
            if (x2 < 0 || y2 < 0 || x1 >= mapW || y1 >= mapH) continue;
            z.a.x = std::max(0, z.a.x);
            z.a.y = std::max(0, z.a.y);
            z.b.x = std::min(mapW - 1, z.b.x);
            z.b.y = std::min(mapH - 1, z.b.y);
            int start = 0, end = 0;
            if (!int_field(h, "start", &start) || !int_field(h, "end", &end)) continue;
            if (!valid_window(start, end, maxTicks)) continue;
            z.start = start;
            z.end = end;
            int dmg = 0;
            int_field(h, "damage", &dmg);
            z.damage = std::max(0, std::min(1000, dmg));
            z.wall = h.value("wall", false);
            z.lethal = h.value("lethal", false);
            if (z.damage == 0 && !z.wall && !z.lethal) continue;
            plan.hazards.push_back(z);
        }
    }

    const auto read_window = [&](const Json& e, const char* key, Window* w) {
        if (!e.contains(key)) return;
        const auto& arr = e[key];
        if (!arr.is_array() || arr.size() != 2) return;
        if (!arr[0].is_number_integer() || !arr[1].is_number_integer()) return;
        const int s = static_cast<int>(arr[0].get<long long>());
        const int t = static_cast<int>(arr[1].get<long long>());
        if (valid_window(s, t, maxTicks)) {
            w->start = s;
            w->end = t;
        }
    };
    if (j.contains("effects") && j["effects"].is_object()) {
        const auto& e = j["effects"];
        read_window(e, "inverted", &plan.inverted);
        read_window(e, "vulnerable", &plan.vulnerable);
        read_window(e, "reflect", &plan.reflect);
        int rd = 20;
        if (int_field(e, "reflect_damage", &rd) && rd >= 0 && rd <= 200) plan.reflectDamage = rd;
    }
    if (j.contains("advice") && j["advice"].is_object()) {
        Point p;
        int prio = 1;
        if (point_field(j["advice"], "target", &p) && p.x >= 0 && p.y >= 0 && p.x < mapW && p.y < mapH) {
            plan.advice = p;
            if (int_field(j["advice"], "priority", &prio)) plan.advicePriority = std::max(1, std::min(3, prio));
        }
    }
    plan.valid = true;
    *out = std::move(plan);
    return true;
}

}  // namespace rmtc2
