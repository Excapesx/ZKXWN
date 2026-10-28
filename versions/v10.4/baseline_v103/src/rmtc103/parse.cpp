// rmtc103/parse.cpp
#include "rmtc103/parse.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace rmtc103 {
namespace {
using Json = nlohmann::json;

bool int_field(const Json& j, const char* key, int* out) {
    if (!j.contains(key)) return false;
    const auto& v = j[key];
    if (!v.is_number_integer()) return false;
    if(v.is_number_unsigned() && v.get<unsigned long long>()>static_cast<unsigned long long>(std::numeric_limits<int>::max())) return false;
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
    Json xy={{"x",j[key][0]},{"y",j[key][1]}};
    if(!int_field(xy,"x",&out->x) || !int_field(xy,"y",&out->y)) return false;
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
    int twos=0;
    bool selfFound = false;
    for (int row = 0; row < 9; ++row) {
        if (row < 8 && text[static_cast<std::size_t>(row) * 10 + 9] != '\n') return false;
        for (int col = 0; col < 9; ++col) {
            const char c = text[static_cast<std::size_t>(row) * 10 + col];
            if (c == '1') {
                if (row != 4 || col != 4) return false;
                selfFound = true;
            } else if (c == '2') {
                if(++twos>1) return false;
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

bool shallow_json(const std::string& text) {
    int depth=0;bool quoted=false,escaped=false;
    for(char c:text){if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}
        else if(c=='"')quoted=true;else if(c=='{'||c=='['){if(++depth>64)return false;}else if(c=='}'||c==']'){if(--depth<0)return false;}}
    return depth==0&&!quoted;
}
bool valid_window(int start, int end, int maxTicks) {
    return start >= 0 && end > start && end <= maxTicks + 400;
}
}  // namespace

static bool parse_perception_impl(const std::string& text, Perception* out, std::string* err) {
    if (!out) return false;
    const auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    if(text.size()>1024*1024 || !shallow_json(text)) return fail("E_SIZE_OR_DEPTH");
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
    if (j.contains("vision_radius")) {
        int raw = 4;
        if (int_field(j, "vision_radius", &raw)) vision = std::max(0, std::min(4, raw));
        else return fail("E_VISION");
    }
    p.vision = vision;

    if (!j.contains("self") || !j["self"].is_object()) return fail("E_SCHEMA");
    const auto& self = j["self"];
    if (!point_field(self, "position", &p.self)) return fail("E_SCHEMA");
    if (p.self.x < 0 || p.self.y < 0 || p.self.x >= p.mapW || p.self.y >= p.mapH) return fail("E_SCHEMA");
    int hp = 0;
    if (!int_field(self, "hp", &hp)) return fail("E_SCHEMA");
    p.hp = std::max(0, std::min(100, hp));
    p.team = self.value("team", std::string{});
    if (p.team.empty()) p.team = self.value("robot_id", std::string{});
    const bool aliveFlag = self.value("alive", p.hp > 0);
    // A frame that says alive=true while hp=0 (death tick) is still dead for planning.
    p.alive = aliveFlag && p.hp > 0;
    p.defending = self.value("defending", false);
    p.protectedSpawn = self.value("protected", false);
    int_field(self, "respawn_in", &p.respawnIn);
    const std::string reason = self.value("position_reason", std::string{});
    p.positionInitial = (reason == "initial");
    p.positionRespawn = (reason == "respawn");
    if(self.contains("last_action") && self["last_action"].is_object()) {
        const auto& receipt=self["last_action"];
        int receiptTick=-1;
        if(int_field(receipt,"tick",&receiptTick) && receiptTick<p.tick) {
            p.lastActionTick=receiptTick;
            p.lastMoved=receipt.value("moved",false);
            if (receipt.contains("action") && receipt["action"].is_string()) {
                const auto kind=receipt["action"].get<std::string>();
                if(kind=="move" || kind=="attack" || kind=="defend" || kind=="wait") p.lastActionKind=kind;
            }
        }
    }
    // Missing visible_entities is treated as an empty frame, not as a fatal schema error.
    if (!j.contains("visible_entities")) p.observationComplete = false;
    if (j.contains("visible_entities") && !j["visible_entities"].is_array()) return fail("E_ENTITIES");
    if (j.contains("visible_entities")) {
        const auto& list = j["visible_entities"];
        const std::size_t count = std::min<std::size_t>(list.size(), 4096);
        p.entities.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& e = list[i];
            if (!e.is_object() || !e.contains("type") || !e["type"].is_string()) { p.observationComplete=false; continue; }
            const std::string type = e["type"].get<std::string>();
            Entity entity;
            if (type == "robot") entity.type = EntityType::Robot;
            else if (type == "resource_normal") entity.type = EntityType::ResNormal;
            else if (type == "resource_high") entity.type = EntityType::ResHigh;
            else if (type == "obstacle") entity.type = EntityType::Obstacle;
            else { p.observationComplete=false; continue; } // only positive observations may update memory

            Point delta;
            if (!e.contains("position") || !e["position"].is_string() ||
                !relative_delta(e["position"].get<std::string>(), &delta)) {
                p.observationComplete=false;
                continue;
            }
            if(std::abs(delta.x)>p.vision || std::abs(delta.y)>p.vision) { p.observationComplete=false; continue; }
            entity.pos = {p.self.x + delta.x, p.self.y + delta.y};
            if (entity.pos.x < 0 || entity.pos.y < 0 || entity.pos.x >= p.mapW || entity.pos.y >= p.mapH) {
                p.observationComplete=false;
                continue;
            }
            entity.id = e.value("id", std::string{});
            entity.team = e.value("team", std::string{});
            if (entity.type == EntityType::Robot) {
                int ehp = 0;
                if (!int_field(e, "hp", &ehp) || ehp<=0 || ehp>100) { p.observationComplete=false; continue; }
                if (entity.team.empty()) entity.team = e.value("robot_id", std::string{});
                if (entity.team.empty()) { p.observationComplete=false; continue; }
                if (entity.team == p.team) continue;
                bool duplicate = false;
                for (const auto& known : p.entities)
                    if (known.type == EntityType::Robot && !entity.team.empty() && known.team == entity.team) duplicate = true;
                if (duplicate) { p.observationComplete=false; continue; }
                entity.hp = std::max(0, ehp);
                entity.defending = e.value("defending", false);
                entity.protectedSpawn = e.value("protected", false);
            } else if (entity.type != EntityType::Obstacle) {
                double v = 0;
                if (!num_field(e, "value", &v) || v <= 0) {
                    v = entity.type == EntityType::ResHigh ? 20 : 10;
                }
                if (v > 10000) v = 10000;
                entity.value = static_cast<int>(std::llround(v));
                if (entity.value <= 0) entity.value = entity.type == EntityType::ResHigh ? 20 : 10;
                if(entity.id.empty()) entity.id="@"+std::to_string(entity.pos.x)+","+std::to_string(entity.pos.y);
            }
            p.entities.push_back(std::move(entity));
        }
    }

    if (j.contains("score") && j["score"].is_object()) {
        const auto& s = j["score"];
        double v = 0;
        if (num_field(s,"resource_value",&v)) p.resourceValue = std::max(0.0, std::min(1000000.0, v));
        double d = 0;
        if (num_field(s,"effective_damage",&d)) p.effectiveDamage = std::max(0.0, std::min(1000000.0, d));
        int_field(s, "mission_completed", &p.missionsCompleted);
        int_field(s,"invalid_actions",&p.invalidActions);
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
    auto start=text.find('{');if(start==std::string::npos)return {};
    int depth=0;bool quoted=false,escaped=false;
    for(std::size_t i=start;i<text.size();++i){char c=text[i];
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"')quoted=true;else if(c=='{'){if(++depth>64)return {};}else if(c=='}'&&--depth==0){auto object=text.substr(start,i-start+1);return shallow_json(object)?object:std::string{};}
    }return {};
}

namespace {
// 模型输出里的坐标：接受 [x,y] 或 {"x":..,"y":..}
bool loose_point(const Json& obj, const char* key, Point* out) {
    if (!obj.contains(key)) return false;
    const auto& v = obj[key];
    if (v.is_array() && v.size() == 2 && v[0].is_number_integer() && v[1].is_number_integer()) {
        out->x = static_cast<int>(std::max<long long>(-100000, std::min<long long>(100000, v[0].get<long long>())));
        out->y = static_cast<int>(std::max<long long>(-100000, std::min<long long>(100000, v[1].get<long long>())));
        return true;
    }
    if (v.is_object() && v.contains("x") && v.contains("y") && v["x"].is_number_integer() && v["y"].is_number_integer()) {
        out->x = static_cast<int>(std::max<long long>(-100000, std::min<long long>(100000, v["x"].get<long long>())));
        out->y = static_cast<int>(std::max<long long>(-100000, std::min<long long>(100000, v["y"].get<long long>())));
        return true;
    }
    return false;
}
bool loose_int(const Json& obj, const char* key, int* out) {
    if (!obj.contains(key) || !obj[key].is_number()) return false;
    const double v = obj[key].get<double>();
    if (!std::isfinite(v) || v < -1e6 || v > 1e6) return false;
    *out = static_cast<int>(std::lround(v));
    return true;
}
// 模型可能用规则原文里的名字；这里只把“模型输出的 kind 字段”归一，不解析公告文本。
std::string canonical_kind(std::string k) {
    for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (k == "explosion" || k == "blast" || k == "爆炸") return "explosion";
    if (k == "inversion" || k == "illusion" || k == "幻术") return "inversion";
    if (k == "heal" || k == "angel" || k == "天使恩泽") return "heal";
    if (k == "fortress" || k == "unrivaled" || k == "天下无双") return "fortress";
    if (k == "split" || k == "world_split" || k == "世界分裂") return "split";
    if (k == "chili" || k == "pepper" || k == "火爆辣椒") return "chili";
    if (k == "reflect" || k == "purity" || k == "洁身自好") return "reflect";
    if (k == "vulnerability" || k == "pan" || k == "潘潘的神") return "vulnerability";
    return {};
}
// 单条事件：缺的数值用规则正文公布的标准数值补（公开规则，不是猜测）；坏条目返回 false 只丢这一条。
bool parse_event(const Json& obj, int maxTicks, int mapW, int mapH, Event* out) {
    if (!obj.is_object() || !obj.contains("kind") || !obj["kind"].is_string()) return false;
    Event e;
    e.kind = canonical_kind(obj["kind"].get<std::string>());
    if (e.kind.empty()) return false;
    e.id = obj.contains("id") && obj["id"].is_string() ? obj["id"].get<std::string>() : std::string{};
    if (e.id.size() > 128) e.id.resize(128);
    const bool global = e.kind == "reflect" || e.kind == "vulnerability";
    if (!loose_point(obj, "a", &e.a)) {
        if (!global) return false;
        e.a = {0, 0};
    }
    if (!loose_point(obj, "b", &e.b)) e.b = global && !obj.contains("a") ? Point{mapW - 1, mapH - 1} : e.a;
    if (e.kind == "fortress" && e.a == e.b) { e.a = {e.a.x - 1, e.a.y - 1}; e.b = {e.b.x + 1, e.b.y + 1}; }  // 只给了中心
    if (e.a.x > e.b.x) std::swap(e.a.x, e.b.x);
    if (e.a.y > e.b.y) std::swap(e.a.y, e.b.y);
    if (e.b.x < 0 || e.b.y < 0 || e.a.x >= mapW || e.a.y >= mapH) return false;
    if (e.kind == "split" || e.kind == "chili" || e.kind == "heal") {
        if (e.a.x < 0 || e.a.y < 0 || e.a.x >= mapW || e.a.y >= mapH) return false;  // 点位必须在图内
    }
    e.a.x = std::max(0, e.a.x); e.a.y = std::max(0, e.a.y);
    e.b.x = std::min(mapW - 1, e.b.x); e.b.y = std::min(mapH - 1, e.b.y);
    if (!loose_int(obj, "start", &e.start) || e.start < 0 || e.start > maxTicks + 400) return false;
    int standardEnd = e.start + 1;
    if (e.kind == "split" || e.kind == "chili" || e.kind == "vulnerability") standardEnd = e.start + 30;
    else if (e.kind == "heal" || e.kind == "reflect") standardEnd = e.start + 20;
    else if (e.kind == "fortress") standardEnd = e.start + 50;
    if (!loose_int(obj, "end", &e.end) || e.end <= e.start) e.end = standardEnd;
    if (!valid_window(e.start, e.end, maxTicks)) return false;
    loose_int(obj, "damage", &e.damage);
    const bool hasDefendDamage = loose_int(obj, "defend_damage", &e.defendDamage);
    loose_int(obj, "duration", &e.duration);
    loose_int(obj, "heal", &e.heal);
    loose_int(obj, "dwell", &e.dwell);
    loose_int(obj, "trigger_end", &e.triggerEnd);
    if (e.kind == "explosion" && (e.damage <= 0 || e.damage > 1000)) e.damage = 80;
    if (e.kind == "chili" && (e.damage <= 0 || e.damage > 1000)) e.damage = 50;
    if (e.kind == "inversion" && (e.duration <= 0 || e.duration > maxTicks)) e.duration = 50;
    if (e.kind == "heal") {
        if (e.duration <= 0 || e.duration > maxTicks) e.duration = 10;
        if (e.heal <= 0 || e.heal > 100) e.heal = 10;
    }
    if (e.kind == "fortress") {
        if (e.dwell <= 0 || e.dwell > 100) e.dwell = 10;
        if (e.damage <= 0 || e.damage > 1000) e.damage = 50;
        if (!hasDefendDamage || e.defendDamage < 0 || e.defendDamage > e.damage)
            e.defendDamage = std::min(10, e.damage);
    }
    if (e.kind == "reflect" && (e.damage <= 0 || e.damage > 1000)) e.damage = 20;
    if (e.kind == "vulnerability" && (e.triggerEnd <= e.start || e.triggerEnd > e.end)) e.triggerEnd = std::min(e.end, e.start + 20);
    *out = e;
    return true;
}
}  // namespace

static bool parse_plan_impl(const std::string& content, const std::string& session, int sourceTick,
                int maxTicks, int mapW, int mapH, Plan* out) {
    if (!out) return false;
    if(content.size()>256*1024 || mapW<=0 || mapH<=0 || maxTicks<=0 || maxTicks>1000000) return false;
    const std::string body = first_json_object(content);
    if (body.empty()) return false;
    Json j;
    try {
        j = Json::parse(body);
    } catch (const std::exception&) {
        return false;
    }
    if (!j.is_object()) return false;

    // 8.0：逐条校验。7.0 只要有一条事件字段不全、kind 不认识、id 重复，或者模型
    // 同时写了 events 和 hazards，就把整份计划作废——真实模型很容易这样写。
    Plan plan;
    plan.session = session;
    plan.sourceTick = sourceTick;
    int accepted = 0, rejected = 0;
    // 顶层字段类型不对（例如 "effects": 3）记为一处错误；若整份没有任何可用条目则返回失败以便重试。
    if (j.contains("events") && !j["events"].is_array()) ++rejected;
    if (j.contains("hazards") && !j["hazards"].is_array()) ++rejected;
    if (j.contains("effects") && !j["effects"].is_object()) ++rejected;
    if (j.contains("advice") && !j["advice"].is_object() && !j["advice"].is_null()) ++rejected;

    if (j.contains("events") && j["events"].is_array()) {
        std::set<std::string> eventIds;
        int index = 0;
        for (const auto& obj : j["events"]) {
            if (++index > 64) break;
            Event e;
            if (!parse_event(obj, maxTicks, mapW, mapH, &e)) { ++rejected; continue; }
            if (e.id.empty()) e.id = e.kind + "@" + std::to_string(e.start) + ":" + std::to_string(e.a.x) + "," + std::to_string(e.a.y);
            if (!eventIds.insert(e.id).second) {
                // One stable id names one fact. Keep the first valid occurrence;
                // never turn a duplicate/conflict into a second damage source.
                ++rejected;
                continue;
            }
            plan.events.push_back(e);
            ++accepted;
        }
    }
    // 有类型化事件时忽略 hazards，避免同一公告被算两遍；没有事件时才用 hazards。
    if (plan.events.empty() && j.contains("hazards") && j["hazards"].is_array()) {
        int index = 0;
        for (const auto& h : j["hazards"]) {
            if (++index > 64) break;
            if (!h.is_object()) { ++rejected; continue; }
            Hazard z;
            const std::string shape = h.contains("shape") && h["shape"].is_string() ? h["shape"].get<std::string>() : std::string("rect");
            Point a, b;
            if (!loose_point(h, "a", &a)) { ++rejected; continue; }
            if (!loose_point(h, "b", &b)) b = a;
            if (shape == "cross") { z.cross = true; z.a = z.b = a; }
            else { z.a = {std::min(a.x, b.x), std::min(a.y, b.y)}; z.b = {std::max(a.x, b.x), std::max(a.y, b.y)}; }
            if (z.b.x < 0 || z.b.y < 0 || z.a.x >= mapW || z.a.y >= mapH) { ++rejected; continue; }
            if (z.cross && (z.a.x < 0 || z.a.y < 0)) { ++rejected; continue; }
            z.a.x = std::max(0, z.a.x); z.a.y = std::max(0, z.a.y);
            z.b.x = std::min(mapW - 1, z.b.x); z.b.y = std::min(mapH - 1, z.b.y);
            if (!loose_int(h, "start", &z.start) || !loose_int(h, "end", &z.end) || !valid_window(z.start, z.end, maxTicks)) { ++rejected; continue; }
            int dmg = 0;
            loose_int(h, "damage", &dmg);
            z.damage = std::max(0, std::min(1000, dmg));
            bool invalidBoolean = false;
            for (const char* key : {"wall", "lethal", "respects_protection"})
                if (h.contains(key) && !h[key].is_boolean()) invalidBoolean = true;
            if (invalidBoolean) { ++rejected; continue; }
            z.wall = h.value("wall", false) == true;
            z.lethal = h.value("lethal", false) == true;
            z.respectsProtection = h.value("respects_protection", false) == true;
            if (z.damage == 0 && !z.wall && !z.lethal) continue;
            plan.hazards.push_back(z);
            ++accepted;
        }
    }

    const auto read_window = [&](const Json& e, const char* key, Window* w) {
        if (!e.contains(key)) return;
        const auto& arr = e[key];
        if (!arr.is_array() || arr.size() != 2 || !arr[0].is_number() || !arr[1].is_number()) { ++rejected; return; }
        const double start = arr[0].get<double>(), end = arr[1].get<double>();
        // Check before rounding/conversion. Model time windows are whole ticks.
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end > maxTicks + 400.0 ||
            std::floor(start) != start || std::floor(end) != end || end <= start) { ++rejected; return; }
        const int s0 = static_cast<int>(start), t0 = static_cast<int>(end);
        if (valid_window(s0, t0, maxTicks)) { w->start = s0; w->end = t0; ++accepted; }
        else ++rejected;
    };
    if (plan.events.empty() && j.contains("effects") && j["effects"].is_object()) {
        const auto& e = j["effects"];
        read_window(e, "inverted", &plan.inverted);
        read_window(e, "vulnerable", &plan.vulnerable);
        read_window(e, "reflect", &plan.reflect);
        int rd = 20;
        if (loose_int(e, "reflect_damage", &rd) && rd >= 0 && rd <= 200) plan.reflectDamage = rd;
    }
    if (j.contains("advice") && j["advice"].is_object()) {
        Point p;
        int prio = 1;
        if (loose_point(j["advice"], "target", &p) && p.x >= 0 && p.y >= 0 && p.x < mapW && p.y < mapH) {
            plan.advice = p;
            if (loose_int(j["advice"], "priority", &prio)) plan.advicePriority = std::max(1, std::min(3, prio));
        }
    }
    // 空对象 {} 是合法的“无事件计划”；但如果模型写了条目却一条都用不了，返回失败以便有限重试。
    if (accepted == 0 && rejected > 0) return false;
    plan.valid = true;
    *out = std::move(plan);
    return true;
}

bool parse_perception(const std::string& text, Perception* out, std::string* err) {
    try { return parse_perception_impl(text,out,err); }
    catch(const std::exception&) { if(err) *err="E_SCHEMA"; return false; }
}
bool parse_plan(const std::string& text,const std::string& session,int sourceTick,
                int maxTicks,int w,int h,Plan* out) {
    try { return parse_plan_impl(text,session,sourceTick,maxTicks,w,h,out); }
    catch(const std::exception&) { return false; }
}
}  // namespace rmtc103
