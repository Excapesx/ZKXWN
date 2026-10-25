#pragma once

// Local fixture/example only. Replace with the teammate's production planner adapter.
#include "rmtc_decision/types.hpp"
#include <algorithm>
#include <array>
#include <queue>

class ExampleGridPlanner final : public rmtc::decision::Planner {
public:
    mutable int calls = 0;
    std::optional<rmtc::decision::Route> plan(const rmtc::decision::RouteQuery& q) const override {
        using namespace rmtc::decision;
        ++calls;
        if (q.width <= 0 || q.height <= 0 || q.width > 1000 || q.height > 1000) return {};
        const auto inside = [&](Point p) { return p.x >= 0 && p.y >= 0 && p.x < q.width && p.y < q.height; };
        if (!inside(q.from) || !inside(q.goal)) return {};
        if (q.from == q.goal) return Route{0, {}};
        struct Visit { Point p; int distance; std::optional<Direction> first; };
        std::queue<Visit> pending;
        std::vector<bool> seen(static_cast<std::size_t>(q.width) * q.height, false);
        const auto index = [&](Point p) { return static_cast<std::size_t>(p.y) * q.width + p.x; };
        pending.push({q.from, 0, {}});
        seen[index(q.from)] = true;
        constexpr std::array<Direction, 4> dirs{Direction::Up, Direction::Right, Direction::Down, Direction::Left};
        while (!pending.empty()) {
            const auto v = pending.front(); pending.pop();
            for (const auto d : dirs) {
                const auto p = step(v.p, d);
                if (!inside(p) || seen[index(p)] ||
                    std::find(q.blocked.begin(), q.blocked.end(), p) != q.blocked.end()) continue;
                seen[index(p)] = true;
                const auto first = v.first ? *v.first : d;
                if (p == q.goal) return Route{v.distance + 1, first};
                pending.push({p, v.distance + 1, first});
            }
        }
        return {};
    }
};

