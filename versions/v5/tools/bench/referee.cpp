// tools/bench/referee.cpp
#include "referee.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
#include <set>
#include <queue>
#include <random>

namespace bench {
namespace {

bool inside_of(const MapDef& m, rmtc3::Point p) {
    return p.x >= 0 && p.y >= 0 && p.x < m.w && p.y < m.h;
}

int count_reachable(const MapDef& m, rmtc3::Point from) {
    if (!inside_of(m, from) || m.wall[static_cast<std::size_t>(from.y) * m.w + from.x]) return 0;
    std::vector<char> seen(static_cast<std::size_t>(m.w) * m.h, 0);
    std::queue<rmtc3::Point> q;
    q.push(from);
    seen[static_cast<std::size_t>(from.y) * m.w + from.x] = 1;
    int count = 0;
    while (!q.empty()) {
        const auto p = q.front();
        q.pop();
        ++count;
        for (int d = 0; d < 4; ++d) {
            const rmtc3::Point n{p.x + rmtc3::kDirX[d], p.y + rmtc3::kDirY[d]};
            if (!inside_of(m, n)) continue;
            const std::size_t idx = static_cast<std::size_t>(n.y) * m.w + n.x;
            if (m.wall[idx] || seen[idx]) continue;
            seen[idx] = 1;
            q.push(n);
        }
    }
    return count;
}

}  // namespace

MapDef generate_map(int w, int h, int robotCount, unsigned seed, int stage) {
    std::mt19937 rng(seed);
    MapDef map;
    map.w = w;
    map.h = h;
    map.wall.assign(static_cast<std::size_t>(w) * h, 0);

    const rmtc3::Point corners[4] = {{1, 1}, {w - 2, 1}, {1, h - 2}, {w - 2, h - 2}};
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
    std::vector<rmtc3::Point> used;
    const auto farEnough = [&](rmtc3::Point p) {
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
            const rmtc3::Point p{rx(rng), ry(rng)};
            if (!inside_of(map, p) || map.wall[static_cast<std::size_t>(p.y) * w + p.x]) continue;
            if (!farEnough(p)) continue;
            // 必须可达
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
        rmtc3::Mission m1;
        m1.id = "collect_two_normal";
        m1.type = "normal_resources";
        m1.target = 2;
        rmtc3::Mission m2;
        m2.id = "collect_high_value";
        m2.type = "high_resources";
        m2.target = 1;
        rmtc3::Mission m3;
        m3.id = "reach_target";
        m3.type = "reach";
        m3.target = 1;
        // 任务点取地图中心附近的第一个空地
        rmtc3::Point center{w / 2, h / 2};
        for (int radius = 0; radius < std::max(w, h); ++radius) {
            bool found = false;
            for (int dy = -radius; dy <= radius && !found; ++dy) {
                for (int dx = -radius; dx <= radius && !found; ++dx) {
                    const rmtc3::Point p{center.x + dx, center.y + dy};
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

void Referee::reset(const MapDef& map, int maxTicks) {
    map_ = map;
    maxTicks_ = maxTicks;
    tick_ = 0;
    robots_.clear();
    for (std::size_t i = 0; i < map_.spawns.size(); ++i) {
        Robot r;
        r.team = std::string(1, static_cast<char>('A' + i));
        r.spawn = map_.spawns[i];
        r.pos = r.spawn;
        robots_.push_back(r);
    }
}

void Referee::build_relative(rmtc3::Point self, rmtc3::Point entity, std::string* out) const {
    std::string grid(89, '0');
    for (int row = 0; row < 8; ++row) grid[static_cast<std::size_t>(row) * 10 + 9] = '\n';
    grid[4 * 10 + 4] = '1';
    const int dx = entity.x - self.x;
    const int dy = entity.y - self.y;
    if ((dx != 0 || dy != 0) && std::abs(dx) <= 4 && std::abs(dy) <= 4) {
        grid[static_cast<std::size_t>(4 + dy) * 10 + (4 + dx)] = '2';
    }
    *out = grid;
}

rmtc3::Perception Referee::perceive(int index) const {
    const Robot& r = robots_[static_cast<std::size_t>(index)];
    rmtc3::Perception p;
    p.tick = tick_;
    p.session = session_;
    p.mapW = map_.w;
    p.mapH = map_.h;
    p.vision = 4;
    p.self = r.pos;
    p.team = r.team;
    p.hp = r.hp;
    p.alive = r.alive;
    p.defending = r.defending;
    p.protectedSpawn = r.protectLeft > 0;
    p.respawnIn = r.respawnIn;
    p.positionInitial = (tick_ == 0);

    for (std::size_t i = 0; i < robots_.size(); ++i) {
        if (static_cast<int>(i) == index) continue;
        const Robot& o = robots_[i];
        if (!o.alive) continue;
        if (std::abs(o.pos.x - r.pos.x) > 4 || std::abs(o.pos.y - r.pos.y) > 4) continue;
        rmtc3::Entity e;
        e.type = rmtc3::EntityType::Robot;
        e.id = "robot_" + o.team;
        e.team = o.team;
        e.pos = o.pos;
        e.hp = o.hp;
        e.defending = o.defending;
        e.protectedSpawn = o.protectLeft > 0;
        p.entities.push_back(e);
    }
    for (const auto& res : map_.resources) {
        if (std::abs(res.pos.x - r.pos.x) > 4 || std::abs(res.pos.y - r.pos.y) > 4) continue;
        rmtc3::Entity e;
        e.type = res.value >= 20 ? rmtc3::EntityType::ResHigh : rmtc3::EntityType::ResNormal;
        e.id = res.id;
        e.pos = res.pos;
        e.value = res.value;
        p.entities.push_back(e);
    }
    for (int y = std::max(0, r.pos.y - 4); y <= std::min(map_.h - 1, r.pos.y + 4); ++y) {
        for (int x = std::max(0, r.pos.x - 4); x <= std::min(map_.w - 1, r.pos.x + 4); ++x) {
            if (map_.wall[static_cast<std::size_t>(y) * map_.w + x] == 0) continue;
            rmtc3::Entity e;
            e.type = rmtc3::EntityType::Obstacle;
            e.pos = {x, y};
            p.entities.push_back(e);
        }
    }

    p.resourceValue = r.resourceValue;
    p.effectiveDamage = r.damage;
    p.missionsCompleted = r.missionsDone;
    p.completedIds = r.doneIds;
    p.missionText = map_.missionText;
    p.missions = map_.missions;
    return p;
}

void Referee::submit(int index, const rmtc3::Action& action) {
    if (index < 0 || index >= static_cast<int>(robots_.size())) return;
    robots_[static_cast<std::size_t>(index)].action = action;
}

void Referee::step() {
    const int n = static_cast<int>(robots_.size());

    // ---- 1. 防御状态与非法动作 ----
    for (auto& r : robots_) {
        r.defending = r.alive && r.action.kind == rmtc3::Act::Defend;
        if (!r.alive) continue;
        if (r.action.kind == rmtc3::Act::Attack && r.protectLeft > 0) {
            ++r.invalid;
            r.action = rmtc3::Action::wait();
        }
        if (!r.action.legal_shape()) {
            ++r.invalid;
            r.action = rmtc3::Action::wait();
        }
    }

    // ---- 2. 攻击结算（移动前坐标，同时结算）----
    std::vector<std::vector<double>> nominal(static_cast<std::size_t>(n), std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) {
        Robot& a = robots_[static_cast<std::size_t>(i)];
        if (!a.alive || a.action.kind != rmtc3::Act::Attack) continue;
        const int dx = a.action.dx;
        const int dy = a.action.dy;
        int target = -1;
        int best = 3;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const Robot& o = robots_[static_cast<std::size_t>(j)];
            if (!o.alive) continue;
            const int ddx = o.pos.x - a.pos.x;
            const int ddy = o.pos.y - a.pos.y;
            int dist = -1;
            if (dx == 0 && dy < 0 && ddx == 0 && ddy < 0) dist = -ddy;
            else if (dx == 1 && dy == 0 && ddy == 0 && ddx > 0) dist = ddx;
            else if (dx == 0 && dy > 0 && ddx == 0 && ddy > 0) dist = ddy;
            else if (dx == -1 && dy == 0 && ddy == 0 && ddx < 0) dist = -ddx;
            if (dist >= 1 && dist <= 2 && dist < best) {
                best = dist;
                target = j;
            }
        }
        if (target < 0) continue;
        const Robot& t = robots_[static_cast<std::size_t>(target)];
        if (t.protectLeft > 0) continue;  // 出生保护：0 伤害，但仍然挡枪
        double dmg = t.action.kind == rmtc3::Act::Defend ? 10.0 : 20.0;
        nominal[static_cast<std::size_t>(i)][static_cast<std::size_t>(target)] = dmg;
    }

    std::vector<std::vector<double>> actual(static_cast<std::size_t>(n), std::vector<double>(n, 0.0));
    std::vector<double> damageTaken(static_cast<std::size_t>(n), 0.0);
    std::vector<int> killers(static_cast<std::size_t>(n), 0);
    for (int j = 0; j < n; ++j) {
        double total = 0;
        for (int i = 0; i < n; ++i) total += nominal[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        if (total <= 0) continue;
        Robot& t = robots_[static_cast<std::size_t>(j)];
        const double remaining = static_cast<double>(t.hp);
        const double scale = total > remaining ? remaining / total : 1.0;
        double sum = 0;
        for (int i = 0; i < n; ++i) {
            const double a2 = nominal[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] * scale;
            actual[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = a2;
            sum += a2;
        }
        t.hp -= static_cast<int>(std::lround(sum));
        damageTaken[static_cast<std::size_t>(j)] = sum;
    }

    // ---- 3. 伤害与击杀奖励计入 ----
    for (int i = 0; i < n; ++i) {
        double gained = 0;
        for (int j = 0; j < n; ++j) gained += actual[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        robots_[static_cast<std::size_t>(i)].damage += gained;
        for (int j = 0; j < n; ++j) robots_[static_cast<std::size_t>(i)].dmgTo[j] += actual[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
    }
    std::vector<char> died(static_cast<std::size_t>(n), 0);
    for (int j = 0; j < n; ++j) {
        Robot& t = robots_[static_cast<std::size_t>(j)];
        if (t.hp <= 0 && t.alive) {
            died[static_cast<std::size_t>(j)] = 1;
            // 贡献者去重后平分 60
            std::vector<int> contributors;
            std::vector<std::string> teams;
            for (int i = 0; i < n; ++i) {
                if (actual[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] <= 0) continue;
                const std::string& team = robots_[static_cast<std::size_t>(i)].team;
                if (std::find(teams.begin(), teams.end(), team) != teams.end()) continue;
                teams.push_back(team);
                contributors.push_back(i);
            }
            if (!contributors.empty()) {
                const double share = 60.0 / static_cast<double>(contributors.size());
                for (int i : contributors) {
                    robots_[static_cast<std::size_t>(i)].damage += share;
                    robots_[static_cast<std::size_t>(i)].dmgTo[j] += share;
                    robots_[static_cast<std::size_t>(i)].kills += 1;
                }
            }
        }
    }

    // ---- 4. 死亡处理与扣分（先奖后罚）----
    for (int j = 0; j < n; ++j) {
        if (!died[static_cast<std::size_t>(j)]) continue;
        Robot& t = robots_[static_cast<std::size_t>(j)];
        t.alive = false;
        t.hp = 0;
        t.respawnIn = 3;
        ++t.deaths;
        t.damage = std::max(0.0, t.damage - 60.0);
        t.action = rmtc3::Action::wait();
    }

    // ---- 5. 移动（同时结算，争格全部失败）----
    std::vector<rmtc3::Point> dest(static_cast<std::size_t>(n));
    std::vector<char> wants(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        if (!r.alive || r.action.kind != rmtc3::Act::Move) continue;
        const rmtc3::Point to{r.pos.x + r.action.dx, r.pos.y + r.action.dy};
        if (wall(to)) continue;
        dest[static_cast<std::size_t>(i)] = to;
        wants[static_cast<std::size_t>(i)] = 1;
    }
    // 目标格被存活车辆占据（无论它是否离开）→ 失败
    for (int i = 0; i < n; ++i) {
        if (!wants[static_cast<std::size_t>(i)]) continue;
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            if (robots_[static_cast<std::size_t>(j)].alive && robots_[static_cast<std::size_t>(j)].pos == dest[static_cast<std::size_t>(i)]) {
                wants[static_cast<std::size_t>(i)] = 0;
                break;
            }
        }
    }
    // Count from the unmodified candidate set: three-way conflicts must all fail.
    std::map<rmtc3::Point,int> contenders;
    for(int i=0;i<n;++i)if(wants[i])++contenders[dest[i]];
    for(int i=0;i<n;++i)if(wants[i]&&contenders[dest[i]]>1)wants[i]=0;
    for (int i = 0; i < n; ++i) {
        if (!wants[static_cast<std::size_t>(i)]) continue;
        robots_[static_cast<std::size_t>(i)].pos = dest[static_cast<std::size_t>(i)];
        // 离开出生点即取消保护
        if (robots_[static_cast<std::size_t>(i)].pos != robots_[static_cast<std::size_t>(i)].spawn) {
            robots_[static_cast<std::size_t>(i)].protectLeft = 0;
        }
    }

    // ---- 6. 资源收集 ----
    for (int i = 0; i < n; ++i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        if (!r.alive) continue;
        for (auto it = map_.resources.begin(); it != map_.resources.end();) {
            if (it->pos != r.pos) {
                ++it;
                continue;
            }
            r.resourceValue += it->value;
            if (it->value >= 20) ++r.highCount;
            else ++r.normalCount;
            it = map_.resources.erase(it);
        }
    }

    // ---- 7. 任务检查（仅存活）----
    for (int i = 0; i < n; ++i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        if (!r.alive) continue;
        for (const auto& m : map_.missions) {
            if (std::find(r.doneIds.begin(), r.doneIds.end(), m.id) != r.doneIds.end()) continue;
            bool ok = false;
            if (m.type == "normal_resources") ok = r.normalCount >= static_cast<int>(m.target);
            else if (m.type == "high_resources") ok = r.highCount >= static_cast<int>(m.target);
            else if (m.type == "resource_value") ok = r.resourceValue >= m.target;
            else if (m.type == "effective_damage") ok = r.damage >= m.target;
            else if (m.type == "reach") ok = m.position && r.pos == *m.position;
            if (ok) {
                r.doneIds.push_back(m.id);
                ++r.missionsDone;
            }
        }
    }

    // ---- 8. 复活与保护倒计时 ----
    for (int i = 0; i < n; ++i) {
        Robot& r = robots_[static_cast<std::size_t>(i)];
        if (!r.alive) {
            if (died[static_cast<std::size_t>(i)]) continue;  // 死亡当 Tick 不递减
            if (r.respawnIn > 0) --r.respawnIn;
            if (r.respawnIn == 0) {
                std::queue<rmtc3::Point> queue;std::set<rmtc3::Point> seen;
                queue.push(r.spawn);seen.insert(r.spawn);bool found=false;rmtc3::Point chosen;
                while(!queue.empty()&&!found){auto c=queue.front();queue.pop();bool occupied=false;
                    for(const auto& other:robots_)if(other.alive&&other.pos==c)occupied=true;
                    if(!wall(c)&&!occupied){chosen=c;found=true;break;}
                    for(int dir=0;dir<4;++dir){rmtc3::Point next{c.x+rmtc3::kDirX[dir],c.y+rmtc3::kDirY[dir]};if(inside_of(map_,next)&&seen.insert(next).second)queue.push(next);}
                }
                if(found){r.alive=true;r.hp=100;r.pos=chosen;r.protectLeft=2;}
            }
        } else if (r.protectLeft > 0) {
            --r.protectLeft;
        }
    }

    for (auto& r : robots_) r.action = rmtc3::Action::wait();
    ++tick_;
}

double Referee::score(int index) const {
    const Robot& r = robots_[static_cast<std::size_t>(index)];
    if (map_.missions.empty()) {
        return 40.0 * r.resourceValue / 200.0;
    }
    const double res = 30.0 * std::min(1.0, r.resourceValue / 120.0);
    const double dmg = 15.0 * std::min(1.0, r.damage / 500.0);
    const double mis = 5.0 * r.missionsDone;
    return res + dmg + mis;
}

}  // namespace bench
