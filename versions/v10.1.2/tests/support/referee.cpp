// tools/bench/referee.cpp —— 8.0 离线仿真裁判（见 referee.hpp）
#include "referee.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace bench {
using rmtc9::Point;
namespace {

bool inside_of(const MapDef& m, Point p) {
    return p.x >= 0 && p.y >= 0 && p.x < m.w && p.y < m.h;
}

int count_reachable(const MapDef& m, Point from) {
    if (!inside_of(m, from) || m.wall[static_cast<std::size_t>(from.y) * m.w + from.x]) return 0;
    std::vector<char> seen(static_cast<std::size_t>(m.w) * m.h, 0);
    std::queue<Point> q;
    q.push(from);
    seen[static_cast<std::size_t>(from.y) * m.w + from.x] = 1;
    int count = 0;
    while (!q.empty()) {
        const auto p = q.front();
        q.pop();
        ++count;
        for (int d = 0; d < 4; ++d) {
            const Point n{p.x + rmtc9::kDirX[d], p.y + rmtc9::kDirY[d]};
            if (!inside_of(m, n)) continue;
            const std::size_t idx = static_cast<std::size_t>(n.y) * m.w + n.x;
            if (m.wall[idx] || seen[idx]) continue;
            seen[idx] = 1;
            q.push(n);
        }
    }
    return count;
}

bool in_rect(Point p, Point a, Point b) { return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y; }

}  // namespace

// 与 7.0 相同的随机地图（保证新旧版本在同一批地图上比较）
MapDef generate_map(int w, int h, int robotCount, unsigned seed, int stage) {
    std::mt19937 rng(seed);
    MapDef map;
    map.stage = stage;
    map.w = w;
    map.h = h;
    map.wall.assign(static_cast<std::size_t>(w) * h, 0);

    const Point corners[4] = {{1, 1}, {w - 2, 1}, {1, h - 2}, {w - 2, h - 2}};
    for (int i = 0; i < std::min(robotCount, 4); ++i) map.spawns.push_back(corners[i]);

    for (int attempt = 0; attempt < 200; ++attempt) {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                bool nearSpawn = false;
                for (const auto& s : map.spawns) {
                    if (std::abs(s.x - x) <= 1 && std::abs(s.y - y) <= 1) nearSpawn = true;
                }
                map.wall[static_cast<std::size_t>(y) * w + x] = (!nearSpawn && u(rng) < 0.13) ? 1 : 0;
            }
        }
        int freeCells = w * h - static_cast<int>(std::count(map.wall.begin(), map.wall.end(), 1));
        const int reach = count_reachable(map, map.spawns.front());
        if (reach < freeCells * 3 / 4) continue;
        bool allConnected = true;
        for (const auto& s : map.spawns) {
            if (count_reachable(map, s) != reach) allConnected = false;
        }
        if (!allConnected) continue;
        break;
    }

    std::uniform_int_distribution<int> rx(0, w - 1);
    std::uniform_int_distribution<int> ry(0, h - 1);
    std::vector<Point> used;
    const auto farEnough = [&](Point p) {
        for (const auto& s : map.spawns) {
            if (std::abs(s.x - p.x) + std::abs(s.y - p.y) < 4) return false;
        }
        for (const auto& u2 : used) {
            if (u2 == p || std::abs(u2.x - p.x) + std::abs(u2.y - p.y) < 2) return false;
        }
        return true;
    };
    int id = 0;
    for (int i = 0; i < 16; ++i) {
        const int value = i < 12 ? 10 : 20;
        for (int tries = 0; tries < 4000; ++tries) {
            const Point p{rx(rng), ry(rng)};
            if (!inside_of(map, p) || map.wall[static_cast<std::size_t>(p.y) * w + p.x]) continue;
            if (!farEnough(p)) continue;
            if (count_reachable(map, p) != count_reachable(map, map.spawns.front())) continue;
            used.push_back(p);
            Resource r;
            r.id = (value >= 20 ? "h" : "n") + std::to_string(id++);
            r.pos = p;
            r.value = value;
            map.resources.push_back(r);
            break;
        }
    }

    if (stage == 2) {
        rmtc9::Mission m1;
        m1.id = "collect_two_normal";
        m1.type = "normal_resources";
        m1.target = 2;
        rmtc9::Mission m2;
        m2.id = "collect_high_value";
        m2.type = "high_resources";
        m2.target = 1;
        rmtc9::Mission m3;
        m3.id = "reach_target";
        m3.type = "reach";
        m3.target = 1;
        Point center{w / 2, h / 2};
        for (int radius = 0; radius < std::max(w, h); ++radius) {
            bool found = false;
            for (int dy = -radius; dy <= radius && !found; ++dy) {
                for (int dx = -radius; dx <= radius && !found; ++dx) {
                    const Point p{center.x + dx, center.y + dy};
                    if (!inside_of(map, p)) continue;
                    if (map.wall[static_cast<std::size_t>(p.y) * w + p.x]) continue;
                    m3.position = p;
                    found = true;
                }
            }
            if (found) break;
        }
        map.missions = {m1, m2, m3};
        map.missionText = "收集两个普通资源、一个高价值资源，并到达指定坐标。";
    }
    return map;
}

