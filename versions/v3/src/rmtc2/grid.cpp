// rmtc2/grid.cpp
#include "rmtc2/grid.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <set>

namespace rmtc2 {
namespace {
constexpr int kInf = std::numeric_limits<int>::max() / 4;
}

// ---------------------------------------------------------------------------
// WorldMap
// ---------------------------------------------------------------------------
void WorldMap::reset(int w, int h) {
    w_ = std::max(1, w);
    h_ = std::max(1, h);
    cell_.assign(static_cast<std::size_t>(w_) * h_, kUnknown);
    visited_.assign(static_cast<std::size_t>(w_) * h_, 0);
    resources_.clear();
    seenThisFrame_.clear();
    walls_.clear();
    ++generation_;
    lastTick_ = -1;
    lastValue_ = 0;
    normalCollected_ = 0;
    highCollected_ = 0;
    session_.clear();
}

CellState WorldMap::at(Point p) const {
    if (!inside(p)) return kWall;
    return cell_[static_cast<std::size_t>(p.y) * w_ + p.x];
}

bool WorldMap::visited(Point p) const {
    if (!inside(p)) return false;
    return visited_[static_cast<std::size_t>(p.y) * w_ + p.x] != 0;
}

void WorldMap::mark(int x, int y, CellState s) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    cell_[static_cast<std::size_t>(y) * w_ + x] = s;
}

bool WorldMap::update(const Perception& p) {
    bool structural = false;
    if (p.session != session_ || p.mapW != w_ || p.mapH != h_) {
        reset(p.mapW, p.mapH);
        session_ = p.session;
        structural = true;
    }
    if (p.tick <= lastTick_) return structural;
    if (p.positionInitial && !spawn_) spawn_ = p.self;

    // 资源增量：得分上涨即视为在自己脚下收集了一个同价值资源
    const double delta = p.resourceValue - lastValue_;
    if (lastTick_ >= 0 && delta > 1e-9) {
        for (auto it = resources_.begin(); it != resources_.end(); ++it) {
            if (it->second.pos == p.self && std::abs(it->second.value - delta) < 1e-6) {
                if (it->second.value >= 20) ++highCollected_;
                else ++normalCollected_;
                resources_.erase(it);
                ++generation_;
                break;
            }
        }
    }
    lastTick_ = p.tick;
    lastValue_ = p.resourceValue;

    if (!p.alive || p.hp <= 0) return structural;  // 阵亡帧没有视野，不能抹掉地图

    const int r = p.vision;
    std::set<std::pair<int, int>> observedWalls;
    seenThisFrame_.clear();
    for (const auto& e : p.entities) {
        if (e.type == EntityType::Obstacle) {
            observedWalls.insert({e.pos.x, e.pos.y});
        } else if (e.type == EntityType::ResNormal || e.type == EntityType::ResHigh) {
            Resource res;
            res.id = e.id;
            res.pos = e.pos;
            res.value = e.value > 0 ? e.value : (e.type == EntityType::ResHigh ? 20 : 10);
            resources_[e.id] = res;
            seenThisFrame_[e.id] = true;
        }
    }

    // 视野内：有障碍标障碍，其余为可通行；视野外不动
    for (int y = std::max(0, p.self.y - r); y <= std::min(h_ - 1, p.self.y + r); ++y) {
        for (int x = std::max(0, p.self.x - r); x <= std::min(w_ - 1, p.self.x + r); ++x) {
            const bool wall = observedWalls.count({x, y}) != 0;
            const CellState before = cell_[static_cast<std::size_t>(y) * w_ + x];
            mark(x, y, wall ? kWall : kFree);
            if (before != (wall ? kWall : kFree)) ++generation_;
        }
    }
    // 资源消失（视野内却看不到）= 已被别人拾取
    for (auto it = resources_.begin(); it != resources_.end();) {
        const bool inSight = std::abs(it->second.pos.x - p.self.x) <= r &&
                             std::abs(it->second.pos.y - p.self.y) <= r;
        if (inSight && seenThisFrame_.count(it->first) == 0) {
            it = resources_.erase(it);
            ++generation_;
        } else {
            ++it;
        }
    }
    mark(p.self.x, p.self.y, kFree);
    visited_[static_cast<std::size_t>(p.self.y) * w_ + p.self.x] = 1;

    walls_.clear();
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x)
            if (cell_[static_cast<std::size_t>(y) * w_ + x] == kWall) walls_.push_back({x, y});
    return structural;
}

