// rmtc2/parse.cpp
#include "rmtc2/parse.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace rmtc2 {
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
    if (j.contains("vision_radius") &&
        (!int_field(j, "vision_radius", &vision) || vision < 0 || vision > 4)) return fail("E_VISION");
    p.vision = vision;

    if (!j.contains("self") || !j["self"].is_object()) return fail("E_SCHEMA");
    const auto& self = j["self"];
    if (!point_field(self, "position", &p.self)) return fail("E_SCHEMA");
    if (p.self.x < 0 || p.self.y < 0 || p.self.x >= p.mapW || p.self.y >= p.mapH) return fail("E_SCHEMA");
    int hp = 0;
    if (!int_field(self, "hp", &hp)) return fail("E_SCHEMA");
    if(hp<0 || hp>100) return fail("E_HP");
    p.hp = hp;
    p.team = self.value("team", std::string{});
    if(!self.contains("alive") || !self["alive"].is_boolean()) return fail("E_SCHEMA");
    p.alive = self["alive"].get<bool>();
    if(p.team.size()!=1 || p.team[0]<'A' || p.team[0]>'D' || p.alive!=(p.hp>0)) return fail("E_STATE");
    p.defending = self.value("defending", false);
    p.protectedSpawn = self.value("protected", false);
    int_field(self, "respawn_in", &p.respawnIn);
    const std::string reason = self.value("position_reason", std::string{});
    p.positionInitial = (reason == "initial");
    p.positionRespawn = (reason == "respawn");
    if(self.contains("last_action") && self["last_action"].is_object()) {
        const auto& receipt=self["last_action"];
        if(!int_field(receipt,"tick",&p.lastActionTick) || p.lastActionTick>=p.tick) return fail("E_RECEIPT");
        p.lastMoved=receipt.value("moved",false);
    }
    if(!j.contains("visible_entities") || !j["visible_entities"].is_array() || j["visible_entities"].size()>1024) return fail("E_ENTITIES");

    if (j.contains("visible_entities")) {
        if (!j["visible_entities"].is_array()) return fail("E_SCHEMA");
        p.entities.reserve(j["visible_entities"].size());
        for (const auto& e : j["visible_entities"]) {
            if (!e.is_object() || !e.contains("type") || !e["type"].is_string()) return fail("E_ENTITY_SCHEMA");
            const std::string type = e["type"].get<std::string>();
            Entity entity;
            if (type == "robot") entity.type = EntityType::Robot;
            else if (type == "resource_normal") entity.type = EntityType::ResNormal;
            else if (type == "resource_high") entity.type = EntityType::ResHigh;
            else if (type == "obstacle") entity.type = EntityType::Obstacle;
            else { p.observationComplete=false; continue; } // only positive observations may update memory

            Point delta;
            if (!e.contains("position") || !e["position"].is_string()) return fail("E_ENTITY_POSITION");
            if (!relative_delta(e["position"].get<std::string>(), &delta)) return fail("E_ENTITY_POSITION");
            if(std::abs(delta.x)>p.vision || std::abs(delta.y)>p.vision) return fail("E_ENTITY_VISION");
            entity.pos = {p.self.x + delta.x, p.self.y + delta.y};
            if (entity.pos.x < 0 || entity.pos.y < 0 || entity.pos.x >= p.mapW || entity.pos.y >= p.mapH) return fail("E_ENTITY_POSITION");
            entity.id = e.value("id", std::string{});
            entity.team = e.value("team", std::string{});
            if (entity.type == EntityType::Robot) {
                int ehp = 0;
                if (!int_field(e, "hp", &ehp) || ehp<=0 || ehp>100) return fail("E_ENTITY_HP");
                if(entity.team.size()!=1 || entity.team[0]<'A' || entity.team[0]>'D' || entity.team==p.team) return fail("E_ENTITY_TEAM");
                entity.hp = std::max(0, ehp);
                entity.defending = e.value("defending", false);
                entity.protectedSpawn = e.value("protected", false);
            } else if (entity.type != EntityType::Obstacle) {
                double v = 0;
                if (!num_field(e, "value", &v) || v <= 0) {
                    v = entity.type == EntityType::ResHigh ? 20 : 10;
                }
                if(v>10000 || std::floor(v)!=v) return fail("E_RESOURCE_VALUE");
                entity.value = static_cast<int>(v);
                if(entity.id.empty()) entity.id="@"+std::to_string(entity.pos.x)+","+std::to_string(entity.pos.y);
            }
            p.entities.push_back(std::move(entity));
        }
    }

    if(!j.contains("score") || !j["score"].is_object()) return fail("E_SCORE");
    if (j.contains("score") && j["score"].is_object()) {
        const auto& s = j["score"];
        double v = 0;
        if (!num_field(s,"resource_value",&v)||v<0||v>1000000)return fail("E_SCORE");
        p.resourceValue=v;
        double d = 0;
        if (!num_field(s,"effective_damage",&d)||d<0||d>1000000)return fail("E_SCORE");
        p.effectiveDamage=d;
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

    if ((!j.contains("hazards") || !j["hazards"].is_array()) && (!j.contains("events") || !j["events"].is_array())) return false;
    if (j.contains("effects") && !j["effects"].is_object()) return false;
    if (j.contains("advice") && !j["advice"].is_object() && !j["advice"].is_null()) return false;
    Plan plan;
    plan.session = session;
    plan.sourceTick = sourceTick;

    if (j.contains("hazards")) {
        if (!j["hazards"].is_array() || j["hazards"].size() > 32) return false;
        for (const auto& h : j["hazards"]) {
            if (!h.is_object()) return false;
            Hazard z;
            const std::string shape = h.value("shape", std::string("rect"));
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (shape == "cross") {
                Point p;
                if (!point_field(h,"a",&p)) return false;
                z.cross = true;
                z.a = p;
                z.b = p;
                x1 = x2 = p.x;
                y1 = y2 = p.y;
            } else if(shape=="rect") {
                Point a, b;
                if (!point_field(h,"a",&a)||!point_field(h,"b",&b)) return false;
                x1 = std::min(a.x, b.x);
                y1 = std::min(a.y, b.y);
                x2 = std::max(a.x, b.x);
                y2 = std::max(a.y, b.y);
                z.a = {x1, y1};
                z.b = {x2, y2};
            } else return false;
            if (x1<0 || y1<0 || x2>=mapW || y2>=mapH) return false;
            if (x2 < 0 || y2 < 0 || x1 >= mapW || y1 >= mapH) continue;
            z.a.x = std::max(0, z.a.x);
            z.a.y = std::max(0, z.a.y);
            z.b.x = std::min(mapW - 1, z.b.x);
            z.b.y = std::min(mapH - 1, z.b.y);
            int start = 0, end = 0;
            if (!int_field(h,"start",&start)||!int_field(h,"end",&end)) return false;
            if(!valid_window(start,end,maxTicks))return false;
            z.start = start;
            z.end = end;
            int dmg = 0;
            int_field(h, "damage", &dmg);
            z.damage = std::max(0, std::min(1000, dmg));
            z.wall = h.value("wall", false);
            z.lethal = h.value("lethal", false);
            z.respectsProtection=h.value("respects_protection",false);
            if (z.damage == 0 && !z.wall && !z.lethal) continue;
            plan.hazards.push_back(z);
        }
    }

    const auto read_window = [&](const Json& e, const char* key, Window* w) {
        if (!e.contains(key)) return;
        const auto& arr = e[key];
        if (!arr.is_array() || arr.size() != 2) return;
        if (!arr[0].is_number_integer() || !arr[1].is_number_integer()) return;
        Json values={{"s",arr[0]},{"t",arr[1]}};
        int s=-1,t=-1;if(!int_field(values,"s",&s)||!int_field(values,"t",&t)) return;
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
    if(j.contains("events")) {
        if(!j["events"].is_array() || j["events"].size()>32) return false;
        std::set<std::string> eventIds;
        for(const auto& obj:j["events"]) {
            if(!obj.is_object()) return false;
            Event e;e.id=obj.value("id",std::string{});e.kind=obj.value("kind",std::string{});
            if(e.id.empty() || e.id.size()>128 || !point_field(obj,"a",&e.a) || !point_field(obj,"b",&e.b) ||
               e.a.x<0||e.a.y<0||e.b.x>=mapW||e.b.y>=mapH||e.a.x>e.b.x||e.a.y>e.b.y) return false;
            if(!int_field(obj,"start",&e.start)||!int_field(obj,"end",&e.end)||!valid_window(e.start,e.end,maxTicks)) return false;
            for(const char* key:{"damage","defend_damage","duration","heal","dwell","trigger_end"}) {
                int value=0;if(obj.contains(key) && (!int_field(obj,key,&value)||value<0||value>1000000)) return false;
            }
            int_field(obj,"damage",&e.damage);int_field(obj,"defend_damage",&e.defendDamage);
            int_field(obj,"duration",&e.duration);int_field(obj,"heal",&e.heal);
            int_field(obj,"dwell",&e.dwell);int_field(obj,"trigger_end",&e.triggerEnd);
            if(e.kind=="explosion"||e.kind=="chili") {if(e.damage<=0||e.damage>1000)return false;}
            else if(e.kind=="inversion") {if(e.duration<=0||e.duration>maxTicks)return false;}
            else if(e.kind=="heal") {if(e.duration<=0||e.heal<=0||e.heal>100)return false;}
            else if(e.kind=="fortress") {if(e.dwell<=0||e.dwell>100||e.damage<=0||e.damage>1000||e.defendDamage>e.damage)return false;}
            else if(e.kind=="reflect") {if(e.damage<=0||e.damage>1000)return false;}
            else if(e.kind=="vulnerability") {if(e.triggerEnd<=e.start||e.triggerEnd>e.end)return false;}
            else if(e.kind!="split") return false;
            if(!eventIds.insert(e.id).second)return false;
            plan.events.push_back(e);
        }
    }
    if(!plan.events.empty()&&!plan.hazards.empty())return false; // no double encoding of the same event
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
}  // namespace rmtc2
