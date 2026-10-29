// rmtc103/grid.hpp —— 地图记忆、危险格、距离场、定向越野求解
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "rmtc103/types.hpp"

namespace rmtc103 {

struct Resource {
    std::string id;
    Point pos;
    int value = 10;
    int observedTick = -1;
};

enum CellState : unsigned char { kUnknown = 0, kFree = 1, kWall = 2 };

// 只记录"合法观测到"的信息：视野内的空地/障碍/资源。
class WorldMap {
public:
    void reset(int w, int h);
    int width() const { return w_; }
    int height() const { return h_; }
    bool inside(Point p) const { return p.x >= 0 && p.y >= 0 && p.x < w_ && p.y < h_; }
    CellState at(Point p) const;
    bool isWall(Point p) const { return at(p) == kWall; }
    bool visited(Point p) const;

    // 吃一帧感知；返回 true 表示尺寸/场次变化，调用方应清空缓存
    bool update(const Perception& p, const Plan* plan=nullptr);
    std::optional<Point> spawn() const { return spawn_; }

    const std::map<std::string, Resource>& resources() const { return resources_; }
    const std::vector<Point>& walls() const { return walls_; }
    std::vector<Point> frontiers(bool probe_unknown = false) const;
    int normalCollected() const { return normalCollected_; }
    // 8.0：本场见过的不同资源个数（含后来被别人拿走的），用来估计“还没露面”的数量
    int everSeenNormal() const { return everSeenNormal_; }
    int everSeenHigh() const { return everSeenHigh_; }
    int highCollected() const { return highCollected_; }
    int disappearedElsewhere() const { return disappearedElsewhere_; }
    unsigned generation() const { return generation_; }

private:
    void mark(int x, int y, CellState s);
    int w_ = 0;
    int h_ = 0;
    std::vector<CellState> cell_;
    std::vector<unsigned char> visited_;
    std::map<std::string, Resource> resources_;
    std::map<std::string, bool> seenThisFrame_;
    std::vector<Point> walls_;
    std::map<Point,int> wall_first_seen_;
    unsigned generation_ = 1;
    int lastTick_ = -1;
    double lastValue_ = 0;
    int normalCollected_ = 0;
    int highCollected_ = 0;
    int disappearedElsewhere_ = 0;
    std::map<std::string, bool> everSeen_;
    int everSeenNormal_ = 0;
    int everSeenHigh_ = 0;
    std::string session_;
    std::optional<Point> spawn_;
};

// 随机事件危险场。只接受经过校验的 Plan。
struct HazardField {
    struct Cell {
        int start = 1 << 29;
        int end = 0;
        int damage = 0;
        bool wall = false;
        bool lethal = false;
        bool respectsProtection = false;
    };
    int w = 0;
    int h = 0;
    std::vector<std::vector<Cell>> cells;

    void reset(int width, int height) {
        w = width;
        h = height;
        cells.assign(static_cast<std::size_t>(w) * h, {});
    }
    void clear() { for(auto& c : cells) c.clear(); }
    void apply(const Plan& plan);
    bool any() const {
        for (const auto& c : cells) if (!c.empty()) return true;
        return false;
    }
    const std::vector<Cell>* at(Point p) const {
        if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) return nullptr;
        return &cells[static_cast<std::size_t>(p.y) * w + p.x];
    }
    bool blockedAt(Point p, int tick) const;
    int damageAt(Point p, int tick, bool protectedSpawn=false) const;
    bool lethalAt(Point p, int tick, bool protectedSpawn=false) const;
};

struct BlockMask {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> mask;

    void reset(int width, int height) {
        w = width;
        h = height;
        mask.assign(static_cast<std::size_t>(w) * h, 0);
    }
    bool blocked(Point p) const {
        if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) return true;
        return mask[static_cast<std::size_t>(p.y) * w + p.x] != 0;
    }
    void set(Point p, bool value) {
        if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) return;
        mask[static_cast<std::size_t>(p.y) * w + p.x] = value ? 1 : 0;
    }
};

// 单次 BFS 得到"到处走几步 + 第一步往哪走"，后续所有目标查询都是 O(1)。
// First-action code, deliberately outside the four direction indices.
inline constexpr int8_t kFirstWait = 4;
struct Field {
    int w = 0;
    int h = 0;
    std::vector<int> dist;
    std::vector<int8_t> first;  // 0..3: move, kFirstWait: wait, -1: none/origin

    void reset(int width, int height, int fill) {
        w = width;
        h = height;
        dist.assign(static_cast<std::size_t>(w) * h, fill);
        first.assign(static_cast<std::size_t>(w) * h, -1);
    }
    int at(Point p) const {
        if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) return -1;
        const int d = dist[static_cast<std::size_t>(p.y) * w + p.x];
        return d < 0 ? -1 : d;
    }
    int8_t firstAt(Point p) const {
        if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) return -1;
        return first[static_cast<std::size_t>(p.y) * w + p.x];
    }
};

Field bfs(const BlockMask& blocked, Point from, int horizon);

// Time-expanded BFS: a cell is impassable only while a wall/lethal hazard is
// active at movement settlement. Also avoid a lethal destination at the next
// frame. The first action is propagated per (cell,time), including WAIT.
Field bfs_timed(const BlockMask& blocked, const HazardField& hz, Point from,
                int startTick, int horizon);

// 定向越野：预算内最大化总价值的访问序列（n<=13 精确 DP，n>13 贪心（不声称全局最优））
struct Tour {
    std::vector<int> order;
    int value = 0;
    int length = 0;
    bool empty() const { return order.empty(); }
};

Tour solve_tour(const std::vector<int>& start_dist,
                const std::vector<std::vector<int>>& between,
                const std::vector<double>& values, int budget);

}  // namespace rmtc103