std::vector<Point> WorldMap::frontiers(bool probe_unknown) const {
    std::vector<Point> out;
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            // With zero vision, target an unknown neighbour of observed free space.
            // This is a probe, not a claim that the unknown cell is traversable.
            if (at({x, y}) != (probe_unknown ? kUnknown : kFree)) continue;
            bool edge = false;
            for (int d = 0; d < 4 && !edge; ++d) {
                const Point n{x + kDirX[d], y + kDirY[d]};
                if (inside(n) && at(n) == (probe_unknown ? kFree : kUnknown)) edge = true;
            }
            if (edge) out.push_back({x, y});
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// HazardField
// ---------------------------------------------------------------------------
void HazardField::apply(const Plan& plan) {
    clear();
    for (const auto& zone : plan.hazards) {
        const int x0 = zone.cross ? 0 : std::max(0, zone.a.x);
        const int x1 = zone.cross ? w - 1 : std::min(w - 1, zone.b.x);
        const int y0 = zone.cross ? 0 : std::max(0, zone.a.y);
        const int y1 = zone.cross ? h - 1 : std::min(h - 1, zone.b.y);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const Point p{x, y};
                if (!zone.contains(p)) continue;
                cells[static_cast<std::size_t>(y) * w + x].push_back(
                    Cell{zone.start, zone.end, zone.damage, zone.wall, zone.lethal});
            }
        }
    }
}

bool HazardField::blockedAt(Point p, int tick) const {
    const auto* intervals=at(p); if(!intervals) return false;
    for(const auto& c:*intervals) if(c.start<=tick && tick<c.end && (c.wall||c.lethal)) return true;
    return false;
}
int HazardField::damageAt(Point p, int tick) const {
    const auto* intervals=at(p); if(!intervals) return 0;
    int sum=0;
    for(const auto& c:*intervals) if(c.start<=tick && tick<c.end) sum+=c.damage;
    return sum; // distinct active hazards: additive planning estimate, not official settlement
}
bool HazardField::lethalAt(Point p, int tick) const {
    const auto* intervals=at(p); if(!intervals) return false;
    for(const auto& c:*intervals) if(c.start<=tick && tick<c.end && c.lethal) return true;
    return false;
}

// ---------------------------------------------------------------------------
// BFS 距离场
// ---------------------------------------------------------------------------
Field bfs(const BlockMask& blocked, Point from, int horizon) {
    Field f;
    f.reset(blocked.w, blocked.h, -1);
    if (blocked.w <= 0 || blocked.h <= 0) return f;
    if (from.x < 0 || from.y < 0 || from.x >= blocked.w || from.y >= blocked.h) return f;
    if (blocked.blocked(from)) return f;

    const std::size_t cells = static_cast<std::size_t>(blocked.w) * blocked.h;
    std::vector<int> queue;
    queue.reserve(cells);
    const std::size_t start = static_cast<std::size_t>(from.y) * blocked.w + from.x;
    f.dist[start] = 0;
    f.first[start] = -1;
    queue.push_back(static_cast<int>(start));
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int idx = queue[head];
        const Point cur{idx % blocked.w, idx / blocked.w};
        const int d = f.dist[static_cast<std::size_t>(idx)];
        if (d >= horizon) continue;
        for (int dir = 0; dir < 4; ++dir) {
            const Point nx{cur.x + kDirX[dir], cur.y + kDirY[dir]};
            if (blocked.blocked(nx)) continue;
            const std::size_t nidx = static_cast<std::size_t>(nx.y) * blocked.w + nx.x;
            if (f.dist[nidx] >= 0) continue;
            f.dist[nidx] = d + 1;
            f.first[nidx] = (d == 0) ? static_cast<int8_t>(dir) : f.first[idx];
            queue.push_back(static_cast<int>(nidx));
        }
    }
    return f;
}