bool load_practice_config(const std::string& path, int robotCount, MapDef* out, std::string* err) {
    using Json = nlohmann::json;
    try {
        std::ifstream in(path);
        if (!in) { if (err) *err = "cannot open " + path; return false; }
        in.seekg(0,std::ios::end);
        if (in.tellg()>1024*1024) throw std::runtime_error("configuration exceeds 1 MiB");
        in.seekg(0);
        const Json j = Json::parse(in);
        MapDef m;
        const auto integer=[](const Json& v,int lo,int hi) {
            if(!v.is_number_integer() || v<lo || v>hi) throw std::runtime_error("integer out of bounds");
            return v.get<int>();
        };
        m.stage = integer(j.at("stage"),1,2);
        m.w = integer(j.at("map").at("width"),1,256);
        m.h = integer(j.at("map").at("height"),1,256);
        m.vision = integer(j.value("vision_radius",Json(4)),0,4);
        const auto point=[&](const Json& v) {
            if(!v.is_array() || v.size()!=2) throw std::runtime_error("expected coordinate pair");
            return Point{integer(v[0],0,m.w-1),integer(v[1],0,m.h-1)};
        };
        m.wall.assign(static_cast<std::size_t>(m.w) * m.h, 0);
        if (!j.at("map").at("obstacles").is_array() || !j.at("robots").is_array() ||
            !j.at("map").at("resources").is_array()) throw std::runtime_error("expected map arrays");
        for (const auto& o : j.at("map").at("obstacles")) {
            const auto q=point(o);m.wall[static_cast<std::size_t>(q.y)*m.w+q.x]=1;
        }
        if(robotCount<1 || robotCount>4 || j.at("robots").size()<static_cast<std::size_t>(robotCount))
            throw std::runtime_error("insufficient robot seats");
        int k = 0;
        for (const auto& r : j.at("robots")) {
            if (k++ >= robotCount) break;
            const auto q=point(r.at("spawn"));
            if(m.wall[static_cast<std::size_t>(q.y)*m.w+q.x] || std::find(m.spawns.begin(),m.spawns.end(),q)!=m.spawns.end())
                throw std::runtime_error("blocked or duplicate spawn");
            m.spawns.push_back(q);
        }
        std::set<std::string> ids;
        for (const auto& r : j.at("map").at("resources")) {
            Resource res;
            res.id = r.at("id").get<std::string>();
            if(res.id.empty() || !ids.insert(res.id).second) throw std::runtime_error("duplicate/empty resource ID");
            res.pos = point(r.at("position"));
            if(m.wall[static_cast<std::size_t>(res.pos.y)*m.w+res.pos.x]) throw std::runtime_error("resource inside wall");
            res.value = integer(r.at("value"),1,200);
            m.resources.push_back(res);
        }
        if (j.value("stage", 1) == 2) {
            for (const auto& mj : j.at("missions")) {
                rmtc9::Mission mi;
                mi.id = mj.at("id").get<std::string>();
                mi.type = mj.at("type").get<std::string>();
                mi.target = mj.value("target", 1);
                if (mj.contains("position")) mi.position = point(mj["position"]);
                m.missions.push_back(mi);
            }
            m.missionText = j.value("mission_text", std::string{});
            const auto& ev = j.at("events");
            if (ev.value("enabled", false)) {
                const int delay = integer(ev.value("announcement_delay_ticks",Json(5)),0,100000);
                const auto& defs = ev.at("definitions");
                int serial = 0;
                for (const auto& s : ev.at("schedule")) {
                    const std::string kind = s.at("event").get<std::string>();
                    const auto& d = defs.at(kind);
                    EventInst e;
                    e.id = s.value("id", "ev" + std::to_string(serial));
                    ++serial;
                    e.announce = integer(s.at("announce_tick"),0,100000);
                    e.start = e.announce + delay;
                    for(const char* key:{"damage","duration_ticks","effect_ticks","acquire_ticks","heal_per_tick","stay_ticks",
                                         "attack_damage","defended_damage","reflection_damage","vulnerability_ticks","movement_window_ticks"})
                        if(d.contains(key)) integer(d.at(key),0,100000);
                    const auto rect = [&](EventInst* x) {
                        x->a = point(d.at("rect").at("top_left"));
                        x->b = point(d.at("rect").at("bottom_right"));
                        if(x->a.x>x->b.x || x->a.y>x->b.y) throw std::runtime_error("reversed event rectangle");
                    };
                    const auto points = [&](EventInst* x) {
                        if(!d.at("points").is_array() || d.at("points").empty()) throw std::runtime_error("empty event points");
                        for (const auto& p : d.at("points")) x->points.push_back(point(p));
                    };
                    if (kind == "explosion") { e.kind = EventKind::Explosion; rect(&e); e.damage = d.value("damage", 80); e.end = e.start + 1; }
                    else if (kind == "illusion") { e.kind = EventKind::Illusion; rect(&e); e.effect = d.value("effect_ticks", 50); e.end = e.start + 1; }
                    else if (kind == "angel") { e.kind = EventKind::Angel; points(&e); e.end = e.start + d.value("duration_ticks", 20); e.window = d.value("acquire_ticks", 20); e.effect = d.value("effect_ticks", 10); e.heal = d.value("heal_per_tick", 10); }
                    else if (kind == "unrivaled") {
                        e.kind = EventKind::Fortress;
                        // Offline extension for v1.2 coverage. It does not imply
                        // the archived official v1.1 binary accepts this field.
                        if(d.contains("rect")) rect(&e);
                        else {
                            const int size=integer(d.value("size",Json(3)),1,255);
                            if(size%2==0) throw std::runtime_error("fortress size must be odd");
                            const auto center=d.contains("center")?point(d.at("center")):Point{m.w/2,m.h/2};
                            e.a={center.x-size/2,center.y-size/2};e.b={center.x+size/2,center.y+size/2};
                            if(e.a.x<0||e.a.y<0||e.b.x>=m.w||e.b.y>=m.h) throw std::runtime_error("fortress outside map");
                        }
                        e.end=e.start+d.value("duration_ticks",50);e.dwell=d.value("stay_ticks",10);
                        e.damage=d.value("attack_damage",50);e.defendDamage=d.value("defended_damage",10);
                    }
                    else if (kind == "world_split") { e.kind = EventKind::Split; e.a = e.b = point(d.at("point")); e.end = e.start + d.value("duration_ticks", 30); }
                    else if (kind == "pepper") { e.kind = EventKind::Chili; points(&e); e.end = e.start + d.value("duration_ticks", 30); e.damage = d.value("damage", 50); }
                    else if (kind == "purity") { e.kind = EventKind::Purity; e.end = e.start + d.value("duration_ticks", 20); e.damage = d.value("reflection_damage", 20); }
                    else if (kind == "pan") { e.kind = EventKind::Pan; e.end = e.start + d.value("vulnerability_ticks", 30); e.window = d.value("movement_window_ticks", 20); }
                    else continue;
                    m.events.push_back(e);
                }
            }
        }
        *out = std::move(m);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

std::vector<EventInst> generate_events(const MapDef& map, unsigned seed, const std::string& profile) {
    std::mt19937 rng(seed ^ 0x5EEDu);
    const bool practice = profile == "practice";
    std::vector<EventKind> kinds = {EventKind::Explosion, EventKind::Illusion, EventKind::Angel, EventKind::Fortress,
                                    EventKind::Split,     EventKind::Chili,    EventKind::Purity, EventKind::Pan};
    std::shuffle(kinds.begin(), kinds.end(), rng);
    const auto uni = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    const auto free_cell = [&]() {
        for (int t = 0; t < 1000; ++t) {
            const Point p{uni(0, map.w - 1), uni(0, map.h - 1)};
            if (!map.wall[static_cast<std::size_t>(p.y) * map.w + p.x]) return p;
        }
        return Point{map.w / 2, map.h / 2};
    };
    const auto rect = [&](EventInst* e) {
        const int sw = uni(4, 6), sh = uni(4, 6);
        e->a = {uni(0, map.w - sw), uni(0, map.h - sh)};
        e->b = {e->a.x + sw - 1, e->a.y + sh - 1};
    };
    std::vector<EventInst> out;
    int announce = uni(10, 40);
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        EventInst e;
        e.id = "ev" + std::to_string(i);
        e.kind = kinds[i];
        e.announce = announce;
        e.start = announce + (practice ? 5 : uni(5, 20));
        switch (e.kind) {
        case EventKind::Explosion: rect(&e); e.damage = practice ? 35 : 80; e.end = e.start + 1; break;
        case EventKind::Illusion: rect(&e); e.effect = practice ? 12 : 50; e.end = e.start + 1; break;
        case EventKind::Angel:
            e.points = {free_cell(), free_cell()};
            e.end = e.start + (practice ? 16 : 20); e.window = practice ? 16 : 20; e.effect = practice ? 8 : 10; e.heal = practice ? 5 : 10;
            break;
        case EventKind::Fortress:
            { const auto center=practice?Point{map.w/2,map.h/2}:Point{uni(1,map.w-2),uni(1,map.h-2)};
              e.a={center.x-1,center.y-1};e.b={center.x+1,center.y+1}; }
            e.end = e.start + (practice ? 24 : 50); e.dwell = practice ? 4 : 10;
            e.damage = practice ? 35 : 50; e.defendDamage = practice ? 7 : 10;
            break;
        case EventKind::Split: e.a = e.b = free_cell(); e.end = e.start + (practice ? 10 : 30); break;
        case EventKind::Chili: e.points = {free_cell(), free_cell()}; e.end = e.start + (practice ? 16 : 30); e.damage = practice ? 30 : 50; break;
        case EventKind::Purity: e.end = e.start + (practice ? 16 : 20); e.damage = practice ? 12 : 20; break;
        case EventKind::Pan: e.end = e.start + (practice ? 18 : 30); e.window = practice ? 10 : 20; break;
        }
        out.push_back(e);
        announce += uni(25, 55);
        if (announce > 440) break;
    }
    return out;
}

bool has_announcement(const MapDef& map, int tick) {
    for (const auto& e : map.events) if (e.announce == tick) return true;
    return false;
}

std::string oracle_plan(const MapDef& map, int tick, int noise) {
    using Json = nlohmann::json;
    Json events = Json::array();
    Json hazards = Json::array();
    const auto P = [](Point p) { return Json::array({p.x, p.y}); };
    for (const auto& e : map.events) {
        if (e.announce > tick || e.end <= tick) continue;
        switch (e.kind) {
        case EventKind::Explosion:
            events.push_back({{"id", e.id}, {"kind", "explosion"}, {"a", P(e.a)}, {"b", P(e.b)}, {"start", e.start}, {"end", e.start + 1}, {"damage", e.damage}});
            hazards.push_back({{"shape", "rect"}, {"a", P(e.a)}, {"b", P(e.b)}, {"start", e.start}, {"end", e.start + 1}, {"damage", e.damage}, {"wall", false}, {"lethal", false}});
            break;
        case EventKind::Illusion:
            events.push_back({{"id", e.id}, {"kind", "inversion"}, {"a", P(e.a)}, {"b", P(e.b)}, {"start", e.start}, {"end", e.start + 1}, {"duration", e.effect}});
            break;
        case EventKind::Angel:
            for (std::size_t k = 0; k < e.points.size(); ++k)
                events.push_back({{"id", e.id + "_" + std::to_string(k)}, {"kind", "heal"}, {"a", P(e.points[k])}, {"b", P(e.points[k])}, {"start", e.start}, {"end", e.start + e.window}, {"heal", e.heal}, {"duration", e.effect}});
            break;
        case EventKind::Fortress:
            events.push_back({{"id", e.id}, {"kind", "fortress"}, {"a", P(e.a)}, {"b", P(e.b)}, {"start", e.start}, {"end", e.end}, {"damage", e.damage}, {"defend_damage", e.defendDamage}, {"dwell", e.dwell}});
            break;
        case EventKind::Split:
            events.push_back({{"id", e.id}, {"kind", "split"}, {"a", P(e.a)}, {"b", P(e.a)}, {"start", e.start}, {"end", e.end}});
            hazards.push_back({{"shape", "cross"}, {"a", P(e.a)}, {"start", e.start}, {"end", e.end}, {"damage", 0}, {"wall", true}, {"lethal", false}});
            break;
        case EventKind::Chili:
            for (std::size_t k = 0; k < e.points.size(); ++k)
                events.push_back({{"id", e.id + "_" + std::to_string(k)}, {"kind", "chili"}, {"a", P(e.points[k])}, {"b", P(e.points[k])}, {"start", e.start}, {"end", e.end}, {"damage", e.damage}});
            break;
        case EventKind::Purity:
            events.push_back({{"id", e.id}, {"kind", "reflect"}, {"a", P({0, 0})}, {"b", P({map.w - 1, map.h - 1})}, {"start", e.start}, {"end", e.end}, {"damage", e.damage}});
            break;
        case EventKind::Pan:
            events.push_back({{"id", e.id}, {"kind", "vulnerability"}, {"a", P({0, 0})}, {"b", P({map.w - 1, map.h - 1})}, {"start", e.start}, {"end", e.end}, {"trigger_end", e.start + e.window}});
            break;
        }
    }
    Json plan = {{"events", events}};
    if (noise == 1) plan["hazards"] = hazards;  // 模型把同一事件又写了一遍 hazards
    if (noise == 2 && !events.empty()) {        // 模型多写了一条字段不全/种类不认识的事件
        Json bad = events.back();
        bad["id"] = "extra_note";
        bad["kind"] = "announcement";
        plan["events"].push_back(bad);
    }
    if (noise == 3) return "根据公告整理如下：\n```json\n" + plan.dump(2) + "\n```\n以上坐标均为绝对坐标。";
    return plan.dump();
}

// ---------------------------------------------------------------------------
// Referee
// ---------------------------------------------------------------------------
void Referee::reset(const MapDef& map, int maxTicks) {
    map_ = map;
    maxTicks_ = maxTicks;
    tick_ = 0;
    robots_.clear();
    notices_.clear();
    for (std::size_t i = 0; i < map_.spawns.size(); ++i) {
        Robot r;
        r.team = std::string(1, static_cast<char>('A' + i));
        r.spawn = map_.spawns[i];
        r.pos = r.spawn;
        r.prevPos = r.pos;
        robots_.push_back(r);
    }
    for (const auto& e : map_.events) if (e.announce == 0) announce("事件预告 " + e.id);
}

void Referee::announce(const std::string& text) {
    notices_.push_back("第 " + std::to_string(tick_) + " Tick：" + text);
    if (notices_.size() > 8) notices_.erase(notices_.begin());
}

bool Referee::temp_wall(Point p) const {
    for (const auto& e : map_.events) {
        if (e.kind != EventKind::Split || tick_ < e.start || tick_ >= e.end) continue;
        if (p.x != e.a.x && p.y != e.a.y) continue;
        bool spawnCell = false;
        for (const auto& s : map_.spawns) if (s == p) spawnCell = true;
        if (!spawnCell) return true;
    }
    return false;
}

bool Referee::occupied(Point p, int except) const {
    for (int i = 0; i < robot_count(); ++i)
        if (i != except && robots_[static_cast<std::size_t>(i)].alive && robots_[static_cast<std::size_t>(i)].pos == p) return true;
    return false;
}

Point Referee::respawn_cell(const Robot& r) const {
    std::queue<Point> queue;
    std::set<Point> seen;
    queue.push(r.spawn);
    seen.insert(r.spawn);
    const int self = static_cast<int>(&r - robots_.data());
    while (!queue.empty()) {
        const Point c = queue.front();
        queue.pop();
        if (!wall(c) && !temp_wall(c) && !occupied(c, self)) return c;
        for (int dir = 0; dir < 4; ++dir) {
            const Point n{c.x + rmtc9::kDirX[dir], c.y + rmtc9::kDirY[dir]};
            if (inside(n) && seen.insert(n).second) queue.push(n);
        }
    }
    return r.spawn;
}

rmtc9::Perception Referee::perceive(int index) const {
    const Robot& r = robots_[static_cast<std::size_t>(index)];
    rmtc9::Perception p;
    p.tick = tick_;
    p.session = session_;
    p.mapW = map_.w;
    p.mapH = map_.h;
    p.vision = map_.vision;
    p.lastActionTick = r.lastActionTick;
    p.lastMoved = r.lastMoved;
    p.positionRespawn = r.positionRespawn;
    p.invalidActions = r.invalid;
    p.self = r.pos;
    p.team = r.team;
    p.hp = r.hp;
    p.alive = r.alive;
    p.defending = r.defending;
    p.protectedSpawn = r.alive && r.protectLeft > 0;
    p.respawnIn = r.respawnIn;
    p.positionInitial = (tick_ == 0);

    if (r.alive) {
        for (std::size_t i = 0; i < robots_.size(); ++i) {
            if (static_cast<int>(i) == index) continue;
            const Robot& o = robots_[i];
            if (!o.alive) continue;
            if (std::abs(o.pos.x - r.pos.x) > p.vision || std::abs(o.pos.y - r.pos.y) > p.vision) continue;
            rmtc9::Entity e;
            e.type = rmtc9::EntityType::Robot;
            e.id = "robot_" + o.team;
            e.team = o.team;
            e.pos = o.pos;
            e.hp = o.hp;
            e.defending = o.defending;
            e.protectedSpawn = o.protectLeft > 0;
            p.entities.push_back(e);
        }
        for (const auto& res : map_.resources) {
            if (std::abs(res.pos.x - r.pos.x) > p.vision || std::abs(res.pos.y - r.pos.y) > p.vision) continue;
            rmtc9::Entity e;
            e.type = res.value >= 20 ? rmtc9::EntityType::ResHigh : rmtc9::EntityType::ResNormal;
            e.id = res.id;
            e.pos = res.pos;
            e.value = res.value;
            p.entities.push_back(e);
        }
        for (int y = std::max(0, r.pos.y - p.vision); y <= std::min(map_.h - 1, r.pos.y + p.vision); ++y) {
            for (int x = std::max(0, r.pos.x - p.vision); x <= std::min(map_.w - 1, r.pos.x + p.vision); ++x) {
                if (!wall({x, y}) && !temp_wall({x, y})) continue;
                rmtc9::Entity e;
                e.type = rmtc9::EntityType::Obstacle;
                e.pos = {x, y};
                p.entities.push_back(e);
            }
        }
    }
    p.resourceValue = r.resourceValue;
    p.effectiveDamage = r.damage;
    p.missionsCompleted = r.missionsDone;
    p.completedIds = r.doneIds;
    p.missionText = map_.missionText;
    p.missions = map_.missions;
    for (std::size_t i = 0; i < notices_.size(); ++i) p.eventText += (i ? "\n" : "") + notices_[i];
    return p;
}

void Referee::submit(int index, const rmtc9::Action& action) {
    if (index < 0 || index >= static_cast<int>(robots_.size())) return;
    robots_[static_cast<std::size_t>(index)].action = action;
}

void Referee::step() {
    const int n = robot_count();
    const int T = tick_;
    const bool stage1 = map_.stage == 1;
    std::vector<int> deathsNow(static_cast<std::size_t>(n), 0);
    std::vector<char> diedNow(static_cast<std::size_t>(n), 0);
    for (auto& r : robots_) {
        r.positionRespawn = false;
        r.lastMoved = false;
        r.forcedWait = false;
        r.prevPos = r.pos;
        r.lastActionTick = T;
    }
    const auto kill = [&](int i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        r.alive = false;
        r.hp = 0;
        r.respawnIn = 3;
        r.dwell = 0;
        r.defending = false;
        ++r.deaths;
        ++deathsNow[static_cast<std::size_t>(i)];
        diedNow[static_cast<std::size_t>(i)] = 1;
    };

    // ---- 1–2. 激活本 Tick 事件；爆炸、幻术、世界分裂在移动前判定 ----
    for (const auto& e : map_.events) {
        if (e.start != T) continue;
        announce("事件生效 " + e.id);
        if (e.kind == EventKind::Explosion) {
            for (int i = 0; i < n; ++i) {
                Robot& r = robots_[static_cast<std::size_t>(i)];
                if (!r.alive || r.protectLeft > 0 || !in_rect(r.pos, e.a, e.b)) continue;
                r.hp -= e.damage;
                if (r.hp <= 0) { kill(i); ++r.eventDeaths; }
            }
        } else if (e.kind == EventKind::Illusion) {
            for (auto& r : robots_)
                if (r.alive && in_rect(r.pos, e.a, e.b)) r.invertedUntil = std::max(r.invertedUntil, T + e.effect);
        } else if (e.kind == EventKind::Split) {
            for (int i = 0; i < n; ++i) {
                Robot& r = robots_[static_cast<std::size_t>(i)];
                if (!r.alive || (r.pos.x != e.a.x && r.pos.y != e.a.y)) continue;
                bool spawnCell = false;
                for (const auto& s : map_.spawns) if (s == r.pos) spawnCell = true;
                if (spawnCell) continue;
                // 直接阵亡并立即满血复活，本 Tick 强制 WAIT；死亡扣除照常
                ++r.deaths;
                ++deathsNow[static_cast<std::size_t>(i)];
                ++r.eventDeaths;
                r.dwell = 0;
                r.alive = false;  // 让出原格，再找复活格
                const Point cell = respawn_cell(r);
                r.alive = true;
                r.pos = cell;
                r.hp = 100;
                r.protectLeft = 2;
                r.forcedWait = true;
                r.positionRespawn = true;
            }
        }
    }

    // ---- 3. 固化动作 ----
    for (int i = 0; i < n; ++i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        if (!r.alive) {
            if (!diedNow[static_cast<std::size_t>(i)] && r.action.kind != rmtc9::Act::Wait) ++r.invalid;  // E_STATE
            r.action = rmtc9::Action::wait();
            r.defending = false;
            continue;
        }
        if (r.forcedWait) r.action = rmtc9::Action::wait();
        if (!r.action.legal_shape() || (r.action.kind == rmtc9::Act::Attack && r.protectLeft > 0) ||
            (stage1 && r.action.kind != rmtc9::Act::Move && r.action.kind != rmtc9::Act::Wait)) {
            ++r.invalid;
            r.action = rmtc9::Action::wait();
        }
        r.defending = r.action.kind == rmtc9::Act::Defend;
    }

    // ---- 4. 战斗：按移动前位置汇总攻击、反伤、易伤与无双 ----
    bool purity = false;
    int reflectDamage = 20;
    for (const auto& e : map_.events)
        if (e.kind == EventKind::Purity && e.start <= T && T < e.end) { purity = true; reflectDamage = e.damage; }
    struct Claim { int from; double nominal; bool attack; };
    std::vector<std::vector<Claim>> claims(static_cast<std::size_t>(n));
    const auto vulnerable = [&](const Robot& r) { return r.vulnerableFrom >= 0 && r.vulnerableFrom <= T && T < r.vulnerableUntil; };
    const auto hit = [&](int from, int to, int base) {
        Robot& v = robots_[static_cast<std::size_t>(to)];
        if (v.protectLeft > 0) return;  // 保护目标挡枪，伤害 0
        if (purity && v.defending) {    // 洁身自好：目标不受伤，攻击者吃反伤（不乘易伤）
            claims[static_cast<std::size_t>(from)].push_back({to, static_cast<double>(reflectDamage), false});
            return;
        }
        const int dmg = vulnerable(v) ? base * 2 : base;
        claims[static_cast<std::size_t>(to)].push_back({from, static_cast<double>(dmg), true});
    };
    int furyDamage = 50, furyDefend = 10;
    for (const auto& e : map_.events)
        if (e.kind == EventKind::Fortress) { furyDamage = e.damage; furyDefend = e.defendDamage; }
    for (int i = 0; i < n; ++i) {
        const Robot& a = robots_[static_cast<std::size_t>(i)];
        if (!a.alive || a.action.kind != rmtc9::Act::Attack) continue;
        if (a.furyUntil > T) {
            for (int j = 0; j < n; ++j) {
                const Robot& o = robots_[static_cast<std::size_t>(j)];
                if (j == i || !o.alive || std::abs(o.pos.x - a.pos.x) > 2 || std::abs(o.pos.y - a.pos.y) > 2) continue;
                hit(i, j, o.defending ? furyDefend : furyDamage);
            }
            continue;
        }
        int target = -1, best = 3;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const Robot& o = robots_[static_cast<std::size_t>(j)];
            if (!o.alive) continue;
            const int ddx = o.pos.x - a.pos.x, ddy = o.pos.y - a.pos.y;
            int dist = -1;
            if (a.action.dx == 0 && a.action.dy < 0 && ddx == 0 && ddy < 0) dist = -ddy;
            else if (a.action.dx > 0 && a.action.dy == 0 && ddy == 0 && ddx > 0) dist = ddx;
            else if (a.action.dx == 0 && a.action.dy > 0 && ddx == 0 && ddy > 0) dist = ddy;
            else if (a.action.dx < 0 && a.action.dy == 0 && ddy == 0 && ddx < 0) dist = -ddx;
            if (dist >= 1 && dist <= 2 && dist < best) { best = dist; target = j; }
        }
        if (target >= 0) hit(i, target, robots_[static_cast<std::size_t>(target)].defending ? 10 : 20);
    }
    for (int j = 0; j < n; ++j) {
        Robot& v = robots_[static_cast<std::size_t>(j)];
        auto& list = claims[static_cast<std::size_t>(j)];
        if (list.empty() || !v.alive) continue;
        double total = 0;
        for (const auto& c : list) total += c.nominal;
        if (total <= 0) continue;
        const double factor = std::min(1.0, v.hp / total);
        for (const auto& c : list) {
            if (!c.attack) continue;  // 反伤不计攻击贡献
            robots_[static_cast<std::size_t>(c.from)].damage += c.nominal * factor;
            robots_[static_cast<std::size_t>(c.from)].attackDamage += c.nominal * factor;
        }
        v.hp -= static_cast<int>(std::lround(total));
        if (v.hp <= 0) {
            // 击杀奖励 60：致死批次中造成正伤害的贡献者（含反伤致死时的防御方）去重均分
            std::set<int> contributors;
            for (const auto& c : list) if (c.nominal > 0) contributors.insert(c.from);
            for (int c : contributors) {
                robots_[static_cast<std::size_t>(c)].damage += 60.0 / contributors.size();
                robots_[static_cast<std::size_t>(c)].killCredit += 60.0 / contributors.size();
                ++robots_[static_cast<std::size_t>(c)].kills;
            }
            kill(j);
        }
    }

    // ---- 5. 移动：战斗后存活车辆统一移动，应用幻术与临时墙 ----
    std::vector<Point> dest(static_cast<std::size_t>(n));
    std::vector<char> wants(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        if (!r.alive || r.action.kind != rmtc9::Act::Move) continue;
        int dx = r.action.dx, dy = r.action.dy;
        if (r.invertedUntil > T) { dx = -dx; dy = -dy; }
        const Point to{r.pos.x + dx, r.pos.y + dy};
        if (wall(to) || temp_wall(to)) continue;
        dest[static_cast<std::size_t>(i)] = to;
        wants[static_cast<std::size_t>(i)] = 1;
    }
    for (int i = 0; i < n; ++i)
        if (wants[static_cast<std::size_t>(i)] && occupied(dest[static_cast<std::size_t>(i)], i)) wants[static_cast<std::size_t>(i)] = 0;
    std::map<Point, int> contenders;
    for (int i = 0; i < n; ++i) if (wants[static_cast<std::size_t>(i)]) ++contenders[dest[static_cast<std::size_t>(i)]];
    for (int i = 0; i < n; ++i) if (wants[static_cast<std::size_t>(i)] && contenders[dest[static_cast<std::size_t>(i)]] > 1) wants[static_cast<std::size_t>(i)] = 0;
    for (int i = 0; i < n; ++i) {
        if (!wants[static_cast<std::size_t>(i)]) continue;
        Robot& r = robots_[static_cast<std::size_t>(i)];
        r.pos = dest[static_cast<std::size_t>(i)];
        r.lastMoved = true;
        if (r.pos != r.spawn) r.protectLeft = 0;
        for (const auto& e : map_.events)  // 潘潘的神：触发窗口内成功移动获得易伤，下一 Tick 起生效
            if (e.kind == EventKind::Pan && e.start <= T && T < e.start + e.window && !(r.vulnerableUntil > T + 1 && r.vulnerableFrom >= 0)) {
                r.vulnerableFrom = T + 1;
                r.vulnerableUntil = e.end;
            }
    }

    // ---- 6. 入点事件：天使、辣椒 ----
    std::vector<std::vector<Claim>> fire(static_cast<std::size_t>(n));
    for (const auto& e : map_.events) {
        if (e.kind == EventKind::Angel && e.start <= T && T < e.start + e.window && T < e.end) {
            for (auto& r : robots_)
                for (const auto& q : e.points)
                    if (r.alive && r.lastMoved && r.pos == q && r.prevPos != q) { r.healUntil = T + e.effect; r.healRate = e.heal; }
        }
        if (e.kind == EventKind::Chili && e.start <= T && T < e.end) {
            for (int t = 0; t < n; ++t) {
                const Robot& trig = robots_[static_cast<std::size_t>(t)];
                for (const auto& q : e.points) {
                    if (!trig.alive || !trig.lastMoved || trig.pos != q || trig.prevPos == q) continue;
                    for (int v = 0; v < n; ++v) {
                        const Robot& o = robots_[static_cast<std::size_t>(v)];
                        if (!o.alive || o.pos == q || o.protectLeft > 0) continue;
                        if (o.pos.x == q.x || o.pos.y == q.y) fire[static_cast<std::size_t>(v)].push_back({t, static_cast<double>(e.damage), false});
                    }
                }
            }
        }
    }
    for (int v = 0; v < n; ++v) {
        auto& list = fire[static_cast<std::size_t>(v)];
        Robot& o = robots_[static_cast<std::size_t>(v)];
        if (list.empty() || !o.alive) continue;
        double total = 0;
        for (const auto& c : list) total += c.nominal;
        o.hp -= static_cast<int>(std::lround(total));
        if (o.hp <= 0) {
            std::set<int> contributors;
            for (const auto& c : list) if (c.from != v) contributors.insert(c.from);
            for (int c : contributors) {
                robots_[static_cast<std::size_t>(c)].damage += 60.0 / contributors.size();
                robots_[static_cast<std::size_t>(c)].killCredit += 60.0 / contributors.size();
                ++robots_[static_cast<std::size_t>(c)].kills;
            }
            kill(v);
        }
    }

    // ---- 7. 堡垒驻留与回血 ----
    for (const auto& e : map_.events) {
        if (e.kind != EventKind::Fortress) continue;
        const bool active = e.start <= T && T < e.end;
        for (auto& r : robots_) {
            if (!active || !r.alive || !in_rect(r.pos, e.a, e.b)) { r.dwell = 0; continue; }
            if (++r.dwell >= e.dwell && r.furyUntil < e.end) r.furyUntil = e.end;
        }
    }
    for (auto& r : robots_)
        if (r.alive && r.healUntil > T) r.hp = std::min(100, r.hp + r.healRate);

    // ---- 8. 死亡扣除：本 Tick 全部攻击伤害与击杀奖励计入后，每次死亡扣 60，最低 0 ----
    for (int i = 0; i < n; ++i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        if (deathsNow[static_cast<std::size_t>(i)] > 0) r.damage = std::max(0.0, r.damage - 60.0 * deathsNow[static_cast<std::size_t>(i)]);
    }

    // ---- 9. 资源、任务、复活与保护倒计时 ----
    for (auto& r : robots_) {
        if (!r.alive) continue;
        for (auto it = map_.resources.begin(); it != map_.resources.end();) {
            if (it->pos != r.pos) { ++it; continue; }
            r.resourceValue += it->value;
            if (it->value >= 20) ++r.highCount; else ++r.normalCount;
            it = map_.resources.erase(it);
        }
        for (const auto& m : map_.missions) {
            if (std::find(r.doneIds.begin(), r.doneIds.end(), m.id) != r.doneIds.end()) continue;
            bool ok = false;
            if (m.type == "normal_resources") ok = r.normalCount >= static_cast<int>(m.target);
            else if (m.type == "high_resources") ok = r.highCount >= static_cast<int>(m.target);
            else if (m.type == "resource_value") ok = r.resourceValue >= m.target;
            else if (m.type == "effective_damage") ok = r.damage >= m.target;
            else if (m.type == "reach") ok = m.position && r.pos == *m.position;
            if (ok) { r.doneIds.push_back(m.id); ++r.missionsDone; }
        }
    }
    for (int i = 0; i < n; ++i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        if (!r.alive) {
            if (diedNow[static_cast<std::size_t>(i)]) continue;  // 死亡当 Tick 不递减
            if (r.respawnIn > 0) --r.respawnIn;
            if (r.respawnIn == 0) {
                r.pos = respawn_cell(r);
                r.alive = true;
                r.hp = 100;
                r.protectLeft = 2;
                r.positionRespawn = true;
            }
        } else if (r.protectLeft > 0) {
            --r.protectLeft;
        }
    }

    for (auto& r : robots_) r.action = rmtc9::Action::wait();
    ++tick_;
    for (const auto& e : map_.events) {
        if (e.announce == tick_) announce("事件预告 " + e.id);
        if (e.end == tick_) announce("事件结束 " + e.id);
    }
}

double Referee::score(int index) const {
    const Robot& r = robots_[static_cast<std::size_t>(index)];
    if (map_.stage == 1) return 40.0 * std::min(1.0, r.resourceValue / 200.0);
    const double res = 30.0 * std::min(1.0, r.resourceValue / 120.0);
    const double dmg = 15.0 * std::min(1.0, r.damage / 500.0);
    const double mis = 5.0 * r.missionsDone;
    return res + dmg + mis;
}

}  // namespace bench