// ---------------------------------------------------------------------------
// 定向越野
// ---------------------------------------------------------------------------
namespace {
Tour greedy_tour(const std::vector<int>& start_dist,
                 const std::vector<std::vector<int>>& between,
                 const std::vector<double>& values, int budget, std::vector<char>& used) {
    Tour t;
    int cur = -1;
    int spent = 0;
    const int n = static_cast<int>(values.size());
    while (true) {
        int best = -1;
        double best_score = -1;
        int best_cost = 0;
        for (int j = 0; j < n; ++j) {
            if (used[static_cast<std::size_t>(j)]) continue;
            const int step = (cur < 0) ? start_dist[static_cast<std::size_t>(j)]
                                       : between[static_cast<std::size_t>(cur)][static_cast<std::size_t>(j)];
            if (step < 0 || spent + step > budget) continue;
            const double score = values[static_cast<std::size_t>(j)] / (step + 1.0);
            if (score > best_score || (score == best_score && step < best_cost)) {
                best_score = score;
                best = j;
                best_cost = step;
            }
        }
        if (best < 0) break;
        used[static_cast<std::size_t>(best)] = 1;
        t.order.push_back(best);
        t.value += static_cast<int>(values[static_cast<std::size_t>(best)] + 0.5);
        spent += best_cost;
        t.length = spent;
        cur = best;
    }
    return t;
}
}  // namespace

Tour solve_tour(const std::vector<int>& start_dist,
                const std::vector<std::vector<int>>& between,
                const std::vector<double>& values, int budget) {
    const int n = static_cast<int>(values.size());
    Tour best;
    if (n == 0) return best;
    std::vector<char> used(static_cast<std::size_t>(n), 0);
    if (n > 13) return greedy_tour(start_dist, between, values, budget, used);

    const int states = 1 << n;
    std::vector<int> dp(static_cast<std::size_t>(states) * n, kInf);
    std::vector<int8_t> parent(static_cast<std::size_t>(states) * n, -1);
    std::vector<int> mask_value(states, 0);
    for (int mask = 1; mask < states; ++mask) {
        const int low = mask & (-mask);
        int bit = 0;
        while ((1 << bit) != low) ++bit;
        mask_value[mask] = mask_value[mask ^ low] + static_cast<int>(values[bit] + 0.5);
    }
    for (int i = 0; i < n; ++i) {
        if (start_dist[i] < 0 || start_dist[i] > budget) continue;
        dp[(std::size_t{1} << i) * n + i] = start_dist[i];
    }

    int best_mask = 0;
    int best_end = -1;
    int best_len = kInf;
    for (int mask = 1; mask < states; ++mask) {
        for (int i = 0; i < n; ++i) {
            if (!(mask & (1 << i))) continue;
            const int cur = dp[static_cast<std::size_t>(mask) * n + i];
            if (cur >= kInf) continue;
            // 该 mask 可作为终点
            const int value = mask_value[mask];
            if (cur <= budget) {
                const int bval = best_mask ? mask_value[best_mask] : -1;
                if (value > bval || (value == bval && cur < best_len)) {
                    best_mask = mask;
                    best_end = i;
                    best_len = cur;
                }
            }
            for (int j = 0; j < n; ++j) {
                if (mask & (1 << j)) continue;
                const int step = between[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
                if (step < 0) continue;
                const int next = cur + step;
                if (next > budget) continue;
                const int nmask = mask | (1 << j);
                int& slot = dp[static_cast<std::size_t>(nmask) * n + j];
                if (next < slot) {
                    slot = next;
                    parent[static_cast<std::size_t>(nmask) * n + j] = static_cast<int8_t>(i);
                }
            }
        }
    }
    if (best_mask == 0 || best_end < 0) return best;

    best.value = mask_value[best_mask];
    best.length = best_len;
    int mask = best_mask;
    int node = best_end;
    while (node >= 0) {
        best.order.push_back(node);
        const int8_t prev = parent[static_cast<std::size_t>(mask) * n + node];
        mask &= ~(1 << node);
        node = prev;
    }
    std::reverse(best.order.begin(), best.order.end());
    return best;
}

}  // namespace rmtc2
